#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mtran::ssru {

// Contiguous row-major FP32; the owner keeps storage alive and immutable.
struct TensorView {
  const float* data = nullptr;
  std::size_t size = 0;
  std::vector<std::int64_t> shape;
};

struct NamedTensor {
  std::string name;
  TensorView tensor;
};

struct Config {
  std::size_t dim = 384;
  std::size_t heads = 8;
  std::size_t depth = 4;
  std::size_t ffn_depth = 2;
};

struct Matrix {
  std::string name;
  const float* data = nullptr;
  std::size_t rows = 0;
  std::size_t cols = 0;
};

struct LinearBackend {
  void* context = nullptr;
  // Synchronous Y[M,N] = X[M,K] * weight[K,N], no bias or activation.
  // Input/output never alias. Write every output, retain no buffer pointers.
  // decoder_* weights are model constants and may use cached dynamic-INT8 ops.
  // shortlist_projection_T is request-local FP32 [dim,candidate_count]: use
  // FP32 MatMul, never dynamic quantization or a name-only weight cache.
  // Throw on failure. The callback/context must be isolated per request or safe
  // for simultaneous calls. Core never invokes ORT or owns backend state.
  void (*matmul)(void* context, const Matrix& weight, const float* input,
                 std::size_t rows, float* output) = nullptr;
  // Optional fused quantized bias; separate_bias preserves first-step constant folding.
  void (*matmul_bias)(void* context, const Matrix& weight, const float* input,
                      std::size_t rows, const float* bias, bool separate_bias, float* output) = nullptr;
  std::size_t (*argmax_shortlist)(void* context, const float* x, std::size_t dim,
                                  const float* embedding, const float* output_bias,
                                  const std::int64_t* candidate_ids, std::size_t candidate_count) = nullptr;
};

// Optional synchronous math operations, independent of the matrix backend.
// Null callbacks use scalar reference math. All arrays are contiguous FP32.
// Vector operations update x in place; callbacks must write every element,
// retain no pointers, and throw on failure. Context is request-local or thread-safe.
struct MathBackend {
  void* context = nullptr;
  // Normalize across dim using population variance and epsilon=1e-6, then
  // multiply by scale and add bias in separate FP32 operations.
  void (*normalize)(void* context, float* x, std::size_t dim,
                    const float* scale, const float* bias) = nullptr;
  void (*sigmoid)(void* context, float* x, std::size_t dim) = nullptr;
  void (*sin)(void* context, float* x, std::size_t dim) = nullptr;
  // Q [heads,width], K [heads,width,length], V [heads,length,width].
  // Output [heads,width] = softmax(Q*K/sqrt(width))*V, independently per head.
  // Match the optimized graph's FP32 arithmetic (including fused QK scaling).
  void (*attention)(void* context, const float* query, const float* keys,
                    const float* values, std::size_t heads, std::size_t width,
                    std::size_t length, float* output) = nullptr;
};

struct Request {
  std::size_t source_length = 0;  // X length, including source EOS.
  std::int64_t eos_id = 0;
  const std::int64_t* candidate_ids = nullptr;  // Preserve input order for ties.
  std::size_t candidate_count = 0;
  std::vector<TensorView> keys;    // Per layer: [1, heads, dim/heads, source_length].
  std::vector<TensorView> values;  // Per layer: [1, heads, source_length, dim/heads].
};

class Decoder;

// One workspace per active request; reuse across successive requests is allowed.
// Results remain here until the next decode. On failure tokens are cleared.
class Workspace {
 public:
  const std::vector<std::int64_t>& tokens() const noexcept { return tokens_; }
  bool ended_with_eos() const noexcept { return ended_with_eos_; }

 private:
  friend class Decoder;
  std::vector<std::int64_t> tokens_;
  bool ended_with_eos_ = false;
  std::vector<float> cells_, x_, gate_, update_, query_, context_, output_;
  std::vector<float> ffn_a_, ffn_b_, scores_, projection_, bias_, logits_;
};

// Required graph names (l=1..depth, j=1..ffn_depth):
// Wemb OR decoder_Wemb [vocab,dim]; decoder_ff_logit_out_b [1,vocab];
// decoder_l{l}_rnn_{Wf,W} [dim,dim], _bf [1,dim],
//   _ffn_ln_{scale,bias} [1,dim];
// decoder_l{l}_context_{Wq,Wo} [dim,dim], _{bq,bo} [1,dim],
//   _Wo_ln_{scale,bias} [1,dim];
// decoder_l{l}_ffn_W{j} [input,output], _b{j} [1,output],
//   _ffn_ln_{scale,bias} [1,dim]. FFN begins/ends at dim; widths inferred.
// Vector weights also accept [width]. Extra weights (including Wk/Wv) ignored.
// Supply position_frequency/position_offset [dim] from graph for its rounding;
// if both are absent, construct graph.ts's sinusoidal constants once.
// Constructor validates weights once; their external storage outlives Decoder.
class Decoder {
 public:
  Decoder(Config config, const std::vector<NamedTensor>& weights);
  // Includes EOS; at most 1 + 3*source_length tokens, exactly like graph.ts.
  // First step uses a zero embedding at position 0; later steps gather tokens.
  void decode(const Request& request, LinearBackend backend, Workspace& workspace,
              MathBackend math = {}) const;
  const Config& config() const noexcept { return config_; }

 private:
  struct Norm { const float* scale; const float* bias; };
  struct Linear { Matrix weight; const float* bias; };
  struct Layer {
    Linear gate, state, query, attention;
    Norm rnn_norm, attention_norm, ffn_norm;
    std::vector<Linear> ffn;
  };
  Config config_;
  TensorView embedding_;
  const float* output_bias_ = nullptr;
  std::vector<Layer> layers_;
  std::vector<float> frequency_, offset_;
  std::size_t max_ffn_width_ = 0;
};

}  // namespace mtran::ssru
