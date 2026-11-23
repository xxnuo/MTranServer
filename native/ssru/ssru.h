#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

// Contiguous row-major INT8 (0x4101); the owner keeps storage alive and immutable.
struct Int8TensorView {
  const std::int8_t* data = nullptr;
  std::size_t size = 0;
  std::vector<std::int64_t> shape;
  float scale = 0.0f;
};

struct NamedInt8Tensor {
  std::string name;
  Int8TensorView tensor;
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
  void (*matmul_pair)(void* context, const Matrix& w1, const float* b1, bool separate_b1, float* out1,
                      const Matrix& w2, float* out2, const float* input, std::size_t rows) = nullptr;
  std::size_t (*argmax_shortlist)(void* context, const float* x, std::size_t dim,
                                  const float* embedding, const float* output_bias,
                                  const std::int64_t* candidate_ids, std::size_t candidate_count) = nullptr;
  std::size_t (*argmax_full)(void* context, const float* x, std::size_t dim) = nullptr;
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
  bool prevalidated_kv = false;
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

struct BatchResult {
  std::vector<std::int64_t> tokens;
  bool ended_with_eos = false;
};

struct BatchCallbacks {
  // Invoked immediately when a request finishes (receiving its assigned request index).
  std::function<void(std::size_t request_index, BatchResult&& result)> on_complete;
  // Invoked after slot retirement when active < max_active_batch. Append up to
  // max_new requests into out_requests and return the starting request_index for
  // out_requests[0] (assigns base + i for i < added).
  std::function<std::size_t(std::vector<Request>& out_requests, std::size_t max_new)> try_refill;
};

// Reusable workspace across successive decodeBatch calls. On failure results are cleared.
class BatchWorkspace {
 public:
  BatchWorkspace();
  ~BatchWorkspace();
  BatchWorkspace(BatchWorkspace&&) noexcept;
  BatchWorkspace& operator=(BatchWorkspace&&) noexcept;
  BatchWorkspace(const BatchWorkspace&) = delete;
  BatchWorkspace& operator=(const BatchWorkspace&) = delete;

  const std::vector<BatchResult>& results() const noexcept { return results_; }

 private:
  friend class Decoder;
  struct Scratch;
  std::vector<BatchResult> results_;
  std::unique_ptr<Scratch> scratch_;
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
  Decoder(Config config, const std::vector<NamedTensor>& weights,
          const std::vector<NamedInt8Tensor>& int8_weights = {});
  ~Decoder();
  Decoder(Decoder&&) noexcept;
  Decoder& operator=(Decoder&&) noexcept;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  // Includes EOS; at most 1 + 3*source_length tokens, exactly like graph.ts.
  // First step uses a zero embedding at position 0; later steps gather tokens.
  void decode(const Request& request, LinearBackend backend, Workspace& workspace,
              MathBackend math = {}) const;

  // Continuous-batching decoder over requests[0..count-1]. Uses packed INT8 /
  // Q4 / ops kernels + PcorePool when compiled with MTRAN_SSRU_PACKED (and
  // dim % 32 == 0, vocab >= 9), otherwise runs scalar/LinearBackend fallback.
  void decodeBatch(const Request* requests, std::size_t count,
                   BatchWorkspace& workspace,
                   std::size_t max_threads = 1,
                   std::size_t max_active_batch = 16,
                   LinearBackend backend = {},
                   MathBackend math = {},
                   const std::atomic<std::size_t>* active_submitted = nullptr) const;

  void decodeBatchStreaming(const Request* initial_requests, std::size_t initial_count,
                            BatchWorkspace& workspace,
                            const BatchCallbacks& callbacks,
                            std::size_t max_threads = 1,
                            std::size_t max_active_batch = 8,
                            const std::atomic<std::size_t>* active_submitted = nullptr,
                            LinearBackend backend = {},
                            MathBackend math = {}) const;

  bool has_native_fast() const noexcept;
  std::size_t vocab_size() const noexcept { return vocab_size_; }
  const Config& config() const noexcept { return config_; }

 private:
  struct Norm { const float* scale; const float* bias; };
  struct Linear {
    Matrix weight;
    Int8TensorView int8_weight{};
    const float* bias = nullptr;
  };
  struct Layer {
    Linear gate, state, query, attention;
    Norm rnn_norm, attention_norm, ffn_norm;
    std::vector<Linear> ffn;
  };
  struct NativeState;
  void decodeBatchImpl(const Request* initial_requests, std::size_t initial_count,
                       BatchWorkspace& workspace,
                       const BatchCallbacks& callbacks,
                       std::size_t max_threads,
                       std::size_t max_active_batch,
                       const std::atomic<std::size_t>* active_submitted,
                       LinearBackend backend,
                       MathBackend math) const;
  Config config_;
  TensorView embedding_;
  Int8TensorView int8_embedding_;
  std::size_t vocab_size_ = 0;
  const float* output_bias_ = nullptr;
  std::vector<Layer> layers_;
  std::vector<float> frequency_, offset_;
  std::size_t max_ffn_width_ = 0;
  std::unique_ptr<const NativeState> native_;
};

}  // namespace mtran::ssru
