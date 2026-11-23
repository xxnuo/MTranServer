#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace mtran::ssru::packed {
namespace detail {
enum X86Feature : unsigned { Avx2 = 1, AvxVnni = 2, Avx512Vnni = 4 };
// Decode CPU bits separately from OS-enabled register state for dispatch tests.
constexpr unsigned x86Features(unsigned leaf1_ecx, unsigned leaf7_ebx,
                               unsigned leaf7_ecx, unsigned leaf71_eax,
                               std::uint64_t xcr0) noexcept {
  if ((leaf1_ecx & 0x18000000u) != 0x18000000u || (xcr0 & 6) != 6 || !(leaf7_ebx & (1u << 5))) return 0;
  unsigned result = Avx2;
  if (leaf71_eax & (1u << 4)) result |= AvxVnni;
  // ORT's AVX512VNNI kernel also requires F/DQ/BW/VL and opmask/ZMM OS state.
  if ((leaf7_ebx & 0xc0030000u) == 0xc0030000u && (leaf7_ecx & (1u << 11)) && (xcr0 & 0xe6) == 0xe6)
    result |= Avx512Vnni;
  return result;
}
}
// This entry point and all callers must be compiled for the baseline CPU.
// Returns nullptr if CPU or OS support is missing. No optional ISA executes here.
// packed-avx2-ort preserves ORT's int16 pair saturation; VNNI-capable OS/CPUs
// retain exact accumulation even when this build has no VNNI kernel.
const char* backend() noexcept;

// Storage is private to one active request and reused across its linear calls.
class Workspace {
 public:
  Workspace();
  ~Workspace();
  std::uint8_t* resize(std::size_t size);
  float* resizeFloats(std::size_t count);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

void quantizeLinearU8(const float* input, std::size_t rows, std::size_t cols,
                      std::uint8_t* output, float& scale, std::uint8_t& zero_point);

// Per-row dynamic uint8 quantization (preserving exact M=1 per-row scale/zero_point).
void quantizeLinearU8Batch(const float* input, std::size_t rows, std::size_t cols,
                           std::size_t input_stride, std::uint8_t* output,
                           std::size_t output_stride, float* scales, std::uint8_t* zero_points);

// Per-row dynamic uint8 quantization when row [min, max] (clamped around 0) are already known.
void quantizeLinearU8FromMinMax(const float* input, std::size_t rows, std::size_t cols,
                                std::size_t input_stride, const float* mins, const float* maxs,
                                std::uint8_t* output, std::size_t output_stride,
                                float* scales, std::uint8_t* zero_points);

// Fused quantizeLinearU8 + ||x - qx||_2^2 (and optional ||qx||_2^2) in one L1 pass.
void quantizeLinearU8WithNorm(const float* x, std::size_t dim, std::uint8_t* output,
                              float& scale, std::uint8_t& zero_point,
                              float& dx_sq, float* qx_sq = nullptr);

// Immutable once constructed from row-major FP32 [K,N] or 0x4101 INT8 [K,N].
// Requires K % 32 == 0, N % 8 == 0 and K <= INT32_MAX / (255 * 128).
class Weight {
 public:
  Weight(const float* data, std::size_t k, std::size_t n);
  static Weight packInt8(const std::int8_t* w_q8, float w_scale,
                         std::size_t rows, std::size_t cols,
                         const float* bias = nullptr, const float* separate_bias = nullptr,
                         bool enable_vnni = true);
  ~Weight();
  Weight(Weight&&) noexcept;
  Weight& operator=(Weight&&) noexcept;
  Weight(const Weight&) = delete;
  Weight& operator=(const Weight&) = delete;

  void multiply(const std::uint8_t* activation, std::size_t rows,
                float activation_scale, std::uint8_t zero_point, float* output,
                const float* fused_bias = nullptr, const float* separate_bias = nullptr) const;

  // Batched GEMM with per-row activation scale/zp (bit-identical to M=1).
  // col_begin / col_end should be aligned to 32 (8 minimum); col_end == ~0 -> cols.
  void multiplyBatch(const std::uint8_t* a_q, const float* a_scales, const std::uint8_t* a_zps,
                     std::size_t rows, float* out, std::size_t out_stride,
                     std::size_t col_begin = 0, std::size_t col_end = ~std::size_t(0),
                     bool force_separate_bias = false) const;

  // Fuses multiplyBatch + ReLU + per-row output max tracking over [col_begin, col_end).
  void multiplyBatchReluMinMax(const std::uint8_t* a_q, const float* a_scales, const std::uint8_t* a_zps,
                               std::size_t rows, float* out, std::size_t out_stride,
                               float* out_maxs, std::size_t col_begin = 0,
                               std::size_t col_end = ~std::size_t(0)) const;

 private:
  struct Impl;
  explicit Weight(std::unique_ptr<const Impl> impl) noexcept;
  std::unique_ptr<const Impl> impl_;
};

// Two-stage L3-resident per-row INT8 shortlist filter with exact FP32 margin reranking.
// Falls back to exact FP32 row dot products when dim % 32 != 0 or INT8 kernels do not apply.
class ShortlistTable {
 public:
  ShortlistTable(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
                 bool sequential_fma = false);
  // Borrows 0x4101 INT8 embedding [vocab, dim] without retaining an FP32 Wemb copy.
  static ShortlistTable createInt8(const std::int8_t* emb_q8, float min_pos,
                                   const float* bias, std::size_t vocab, std::size_t dim,
                                   bool sequential_fma = false);
  ~ShortlistTable();
  ShortlistTable(ShortlistTable&&) noexcept;
  ShortlistTable& operator=(ShortlistTable&&) noexcept;
  ShortlistTable(const ShortlistTable&) = delete;
  ShortlistTable& operator=(const ShortlistTable&) = delete;

  std::size_t argmax(const float* x, const std::int64_t* candidate_ids,
                     std::size_t candidate_count, Workspace& workspace,
                     bool prevalidated = false) const;

  // Batched masked-union shortlist argmax for rows <= 32. Writes winning token IDs into out_tokens.
  void argMaxBatch(const float* x_rows, std::size_t rows, std::size_t x_stride,
                   const std::int64_t* union_ids, const std::uint32_t* slot_masks,
                   std::size_t union_count, std::int64_t* out_tokens) const;

  struct alignas(16) RowMeta {
    float scale;
    float bias;
    float w_l2;
    std::int32_t sum;
  };
  struct ScanResult {
    float best_logit;
    std::size_t best_j;
    float second_logit;
  };

 private:
  struct Impl;
  explicit ShortlistTable(std::unique_ptr<const Impl> impl) noexcept;
  std::unique_ptr<const Impl> impl_;
};

class FullVocabQ4 {
 public:
  FullVocabQ4(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
              bool sequential_fma = false);
  // Quantizes from 0x4101 INT8 embedding and reconstructs Stage-2 FP32 rows on-the-fly.
  static FullVocabQ4 createInt8(const std::int8_t* emb_q8, float min_pos,
                                const float* bias, std::size_t vocab, std::size_t dim,
                                bool sequential_fma = false);
  ~FullVocabQ4();
  FullVocabQ4(FullVocabQ4&&) noexcept;
  FullVocabQ4& operator=(FullVocabQ4&&) noexcept;
  FullVocabQ4(const FullVocabQ4&) = delete;
  FullVocabQ4& operator=(const FullVocabQ4&) = delete;

  std::size_t argmax(const float* x, Workspace& workspace, bool allow_parallel = false) const;

  // Batched full-vocab Q4 argmax with PcorePool sharding. Writes winning token IDs into out_tokens.
  void argMaxBatch(const float* x_rows, std::size_t rows, std::size_t x_stride,
                   std::int64_t* out_tokens, std::size_t max_threads = 1) const;

#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_PACKED_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define MTRAN_PACKED_INLINE __attribute__((always_inline)) inline
#else
#define MTRAN_PACKED_INLINE inline
#endif

  struct TopK9 {
    float scores[9];
    std::uint32_t ids[9];
    int count = 0;
    MTRAN_PACKED_INLINE void init() noexcept {
      count = 0;
      for (int i = 0; i < 9; ++i) {
        scores[i] = -std::numeric_limits<float>::infinity();
        ids[i] = 0;
      }
    }
    MTRAN_PACKED_INLINE void push(float s, std::uint32_t id) noexcept {
      if (count == 9 && !(s > scores[8])) return;
      int pos = count < 9 ? count++ : 8;
      while (pos > 0 && s > scores[pos - 1]) {
        scores[pos] = scores[pos - 1];
        ids[pos] = ids[pos - 1];
        --pos;
      }
      scores[pos] = s;
      ids[pos] = id;
    }
  };
#undef MTRAN_PACKED_INLINE

 private:
  struct Impl;
  explicit FullVocabQ4(std::unique_ptr<const Impl> impl) noexcept;
  std::unique_ptr<const Impl> impl_;
};
}
