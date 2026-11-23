#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ssru.h"

namespace mtran::ssru {

struct EncoderConfig {
  std::size_t dim = 384;
  std::size_t heads = 8;
  std::size_t enc_depth = 6;
  std::size_t dec_depth = 4;
  std::size_t ffn_depth = 2;
};

class Encoder;

// Reusable encoder output + ragged workspace across successive encodeBatch calls.
// Stores per-layer cross-attention keys [1, heads, head_dim, src_len] and values
// [1, heads, src_len, head_dim] contiguously per sentence without padding waste.
class EncodedBatch {
 public:
  EncodedBatch();
  ~EncodedBatch();
  EncodedBatch(EncodedBatch&&) noexcept;
  EncodedBatch& operator=(EncodedBatch&&) noexcept;
  EncodedBatch(const EncodedBatch&) = delete;
  EncodedBatch& operator=(const EncodedBatch&) = delete;

  std::size_t batch_size() const noexcept { return lengths_.size(); }
  std::size_t dec_depth() const noexcept { return dec_depth_; }
  std::size_t heads() const noexcept { return heads_; }
  std::size_t head_dim() const noexcept { return head_dim_; }
  const std::vector<std::size_t>& lengths() const noexcept { return lengths_; }

  // Per-sentence, per-decoder-layer views for Decoder::decode / decodeBatch.
  TensorView key_view(std::size_t batch_idx, std::size_t layer) const;
  TensorView value_view(std::size_t batch_idx, std::size_t layer) const;

  // Populates source_length, keys, and values on requests[0..batch_size-1].
  // Caller keeps eos_id, candidate_ids, and candidate_count populated.
  void populate_requests(std::vector<Request>& requests) const;

 private:
  friend class Encoder;
  struct Scratch;
  std::size_t dec_depth_ = 0;
  std::size_t heads_ = 0;
  std::size_t head_dim_ = 0;
  std::vector<std::size_t> lengths_;
  std::vector<std::size_t> offsets_;
  std::vector<float> keys_data_;
  std::vector<float> vals_data_;
  std::unique_ptr<Scratch> scratch_;
};

// Required graph names (l=1..enc_depth, d=1..dec_depth, j=1..ffn_depth):
// Wemb OR encoder_Wemb [vocab,dim];
// encoder_l{l}_self_{Wq,Wk,Wv,Wo} [dim,dim], _{bq,bk,bv,bo} [1,dim],
//   _Wo_ln_{scale,bias} [1,dim];
// encoder_l{l}_ffn_W{j} [input,output], _b{j} [1,output],
//   _ffn_ln_{scale,bias} [1,dim];
// decoder_l{d}_context_{Wk,Wv} [dim,dim], _{bk,bv} [1,dim].
// Vector weights also accept [width]. Extra decoder weights are ignored.
// Supply position_frequency/position_offset [dim] from graph for its rounding;
// if both are absent, constructs sinusoidal constants once.
// External storage referenced by TensorView/Int8TensorView must outlive Encoder.
class Encoder {
 public:
  Encoder(EncoderConfig config,
          const std::vector<NamedTensor>& weights,
          const std::vector<NamedInt8Tensor>& int8_weights = {});
  ~Encoder();
  Encoder(Encoder&&) noexcept;
  Encoder& operator=(Encoder&&) noexcept;
  Encoder(const Encoder&) = delete;
  Encoder& operator=(const Encoder&) = delete;

  void encodeBatch(const std::int64_t* const* inputs,
                   const std::size_t* lengths,
                   std::size_t batch_size,
                   EncodedBatch& out,
                   std::size_t max_threads = 1,
                   const std::atomic<std::size_t>* active_submitted = nullptr) const;

  bool has_native_fast() const noexcept;
  std::size_t vocab_size() const noexcept { return vocab_size_; }
  const EncoderConfig& config() const noexcept { return config_; }

 private:
  struct Norm {
    const float* scale = nullptr;
    const float* bias = nullptr;
  };
  struct Linear {
    Matrix weight;
    Int8TensorView int8_weight{};
    const float* bias = nullptr;
  };
  struct EncoderLayer {
    Linear q, k, v, o;
    Norm self_norm;
    std::vector<Linear> ffn;
    Norm ffn_norm;
  };
  struct ContextKV {
    Linear k, v;
  };
  struct NativeState;

  EncoderConfig config_;
  TensorView embedding_;
  Int8TensorView int8_embedding_;
  std::size_t vocab_size_ = 0;
  std::vector<EncoderLayer> layers_;
  std::vector<ContextKV> context_kv_;
  std::vector<float> frequency_;
  std::vector<float> offset_;
  std::size_t max_ffn_width_ = 0;
  std::unique_ptr<const NativeState> native_;
};

}  // namespace mtran::ssru
