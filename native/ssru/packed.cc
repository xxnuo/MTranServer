#include "packed.h"
#include "third_party/gemmology/gemmology.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#ifndef __AVXVNNI__
#error "This experiment requires -mavxvnni (no AVX2 fallback)."
#endif
namespace mtran::ssru::packed {
namespace {
template<class T> using Aligned = std::vector<T, xsimd::aligned_allocator<T, 32>>;
float roundEven(float x) {
  const float lo = std::floor(x), fraction = x - lo;
  return fraction < .5f ? lo : fraction > .5f ? lo + 1.f : std::fmod(lo, 2.f) == 0.f ? lo : lo + 1.f;
}
}
struct Workspace::Impl { Aligned<std::uint8_t> activation; };
Workspace::Workspace() : impl_(std::make_unique<Impl>()) {}
Workspace::~Workspace() = default;
std::uint8_t* Workspace::resize(std::size_t size) {
  impl_->activation.resize(size);
  return impl_->activation.data();
}
struct Weight::Impl {
  std::size_t k_, n_;
  float scale_;
  Aligned<std::int8_t> packed_;
  Aligned<std::int32_t> sums_;
  Impl(const float* data, std::size_t k, std::size_t n);
  void multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                std::uint8_t zp, float* output) const;
};
Weight::Weight(const float* data, std::size_t k, std::size_t n) : impl_(std::make_unique<const Impl>(data, k, n)) {}
Weight::~Weight() = default;
void Weight::multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                      std::uint8_t zp, float* output) const {
  impl_->multiply(a, rows, a_scale, zp, output);
}
Weight::Impl::Impl(const float* data, std::size_t k, std::size_t n) : k_(k), n_(n) {
  // Bound both the raw dot product and zero-point-corrected dot product.
  if (!data || !k || !n || k % 32 || n % 8 || k > std::size_t(INT32_MAX) / (255 * 128) || n > SIZE_MAX / k)
    throw std::invalid_argument("Unsupported packed matrix dimensions");
  float maximum = 0;
  for (std::size_t i = 0; i < k * n; ++i) {
    if (!std::isfinite(data[i])) throw std::invalid_argument("Non-finite packed weight");
    maximum = std::max(maximum, std::abs(data[i]));
  }
  scale_ = (maximum * 2.f) / 254.f;
  if (scale_ < std::numeric_limits<float>::min()) scale_ = 1.f;
  if (!std::isfinite(scale_)) throw std::invalid_argument("Invalid packed weight scale");
  Aligned<std::int8_t> transposed(k * n);
  sums_.resize(n);
  for (std::size_t col = 0; col < n; ++col) {
    std::int32_t sum = 0;
    for (std::size_t row = 0; row < k; ++row) {
      const auto q = static_cast<std::int8_t>(std::clamp(roundEven(data[row * n + col] / scale_), -128.f, 127.f));
      transposed[col * k + row] = q;
      sum += q;
    }
    sums_[col] = sum;
  }
  packed_.resize(k * n);
  // This API consumes contiguous [N,K], then groups 8 columns in 32-byte tiles.
  gemmology::Engine<xsimd::avxvnni>::PrepareBQuantizedTransposed(transposed.data(), packed_.data(), k, n);
}
void Weight::Impl::multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                            std::uint8_t zp, float* output) const {
  if (!a || !output || !rows || rows > SIZE_MAX / k_ || rows > SIZE_MAX / n_ ||
      reinterpret_cast<std::uintptr_t>(a) % 32 || !std::isfinite(a_scale) || a_scale <= 0)
    throw std::invalid_argument("Invalid packed activation/output");
  const float combined_scale = a_scale * scale_;
  if (!std::isfinite(combined_scale)) throw std::invalid_argument("Invalid packed output scale");
  gemmology::SequentialExecutionEngine engine;
  gemmology::Engine<xsimd::avxvnni>::Shift::Multiply(a, packed_.data(), rows, k_, n_,
    [&](xsimd::batch<std::int32_t, xsimd::avxvnni> acc, std::size_t row, std::size_t col, std::size_t) {
      using I = xsimd::batch<std::int32_t, xsimd::avxvnni>;
      // Multiply has no built-in 127 correction; use the actual ORT DQL zero point.
      acc = acc - I(zp) * I::load_aligned(sums_.data() + col);
      (xsimd::batch_cast<float>(acc) * combined_scale).store_unaligned(output + row * n_ + col);
    }, engine);
}
}
