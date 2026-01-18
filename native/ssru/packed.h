#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mtran::ssru::packed {
// Call only after the baseline adapter's AVXVNNI host gate succeeds.
// Storage is private to one active request and reused across its linear calls.
class Workspace {
 public:
  Workspace();
  ~Workspace();
  std::uint8_t* resize(std::size_t size);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Immutable once constructed from row-major FP32 [K,N]. No bias.
// Requires K % 32 == 0, N % 8 == 0 and K <= INT32_MAX / (255 * 128).
// Other shapes use the adapter's InvokeOp path. No AVX2 fallback.
class Weight {
 public:
  Weight(const float* data, std::size_t k, std::size_t n);
  ~Weight();
  void multiply(const std::uint8_t* activation, std::size_t rows,
                float activation_scale, std::uint8_t zero_point, float* output) const;
 private:
  struct Impl;
  std::unique_ptr<const Impl> impl_;
};
}
