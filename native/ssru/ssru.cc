#include "ssru.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace mtran::ssru {
namespace {

std::size_t multiply(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b)
    throw std::invalid_argument("SSRU tensor size overflow");
  return a * b;
}

void validate(const TensorView& tensor, const char* name) {
  std::size_t size = 1;
  for (auto dim : tensor.shape) {
    if (dim <= 0 || static_cast<std::uint64_t>(dim) > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument(std::string("SSRU invalid shape: ") + name);
    size = multiply(size, static_cast<std::size_t>(dim));
  }
  if (!tensor.data || size != tensor.size)
    throw std::invalid_argument(std::string("SSRU invalid storage: ") + name);
}

void finite(const float* data, std::size_t size, const char* name) {
  for (std::size_t i = 0; i < size; ++i)
    if (!std::isfinite(data[i]))
      throw std::runtime_error(std::string("SSRU nonfinite value: ") + name);
}

void normalize(float* x, std::size_t dim, const float* scale, const float* bias) {
  float mean = 0;
  for (std::size_t i = 0; i < dim; ++i) mean += x[i];
  mean /= static_cast<float>(dim);
  float variance = 0;
  for (std::size_t i = 0; i < dim; ++i) {
    const float delta = x[i] - mean;
    variance += delta * delta;
  }
  variance /= static_cast<float>(dim);
  if (!std::isfinite(mean) || !std::isfinite(variance))
    throw std::runtime_error("SSRU normalization overflow");
  const float inverse = 1.0f / std::sqrt(variance + 1e-6f);
  for (std::size_t i = 0; i < dim; ++i)
    x[i] = ((x[i] - mean) * inverse) * scale[i] + bias[i];
  finite(x, dim, "normalization");
}

}  // namespace

Decoder::Decoder(Config config, const std::vector<NamedTensor>& weights) : config_(config) {
  const auto dim = config.dim;
  if (dim < 4 || dim % 2 || !config.heads || dim % config.heads ||
      !config.depth || !config.ffn_depth || dim > static_cast<std::size_t>(INT64_MAX))
    throw std::invalid_argument("SSRU invalid configuration");
  multiply(config.depth, dim);
  std::unordered_map<std::string, const TensorView*> names;
  for (const auto& weight : weights)
    if (!names.emplace(weight.name, &weight.tensor).second)
      throw std::invalid_argument("SSRU duplicate weight: " + weight.name);
  const auto get = [&](const std::string& name) -> const TensorView& {
    const auto it = names.find(name);
    if (it == names.end()) throw std::invalid_argument("SSRU missing weight: " + name);
    validate(*it->second, name.c_str());
    return *it->second;
  };
  const auto vector = [&](const std::string& name, std::size_t width) {
    const auto& t = get(name);
    if (t.shape != std::vector<std::int64_t>{static_cast<std::int64_t>(width)} &&
        t.shape != std::vector<std::int64_t>{1, static_cast<std::int64_t>(width)})
      throw std::invalid_argument("SSRU invalid vector: " + name);
    finite(t.data, t.size, name.c_str());
    return t.data;
  };
  const auto linear = [&](const std::string& name, const std::string& bias,
                          std::size_t input, std::size_t output = 0) {
    const auto& t = get(name);
    if (t.shape.size() != 2 || t.shape[0] != static_cast<std::int64_t>(input) ||
        (output && t.shape[1] != static_cast<std::int64_t>(output)))
      throw std::invalid_argument("SSRU invalid matrix: " + name);
    finite(t.data, t.size, name.c_str());
    const auto cols = static_cast<std::size_t>(t.shape[1]);
    return Linear{Matrix{name, t.data, input, cols}, bias.empty() ? nullptr : vector(bias, cols)};
  };
  const auto norm = [&](const std::string& prefix) {
    return Norm{vector(prefix + "_scale", dim), vector(prefix + "_bias", dim)};
  };
  embedding_ = get(names.count("Wemb") ? "Wemb" : "decoder_Wemb");
  if (embedding_.shape.size() != 2 || embedding_.shape[1] != static_cast<std::int64_t>(dim) ||
      embedding_.shape[0] < 2)
    throw std::invalid_argument("SSRU invalid target embedding");
  finite(embedding_.data, embedding_.size, "embedding");
  const auto& bias = get("decoder_ff_logit_out_b");
  const auto vocab = embedding_.shape[0];
  if (bias.shape != std::vector<std::int64_t>{vocab} &&
      bias.shape != std::vector<std::int64_t>{1, vocab})
    throw std::invalid_argument("SSRU invalid output bias");
  for (std::size_t i = 0; i < bias.size; ++i)
    if (!std::isfinite(bias.data[i]) && !(i == 1 && bias.data[i] == -std::numeric_limits<float>::infinity()))
      throw std::invalid_argument("SSRU nonfinite output bias");
  output_bias_ = bias.data;
  max_ffn_width_ = dim;
  for (std::size_t i = 1; i <= config.depth; ++i) {
    const std::string p = "decoder_l" + std::to_string(i);
    Layer layer;
    layer.gate = linear(p + "_rnn_Wf", p + "_rnn_bf", dim, dim);
    layer.state = linear(p + "_rnn_W", "", dim, dim);
    layer.rnn_norm = norm(p + "_rnn_ffn_ln");
    layer.query = linear(p + "_context_Wq", p + "_context_bq", dim, dim);
    layer.attention = linear(p + "_context_Wo", p + "_context_bo", dim, dim);
    layer.attention_norm = norm(p + "_context_Wo_ln");
    std::size_t width = dim;
    for (std::size_t j = 1; j <= config.ffn_depth; ++j) {
      layer.ffn.push_back(linear(p + "_ffn_W" + std::to_string(j),
                                 p + "_ffn_b" + std::to_string(j), width,
                                 j == config.ffn_depth ? dim : 0));
      width = layer.ffn.back().weight.cols;
      max_ffn_width_ = std::max(max_ffn_width_, width);
    }
    layer.ffn_norm = norm(p + "_ffn_ffn_ln");
    layers_.push_back(std::move(layer));
  }
  frequency_.resize(dim);
  offset_.resize(dim);
  if (names.count("position_frequency") || names.count("position_offset")) {
    const auto* frequency = vector("position_frequency", dim);
    const auto* offset = vector("position_offset", dim);
    std::copy_n(frequency, dim, frequency_.data());
    std::copy_n(offset, dim, offset_.data());
  } else {
    const auto half = dim / 2;
    const double pi = std::acos(-1.0);
    for (std::size_t i = 0; i < dim; ++i) {
      frequency_[i] = static_cast<float>(std::pow(1e-4, static_cast<double>(i % half) / (half - 1)));
      offset_[i] = static_cast<float>((i / half) * pi / 2);
    }
  }
}

void Decoder::decode(const Request& r, LinearBackend backend, Workspace& w, MathBackend math) const {
  w.tokens_.clear();
  w.ended_with_eos_ = false;
  try {
    const auto dim = config_.dim;
    const auto head_dim = dim / config_.heads;
    const auto vocab = static_cast<std::size_t>(embedding_.shape[0]);
    if (!backend.matmul || !r.source_length || !r.candidate_count || !r.candidate_ids ||
        r.eos_id < 0 || static_cast<std::uint64_t>(r.eos_id) >= vocab ||
        r.keys.size() != config_.depth || r.values.size() != config_.depth ||
        r.source_length > static_cast<std::size_t>(INT64_MAX))
      throw std::invalid_argument("SSRU invalid request");
    const auto loop_count = multiply(r.source_length, 3);
    if (loop_count >= static_cast<std::size_t>(INT64_MAX))
      throw std::invalid_argument("SSRU generation length overflow");
    const auto kv_size = multiply(dim, r.source_length);
    const std::vector<std::int64_t> key_shape{1, static_cast<std::int64_t>(config_.heads),
      static_cast<std::int64_t>(head_dim), static_cast<std::int64_t>(r.source_length)};
    const std::vector<std::int64_t> value_shape{1, static_cast<std::int64_t>(config_.heads),
      static_cast<std::int64_t>(r.source_length), static_cast<std::int64_t>(head_dim)};
    for (std::size_t i = 0; i < config_.depth; ++i) {
      validate(r.keys[i], "keys");
      validate(r.values[i], "values");
      if (r.keys[i].shape != key_shape || r.values[i].shape != value_shape ||
          r.keys[i].size != kv_size || r.values[i].size != kv_size)
        throw std::invalid_argument("SSRU invalid K/V dimensions");
      finite(r.keys[i].data, kv_size, "keys");
      finite(r.values[i].data, kv_size, "values");
    }
    for (std::size_t i = 0; i < r.candidate_count; ++i)
      if (r.candidate_ids[i] < 0 || static_cast<std::uint64_t>(r.candidate_ids[i]) >= vocab)
        throw std::invalid_argument("SSRU candidate outside vocabulary");

    w.tokens_.reserve(loop_count + 1);
    w.cells_.assign(multiply(config_.depth, dim), 0);
    for (auto* buffer : {&w.x_, &w.gate_, &w.update_, &w.query_, &w.context_, &w.output_}) buffer->resize(dim);
    w.ffn_a_.resize(max_ffn_width_);
    w.ffn_b_.resize(max_ffn_width_);
    w.scores_.resize(r.source_length);
    w.projection_.resize(multiply(dim, r.candidate_count));
    w.bias_.resize(r.candidate_count);
    w.logits_.resize(r.candidate_count);
    for (std::size_t j = 0; j < r.candidate_count; ++j) {
      const auto token = static_cast<std::size_t>(r.candidate_ids[j]);
      w.bias_[j] = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
      for (std::size_t k = 0; k < dim; ++k)
        w.projection_[k * r.candidate_count + j] = embedding_.data[token * dim + k];
    }
    const Matrix projection{"shortlist_projection_T", w.projection_.data(), dim, r.candidate_count};
    const auto matmul = [&](const Matrix& matrix, const float* input, float* output) {
      backend.matmul(backend.context, matrix, input, 1, output);
      finite(output, matrix.cols, matrix.name.c_str());
    };
    const auto linear = [&](const Linear& l, const float* input, float* output) {
      matmul(l.weight, input, output);
      if (l.bias) {
        for (std::size_t j = 0; j < l.weight.cols; ++j) output[j] += l.bias[j];
        finite(output, l.weight.cols, "linear bias");
      }
    };
    const float embedding_scale = static_cast<float>(std::sqrt(static_cast<double>(dim)));
    const float attention_scale = static_cast<float>(1 / std::sqrt(static_cast<double>(head_dim)));
    for (std::size_t position = 0; position <= loop_count; ++position) {
      const float* embedding = position ? embedding_.data + static_cast<std::size_t>(w.tokens_.back()) * dim : nullptr;
      for (std::size_t j = 0; j < dim; ++j) {
        const float phase = static_cast<float>(position) * frequency_[j] + offset_[j];
        w.x_[j] = phase;
      }
      if (math.sin) math.sin(math.context,w.x_.data(),dim);
      else for(auto& phase:w.x_) phase=std::sin(phase);
      for(std::size_t j=0;j<dim;++j) w.x_[j]=(embedding?embedding[j]*embedding_scale:0.0f)+w.x_[j];
      finite(w.x_.data(), dim, "position");
      for (std::size_t layer_index = 0; layer_index < config_.depth; ++layer_index) {
        const auto& layer = layers_[layer_index];
        linear(layer.gate, w.x_.data(), w.gate_.data());
        linear(layer.state, w.x_.data(), w.update_.data());
        if (math.sigmoid) {
          math.sigmoid(math.context, w.gate_.data(), dim);
          finite(w.gate_.data(), dim, "callback sigmoid");
        }
        auto* cell = w.cells_.data() + layer_index * dim;
        for (std::size_t j = 0; j < dim; ++j) {
          const float gate = math.sigmoid ? w.gate_[j] : 1.0f / (1.0f + std::exp(-w.gate_[j]));
          cell[j] = gate * cell[j] + (1.0f - gate) * w.update_[j];
          w.x_[j] += std::max(0.0f, cell[j]);
        }
        finite(cell, dim, "recurrent cell");
        if (math.normalize) {
          math.normalize(math.context, w.x_.data(), dim, layer.rnn_norm.scale, layer.rnn_norm.bias);
          finite(w.x_.data(), dim, "callback normalization");
        } else normalize(w.x_.data(), dim, layer.rnn_norm.scale, layer.rnn_norm.bias);
        linear(layer.query, w.x_.data(), w.query_.data());
        const auto* keys = r.keys[layer_index].data;
        const auto* values = r.values[layer_index].data;
        if (math.attention) math.attention(math.context,w.query_.data(),keys,values,config_.heads,head_dim,r.source_length,w.context_.data());
        else {
        for (std::size_t head = 0; head < config_.heads; ++head) {
          for (std::size_t s = 0; s < r.source_length; ++s) {
            float score = 0;
            for (std::size_t k = 0; k < head_dim; ++k)
              score += w.query_[head * head_dim + k] * keys[(head * head_dim + k) * r.source_length + s];
            w.scores_[s] = score * attention_scale;
          }
          finite(w.scores_.data(), r.source_length, "attention scores");
          const float maximum = *std::max_element(w.scores_.begin(), w.scores_.end());
          float total = 0;
          for (auto& score : w.scores_) { score = std::exp(score - maximum); total += score; }
          if (!std::isfinite(total) || total <= 0) throw std::runtime_error("SSRU invalid softmax");
          for (auto& score : w.scores_) score /= total;
          for (std::size_t k = 0; k < head_dim; ++k) {
            float sum = 0;
            for (std::size_t s = 0; s < r.source_length; ++s)
              sum += w.scores_[s] * values[(head * r.source_length + s) * head_dim + k];
            w.context_[head * head_dim + k] = sum;
          }
        }
        }
        finite(w.context_.data(), dim, "attention context");
        linear(layer.attention, w.context_.data(), w.output_.data());
        for (std::size_t j = 0; j < dim; ++j) w.x_[j] += w.output_[j];
        if (math.normalize) {
          math.normalize(math.context, w.x_.data(), dim, layer.attention_norm.scale, layer.attention_norm.bias);
          finite(w.x_.data(), dim, "callback normalization");
        } else normalize(w.x_.data(), dim, layer.attention_norm.scale, layer.attention_norm.bias);
        const float* input = w.x_.data();
        for (std::size_t j = 0; j < layer.ffn.size(); ++j) {
          auto* output = j % 2 ? w.ffn_b_.data() : w.ffn_a_.data();
          linear(layer.ffn[j], input, output);
          if (j + 1 < layer.ffn.size())
            for (std::size_t k = 0; k < layer.ffn[j].weight.cols; ++k) output[k] = std::max(0.0f, output[k]);
          input = output;
        }
        for (std::size_t j = 0; j < dim; ++j) w.x_[j] += input[j];
        if (math.normalize) {
          math.normalize(math.context, w.x_.data(), dim, layer.ffn_norm.scale, layer.ffn_norm.bias);
          finite(w.x_.data(), dim, "callback normalization");
        } else normalize(w.x_.data(), dim, layer.ffn_norm.scale, layer.ffn_norm.bias);
      }
      matmul(projection, w.x_.data(), w.logits_.data());
      std::size_t best = 0;
      for (std::size_t j = 0; j < r.candidate_count; ++j) {
        w.logits_[j] += w.bias_[j];
        if (!std::isfinite(w.logits_[j]) && !(w.logits_[j] == -std::numeric_limits<float>::infinity() && r.candidate_ids[j] == 1))
          throw std::runtime_error("SSRU nonfinite logit");
        if (w.logits_[j] > w.logits_[best]) best = j;
      }
      w.tokens_.push_back(r.candidate_ids[best]);
      if (w.tokens_.back() == r.eos_id) { w.ended_with_eos_ = true; break; }
    }
  } catch (...) {
    w.tokens_.clear();
    w.ended_with_eos_ = false;
    throw;
  }
}

}  // namespace mtran::ssru
