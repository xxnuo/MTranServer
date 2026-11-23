#include "encoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

void validateView(const TensorView& tensor, const char* name) {
  std::size_t size = 1;
  for (auto dim : tensor.shape) {
    if (dim <= 0 || static_cast<std::uint64_t>(dim) > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument(std::string("SSRU invalid shape: ") + name);
    size = multiply(size, static_cast<std::size_t>(dim));
  }
  if (!tensor.data || size != tensor.size)
    throw std::invalid_argument(std::string("SSRU invalid storage: ") + name);
}

void validateInt8View(const Int8TensorView& tensor, const char* name) {
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

void normalizeRow(float* x, std::size_t dim, const float* scale, const float* bias) {
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

void scalarLinearRows(const Matrix& fp, const Int8TensorView& q8, const float* bias,
                      const float* input, std::size_t rows, float* output) {
  const std::size_t in_dim = q8.data ? static_cast<std::size_t>(q8.shape[0]) : fp.rows;
  const std::size_t out_dim = q8.data ? static_cast<std::size_t>(q8.shape[1]) : fp.cols;
  for (std::size_t r = 0; r < rows; ++r) {
    const float* x = input + r * in_dim;
    float* y = output + r * out_dim;
    for (std::size_t c = 0; c < out_dim; ++c) {
      float sum = 0.0f;
      if (q8.data) {
        const float s = q8.scale;
        for (std::size_t k = 0; k < in_dim; ++k)
          sum += x[k] * (static_cast<float>(q8.data[k * out_dim + c]) * s);
      } else {
        for (std::size_t k = 0; k < in_dim; ++k)
          sum += x[k] * fp.data[k * out_dim + c];
      }
      y[c] = bias ? (sum + bias[c]) : sum;
    }
  }
  finite(output, multiply(rows, out_dim), "encoder scalar linear");
}

void scalarSelfAttention(const float* q, const float* k, const float* v,
                         std::size_t seq_len, std::size_t heads, std::size_t head_dim,
                         float attention_scale, float* scores, float* out) {
  const std::size_t dim = heads * head_dim;
  for (std::size_t h = 0; h < heads; ++h) {
    for (std::size_t qi = 0; qi < seq_len; ++qi) {
      const float* q_row = q + qi * dim + h * head_dim;
      float maximum = -std::numeric_limits<float>::infinity();
      for (std::size_t ki = 0; ki < seq_len; ++ki) {
        const float* k_row = k + ki * dim + h * head_dim;
        float dot = 0.0f;
        for (std::size_t d = 0; d < head_dim; ++d) dot += q_row[d] * k_row[d];
        const float s = dot * attention_scale;
        scores[ki] = s;
        if (s > maximum) maximum = s;
      }
      finite(scores, seq_len, "encoder attention scores");
      float total = 0.0f;
      for (std::size_t ki = 0; ki < seq_len; ++ki) {
        scores[ki] = std::exp(scores[ki] - maximum);
        total += scores[ki];
      }
      if (!std::isfinite(total) || total <= 0.0f)
        throw std::runtime_error("SSRU invalid encoder softmax");
      for (std::size_t ki = 0; ki < seq_len; ++ki) scores[ki] /= total;
      float* out_row = out + qi * dim + h * head_dim;
      for (std::size_t d = 0; d < head_dim; ++d) {
        float sum = 0.0f;
        for (std::size_t ki = 0; ki < seq_len; ++ki)
          sum += scores[ki] * v[ki * dim + h * head_dim + d];
        out_row[d] = sum;
      }
    }
  }
}

void transposeKV(const float* src, std::size_t heads, std::size_t head_dim,
                 std::size_t src_len, bool is_value, float* dst) {
  const std::size_t dim = heads * head_dim;
  if (!is_value) {
    // [src_len, heads, head_dim] -> [heads, head_dim, src_len]
    for (std::size_t h = 0; h < heads; ++h) {
      for (std::size_t s = 0; s < src_len; ++s) {
        const float* row = src + s * dim + h * head_dim;
        for (std::size_t d = 0; d < head_dim; ++d)
          dst[(h * head_dim + d) * src_len + s] = row[d];
      }
    }
  } else {
    // [src_len, heads, head_dim] -> [heads, src_len, head_dim]
    for (std::size_t h = 0; h < heads; ++h) {
      float* head_dst = dst + h * src_len * head_dim;
      for (std::size_t s = 0; s < src_len; ++s)
        std::memcpy(head_dst + s * head_dim, src + s * dim + h * head_dim,
                    head_dim * sizeof(float));
    }
  }
}

}  // namespace

struct EncodedBatch::Scratch {
  std::vector<float> x, q, k, v, attn, proj, ffn_a, ffn_b, kv_proj, scores;
#ifdef MTRAN_SSRU_PACKED
  std::vector<std::uint8_t> a_q, a_zps;
  std::vector<float> a_scales, relu_maxs;
#endif
};

EncodedBatch::EncodedBatch() : scratch_(std::make_unique<Scratch>()) {}
EncodedBatch::~EncodedBatch() = default;
EncodedBatch::EncodedBatch(EncodedBatch&&) noexcept = default;
EncodedBatch& EncodedBatch::operator=(EncodedBatch&&) noexcept = default;

TensorView EncodedBatch::key_view(std::size_t batch_idx, std::size_t layer) const {
  if (batch_idx >= lengths_.size() || layer >= dec_depth_)
    throw std::out_of_range("SSRU EncodedBatch key index out of range");
  const std::size_t len = lengths_[batch_idx];
  const std::size_t off = offsets_[batch_idx];
  const std::size_t total = offsets_.back() + lengths_.back();
  const std::size_t dim = heads_ * head_dim_;
  return TensorView{
      keys_data_.data() + (layer * total + off) * dim,
      len * dim,
      {1, static_cast<std::int64_t>(heads_), static_cast<std::int64_t>(head_dim_),
       static_cast<std::int64_t>(len)}};
}

TensorView EncodedBatch::value_view(std::size_t batch_idx, std::size_t layer) const {
  if (batch_idx >= lengths_.size() || layer >= dec_depth_)
    throw std::out_of_range("SSRU EncodedBatch value index out of range");
  const std::size_t len = lengths_[batch_idx];
  const std::size_t off = offsets_[batch_idx];
  const std::size_t total = offsets_.back() + lengths_.back();
  const std::size_t dim = heads_ * head_dim_;
  return TensorView{
      vals_data_.data() + (layer * total + off) * dim,
      len * dim,
      {1, static_cast<std::int64_t>(heads_), static_cast<std::int64_t>(len),
       static_cast<std::int64_t>(head_dim_)}};
}

void EncodedBatch::populate_requests(std::vector<Request>& requests) const {
  const std::size_t batch_size = lengths_.size();
  requests.resize(batch_size);
  for (std::size_t b = 0; b < batch_size; ++b) {
    requests[b].source_length = lengths_[b];
    requests[b].prevalidated_kv = true;
    requests[b].keys.resize(dec_depth_);
    requests[b].values.resize(dec_depth_);
    for (std::size_t d = 0; d < dec_depth_; ++d) {
      requests[b].keys[d] = key_view(b, d);
      requests[b].values[d] = value_view(b, d);
    }
  }
}

#ifdef MTRAN_SSRU_PACKED
namespace {

struct PackedLinear {
  std::unique_ptr<packed::Weight> weight;
  const float* extra_bias = nullptr;
  std::size_t rows = 0;
  std::size_t cols = 0;
};

struct PackedEncoderLayer {
  PackedLinear q, k, v, o;
  const float* self_ln_scale = nullptr;
  const float* self_ln_bias = nullptr;
  std::vector<PackedLinear> ffn;
  const float* ffn_ln_scale = nullptr;
  const float* ffn_ln_bias = nullptr;
};

void addExtraBiasSlice(const float* bias, std::size_t rows, std::size_t stride,
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

void runMultiplySlice(const PackedLinear& pl,
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
    addExtraBiasSlice(pl.extra_bias, rows, out_stride, col_begin, col_end, relu, out);
  }
}

}  // namespace

struct Encoder::NativeState {
  ops::PosTable pos_table;
  std::vector<PackedEncoderLayer> layers;
  std::vector<PackedLinear> kv_mats;  // [2 * dec_depth]: even=Wk, odd=Wv

  NativeState(ops::PosTable pt,
              std::vector<PackedEncoderLayer> ls,
              std::vector<PackedLinear> kvs)
      : pos_table(std::move(pt)), layers(std::move(ls)), kv_mats(std::move(kvs)) {}
};
#else
struct Encoder::NativeState {};
#endif

Encoder::Encoder(EncoderConfig config,
                 const std::vector<NamedTensor>& weights,
                 const std::vector<NamedInt8Tensor>& int8_weights)
    : config_(config) {
  const auto dim = config.dim;
  if (dim < 4 || dim % 2 || !config.heads || dim % config.heads ||
      !config.enc_depth || !config.dec_depth || !config.ffn_depth ||
      dim > static_cast<std::size_t>(INT64_MAX))
    throw std::invalid_argument("SSRU invalid encoder configuration");
  multiply(config.enc_depth, dim);
  multiply(config.dec_depth, dim);

  std::unordered_map<std::string, const TensorView*> fp_names;
  std::unordered_map<std::string, const Int8TensorView*> i8_names;
  for (const auto& w : weights) {
    if (!fp_names.emplace(w.name, &w.tensor).second)
      throw std::invalid_argument("SSRU duplicate weight: " + w.name);
  }
  for (const auto& w : int8_weights) {
    if (fp_names.count(w.name) || !i8_names.emplace(w.name, &w.tensor).second)
      throw std::invalid_argument("SSRU duplicate weight: " + w.name);
  }

  const auto get_fp = [&](const std::string& name) -> const TensorView& {
    const auto it = fp_names.find(name);
    if (it == fp_names.end())
      throw std::invalid_argument("SSRU missing weight: " + name);
    validateView(*it->second, name.c_str());
    return *it->second;
  };
  const auto vector = [&](const std::string& name, std::size_t width) -> const float* {
    const auto& t = get_fp(name);
    if (t.shape != std::vector<std::int64_t>{static_cast<std::int64_t>(width)} &&
        t.shape != std::vector<std::int64_t>{1, static_cast<std::int64_t>(width)})
      throw std::invalid_argument("SSRU invalid vector: " + name);
    finite(t.data, t.size, name.c_str());
    return t.data;
  };
  const auto linear = [&](const std::string& name, const std::string& bias,
                          std::size_t input, std::size_t output = 0) -> Linear {
    const auto i8_it = i8_names.find(name);
    if (i8_it != i8_names.end()) {
      const auto& t = *i8_it->second;
      validateInt8View(t, name.c_str());
      if (t.shape.size() != 2 || t.shape[0] != static_cast<std::int64_t>(input) ||
          (output && t.shape[1] != static_cast<std::int64_t>(output)))
        throw std::invalid_argument("SSRU invalid matrix: " + name);
      const auto cols = static_cast<std::size_t>(t.shape[1]);
      return Linear{Matrix{name, nullptr, input, cols}, t,
                    bias.empty() ? nullptr : vector(bias, cols)};
    }
    const auto& t = get_fp(name);
    if (t.shape.size() != 2 || t.shape[0] != static_cast<std::int64_t>(input) ||
        (output && t.shape[1] != static_cast<std::int64_t>(output)))
      throw std::invalid_argument("SSRU invalid matrix: " + name);
    finite(t.data, t.size, name.c_str());
    const auto cols = static_cast<std::size_t>(t.shape[1]);
    return Linear{Matrix{name, t.data, input, cols}, Int8TensorView{},
                  bias.empty() ? nullptr : vector(bias, cols)};
  };
  const auto norm = [&](const std::string& prefix) -> Norm {
    return Norm{vector(prefix + "_scale", dim), vector(prefix + "_bias", dim)};
  };

  const std::string emb_name =
      (i8_names.count("Wemb") || fp_names.count("Wemb")) ? "Wemb" : "encoder_Wemb";
  if (const auto i8_emb = i8_names.find(emb_name); i8_emb != i8_names.end()) {
    validateInt8View(*i8_emb->second, emb_name.c_str());
    int8_embedding_ = *i8_emb->second;
    if (int8_embedding_.shape.size() != 2 ||
        int8_embedding_.shape[1] != static_cast<std::int64_t>(dim) ||
        int8_embedding_.shape[0] < 2)
      throw std::invalid_argument("SSRU invalid source embedding");
    vocab_size_ = static_cast<std::size_t>(int8_embedding_.shape[0]);
  } else {
    embedding_ = get_fp(emb_name);
    if (embedding_.shape.size() != 2 ||
        embedding_.shape[1] != static_cast<std::int64_t>(dim) ||
        embedding_.shape[0] < 2)
      throw std::invalid_argument("SSRU invalid source embedding");
    finite(embedding_.data, embedding_.size, "encoder embedding");
    vocab_size_ = static_cast<std::size_t>(embedding_.shape[0]);
  }

  max_ffn_width_ = dim;
  layers_.reserve(config.enc_depth);
  for (std::size_t i = 1; i <= config.enc_depth; ++i) {
    const std::string p = "encoder_l" + std::to_string(i);
    EncoderLayer layer;
    layer.q = linear(p + "_self_Wq", p + "_self_bq", dim, dim);
    layer.k = linear(p + "_self_Wk", p + "_self_bk", dim, dim);
    layer.v = linear(p + "_self_Wv", p + "_self_bv", dim, dim);
    layer.o = linear(p + "_self_Wo", p + "_self_bo", dim, dim);
    layer.self_norm = norm(p + "_self_Wo_ln");
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

  context_kv_.reserve(config.dec_depth);
  for (std::size_t d = 1; d <= config.dec_depth; ++d) {
    const std::string p = "decoder_l" + std::to_string(d) + "_context_";
    ContextKV kv;
    kv.k = linear(p + "Wk", p + "bk", dim, dim);
    kv.v = linear(p + "Wv", p + "bv", dim, dim);
    context_kv_.push_back(std::move(kv));
  }

  frequency_.resize(dim);
  offset_.resize(dim);
  if (fp_names.count("position_frequency") || fp_names.count("position_offset")) {
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
  if (packed::backend() != nullptr && dim >= 32 && dim % 32 == 0) {
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
      const auto packMat = [](const Linear& l) -> PackedLinear {
        PackedLinear pl;
        pl.rows = l.weight.rows;
        pl.cols = l.weight.cols;
        if (l.int8_weight.data) {
          pl.weight = std::make_unique<packed::Weight>(packed::Weight::packInt8(
              l.int8_weight.data, l.int8_weight.scale, pl.rows, pl.cols,
              kUseFusedBias ? l.bias : nullptr, kUseFusedBias ? nullptr : l.bias, true));
          pl.extra_bias = nullptr;
        } else {
          pl.weight = std::make_unique<packed::Weight>(l.weight.data, pl.rows, pl.cols);
          pl.extra_bias = l.bias;
        }
        return pl;
      };
      std::vector<PackedEncoderLayer> packed_layers;
      packed_layers.reserve(layers_.size());
      for (const auto& l : layers_) {
        PackedEncoderLayer pl;
        pl.q = packMat(l.q);
        pl.k = packMat(l.k);
        pl.v = packMat(l.v);
        pl.o = packMat(l.o);
        pl.self_ln_scale = l.self_norm.scale;
        pl.self_ln_bias = l.self_norm.bias;
        pl.ffn.reserve(l.ffn.size());
        for (const auto& f : l.ffn) pl.ffn.push_back(packMat(f));
        pl.ffn_ln_scale = l.ffn_norm.scale;
        pl.ffn_ln_bias = l.ffn_norm.bias;
        packed_layers.push_back(std::move(pl));
      }
      std::vector<PackedLinear> packed_kvs;
      packed_kvs.reserve(context_kv_.size() * 2);
      for (const auto& kv : context_kv_) {
        packed_kvs.push_back(packMat(kv.k));
        packed_kvs.push_back(packMat(kv.v));
      }
      native_ = std::make_unique<NativeState>(
          ops::PosTable::create(frequency_.data(), offset_.data(), dim, kPosTableMaxPos),
          std::move(packed_layers), std::move(packed_kvs));
    }
  }
#endif
}

Encoder::~Encoder() = default;
Encoder::Encoder(Encoder&&) noexcept = default;
Encoder& Encoder::operator=(Encoder&&) noexcept = default;

bool Encoder::has_native_fast() const noexcept {
  return native_ != nullptr;
}

void Encoder::encodeBatch(const std::int64_t* const* inputs,
                          const std::size_t* lengths,
                          std::size_t batch_size,
                          EncodedBatch& out,
                          std::size_t max_threads,
                          const std::atomic<std::size_t>* active_submitted) const {
  if (!out.scratch_) out.scratch_ = std::make_unique<EncodedBatch::Scratch>();
  auto& s = *out.scratch_;

  try {
    if (batch_size == 0) {
      out.dec_depth_ = config_.dec_depth;
      out.heads_ = config_.heads;
      out.head_dim_ = config_.dim / config_.heads;
      out.lengths_.clear();
      out.offsets_.clear();
      return;
    }
    if (!inputs || !lengths)
      throw std::invalid_argument("SSRU null batch encoder input");

    const std::size_t dim = config_.dim;
    const std::size_t heads = config_.heads;
    const std::size_t head_dim = dim / heads;
    const std::size_t dec_depth = config_.dec_depth;

    out.lengths_.resize(batch_size);
    out.offsets_.resize(batch_size);
    std::size_t total_tokens = 0;
    std::size_t max_len = 0;
    for (std::size_t b = 0; b < batch_size; ++b) {
      const std::size_t len = lengths[b];
      if (!len || !inputs[b])
        throw std::invalid_argument("SSRU invalid source sequence in batch");
      for (std::size_t t = 0; t < len; ++t) {
        const auto tok = inputs[b][t];
        if (tok < 0 || static_cast<std::uint64_t>(tok) >= vocab_size_)
          throw std::invalid_argument("SSRU source token outside vocabulary");
      }
      out.offsets_[b] = total_tokens;
      out.lengths_[b] = len;
      if (len > std::numeric_limits<std::size_t>::max() - total_tokens)
        throw std::invalid_argument("SSRU tensor size overflow");
      total_tokens += len;
      max_len = std::max(max_len, len);
    }

    out.dec_depth_ = dec_depth;
    out.heads_ = heads;
    out.head_dim_ = head_dim;

    const std::size_t total_dim = multiply(total_tokens, dim);
    const std::size_t total_kv_size = multiply(dec_depth, total_dim);
    out.keys_data_.resize(total_kv_size);
    out.vals_data_.resize(total_kv_size);

    s.x.resize(total_dim);
    s.q.resize(total_dim);
    s.k.resize(total_dim);
    s.v.resize(total_dim);
    s.attn.resize(total_dim);
    s.proj.resize(total_dim);
    s.ffn_a.resize(multiply(total_tokens, max_ffn_width_));
    s.ffn_b.resize(multiply(total_tokens, max_ffn_width_));
    s.kv_proj.resize(multiply(2 * dec_depth, total_dim));

    const float emb_scale = static_cast<float>(std::sqrt(static_cast<double>(dim)));

#ifdef MTRAN_SSRU_PACKED
    if (native_) {
      auto& pool = PcorePool::instance();
      const std::size_t capped = std::max<std::size_t>(1, max_threads);
      const std::size_t team_threads = std::min(capped, std::max<std::size_t>(1, pool.pcoreCount()));
      (void)active_submitted;

      s.a_q.resize(multiply(total_tokens, std::max(dim, max_ffn_width_)));
      s.a_scales.resize(total_tokens);
      s.a_zps.resize(total_tokens);
      s.relu_maxs.resize(multiply(team_threads, total_tokens));
      s.scores.resize(multiply(team_threads, multiply(heads, max_len)));

      const std::size_t num_kv = native_->kv_mats.size();
      const std::size_t qkv_chunks_per_mat = dim / 32;
      const std::size_t total_qkv_chunks = 3 * qkv_chunks_per_mat;
      const std::size_t total_attn_heads = batch_size * heads;
      const std::size_t kv_chunks_per_mat = dim / 32;
      const std::size_t total_kv_chunks = num_kv * kv_chunks_per_mat;
      const std::size_t total_scatter = batch_size * num_kv;
      const std::size_t scratch_stride = heads * max_len;

      TeamBarrier barrier(team_threads);
      pool.parallelFor(team_threads, [&](std::size_t tid, std::size_t nt) {
        struct BarrierCancelGuard {
          TeamBarrier& b;
          bool done = false;
          ~BarrierCancelGuard() {
            if (!done) b.cancel();
          }
        } cancel_guard{barrier};

        const std::size_t tb = (total_tokens * tid) / nt;
        const std::size_t te = (total_tokens * (tid + 1)) / nt;

        auto quantizeX = [&](const float* src, std::size_t cols) {
          if (batch_size == 1) {
            if (tid == 0) {
              float sb = 0.0f;
              std::uint8_t zb = 0;
              packed::quantizeLinearU8(src, lengths[0], cols, s.a_q.data(), sb, zb);
              std::fill_n(s.a_scales.data(), lengths[0], sb);
              std::fill_n(s.a_zps.data(), lengths[0], zb);
            }
          } else {
            for (std::size_t b = tid; b < batch_size; b += nt) {
              const std::size_t off = out.offsets_[b];
              const std::size_t len = lengths[b];
              float sb = 0.0f;
              std::uint8_t zb = 0;
              packed::quantizeLinearU8(src + off * cols, len, cols,
                                       s.a_q.data() + off * cols, sb, zb);
              std::fill_n(s.a_scales.data() + off, len, sb);
              std::fill_n(s.a_zps.data() + off, len, zb);
            }
          }
        };

        auto shardCols = [&](const PackedLinear& pl, float* dst, bool relu) {
          const std::size_t chunks = pl.cols / 32;
          const std::size_t cb = (chunks * tid) / nt;
          const std::size_t ce = (chunks * (tid + 1)) / nt;
          if (cb < ce) {
            runMultiplySlice(pl, s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                             total_tokens, dst, pl.cols,
                             s.relu_maxs.data() + tid * total_tokens,
                             relu, cb * 32, ce * 32);
          }
        };

        auto normAddSlice = [&](const float* residual_add, const float* scale, const float* bias) {
          if (tb < te) {
            const std::size_t start = tb * dim;
            const std::size_t end = te * dim;
            for (std::size_t i = start; i < end; ++i) s.x[i] += residual_add[i];
            ops::instanceNormAffine(s.x.data() + start, scale, bias, te - tb, dim,
                                    s.x.data() + start);
          }
        };

        // Step 1: Embed + sqrt(dim) + sinusoidal positional encoding (sharded by token).
        if (tb < te) {
          std::size_t cur_b = 0;
          while (cur_b + 1 < batch_size && tb >= out.offsets_[cur_b] + lengths[cur_b]) ++cur_b;
          for (std::size_t t = tb; t < te; ++t) {
            while (t >= out.offsets_[cur_b] + lengths[cur_b]) ++cur_b;
            const std::size_t p = t - out.offsets_[cur_b];
            const auto tok = static_cast<std::size_t>(inputs[cur_b][p]);
            float* dst = s.x.data() + t * dim;
            if (p < kPosTableMaxPos) {
              if (int8_embedding_.data) {
                native_->pos_table.addScaledEmbedInt8(
                    int8_embedding_.data + tok * dim, int8_embedding_.scale, emb_scale, p, dst);
              } else {
                native_->pos_table.addScaledEmbed(
                    embedding_.data + tok * dim, emb_scale, p, dst);
              }
            } else {
              const float fp = static_cast<float>(p);
              if (int8_embedding_.data) {
                const auto* row = int8_embedding_.data + tok * dim;
                const float ws = int8_embedding_.scale;
                for (std::size_t d = 0; d < dim; ++d) {
                  const float e = (static_cast<float>(row[d]) * ws) * emb_scale;
                  dst[d] = e + std::sin(fp * frequency_[d] + offset_[d]);
                }
              } else {
                const auto* row = embedding_.data + tok * dim;
                for (std::size_t d = 0; d < dim; ++d) {
                  dst[d] = row[d] * emb_scale + std::sin(fp * frequency_[d] + offset_[d]);
                }
              }
            }
          }
          finite(s.x.data() + tb * dim, (te - tb) * dim, "encoder embedding");
        }
        if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

        for (const auto& layer : native_->layers) {
          // 1. Single shared quantization of x -> Q, K, V in one parallel slice.
          quantizeX(s.x.data(), dim);
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

          const PackedLinear* qkv_mats[3] = {&layer.q, &layer.k, &layer.v};
          float* qkv_outs[3] = {s.q.data(), s.k.data(), s.v.data()};
          const std::size_t qgb = (total_qkv_chunks * tid) / nt;
          const std::size_t qge = (total_qkv_chunks * (tid + 1)) / nt;
          for (std::size_t m = 0; m < 3; ++m) {
            const std::size_t mb = m * qkv_chunks_per_mat;
            const std::size_t me = mb + qkv_chunks_per_mat;
            const std::size_t lb = std::max(qgb, mb);
            const std::size_t le = std::min(qge, me);
            if (lb < le) {
              runMultiplySlice(*qkv_mats[m], s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                               total_tokens, qkv_outs[m], dim, nullptr, false,
                               (lb - mb) * 32, (le - mb) * 32);
            }
          }
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

          // 2. Per-sentence ragged self-attention across (batch, head) slices.
          float* t_scores = s.scores.data() + tid * scratch_stride;
          for (std::size_t idx = tid; idx < total_attn_heads; idx += nt) {
            const std::size_t b = idx / heads;
            const std::size_t h = idx % heads;
            const std::size_t off = out.offsets_[b] * dim;
            ops::selfAttention(s.q.data() + off, s.k.data() + off, s.v.data() + off,
                               lengths[b], heads, head_dim, t_scores,
                               s.attn.data() + off, h, h + 1);
          }
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

          // 3. Wo + residual + instanceNormAffine.
          quantizeX(s.attn.data(), dim);
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }
          shardCols(layer.o, s.proj.data(), false);
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }
          normAddSlice(s.proj.data(), layer.self_ln_scale, layer.self_ln_bias);
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

          // 4. FFN + residual + instanceNormAffine.
          const float* ffn_in = s.x.data();
          for (std::size_t j = 0; j < layer.ffn.size(); ++j) {
            const bool has_relu = (j + 1 < layer.ffn.size());
            float* ffn_out = has_relu ? (j % 2 ? s.ffn_b.data() : s.ffn_a.data()) : s.proj.data();
            quantizeX(ffn_in, layer.ffn[j].rows);
            if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }
            shardCols(layer.ffn[j], ffn_out, has_relu);
            if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }
            ffn_in = ffn_out;
          }
          normAddSlice(ffn_in, layer.ffn_ln_scale, layer.ffn_ln_bias);
          if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }
        }

        // 5. Fused decoder K/V projections sharing one quantization of encoder output.
        quantizeX(s.x.data(), dim);
        if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

        const std::size_t kgb = (total_kv_chunks * tid) / nt;
        const std::size_t kge = (total_kv_chunks * (tid + 1)) / nt;
        for (std::size_t m = 0; m < num_kv; ++m) {
          const std::size_t mb = m * kv_chunks_per_mat;
          const std::size_t me = mb + kv_chunks_per_mat;
          const std::size_t lb = std::max(kgb, mb);
          const std::size_t le = std::min(kge, me);
          if (lb < le) {
            runMultiplySlice(native_->kv_mats[m], s.a_q.data(), s.a_scales.data(), s.a_zps.data(),
                             total_tokens, s.kv_proj.data() + m * total_dim, dim,
                             nullptr, false, (lb - mb) * 32, (le - mb) * 32);
          }
        }
        if (!barrier.arriveAndWait()) { cancel_guard.done = true; return; }

        // Check finite & direct-transpose into decoder K/V slices (zero extra barrier).
        for (std::size_t idx = tid; idx < total_scatter; idx += nt) {
          const std::size_t b = idx / num_kv;
          const std::size_t m = idx % num_kv;
          const std::size_t d_layer = m / 2;
          const bool is_v = (m % 2) == 1;
          const float* src_kv = s.kv_proj.data() + m * total_dim + out.offsets_[b] * dim;
          finite(src_kv, lengths[b] * dim, "encoder KV projections");
          float* dst_base = is_v ? out.vals_data_.data() : out.keys_data_.data();
          transposeKV(src_kv, heads, head_dim, lengths[b], is_v,
                      dst_base + (d_layer * total_tokens + out.offsets_[b]) * dim);
        }
        cancel_guard.done = true;
      });
      return;
    }
#else
    (void)max_threads;
    (void)active_submitted;
#endif

    // Scalar ragged fallback (standalone tests / non-32-aligned synthetic dimensions).
    s.scores.resize(max_len);
    for (std::size_t b = 0; b < batch_size; ++b) {
      const auto* seq = inputs[b];
      const std::size_t len = lengths[b];
      const std::size_t off = out.offsets_[b];
      for (std::size_t p = 0; p < len; ++p) {
        const auto tok = static_cast<std::size_t>(seq[p]);
        float* dst = s.x.data() + (off + p) * dim;
        const float fp = static_cast<float>(p);
        if (int8_embedding_.data) {
          const auto* row = int8_embedding_.data + tok * dim;
          const float ws = int8_embedding_.scale;
          for (std::size_t d = 0; d < dim; ++d) {
            const float e = (static_cast<float>(row[d]) * ws) * emb_scale;
            dst[d] = e + std::sin(fp * frequency_[d] + offset_[d]);
          }
        } else {
          const auto* row = embedding_.data + tok * dim;
          for (std::size_t d = 0; d < dim; ++d)
            dst[d] = row[d] * emb_scale + std::sin(fp * frequency_[d] + offset_[d]);
        }
      }
    }
    finite(s.x.data(), total_dim, "encoder embedding");

    const float attention_scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
    for (const auto& layer : layers_) {
      scalarLinearRows(layer.q.weight, layer.q.int8_weight, layer.q.bias,
                       s.x.data(), total_tokens, s.q.data());
      scalarLinearRows(layer.k.weight, layer.k.int8_weight, layer.k.bias,
                       s.x.data(), total_tokens, s.k.data());
      scalarLinearRows(layer.v.weight, layer.v.int8_weight, layer.v.bias,
                       s.x.data(), total_tokens, s.v.data());
      for (std::size_t b = 0; b < batch_size; ++b) {
        const std::size_t off = out.offsets_[b] * dim;
        scalarSelfAttention(s.q.data() + off, s.k.data() + off, s.v.data() + off,
                            lengths[b], heads, head_dim, attention_scale,
                            s.scores.data(), s.attn.data() + off);
      }
      finite(s.attn.data(), total_dim, "encoder attention");
      scalarLinearRows(layer.o.weight, layer.o.int8_weight, layer.o.bias,
                       s.attn.data(), total_tokens, s.proj.data());
      for (std::size_t i = 0; i < total_dim; ++i) s.x[i] += s.proj[i];
      for (std::size_t t = 0; t < total_tokens; ++t)
        normalizeRow(s.x.data() + t * dim, dim, layer.self_norm.scale, layer.self_norm.bias);

      const float* ffn_in = s.x.data();
      for (std::size_t j = 0; j < layer.ffn.size(); ++j) {
        const bool has_relu = (j + 1 < layer.ffn.size());
        float* ffn_out = has_relu ? (j % 2 ? s.ffn_b.data() : s.ffn_a.data()) : s.proj.data();
        scalarLinearRows(layer.ffn[j].weight, layer.ffn[j].int8_weight, layer.ffn[j].bias,
                         ffn_in, total_tokens, ffn_out);
        if (has_relu) {
          const std::size_t count = multiply(total_tokens, layer.ffn[j].weight.cols);
          for (std::size_t i = 0; i < count; ++i) ffn_out[i] = std::max(0.0f, ffn_out[i]);
        }
        ffn_in = ffn_out;
      }
      for (std::size_t i = 0; i < total_dim; ++i) s.x[i] += ffn_in[i];
      for (std::size_t t = 0; t < total_tokens; ++t)
        normalizeRow(s.x.data() + t * dim, dim, layer.ffn_norm.scale, layer.ffn_norm.bias);
    }

    for (std::size_t d = 0; d < dec_depth; ++d) {
      float* k_dst = s.kv_proj.data() + (2 * d) * total_dim;
      float* v_dst = s.kv_proj.data() + (2 * d + 1) * total_dim;
      scalarLinearRows(context_kv_[d].k.weight, context_kv_[d].k.int8_weight,
                       context_kv_[d].k.bias, s.x.data(), total_tokens, k_dst);
      scalarLinearRows(context_kv_[d].v.weight, context_kv_[d].v.int8_weight,
                       context_kv_[d].v.bias, s.x.data(), total_tokens, v_dst);
      for (std::size_t b = 0; b < batch_size; ++b) {
        const std::size_t off = out.offsets_[b];
        transposeKV(k_dst + off * dim, heads, head_dim, lengths[b], false,
                    out.keys_data_.data() + (d * total_tokens + off) * dim);
        transposeKV(v_dst + off * dim, heads, head_dim, lengths[b], true,
                    out.vals_data_.data() + (d * total_tokens + off) * dim);
      }
    }
    finite(out.keys_data_.data(), multiply(dec_depth, total_dim), "encoder scalar keys");
    finite(out.vals_data_.data(), multiply(dec_depth, total_dim), "encoder scalar vals");
  } catch (...) {
    out.dec_depth_ = 0;
    out.heads_ = 0;
    out.head_dim_ = 0;
    out.lengths_.clear();
    out.offsets_.clear();
    throw;
  }
}

}  // namespace mtran::ssru
