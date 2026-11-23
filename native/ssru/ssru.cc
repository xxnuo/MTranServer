#include "ssru.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#ifdef MTRAN_SSRU_PACKED
#include "ops.h"
#include "packed.h"
#include "pcore_pool.h"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif
#ifdef _MSC_VER
#pragma fp_contract(off)
#endif

namespace mtran::ssru {
namespace {

#ifdef MTRAN_SSRU_PACKED
constexpr std::size_t kPosTableMaxPos = 512;

#if defined(__linux__) && defined(__aarch64__)
constexpr bool kUseFusedBias = true;
#else
constexpr bool kUseFusedBias = false;
#endif
#endif

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

void validateInt8(const Int8TensorView& tensor, const char* name) {
  std::size_t size = 1;
  for (auto dim : tensor.shape) {
    if (dim <= 0 || static_cast<std::uint64_t>(dim) > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument(std::string("SSRU invalid shape: ") + name);
    size = multiply(size, static_cast<std::size_t>(dim));
  }
  if (!tensor.data || size != tensor.size)
    throw std::invalid_argument(std::string("SSRU invalid storage: ") + name);
  if (!std::isfinite(tensor.scale) || tensor.scale < 0.0f)
    throw std::invalid_argument(std::string("SSRU invalid scale: ") + name);
}

void finite(const float* data, std::size_t size, const char* name) {
  std::size_t i = 0;
#if defined(__x86_64__) || defined(_M_X64)
  const __m128i mask = _mm_set1_epi32(0x7f800000);
  __m128i b0 = _mm_setzero_si128(), b1 = _mm_setzero_si128();
  for (; i + 8 <= size; i += 8) {
    const __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    const __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i + 4));
    b0 = _mm_or_si128(b0, _mm_cmpeq_epi32(_mm_and_si128(v0, mask), mask));
    b1 = _mm_or_si128(b1, _mm_cmpeq_epi32(_mm_and_si128(v1, mask), mask));
  }
  if (_mm_movemask_epi8(_mm_or_si128(b0, b1)) != 0)
    throw std::runtime_error(std::string("SSRU nonfinite value: ") + name);
#elif defined(__aarch64__) || defined(_M_ARM64)
  const uint32x4_t mask = vdupq_n_u32(0x7f800000u);
  uint32x4_t b0 = vdupq_n_u32(0), b1 = vdupq_n_u32(0);
  for (; i + 8 <= size; i += 8) {
    const uint32x4_t v0 = vreinterpretq_u32_f32(vld1q_f32(data + i));
    const uint32x4_t v1 = vreinterpretq_u32_f32(vld1q_f32(data + i + 4));
    b0 = vorrq_u32(b0, vceqq_u32(vandq_u32(v0, mask), mask));
    b1 = vorrq_u32(b1, vceqq_u32(vandq_u32(v1, mask), mask));
  }
  if (vmaxvq_u32(vorrq_u32(b0, b1)) != 0)
    throw std::runtime_error(std::string("SSRU nonfinite value: ") + name);
#endif
  for (; i < size; ++i) {
    std::uint32_t bits;
    std::memcpy(&bits, &data[i], sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u)
      throw std::runtime_error(std::string("SSRU nonfinite value: ") + name);
  }
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

float readEmbedValue(const TensorView& fp_emb, const Int8TensorView& i8_emb,
                     std::size_t token, std::size_t col, std::size_t dim) noexcept {
  if (i8_emb.data)
    return static_cast<float>(i8_emb.data[token * dim + col]) * i8_emb.scale;
  return fp_emb.data[token * dim + col];
}

void scalarLinearBatch(const Matrix& fp, const Int8TensorView& q8, const float* bias,
                       const float* input, std::size_t rows, float* output) {
  const std::size_t in_dim = q8.data ? static_cast<std::size_t>(q8.shape[0]) : fp.rows;
  const std::size_t out_dim = q8.data ? static_cast<std::size_t>(q8.shape[1]) : fp.cols;
  for (std::size_t r = 0; r < rows; ++r) {
    const float* x = input + r * in_dim;
    float* y = output + r * out_dim;
    for (std::size_t c = 0; c < out_dim; ++c) {
      float sum = 0.0f;
      if (q8.data) {
        const float ws = q8.scale;
        for (std::size_t k = 0; k < in_dim; ++k)
          sum += x[k] * (static_cast<float>(q8.data[k * out_dim + c]) * ws);
      } else {
        for (std::size_t k = 0; k < in_dim; ++k)
          sum += x[k] * fp.data[k * out_dim + c];
      }
      y[c] = bias ? (sum + bias[c]) : sum;
    }
  }
  finite(output, multiply(rows, out_dim), "decoder scalar linear");
}

#ifdef MTRAN_SSRU_PACKED
struct PackedDecMat {
  std::unique_ptr<packed::Weight> weight;
  const float* extra_bias = nullptr;
  std::size_t rows = 0;
  std::size_t cols = 0;
};

struct PackedDecLayer {
  PackedDecMat gate, gate_step0, state;
  const float* rnn_ln_scale = nullptr;
  const float* rnn_ln_bias = nullptr;
  PackedDecMat query, query_step0, attn;
  const float* attn_ln_scale = nullptr;
  const float* attn_ln_bias = nullptr;
  std::vector<PackedDecMat> ffn;
  const float* ffn_ln_scale = nullptr;
  const float* ffn_ln_bias = nullptr;
};

void addDecExtraBiasSlice(const float* bias, std::size_t rows, std::size_t stride,
                          std::size_t col_begin, std::size_t col_end, bool relu,
                          float* out) {
  for (std::size_t r = 0; r < rows; ++r) {
    float* row = out + r * stride;
    if (relu) {
      for (std::size_t c = col_begin; c < col_end; ++c)
        row[c] = std::max(0.0f, row[c] + bias[c]);
    } else if (bias) {
      for (std::size_t c = col_begin; c < col_end; ++c)
        row[c] += bias[c];
    }
  }
}

void runDecMultiplySlice(const PackedDecMat& pl,
                         const std::uint8_t* a_q,
                         const float* a_scales,
                         const std::uint8_t* a_zps,
                         std::size_t rows,
                         float* out,
                         std::size_t out_stride,
                         float* out_maxs,
                         bool relu,
                         std::size_t col_begin,
                         std::size_t col_end) {
  if (relu && !pl.extra_bias) {
    pl.weight->multiplyBatchReluMinMax(a_q, a_scales, a_zps, rows, out, out_stride,
                                       out_maxs, col_begin, col_end);
    return;
  }
  if (pl.extra_bias && kUseFusedBias && col_begin == 0 && col_end == pl.cols) {
    for (std::size_t r = 0; r < rows; ++r) {
      pl.weight->multiply(a_q + r * pl.rows, 1, a_scales[r], a_zps[r],
                          out + r * out_stride, pl.extra_bias, nullptr);
      if (relu) {
        float* row = out + r * out_stride;
        float mx = 0.0f;
        for (std::size_t c = 0; c < pl.cols; ++c) {
          const float v = std::max(0.0f, row[c]);
          row[c] = v;
          if (v > mx) mx = v;
        }
        if (out_maxs) out_maxs[r] = mx;
      }
    }
    return;
  }
  pl.weight->multiplyBatch(a_q, a_scales, a_zps, rows, out, out_stride, col_begin, col_end);
  if (pl.extra_bias || relu) {
    addDecExtraBiasSlice(pl.extra_bias, rows, out_stride, col_begin, col_end, relu, out);
  }
}
#endif

struct CandPair {
  std::int64_t id;
  std::uint32_t bit;
};

}  // namespace

struct BatchWorkspace::Scratch {
  std::vector<float> cells, x, gate, update, query, context, output;
  std::vector<float> ffn_a, ffn_b, scores, projection, bias, logits;
  std::vector<std::size_t> slot_req, slot_pos, slot_loop_max;
  std::vector<std::int64_t> slot_prev_token, chosen_tokens;
  std::vector<Request> slot_reqs, refill_reqs;
  std::vector<BatchResult> slot_results;
  std::vector<CandPair> union_pairs;
  std::vector<std::int64_t> union_ids;
  std::vector<std::uint32_t> slot_masks;
#ifdef MTRAN_SSRU_PACKED
  packed::Workspace packed_ws;
  std::vector<std::uint8_t> a_q, a_zps;
  std::vector<float> a_scales, relu_maxs, norm_mins, norm_maxs;
#endif
};

BatchWorkspace::BatchWorkspace() : scratch_(std::make_unique<Scratch>()) {}
BatchWorkspace::~BatchWorkspace() = default;
BatchWorkspace::BatchWorkspace(BatchWorkspace&&) noexcept = default;
BatchWorkspace& BatchWorkspace::operator=(BatchWorkspace&&) noexcept = default;

#ifdef MTRAN_SSRU_PACKED
struct Decoder::NativeState {
  ops::PosTable pos_table;
  std::vector<PackedDecLayer> layers;
  TensorView fp_emb;
  Int8TensorView i8_emb;
  const float* output_bias = nullptr;
  std::size_t vocab = 0;
  std::size_t dim = 0;
  mutable std::once_flag shortlist_once;
  mutable std::unique_ptr<const packed::ShortlistTable> shortlist;
  mutable std::once_flag full_vocab_once;
  mutable std::unique_ptr<const packed::FullVocabQ4> full_vocab;

  NativeState(ops::PosTable pt, std::vector<PackedDecLayer> ls,
              TensorView fp_e, Int8TensorView i8_e, const float* ob,
              std::size_t v, std::size_t d)
      : pos_table(std::move(pt)), layers(std::move(ls)),
        fp_emb(std::move(fp_e)), i8_emb(std::move(i8_e)),
        output_bias(ob), vocab(v), dim(d) {}

  const packed::ShortlistTable& getShortlist() const {
    std::call_once(shortlist_once, [&] {
      if (i8_emb.data) {
        shortlist = std::make_unique<const packed::ShortlistTable>(
            packed::ShortlistTable::createInt8(i8_emb.data, i8_emb.scale, output_bias,
                                               vocab, dim, false));
      } else {
        shortlist = std::make_unique<const packed::ShortlistTable>(
            fp_emb.data, output_bias, vocab, dim, false);
      }
    });
    return *shortlist;
  }

  const packed::FullVocabQ4& getFullVocab() const {
    std::call_once(full_vocab_once, [&] {
      if (i8_emb.data) {
        full_vocab = std::make_unique<const packed::FullVocabQ4>(
            packed::FullVocabQ4::createInt8(i8_emb.data, i8_emb.scale, output_bias,
                                            vocab, dim, false));
      } else {
        full_vocab = std::make_unique<const packed::FullVocabQ4>(
            fp_emb.data, output_bias, vocab, dim, false);
      }
    });
    return *full_vocab;
  }
};
#else
struct Decoder::NativeState {};
#endif

Decoder::Decoder(Config config, const std::vector<NamedTensor>& weights,
                 const std::vector<NamedInt8Tensor>& int8_weights)
    : config_(config) {
  const auto dim = config.dim;
  if (dim < 4 || dim % 2 || !config.heads || dim % config.heads ||
      !config.depth || !config.ffn_depth || dim > static_cast<std::size_t>(INT64_MAX))
    throw std::invalid_argument("SSRU invalid configuration");
  multiply(config.depth, dim);
  std::unordered_map<std::string, const TensorView*> names;
  std::unordered_map<std::string, const Int8TensorView*> i8_names;
  for (const auto& weight : weights)
    if (!names.emplace(weight.name, &weight.tensor).second)
      throw std::invalid_argument("SSRU duplicate weight: " + weight.name);
  for (const auto& weight : int8_weights)
    if (names.count(weight.name) || !i8_names.emplace(weight.name, &weight.tensor).second)
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
    if (const auto i8_it = i8_names.find(name); i8_it != i8_names.end()) {
      const auto& t = *i8_it->second;
      validateInt8(t, name.c_str());
      if (t.shape.size() != 2 || t.shape[0] != static_cast<std::int64_t>(input) ||
          (output && t.shape[1] != static_cast<std::int64_t>(output)))
        throw std::invalid_argument("SSRU invalid matrix: " + name);
      const auto cols = static_cast<std::size_t>(t.shape[1]);
      return Linear{Matrix{name, nullptr, input, cols}, t,
                    bias.empty() ? nullptr : vector(bias, cols)};
    }
    const auto& t = get(name);
    if (t.shape.size() != 2 || t.shape[0] != static_cast<std::int64_t>(input) ||
        (output && t.shape[1] != static_cast<std::int64_t>(output)))
      throw std::invalid_argument("SSRU invalid matrix: " + name);
    finite(t.data, t.size, name.c_str());
    const auto cols = static_cast<std::size_t>(t.shape[1]);
    return Linear{Matrix{name, t.data, input, cols}, Int8TensorView{},
                  bias.empty() ? nullptr : vector(bias, cols)};
  };
  const auto norm = [&](const std::string& prefix) {
    return Norm{vector(prefix + "_scale", dim), vector(prefix + "_bias", dim)};
  };
  const std::string emb_name =
      (i8_names.count("Wemb") || names.count("Wemb")) ? "Wemb" : "decoder_Wemb";
  if (const auto i8_it = i8_names.find(emb_name); i8_it != i8_names.end()) {
    validateInt8(*i8_it->second, emb_name.c_str());
    int8_embedding_ = *i8_it->second;
    if (int8_embedding_.shape.size() != 2 ||
        int8_embedding_.shape[1] != static_cast<std::int64_t>(dim) ||
        int8_embedding_.shape[0] < 2)
      throw std::invalid_argument("SSRU invalid target embedding");
    vocab_size_ = static_cast<std::size_t>(int8_embedding_.shape[0]);
  } else {
    embedding_ = get(emb_name);
    if (embedding_.shape.size() != 2 ||
        embedding_.shape[1] != static_cast<std::int64_t>(dim) ||
        embedding_.shape[0] < 2)
      throw std::invalid_argument("SSRU invalid target embedding");
    finite(embedding_.data, embedding_.size, "embedding");
    vocab_size_ = static_cast<std::size_t>(embedding_.shape[0]);
  }
  const auto& bias = get("decoder_ff_logit_out_b");
  const auto vocab = static_cast<std::int64_t>(vocab_size_);
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

#ifdef MTRAN_SSRU_PACKED
  if (packed::backend() != nullptr && dim >= 32 && dim % 32 == 0 && vocab_size_ >= 9) {
    bool all_packable = true;
    for (const auto& l : layers_) {
      for (const auto& f : l.ffn) {
        if (f.weight.rows % 32 != 0 || f.weight.cols % 32 != 0 ||
            f.weight.rows > static_cast<std::size_t>(INT32_MAX / (255 * 128))) {
          all_packable = false;
          break;
        }
      }
      if (!all_packable) break;
    }
    if (all_packable) {
      const auto packMat = [](const Linear& l, bool force_separate) -> PackedDecMat {
        PackedDecMat pm;
        pm.rows = l.weight.rows;
        pm.cols = l.weight.cols;
        const bool fuse = kUseFusedBias && !force_separate;
        if (l.int8_weight.data) {
          pm.weight = std::make_unique<packed::Weight>(packed::Weight::packInt8(
              l.int8_weight.data, l.int8_weight.scale, pm.rows, pm.cols,
              fuse ? l.bias : nullptr, fuse ? nullptr : l.bias, true));
          pm.extra_bias = nullptr;
        } else {
          pm.weight = std::make_unique<packed::Weight>(l.weight.data, pm.rows, pm.cols);
          pm.extra_bias = l.bias;
        }
        return pm;
      };
      std::vector<PackedDecLayer> packed_layers;
      packed_layers.reserve(layers_.size());
      for (std::size_t li = 0; li < layers_.size(); ++li) {
        const auto& l = layers_[li];
        PackedDecLayer pl;
        pl.gate = packMat(l.gate, false);
        if (kUseFusedBias && li == 0) pl.gate_step0 = packMat(l.gate, true);
        pl.state = packMat(l.state, false);
        pl.rnn_ln_scale = l.rnn_norm.scale;
        pl.rnn_ln_bias = l.rnn_norm.bias;
        pl.query = packMat(l.query, false);
        if (kUseFusedBias && li == 0) pl.query_step0 = packMat(l.query, true);
        pl.attn = packMat(l.attention, false);
        pl.attn_ln_scale = l.attention_norm.scale;
        pl.attn_ln_bias = l.attention_norm.bias;
        pl.ffn.reserve(l.ffn.size());
        for (const auto& f : l.ffn) pl.ffn.push_back(packMat(f, false));
        pl.ffn_ln_scale = l.ffn_norm.scale;
        pl.ffn_ln_bias = l.ffn_norm.bias;
        packed_layers.push_back(std::move(pl));
      }
      native_ = std::make_unique<NativeState>(
          ops::PosTable::create(frequency_.data(), offset_.data(), dim, kPosTableMaxPos),
          std::move(packed_layers), embedding_, int8_embedding_, output_bias_,
          vocab_size_, dim);
    }
  }
#endif
}

Decoder::~Decoder() = default;
Decoder::Decoder(Decoder&&) noexcept = default;
Decoder& Decoder::operator=(Decoder&&) noexcept = default;

bool Decoder::has_native_fast() const noexcept {
  return native_ != nullptr;
}

void Decoder::decode(const Request& r, LinearBackend backend, Workspace& w, MathBackend math) const {
  w.tokens_.clear();
  w.ended_with_eos_ = false;
  try {
    const auto dim = config_.dim;
    const auto head_dim = dim / config_.heads;
    const auto vocab = vocab_size_;
    const bool full_vocab = (r.candidate_count == 0 && backend.argmax_full != nullptr);
    if (!backend.matmul || !r.source_length || (!full_vocab && (!r.candidate_count || !r.candidate_ids)) ||
        r.eos_id < 0 || static_cast<std::uint64_t>(r.eos_id) >= vocab ||
        r.keys.size() != config_.depth || r.values.size() != config_.depth ||
        r.source_length > static_cast<std::size_t>(INT64_MAX))
      throw std::invalid_argument("SSRU invalid request");
    if (!full_vocab && backend.argmax_shortlist && !embedding_.data)
      throw std::invalid_argument("SSRU FP32 embedding required for shortlist callback");
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
    multiply(dim, r.candidate_count);
    if (!full_vocab && !backend.argmax_shortlist) {
      w.projection_.resize(multiply(dim, r.candidate_count));
      w.bias_.resize(r.candidate_count);
      w.logits_.resize(r.candidate_count);
      for (std::size_t j = 0; j < r.candidate_count; ++j) {
        const auto token = static_cast<std::size_t>(r.candidate_ids[j]);
        w.bias_[j] = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
        for (std::size_t k = 0; k < dim; ++k)
          w.projection_[k * r.candidate_count + j] = readEmbedValue(embedding_, int8_embedding_, token, k, dim);
      }
    }
    const Matrix projection{"shortlist_projection_T", w.projection_.data(), dim, r.candidate_count};
    const auto matmul = [&](const Matrix& matrix, const float* input, float* output) {
      if (!matrix.data) throw std::invalid_argument("SSRU FP32 matrix required for LinearBackend");
      backend.matmul(backend.context, matrix, input, 1, output);
      finite(output, matrix.cols, matrix.name.c_str());
    };
    const auto linear = [&](const Linear& l, const float* input, float* output, bool separate_bias = false) {
      if (!l.weight.data) throw std::invalid_argument("SSRU FP32 matrix required for LinearBackend");
      if (l.bias && backend.matmul_bias) {
        backend.matmul_bias(backend.context, l.weight, input, 1, l.bias, separate_bias, output);
        finite(output, l.weight.cols, "linear fused bias");
        return;
      }
      backend.matmul(backend.context, l.weight, input, 1, output);
      if (l.bias) {
        for (std::size_t j = 0; j < l.weight.cols; ++j) output[j] += l.bias[j];
        finite(output, l.weight.cols, "linear bias");
      } else {
        finite(output, l.weight.cols, l.weight.name.c_str());
      }
    };
    const float embedding_scale = static_cast<float>(std::sqrt(static_cast<double>(dim)));
    const float attention_scale = static_cast<float>(1 / std::sqrt(static_cast<double>(head_dim)));
    for (std::size_t position = 0; position <= loop_count; ++position) {
      const std::size_t prev_token = position ? static_cast<std::size_t>(w.tokens_.back()) : 0;
      for (std::size_t j = 0; j < dim; ++j) {
        const float phase = static_cast<float>(position) * frequency_[j] + offset_[j];
        w.x_[j] = phase;
      }
      if (math.sin) math.sin(math.context,w.x_.data(),dim);
      else for(auto& phase:w.x_) phase=std::sin(phase);
      if (position) {
        for (std::size_t j = 0; j < dim; ++j) {
          const float e = readEmbedValue(embedding_, int8_embedding_, prev_token, j, dim);
          w.x_[j] = e * embedding_scale + w.x_[j];
        }
      }
      finite(w.x_.data(), dim, "position");
      for (std::size_t layer_index = 0; layer_index < config_.depth; ++layer_index) {
        const auto& layer = layers_[layer_index];
        if (backend.matmul_pair) {
          if (!layer.gate.weight.data || !layer.state.weight.data)
            throw std::invalid_argument("SSRU FP32 matrix required for LinearBackend");
          backend.matmul_pair(backend.context, layer.gate.weight, layer.gate.bias, position == 0 && layer_index == 0,
                              w.gate_.data(), layer.state.weight, w.update_.data(), w.x_.data(), 1);
          finite(w.gate_.data(), dim, "linear gate");
          finite(w.update_.data(), dim, "linear state");
        } else {
          linear(layer.gate, w.x_.data(), w.gate_.data(), position == 0 && layer_index == 0);
          linear(layer.state, w.x_.data(), w.update_.data());
        }
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
        linear(layer.query, w.x_.data(), w.query_.data(), position == 0 && layer_index == 0);
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
      std::int64_t chosen_token = 0;
      if (full_vocab) {
        const std::size_t tok = backend.argmax_full(backend.context, w.x_.data(), dim);
        if (tok >= vocab) throw std::runtime_error("SSRU invalid full-vocab token");
        chosen_token = static_cast<std::int64_t>(tok);
      } else {
        std::size_t best = 0;
        if (backend.argmax_shortlist) {
          best = backend.argmax_shortlist(backend.context, w.x_.data(), dim,
                                          embedding_.data, output_bias_, r.candidate_ids, r.candidate_count);
          if (best >= r.candidate_count) throw std::runtime_error("SSRU invalid shortlist index");
        } else {
          matmul(projection, w.x_.data(), w.logits_.data());
          for (std::size_t j = 0; j < r.candidate_count; ++j) {
            w.logits_[j] += w.bias_[j];
            if (!std::isfinite(w.logits_[j]) && !(w.logits_[j] == -std::numeric_limits<float>::infinity() && r.candidate_ids[j] == 1))
              throw std::runtime_error("SSRU nonfinite logit");
            if (w.logits_[j] > w.logits_[best]) best = j;
          }
        }
        chosen_token = r.candidate_ids[best];
      }
      w.tokens_.push_back(chosen_token);
      if (w.tokens_.back() == r.eos_id) { w.ended_with_eos_ = true; break; }
    }
  } catch (...) {
    w.tokens_.clear();
    w.ended_with_eos_ = false;
    throw;
  }
}

void Decoder::decodeBatch(const Request* requests, std::size_t count,
                          BatchWorkspace& workspace,
                          std::size_t max_threads,
                          std::size_t max_active_batch,
                          LinearBackend backend,
                          MathBackend math,
                          const std::atomic<std::size_t>* active_submitted) const {
  decodeBatchImpl(requests, count, workspace, {}, max_threads, max_active_batch,
                  active_submitted, backend, math);
}

void Decoder::decodeBatchStreaming(const Request* initial_requests, std::size_t initial_count,
                                   BatchWorkspace& workspace,
                                   const BatchCallbacks& callbacks,
                                   std::size_t max_threads,
                                   std::size_t max_active_batch,
                                   const std::atomic<std::size_t>* active_submitted,
                                   LinearBackend backend,
                                   MathBackend math) const {
  decodeBatchImpl(initial_requests, initial_count, workspace, callbacks, max_threads,
                  max_active_batch, active_submitted, backend, math);
}

void Decoder::decodeBatchImpl(const Request* initial_requests, std::size_t initial_count,
                              BatchWorkspace& workspace,
                              const BatchCallbacks& callbacks,
                              std::size_t max_threads,
                              std::size_t max_active_batch,
                              const std::atomic<std::size_t>* active_submitted,
                              LinearBackend backend,
                              MathBackend math) const {
  if (!workspace.scratch_) workspace.scratch_ = std::make_unique<BatchWorkspace::Scratch>();
  auto& s = *workspace.scratch_;
  const bool has_on_complete = static_cast<bool>(callbacks.on_complete);
  const bool has_try_refill = static_cast<bool>(callbacks.try_refill);

  try {
    if (initial_count == 0 && !has_try_refill) {
      workspace.results_.clear();
      return;
    }
    if (initial_count > 0 && !initial_requests) {
      throw std::invalid_argument("SSRU null batch requests");
    }

    const std::size_t dim = config_.dim;
    const std::size_t heads = config_.heads;
    const std::size_t head_dim = dim / heads;
    const std::size_t depth = config_.depth;
    const std::size_t vocab = vocab_size_;

    std::size_t max_src_len = 1;
    const auto validateRequest = [&](const Request& r) {
      const bool full_vocab = (r.candidate_count == 0);
      if (!r.source_length || (!full_vocab && !r.candidate_ids) ||
          r.eos_id < 0 || static_cast<std::uint64_t>(r.eos_id) >= vocab ||
          r.keys.size() != depth || r.values.size() != depth ||
          r.source_length > static_cast<std::size_t>(INT64_MAX))
        throw std::invalid_argument("SSRU invalid request");
      if (!native_ && full_vocab && backend.matmul && !backend.argmax_full)
        throw std::invalid_argument("SSRU full-vocab callback missing");
      if (!native_ && !full_vocab && backend.matmul && backend.argmax_shortlist && !embedding_.data)
        throw std::invalid_argument("SSRU FP32 embedding required for shortlist callback");
      const auto loop_count = multiply(r.source_length, 3);
      if (loop_count >= static_cast<std::size_t>(INT64_MAX))
        throw std::invalid_argument("SSRU generation length overflow");
      const auto kv_size = multiply(dim, r.source_length);
      const auto h_i64 = static_cast<std::int64_t>(heads);
      const auto hd_i64 = static_cast<std::int64_t>(head_dim);
      const auto sl_i64 = static_cast<std::int64_t>(r.source_length);
      for (std::size_t l = 0; l < depth; ++l) {
        validate(r.keys[l], "keys");
        validate(r.values[l], "values");
        const auto& ks = r.keys[l].shape;
        const auto& vs = r.values[l].shape;
        if (ks.size() != 4 || ks[0] != 1 || ks[1] != h_i64 || ks[2] != hd_i64 || ks[3] != sl_i64 ||
            vs.size() != 4 || vs[0] != 1 || vs[1] != h_i64 || vs[2] != sl_i64 || vs[3] != hd_i64 ||
            r.keys[l].size != kv_size || r.values[l].size != kv_size)
          throw std::invalid_argument("SSRU invalid K/V dimensions");
        finite(r.keys[l].data, kv_size, "keys");
        finite(r.values[l].data, kv_size, "values");
      }
      for (std::size_t j = 0; j < r.candidate_count; ++j) {
        if (r.candidate_ids[j] < 0 || static_cast<std::uint64_t>(r.candidate_ids[j]) >= vocab)
          throw std::invalid_argument("SSRU candidate outside vocabulary");
      }
      max_src_len = std::max(max_src_len, r.source_length);
    };

    for (std::size_t i = 0; i < initial_count; ++i) {
      validateRequest(initial_requests[i]);
    }

    const std::size_t clamped_cap = std::clamp<std::size_t>(max_active_batch, 1, 32);
    const std::size_t b_cap = has_try_refill ? clamped_cap : std::min(initial_count, clamped_cap);
    for (auto& res : workspace.results_) {
      res.tokens.clear();
      res.ended_with_eos = false;
    }
    if (!has_on_complete) {
      workspace.results_.resize(initial_count);
    } else {
      workspace.results_.clear();
    }

    s.slot_req.resize(b_cap);
    s.slot_pos.resize(b_cap);
    s.slot_loop_max.resize(b_cap);
    s.slot_prev_token.resize(b_cap);
    s.chosen_tokens.resize(b_cap);
    s.slot_reqs.resize(b_cap);
    s.slot_results.resize(b_cap);

    const auto copyTensorViews = [](std::vector<TensorView>& dst, const std::vector<TensorView>& src) {
      if (dst.size() != src.size()) dst.resize(src.size());
      for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i].data = src[i].data;
        dst[i].size = src[i].size;
        dst[i].shape.assign(src[i].shape.begin(), src[i].shape.end());
      }
    };
    const auto copySlotReq = [&](Request& dst, const Request& src) {
      dst.source_length = src.source_length;
      dst.eos_id = src.eos_id;
      dst.candidate_ids = src.candidate_ids;
      dst.candidate_count = src.candidate_count;
      copyTensorViews(dst.keys, src.keys);
      copyTensorViews(dst.values, src.values);
    };
    const auto clearSlotReq = [](Request& dst) {
      dst.candidate_ids = nullptr;
      dst.candidate_count = 0;
      dst.source_length = 0;
      for (auto& kv : dst.keys) {
        kv.data = nullptr;
        kv.size = 0;
      }
      for (auto& kv : dst.values) {
        kv.data = nullptr;
        kv.size = 0;
      }
    };

    std::size_t active = std::min(initial_count, b_cap);
    std::size_t next_req = active;
    for (std::size_t m = 0; m < active; ++m) {
      s.slot_req[m] = m;
      copySlotReq(s.slot_reqs[m], initial_requests[m]);
      s.slot_pos[m] = 0;
      s.slot_prev_token[m] = 0;
      s.slot_loop_max[m] = s.slot_reqs[m].source_length * 3;
      s.slot_results[m].tokens.clear();
      s.slot_results[m].tokens.reserve(s.slot_loop_max[m] + 1);
      s.slot_results[m].ended_with_eos = false;
    }

    const std::size_t cap_dim = multiply(b_cap, dim);
    s.cells.assign(multiply(depth, cap_dim), 0.0f);
    for (auto* buf : {&s.x, &s.gate, &s.update, &s.query, &s.context, &s.output})
      buf->resize(cap_dim);
    const std::size_t cap_ffn = multiply(b_cap, max_ffn_width_);
    s.ffn_a.resize(cap_ffn);
    s.ffn_b.resize(cap_ffn);

    const float emb_scale = static_cast<float>(std::sqrt(static_cast<double>(dim)));
    const float attn_scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
    bool shortlist_union_dirty = true;

    std::size_t score_threads = 1;
#ifdef MTRAN_SSRU_PACKED
    if (native_) {
      auto& pool = PcorePool::instance();
      const std::size_t capped = std::max<std::size_t>(1, max_threads);
      score_threads = std::min(capped, std::max<std::size_t>(1, pool.pcoreCount()));
    }
#endif
    s.scores.resize(multiply(score_threads, max_src_len));

    const auto admitRefills = [&]() {
      if (!has_try_refill || active >= b_cap) return;
      s.refill_reqs.clear();
      const std::size_t base_id = callbacks.try_refill(s.refill_reqs, b_cap - active);
      if (s.refill_reqs.size() > b_cap - active) {
        throw std::invalid_argument("SSRU refill exceeds batch capacity");
      }
      for (std::size_t i = 0; i < s.refill_reqs.size(); ++i) {
        validateRequest(s.refill_reqs[i]);
        const std::size_t slot = active++;
        s.slot_req[slot] = base_id + i;
        copySlotReq(s.slot_reqs[slot], s.refill_reqs[i]);
        s.slot_pos[slot] = 0;
        s.slot_prev_token[slot] = 0;
        s.slot_loop_max[slot] = s.slot_reqs[slot].source_length * 3;
        s.slot_results[slot].tokens.clear();
        s.slot_results[slot].tokens.reserve(s.slot_loop_max[slot] + 1);
        s.slot_results[slot].ended_with_eos = false;
        for (std::size_t l = 0; l < depth; ++l) {
          std::fill_n(s.cells.data() + l * cap_dim + slot * dim, dim, 0.0f);
        }
        shortlist_union_dirty = true;
      }
      const std::size_t needed_scores = multiply(score_threads, max_src_len);
      if (s.scores.size() < needed_scores) s.scores.resize(needed_scores);
    };

    const auto retireSlot = [&](std::size_t req_idx, std::size_t slot) {
      if (has_on_complete) {
        BatchResult done = std::move(s.slot_results[slot]);
        callbacks.on_complete(req_idx, std::move(done));
      } else {
        if (req_idx >= workspace.results_.size()) workspace.results_.resize(req_idx + 1);
        auto& dst = workspace.results_[req_idx];
        dst.ended_with_eos = s.slot_results[slot].ended_with_eos;
        dst.tokens.assign(s.slot_results[slot].tokens.begin(), s.slot_results[slot].tokens.end());
      }
    };

    const auto recordAndRetire = [&]() {
      for (std::size_t m = active; m-- > 0;) {
        const std::size_t req_idx = s.slot_req[m];
        const std::int64_t tok = s.chosen_tokens[m];
        if (tok < 0 || static_cast<std::uint64_t>(tok) >= vocab)
          throw std::runtime_error("SSRU invalid batch token");
        s.slot_results[m].tokens.push_back(tok);
        const bool hit_eos = (tok == s.slot_reqs[m].eos_id);
        const bool hit_max = (s.slot_pos[m] >= s.slot_loop_max[m]);
        if (hit_eos) s.slot_results[m].ended_with_eos = true;
        if (hit_eos || hit_max) {
          shortlist_union_dirty = true;
          clearSlotReq(s.slot_reqs[m]);
          retireSlot(req_idx, m);
          if (next_req < initial_count) {
            const std::size_t nr = next_req++;
            s.slot_req[m] = nr;
            copySlotReq(s.slot_reqs[m], initial_requests[nr]);
            s.slot_pos[m] = 0;
            s.slot_prev_token[m] = 0;
            s.slot_loop_max[m] = s.slot_reqs[m].source_length * 3;
            s.slot_results[m].tokens.clear();
            s.slot_results[m].tokens.reserve(s.slot_loop_max[m] + 1);
            s.slot_results[m].ended_with_eos = false;
            for (std::size_t l = 0; l < depth; ++l) {
              std::fill_n(s.cells.data() + l * cap_dim + m * dim, dim, 0.0f);
            }
          } else {
            const std::size_t last = active - 1;
            if (m != last) {
              s.slot_req[m] = s.slot_req[last];
              copySlotReq(s.slot_reqs[m], s.slot_reqs[last]);
              s.slot_pos[m] = s.slot_pos[last];
              s.slot_prev_token[m] = s.slot_prev_token[last];
              s.slot_loop_max[m] = s.slot_loop_max[last];
              s.slot_results[m].ended_with_eos = s.slot_results[last].ended_with_eos;
              s.slot_results[m].tokens.assign(s.slot_results[last].tokens.begin(),
                                              s.slot_results[last].tokens.end());
              for (std::size_t l = 0; l < depth; ++l) {
                float* c_layer = s.cells.data() + l * cap_dim;
                std::copy_n(c_layer + last * dim, dim, c_layer + m * dim);
              }
            }
            clearSlotReq(s.slot_reqs[last]);
            --active;
          }
        } else {
          s.slot_prev_token[m] = tok;
          ++s.slot_pos[m];
        }
      }
      admitRefills();
    };

    admitRefills();
    if (active == 0) return;

#ifdef MTRAN_SSRU_PACKED
    if (native_) {
      auto& pool = PcorePool::instance();
      const std::size_t threads = score_threads;
      std::size_t step_threads = threads;

      s.a_q.resize(multiply(b_cap, std::max(dim, max_ffn_width_)));
      s.a_scales.resize(b_cap);
      s.a_zps.resize(b_cap);
      s.relu_maxs.resize(multiply(threads, b_cap));
      s.norm_mins.resize(b_cap);
      s.norm_maxs.resize(b_cap);

      const auto quantizeActiveRows = [&](const float* src, std::size_t cols) {
        if (active >= 4 && step_threads >= 2) {
          const std::size_t team = std::min(step_threads, active);
          pool.parallelFor(team, [&](std::size_t tid, std::size_t nt) {
            const std::size_t mb = (active * tid) / nt;
            const std::size_t me = (active * (tid + 1)) / nt;
            for (std::size_t m = mb; m < me; ++m) {
              packed::quantizeLinearU8(src + m * cols, 1, cols, s.a_q.data() + m * cols,
                                       s.a_scales[m], s.a_zps[m]);
            }
          });
        } else {
          for (std::size_t m = 0; m < active; ++m) {
            packed::quantizeLinearU8(src + m * cols, 1, cols, s.a_q.data() + m * cols,
                                     s.a_scales[m], s.a_zps[m]);
          }
        }
      };

      const auto normAndQuantize = [&](const float* ln_scale, const float* ln_bias) {
        if (active >= 4 && step_threads >= 2) {
          const std::size_t team = std::min(step_threads, active);
          pool.parallelFor(team, [&](std::size_t tid, std::size_t nt) {
            const std::size_t mb = (active * tid) / nt;
            const std::size_t me = (active * (tid + 1)) / nt;
            if (mb < me) {
              const std::size_t cnt = me - mb;
              ops::instanceNormAffine(s.x.data() + mb * dim, ln_scale, ln_bias,
                                      cnt, dim, s.x.data() + mb * dim,
                                      s.norm_mins.data() + mb, s.norm_maxs.data() + mb);
              packed::quantizeLinearU8FromMinMax(
                  s.x.data() + mb * dim, cnt, dim, dim,
                  s.norm_mins.data() + mb, s.norm_maxs.data() + mb,
                  s.a_q.data() + mb * dim, dim,
                  s.a_scales.data() + mb, s.a_zps.data() + mb);
            }
          });
        } else {
          ops::instanceNormAffine(s.x.data(), ln_scale, ln_bias, active, dim, s.x.data(),
                                  s.norm_mins.data(), s.norm_maxs.data());
          packed::quantizeLinearU8FromMinMax(
              s.x.data(), active, dim, dim,
              s.norm_mins.data(), s.norm_maxs.data(),
              s.a_q.data(), dim, s.a_scales.data(), s.a_zps.data());
        }
      };

      const auto shardMatmul = [&](const PackedDecMat& pm, float* dst, bool relu, bool allow_parallel) -> std::size_t {
        const std::size_t chunks = pm.cols / 32;
        if (!allow_parallel || step_threads <= 1 || chunks <= 1) {
          runDecMultiplySlice(pm, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                              active, dst, pm.cols, s.relu_maxs.data(), relu, 0, pm.cols);
          return 1;
        }
        const std::size_t team = std::min(step_threads, chunks);
        pool.parallelFor(team, [&](std::size_t tid, std::size_t nt) {
          const std::size_t cb = (chunks * tid) / nt;
          const std::size_t ce = (chunks * (tid + 1)) / nt;
          if (cb < ce) {
            runDecMultiplySlice(pm, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                                active, dst, pm.cols, s.relu_maxs.data() + tid * active,
                                relu, cb * 32, ce * 32);
          }
        });
        return team;
      };

      const std::size_t q_chunks = dim / 32;
      const std::size_t rnn_chunks = 2 * q_chunks;

      while (active > 0) {
        step_threads = (active == 1 && active_submitted &&
                        active_submitted->load(std::memory_order_relaxed) > 1)
                           ? 1
                           : threads;
        std::size_t max_active_src_len = 0;
        bool any_pos0 = false;
        bool all_pos0 = true;

        // 1. Embed + positional encoding across all active slots.
        for (std::size_t m = 0; m < active; ++m) {
          const std::size_t pos = s.slot_pos[m];
          any_pos0 = any_pos0 || (pos == 0);
          all_pos0 = all_pos0 && (pos == 0);
          max_active_src_len = std::max(max_active_src_len, s.slot_reqs[m].source_length);
          float* dst = s.x.data() + m * dim;
          if (pos == 0) {
            native_->pos_table.addScaledEmbed(nullptr, 0.0f, 0, dst);
          } else {
            const auto tok = static_cast<std::size_t>(s.slot_prev_token[m]);
            if (pos < kPosTableMaxPos) {
              if (int8_embedding_.data) {
                native_->pos_table.addScaledEmbedInt8(
                    int8_embedding_.data + tok * dim, int8_embedding_.scale, emb_scale, pos, dst);
              } else {
                native_->pos_table.addScaledEmbed(
                    embedding_.data + tok * dim, emb_scale, pos, dst);
              }
            } else {
              const float fp = static_cast<float>(pos);
              for (std::size_t d = 0; d < dim; ++d) {
                const float e = readEmbedValue(embedding_, int8_embedding_, tok, d, dim);
                dst[d] = e * emb_scale + std::sin(fp * frequency_[d] + offset_[d]);
              }
            }
          }
        }

        // 2. Decoder layers.
        for (std::size_t li = 0; li < depth; ++li) {
          const auto& pl = native_->layers[li];
          const bool use_step0_all = (kUseFusedBias && li == 0 && all_pos0);
          const bool use_step0_mixed = (kUseFusedBias && li == 0 && any_pos0 && !all_pos0);

          // 2a. SSRU recurrent gate + state sharing one activation quantization.
          if (li == 0) quantizeActiveRows(s.x.data(), dim);
          const PackedDecMat& gate_mat = use_step0_all ? pl.gate_step0 : pl.gate;
          if (step_threads <= 1) {
            runDecMultiplySlice(gate_mat, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                                active, s.gate.data(), dim, nullptr, false, 0, dim);
            runDecMultiplySlice(pl.state, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                                active, s.update.data(), dim, nullptr, false, 0, dim);
          } else {
            const std::size_t team = std::min(step_threads, rnn_chunks);
            pool.parallelFor(team, [&](std::size_t tid, std::size_t nt) {
              const std::size_t gb = (rnn_chunks * tid) / nt;
              const std::size_t ge = (rnn_chunks * (tid + 1)) / nt;
              if (gb < q_chunks) {
                const std::size_t le = std::min(ge, q_chunks);
                runDecMultiplySlice(gate_mat, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                                    active, s.gate.data(), dim, nullptr, false, gb * 32, le * 32);
              }
              if (ge > q_chunks) {
                const std::size_t lb = std::max(gb, q_chunks) - q_chunks;
                const std::size_t le = ge - q_chunks;
                runDecMultiplySlice(pl.state, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                                    active, s.update.data(), dim, nullptr, false, lb * 32, le * 32);
              }
            });
          }
          if (use_step0_mixed) {
            for (std::size_t m = 0; m < active; ++m) {
              if (s.slot_pos[m] == 0) {
                runDecMultiplySlice(pl.gate_step0, s.a_q.data() + m * dim, &s.a_scales[m],
                                    &s.a_zps[m], 1, s.gate.data() + m * dim, dim,
                                    nullptr, false, 0, dim);
              }
            }
          }

          float* cell_layer = s.cells.data() + li * cap_dim;
          ops::ssruGateUpdate(s.gate.data(), s.update.data(), s.x.data(),
                              active, dim, cell_layer, s.x.data());
          normAndQuantize(pl.rnn_ln_scale, pl.rnn_ln_bias);

          // 2b. Cross-attention query -> crossAttention -> output -> residual + norm.
          const PackedDecMat& query_mat = use_step0_all ? pl.query_step0 : pl.query;
          shardMatmul(query_mat, s.query.data(), false, active >= 3);
          if (use_step0_mixed) {
            for (std::size_t m = 0; m < active; ++m) {
              if (s.slot_pos[m] == 0) {
                runDecMultiplySlice(pl.query_step0, s.a_q.data() + m * dim, &s.a_scales[m],
                                    &s.a_zps[m], 1, s.query.data() + m * dim, dim,
                                    nullptr, false, 0, dim);
              }
            }
          }

          const std::size_t total_heads = active * heads;
          if (step_threads <= 1 || (active == 1 && max_active_src_len < 32)) {
            for (std::size_t m = 0; m < active; ++m) {
              const auto& req = s.slot_reqs[m];
              ops::crossAttention(s.query.data() + m * dim, req.keys[li].data,
                                  req.values[li].data, heads, head_dim, req.source_length,
                                  s.scores.data(), s.context.data() + m * dim, 0, heads);
            }
          } else {
            const std::size_t team = std::min(step_threads, total_heads);
            pool.parallelFor(team, [&](std::size_t tid, std::size_t nt) {
              float* t_scores = s.scores.data() + tid * max_src_len;
              for (std::size_t idx = tid; idx < total_heads; idx += nt) {
                const std::size_t m = idx / heads;
                const std::size_t h = idx % heads;
                const auto& req = s.slot_reqs[m];
                ops::crossAttention(s.query.data() + m * dim, req.keys[li].data,
                                    req.values[li].data, heads, head_dim, req.source_length,
                                    t_scores, s.context.data() + m * dim, h, h + 1);
              }
            });
          }

          quantizeActiveRows(s.context.data(), dim);
          shardMatmul(pl.attn, s.output.data(), false, active >= 3);
          for (std::size_t i = 0; i < active * dim; ++i) s.x[i] += s.output[i];
          normAndQuantize(pl.attn_ln_scale, pl.attn_ln_bias);

          // 2c. FFN (ReLU fused on hidden layers) + residual + norm.
          const float* ffn_in = s.x.data();
          bool prev_relu_minmax = false;
          std::size_t prev_team = 1;
          for (std::size_t j = 0; j < pl.ffn.size(); ++j) {
            const bool has_relu = (j + 1 < pl.ffn.size());
            float* ffn_out = has_relu ? (j % 2 ? s.ffn_b.data() : s.ffn_a.data()) : s.output.data();
            if (j > 0) {
              if (prev_relu_minmax) {
                const std::size_t cols = pl.ffn[j].rows;
                for (std::size_t m = 0; m < active; ++m) {
                  s.norm_mins[m] = 0.0f;
                  float mx = s.relu_maxs[m];
                  for (std::size_t t = 1; t < prev_team; ++t) {
                    mx = std::max(mx, s.relu_maxs[t * active + m]);
                  }
                  s.norm_maxs[m] = mx;
                }
                if (active >= 4 && step_threads >= 2) {
                  const std::size_t qteam = std::min(step_threads, active);
                  pool.parallelFor(qteam, [&](std::size_t tid, std::size_t nt) {
                    const std::size_t mb = (active * tid) / nt;
                    const std::size_t me = (active * (tid + 1)) / nt;
                    if (mb < me) {
                      packed::quantizeLinearU8FromMinMax(
                          ffn_in + mb * cols, me - mb, cols, cols,
                          s.norm_mins.data() + mb, s.norm_maxs.data() + mb,
                          s.a_q.data() + mb * cols, cols,
                          s.a_scales.data() + mb, s.a_zps.data() + mb);
                    }
                  });
                } else {
                  packed::quantizeLinearU8FromMinMax(
                      ffn_in, active, cols, cols,
                      s.norm_mins.data(), s.norm_maxs.data(),
                      s.a_q.data(), cols, s.a_scales.data(), s.a_zps.data());
                }
              } else {
                quantizeActiveRows(ffn_in, pl.ffn[j].rows);
              }
            }
            const bool parallel_ffn = (pl.ffn[j].cols >= dim * 2) ||
                                      (pl.ffn[j].rows >= dim * 2) || (active >= 3);
            prev_team = shardMatmul(pl.ffn[j], ffn_out, has_relu, parallel_ffn);
            prev_relu_minmax = (has_relu && pl.ffn[j].weight != nullptr);
            ffn_in = ffn_out;
          }
          for (std::size_t i = 0; i < active * dim; ++i) s.x[i] += ffn_in[i];
          if (li + 1 < depth) {
            normAndQuantize(pl.ffn_ln_scale, pl.ffn_ln_bias);
          } else if (active >= 4 && step_threads >= 2) {
            const std::size_t team = std::min(step_threads, active);
            pool.parallelFor(team, [&](std::size_t tid, std::size_t nt) {
              const std::size_t mb = (active * tid) / nt;
              const std::size_t me = (active * (tid + 1)) / nt;
              if (mb < me) {
                ops::instanceNormAffine(s.x.data() + mb * dim, pl.ffn_ln_scale, pl.ffn_ln_bias,
                                        me - mb, dim, s.x.data() + mb * dim);
              }
            });
          } else {
            ops::instanceNormAffine(s.x.data(), pl.ffn_ln_scale, pl.ffn_ln_bias,
                                    active, dim, s.x.data());
          }
        }

        // 3. Logit projection & argmax.
        bool any_full = false;
        bool any_short = false;
        for (std::size_t m = 0; m < active; ++m) {
          if (s.slot_reqs[m].candidate_count == 0) any_full = true;
          else any_short = true;
        }
        if (any_full && !any_short) {
          native_->getFullVocab().argMaxBatch(s.x.data(), active, dim,
                                              s.chosen_tokens.data(), step_threads);
        } else if (!any_full && any_short) {
          const auto& sl = native_->getShortlist();
          if (active == 1) {
            const auto& req = s.slot_reqs[0];
            const std::size_t best = sl.argmax(s.x.data(), req.candidate_ids,
                                               req.candidate_count, s.packed_ws, true);
            if (best >= req.candidate_count)
              throw std::runtime_error("SSRU invalid shortlist index");
            s.chosen_tokens[0] = req.candidate_ids[best];
          } else {
            if (shortlist_union_dirty) {
              s.union_pairs.clear();
              for (std::size_t m = 0; m < active; ++m) {
                const auto& req = s.slot_reqs[m];
                const std::uint32_t bit = static_cast<std::uint32_t>(1u) << m;
                for (std::size_t j = 0; j < req.candidate_count; ++j) {
                  s.union_pairs.push_back(CandPair{req.candidate_ids[j], bit});
                }
              }
              std::sort(s.union_pairs.begin(), s.union_pairs.end(),
                        [](const CandPair& a, const CandPair& b) { return a.id < b.id; });
              s.union_ids.clear();
              s.slot_masks.clear();
              for (const auto& p : s.union_pairs) {
                if (!s.union_ids.empty() && s.union_ids.back() == p.id) {
                  s.slot_masks.back() |= p.bit;
                } else {
                  s.union_ids.push_back(p.id);
                  s.slot_masks.push_back(p.bit);
                }
              }
              shortlist_union_dirty = false;
            }
            sl.argMaxBatch(s.x.data(), active, dim, s.union_ids.data(),
                           s.slot_masks.data(), s.union_ids.size(), s.chosen_tokens.data());
          }
        } else {
          for (std::size_t m = 0; m < active; ++m) {
            const auto& req = s.slot_reqs[m];
            const float* row = s.x.data() + m * dim;
            if (req.candidate_count == 0) {
              native_->getFullVocab().argMaxBatch(row, 1, dim, &s.chosen_tokens[m], step_threads);
            } else {
              const std::size_t best = native_->getShortlist().argmax(
                  row, req.candidate_ids, req.candidate_count, s.packed_ws, true);
              if (best >= req.candidate_count)
                throw std::runtime_error("SSRU invalid shortlist index");
              s.chosen_tokens[m] = req.candidate_ids[best];
            }
          }
        }

        // 4. Record tokens, retire completed requests, and refill/compact slots.
        recordAndRetire();
      }
      return;
    }
#else
    (void)max_threads;
    (void)active_submitted;
#endif
    (void)shortlist_union_dirty;

    // Scalar / LinearBackend continuous-batching fallback.
    if (s.scores.size() < max_src_len) s.scores.resize(max_src_len);
    const auto runLinear = [&](const Linear& l, const float* input, float* output,
                               bool separate_bias, std::size_t m) {
      if (backend.matmul) {
        if (!l.weight.data) throw std::invalid_argument("SSRU FP32 matrix required for LinearBackend");
        const float* in_row = input + m * l.weight.rows;
        float* out_row = output + m * l.weight.cols;
        if (l.bias && backend.matmul_bias) {
          backend.matmul_bias(backend.context, l.weight, in_row, 1, l.bias, separate_bias, out_row);
          finite(out_row, l.weight.cols, "linear fused bias");
        } else {
          backend.matmul(backend.context, l.weight, in_row, 1, out_row);
          if (l.bias) {
            for (std::size_t c = 0; c < l.weight.cols; ++c) out_row[c] += l.bias[c];
          }
          finite(out_row, l.weight.cols, l.weight.name.c_str());
        }
        return;
      }
      scalarLinearBatch(l.weight, l.int8_weight, l.bias,
                        input + m * l.weight.rows, 1, output + m * l.weight.cols);
    };

    while (active > 0) {
      for (std::size_t m = 0; m < active; ++m) {
        const std::size_t pos = s.slot_pos[m];
        const std::size_t prev_tok = pos ? static_cast<std::size_t>(s.slot_prev_token[m]) : 0;
        float* x_row = s.x.data() + m * dim;
        for (std::size_t j = 0; j < dim; ++j)
          x_row[j] = static_cast<float>(pos) * frequency_[j] + offset_[j];
        if (math.sin) math.sin(math.context, x_row, dim);
        else for (std::size_t j = 0; j < dim; ++j) x_row[j] = std::sin(x_row[j]);
        if (pos) {
          for (std::size_t j = 0; j < dim; ++j)
            x_row[j] = readEmbedValue(embedding_, int8_embedding_, prev_tok, j, dim) * emb_scale + x_row[j];
        }
        finite(x_row, dim, "batch scalar position");
      }

      for (std::size_t li = 0; li < depth; ++li) {
        const auto& layer = layers_[li];
        for (std::size_t m = 0; m < active; ++m) {
          const bool sep = (s.slot_pos[m] == 0 && li == 0);
          if (backend.matmul && backend.matmul_pair) {
            if (!layer.gate.weight.data || !layer.state.weight.data)
              throw std::invalid_argument("SSRU FP32 matrix required for LinearBackend");
            backend.matmul_pair(backend.context, layer.gate.weight, layer.gate.bias, sep,
                                s.gate.data() + m * dim, layer.state.weight,
                                s.update.data() + m * dim, s.x.data() + m * dim, 1);
            finite(s.gate.data() + m * dim, dim, "linear gate");
            finite(s.update.data() + m * dim, dim, "linear state");
          } else {
            runLinear(layer.gate, s.x.data(), s.gate.data(), sep, m);
            runLinear(layer.state, s.x.data(), s.update.data(), false, m);
          }
          if (math.sigmoid) {
            math.sigmoid(math.context, s.gate.data() + m * dim, dim);
            finite(s.gate.data() + m * dim, dim, "callback sigmoid");
          }
          float* cell = s.cells.data() + li * cap_dim + m * dim;
          float* x_row = s.x.data() + m * dim;
          const float* gate_row = s.gate.data() + m * dim;
          const float* upd_row = s.update.data() + m * dim;
          for (std::size_t j = 0; j < dim; ++j) {
            const float g = math.sigmoid ? gate_row[j] : 1.0f / (1.0f + std::exp(-gate_row[j]));
            cell[j] = g * cell[j] + (1.0f - g) * upd_row[j];
            x_row[j] += std::max(0.0f, cell[j]);
          }
          finite(cell, dim, "recurrent cell");
          if (math.normalize) {
            math.normalize(math.context, x_row, dim, layer.rnn_norm.scale, layer.rnn_norm.bias);
            finite(x_row, dim, "callback normalization");
          } else normalize(x_row, dim, layer.rnn_norm.scale, layer.rnn_norm.bias);

          runLinear(layer.query, s.x.data(), s.query.data(), sep, m);
          const auto& req = s.slot_reqs[m];
          const float* keys = req.keys[li].data;
          const float* values = req.values[li].data;
          float* q_row = s.query.data() + m * dim;
          float* ctx_row = s.context.data() + m * dim;
          if (math.attention) {
            math.attention(math.context, q_row, keys, values, heads, head_dim,
                           req.source_length, ctx_row);
          } else {
            for (std::size_t h = 0; h < heads; ++h) {
              for (std::size_t sp = 0; sp < req.source_length; ++sp) {
                float score = 0.0f;
                for (std::size_t k = 0; k < head_dim; ++k)
                  score += q_row[h * head_dim + k] * keys[(h * head_dim + k) * req.source_length + sp];
                s.scores[sp] = score * attn_scale;
              }
              finite(s.scores.data(), req.source_length, "attention scores");
              const float mx = *std::max_element(s.scores.begin(), s.scores.begin() + static_cast<std::ptrdiff_t>(req.source_length));
              float total = 0.0f;
              for (std::size_t sp = 0; sp < req.source_length; ++sp) {
                s.scores[sp] = std::exp(s.scores[sp] - mx);
                total += s.scores[sp];
              }
              if (!std::isfinite(total) || total <= 0.0f) throw std::runtime_error("SSRU invalid softmax");
              for (std::size_t sp = 0; sp < req.source_length; ++sp) s.scores[sp] /= total;
              for (std::size_t k = 0; k < head_dim; ++k) {
                float sum = 0.0f;
                for (std::size_t sp = 0; sp < req.source_length; ++sp)
                  sum += s.scores[sp] * values[(h * req.source_length + sp) * head_dim + k];
                ctx_row[h * head_dim + k] = sum;
              }
            }
          }
          finite(ctx_row, dim, "attention context");

          runLinear(layer.attention, s.context.data(), s.output.data(), false, m);
          for (std::size_t j = 0; j < dim; ++j) x_row[j] += s.output[m * dim + j];
          if (math.normalize) {
            math.normalize(math.context, x_row, dim, layer.attention_norm.scale, layer.attention_norm.bias);
            finite(x_row, dim, "callback normalization");
          } else normalize(x_row, dim, layer.attention_norm.scale, layer.attention_norm.bias);

          const float* ffn_in = s.x.data();
          for (std::size_t j = 0; j < layer.ffn.size(); ++j) {
            const bool has_relu = (j + 1 < layer.ffn.size());
            float* ffn_out = has_relu ? (j % 2 ? s.ffn_b.data() : s.ffn_a.data()) : s.output.data();
            runLinear(layer.ffn[j], ffn_in, ffn_out, false, m);
            if (has_relu) {
              float* out_row = ffn_out + m * layer.ffn[j].weight.cols;
              for (std::size_t k = 0; k < layer.ffn[j].weight.cols; ++k)
                out_row[k] = std::max(0.0f, out_row[k]);
            }
            ffn_in = ffn_out;
          }
          const float* final_ffn = ffn_in + m * dim;
          for (std::size_t j = 0; j < dim; ++j) x_row[j] += final_ffn[j];
          if (math.normalize) {
            math.normalize(math.context, x_row, dim, layer.ffn_norm.scale, layer.ffn_norm.bias);
            finite(x_row, dim, "callback normalization");
          } else normalize(x_row, dim, layer.ffn_norm.scale, layer.ffn_norm.bias);
        }
      }

      for (std::size_t m = 0; m < active; ++m) {
        const auto& req = s.slot_reqs[m];
        const float* x_row = s.x.data() + m * dim;
        if (req.candidate_count == 0) {
          if (backend.argmax_full) {
            const std::size_t tok = backend.argmax_full(backend.context, x_row, dim);
            if (tok >= vocab) throw std::runtime_error("SSRU invalid full-vocab token");
            s.chosen_tokens[m] = static_cast<std::int64_t>(tok);
          } else {
            std::size_t best = 0;
            float best_logit = -std::numeric_limits<float>::infinity();
            for (std::size_t t = 0; t < vocab; ++t) {
              float dot = 0.0f;
              for (std::size_t k = 0; k < dim; ++k)
                dot += x_row[k] * readEmbedValue(embedding_, int8_embedding_, t, k, dim);
              const float logit = dot + (t == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[t]);
              if (!std::isfinite(logit) && !(logit == -std::numeric_limits<float>::infinity() && t == 1))
                throw std::runtime_error("SSRU nonfinite logit");
              if (t == 0 || logit > best_logit) {
                best_logit = logit;
                best = t;
              }
            }
            s.chosen_tokens[m] = static_cast<std::int64_t>(best);
          }
        } else {
          if (backend.argmax_shortlist) {
            const std::size_t best = backend.argmax_shortlist(
                backend.context, x_row, dim, embedding_.data, output_bias_,
                req.candidate_ids, req.candidate_count);
            if (best >= req.candidate_count) throw std::runtime_error("SSRU invalid shortlist index");
            s.chosen_tokens[m] = req.candidate_ids[best];
          } else if (backend.matmul) {
            s.projection.resize(multiply(dim, req.candidate_count));
            s.bias.resize(req.candidate_count);
            s.logits.resize(req.candidate_count);
            for (std::size_t j = 0; j < req.candidate_count; ++j) {
              const auto token = static_cast<std::size_t>(req.candidate_ids[j]);
              s.bias[j] = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
              for (std::size_t k = 0; k < dim; ++k)
                s.projection[k * req.candidate_count + j] =
                    readEmbedValue(embedding_, int8_embedding_, token, k, dim);
            }
            const Matrix projection{"shortlist_projection_T", s.projection.data(), dim, req.candidate_count};
            backend.matmul(backend.context, projection, x_row, 1, s.logits.data());
            finite(s.logits.data(), req.candidate_count, projection.name.c_str());
            std::size_t best = 0;
            for (std::size_t j = 0; j < req.candidate_count; ++j) {
              s.logits[j] += s.bias[j];
              if (!std::isfinite(s.logits[j]) &&
                  !(s.logits[j] == -std::numeric_limits<float>::infinity() && req.candidate_ids[j] == 1))
                throw std::runtime_error("SSRU nonfinite logit");
              if (s.logits[j] > s.logits[best]) best = j;
            }
            s.chosen_tokens[m] = req.candidate_ids[best];
          } else {
            std::size_t best = 0;
            float best_logit = 0.0f;
            for (std::size_t j = 0; j < req.candidate_count; ++j) {
              const auto token = static_cast<std::size_t>(req.candidate_ids[j]);
              float dot = 0.0f;
              for (std::size_t k = 0; k < dim; ++k)
                dot += x_row[k] * readEmbedValue(embedding_, int8_embedding_, token, k, dim);
              const float logit = dot + (token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token]);
              if (!std::isfinite(logit) && !(logit == -std::numeric_limits<float>::infinity() && token == 1))
                throw std::runtime_error("SSRU nonfinite logit");
              if (j == 0 || logit > best_logit) {
                best_logit = logit;
                best = j;
              }
            }
            s.chosen_tokens[m] = req.candidate_ids[best];
          }
        }
      }

      recordAndRetire();
    }
  } catch (...) {
    s.slot_reqs.clear();
    workspace.results_.clear();
    throw;
  }
}

}  // namespace mtran::ssru
