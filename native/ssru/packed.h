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

// Immutable once constructed from row-major FP32 [K,N]. No bias.
// Requires K % 32 == 0, N % 8 == 0 and K <= INT32_MAX / (255 * 128).
// Other shapes use the adapter's InvokeOp path. Unsupported CPUs are rejected.
class Weight {
 public:
  Weight(const float* data, std::size_t k, std::size_t n);
  ~Weight();
  void multiply(const std::uint8_t* activation, std::size_t rows,
                float activation_scale, std::uint8_t zero_point, float* output,
                const float* fused_bias = nullptr, const float* separate_bias = nullptr) const;
 private:
  struct Impl;
  std::unique_ptr<const Impl> impl_;
};

// Two-stage L3-resident per-row INT8 shortlist filter with exact FP32 margin reranking.
// Falls back to exact FP32 row dot products when dim % 32 != 0 or INT8 kernels do not apply.
class ShortlistTable {
 public:
  ShortlistTable(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
                 bool sequential_fma = false);
  ~ShortlistTable();
  std::size_t argmax(const float* x, const std::int64_t* candidate_ids,
                     std::size_t candidate_count, Workspace& workspace,
                     bool prevalidated = false) const;
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
  std::unique_ptr<const Impl> impl_;
};

class FullVocabQ4 {
 public:
  FullVocabQ4(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
              bool sequential_fma = false);
  ~FullVocabQ4();
  FullVocabQ4(const FullVocabQ4&) = delete;
  FullVocabQ4& operator=(const FullVocabQ4&) = delete;
  std::size_t argmax(const float* x, Workspace& workspace, bool allow_parallel = false) const;

  struct TopK9 {
    float scores[9];
    std::uint32_t ids[9];
    int count = 0;
    void init() noexcept {
      count = 0;
      for (int i = 0; i < 9; ++i) {
        scores[i] = -std::numeric_limits<float>::infinity();
        ids[i] = 0;
      }
    }
    void push(float s, std::uint32_t id) noexcept {
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

 private:
  struct Impl;
  std::unique_ptr<const Impl> impl_;
};
}
