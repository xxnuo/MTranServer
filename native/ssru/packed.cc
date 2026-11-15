#include "packed.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_AVX2 __declspec(noinline)
#else
#define MTRAN_AVX2 __attribute__((target("avx2"), noinline))
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_DOTPROD __declspec(noinline)
#elif defined(__clang__)
#define MTRAN_DOTPROD __attribute__((target("dotprod"), noinline))
#else
#define MTRAN_DOTPROD __attribute__((target("arch=armv8.2-a+dotprod"), noinline))
#endif
#endif

namespace mtran::ssru::packed {
#ifdef MTRAN_SSRU_HAVE_VNNI
void multiplyVnni(const std::uint8_t*, const std::int8_t*, const std::int32_t*,
                  std::size_t, std::size_t, std::size_t, std::uint8_t, float, float*, const float*);
#endif
namespace {
float roundEven(float x) {
  const float lo = std::floor(x), fraction = x - lo;
  return fraction < .5f ? lo : fraction > .5f ? lo + 1.f : std::fmod(lo, 2.f) == 0.f ? lo : lo + 1.f;
}
#if defined(__x86_64__) || defined(_M_X64)
unsigned features() noexcept {
  unsigned leaf1_ecx, leaf7_ebx, leaf7_ecx, leaf71_eax = 0;
  std::uint64_t xcr0;
#ifdef _MSC_VER
  int regs[4];
  __cpuid(regs, 0);
  if (regs[0] < 7) return 0;
  __cpuidex(regs, 1, 0);
  leaf1_ecx = static_cast<unsigned>(regs[2]);
  if ((leaf1_ecx & 0x18000000u) != 0x18000000u) return 0;
  xcr0 = _xgetbv(0);
  __cpuidex(regs, 7, 0);
  leaf7_ebx = static_cast<unsigned>(regs[1]);
  leaf7_ecx = static_cast<unsigned>(regs[2]);
  const bool subleaf1 = regs[0] >= 1;
  if (subleaf1) {
    __cpuidex(regs, 7, 1);
    leaf71_eax = static_cast<unsigned>(regs[0]);
  }
#else
  unsigned eax, ebx, ecx, edx;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
  leaf1_ecx = ecx;
  if ((leaf1_ecx & 0x18000000u) != 0x18000000u) return 0;
  // XGETBV is safe after OSXSAVE; avoid globally enabling AVX just to read it.
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  xcr0 = (std::uint64_t(edx) << 32) | eax;
  if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 0;
  leaf7_ebx = ebx;
  leaf7_ecx = ecx;
  if (eax >= 1 && __get_cpuid_count(7, 1, &eax, &ebx, &ecx, &edx)) leaf71_eax = eax;
#endif
  return detail::x86Features(leaf1_ecx, leaf7_ebx, leaf7_ecx, leaf71_eax, xcr0);
}
template<bool ortSaturation>
MTRAN_AVX2 void dotAvx2(const std::uint8_t* a, const std::int8_t* b, std::size_t k, std::int32_t* result) {
  __m256i acc[8]{};
  const auto low = _mm256_set1_epi8(127), high = _mm256_set1_epi8(-128), ones = _mm256_set1_epi16(1);
  for (std::size_t j = 0; j < k; j += 32, b += 256) {
    const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + j));
    const auto xl = _mm256_and_si256(x, low), xh = _mm256_and_si256(x, high);
    for (std::size_t lane = 0; lane < 8; ++lane) {
      const auto w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + lane * 32));
      if constexpr (ortSaturation) {
        // Preserve ORT AVX2 VPMADDUBSW's signed int16 saturation per adjacent pair.
        acc[lane] = _mm256_add_epi32(acc[lane], _mm256_madd_epi16(_mm256_maddubs_epi16(x, w), ones));
      } else {
        // Splitting u8 into <=127 and {0,128} bounds every pair to [-32768,32512].
        const auto lo = _mm256_maddubs_epi16(xl, w), hi = _mm256_maddubs_epi16(xh, w);
        acc[lane] = _mm256_add_epi32(acc[lane], _mm256_add_epi32(_mm256_madd_epi16(lo, ones), _mm256_madd_epi16(hi, ones)));
      }
    }
  }
  // Same eight-column reduction as Gemmology.
  const auto p0 = _mm256_hadd_epi32(_mm256_hadd_epi32(acc[0], acc[1]), _mm256_hadd_epi32(acc[2], acc[3]));
  const auto p1 = _mm256_hadd_epi32(_mm256_hadd_epi32(acc[4], acc[5]), _mm256_hadd_epi32(acc[6], acc[7]));
  const auto total = _mm256_add_epi32(_mm256_permute2f128_si256(p0, p1, 0x21), _mm256_blend_epi32(p0, p1, 0xf0));
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(result), total);
}
#elif defined(__aarch64__) || defined(_M_ARM64)
bool hasDotprod() noexcept {
#ifdef MTRAN_SSRU_NO_DOTPROD
  return false;
#elif defined(_WIN32)
  // PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE, including older Windows SDK headers.
  return IsProcessorFeaturePresent(43) != 0;
#elif defined(__APPLE__)
  int supported = 0;
  std::size_t size = sizeof(supported);
  return sysctlbyname("hw.optional.arm.FEAT_DotProd", &supported, &size, nullptr, 0) == 0 && supported;
#elif defined(__linux__)
  return (getauxval(AT_HWCAP) & HWCAP_ASIMDDP) != 0;
#else
  return false;
#endif
}
MTRAN_DOTPROD void dotSdot32(const std::uint8_t* a, const std::int8_t* b, std::size_t k, std::int32_t* result) {
  auto s0 = vdupq_n_s32(0), s1 = s0, s2 = s0, s3 = s0;
  auto s4 = s0, s5 = s0, s6 = s0, s7 = s0;
  for (std::size_t j = 0; j < k; j += 4, b += 128) {
    std::int32_t bytes;
    std::memcpy(&bytes, a + j, 4);
    const auto x = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes), vdupq_n_s32(-2139062144)));
    s0 = vdotq_s32(s0, vld1q_s8(b), x);
    s1 = vdotq_s32(s1, vld1q_s8(b + 16), x);
    s2 = vdotq_s32(s2, vld1q_s8(b + 32), x);
    s3 = vdotq_s32(s3, vld1q_s8(b + 48), x);
    s4 = vdotq_s32(s4, vld1q_s8(b + 64), x);
    s5 = vdotq_s32(s5, vld1q_s8(b + 80), x);
    s6 = vdotq_s32(s6, vld1q_s8(b + 96), x);
    s7 = vdotq_s32(s7, vld1q_s8(b + 112), x);
  }
  vst1q_s32(result, s0); vst1q_s32(result + 4, s1);
  vst1q_s32(result + 8, s2); vst1q_s32(result + 12, s3);
  vst1q_s32(result + 16, s4); vst1q_s32(result + 20, s5);
  vst1q_s32(result + 24, s6); vst1q_s32(result + 28, s7);
}
MTRAN_DOTPROD void dotSdot(const std::uint8_t* a, const std::int8_t* b, std::size_t k, std::int32_t* result) {
  auto lo0 = vdupq_n_s32(0), hi0 = lo0, lo1 = lo0, hi1 = lo0;
  auto lo2 = lo0, hi2 = lo0, lo3 = lo0, hi3 = lo0;
  for (std::size_t j = 0; j < k; j += 16, b += 128) {
    std::int32_t bytes[4];
    std::memcpy(bytes, a + j, 16);
    const auto x0 = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes[0]), vdupq_n_s32(-2139062144)));
    const auto x1 = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes[1]), vdupq_n_s32(-2139062144)));
    const auto x2 = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes[2]), vdupq_n_s32(-2139062144)));
    const auto x3 = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes[3]), vdupq_n_s32(-2139062144)));
    lo0 = vdotq_s32(lo0, vld1q_s8(b), x0);
    hi0 = vdotq_s32(hi0, vld1q_s8(b + 16), x0);
    lo1 = vdotq_s32(lo1, vld1q_s8(b + 32), x1);
    hi1 = vdotq_s32(hi1, vld1q_s8(b + 48), x1);
    lo2 = vdotq_s32(lo2, vld1q_s8(b + 64), x2);
    hi2 = vdotq_s32(hi2, vld1q_s8(b + 80), x2);
    lo3 = vdotq_s32(lo3, vld1q_s8(b + 96), x3);
    hi3 = vdotq_s32(hi3, vld1q_s8(b + 112), x3);
  }
  vst1q_s32(result, vaddq_s32(vaddq_s32(lo0, lo1), vaddq_s32(lo2, lo3)));
  vst1q_s32(result + 4, vaddq_s32(vaddq_s32(hi0, hi1), vaddq_s32(hi2, hi3)));
}
void dotNeon(const std::uint8_t* a, const std::int8_t* b, std::size_t k, std::int32_t* result) {
  auto lo = vdupq_n_s32(0), hi = vdupq_n_s32(0);
  for (std::size_t j = 0; j < k; ++j, b += 8) {
    const auto w = vmovl_s8(vld1_s8(b));
    // Widen before multiplying: uint8 128..255 must remain positive.
    lo = vmlal_n_s16(lo, vget_low_s16(w), a[j]);
    hi = vmlal_n_s16(hi, vget_high_s16(w), a[j]);
  }
  vst1q_s32(result, lo);
  vst1q_s32(result + 4, hi);
}
#endif
using Dot = void (*)(const std::uint8_t*, const std::int8_t*, std::size_t, std::int32_t*);
Dot selectDot() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
  const unsigned f = features();
  if (!(f & detail::Avx2)) return nullptr;
#if defined(MTRAN_SSRU_TEST_AVX2_EXACT)
  return dotAvx2<false>;
#elif defined(MTRAN_SSRU_TEST_AVX2_ORT)
  return dotAvx2<true>;
#else
  // NO_VNNI disables our kernel, not ORT's actual CPU/OS-selected arithmetic.
  return (f & (detail::AvxVnni | detail::Avx512Vnni)) ? dotAvx2<false> : dotAvx2<true>;
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  // Advanced SIMD is part of the supported ARM64 OS ABI; no dotprod/SVE needed.
  return hasDotprod() ? dotSdot : dotNeon;
#else
  return nullptr;
#endif
}
}
const char* backend() noexcept {
  const auto dot = selectDot();
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
  if (features() & detail::AvxVnni) return "packed-avxvnni";
#endif
  if (dot == dotAvx2<true>) return "packed-avx2-ort";
  return dot ? "packed-avx2" : nullptr;
#else
#if defined(__aarch64__) || defined(_M_ARM64)
  if (dot == dotSdot) return "packed-sdot";
#endif
  return dot ? "packed-neon" : nullptr;
#endif
}
struct Workspace::Impl { std::vector<std::uint8_t> activation; };
Workspace::Workspace() : impl_(std::make_unique<Impl>()) {}
Workspace::~Workspace() = default;
std::uint8_t* Workspace::resize(std::size_t size) {
  if (size > SIZE_MAX - 31) throw std::invalid_argument("Packed workspace overflow");
  impl_->activation.resize(size + 31);
  auto* p = impl_->activation.data();
  return p + ((32 - reinterpret_cast<std::uintptr_t>(p) % 32) % 32);
}
struct Weight::Impl {
  std::size_t k_, n_, block_ = 8;
  float scale_;
  Dot dot_;
  bool vnni_ = false, sdot_ = false;
  std::vector<std::int8_t> packed_;
  std::vector<std::int32_t> sums_;
  std::int8_t* packedData() {
    auto* p = packed_.data();
    return p + ((32 - reinterpret_cast<std::uintptr_t>(p) % 32) % 32);
  }
  const std::int8_t* packedData() const { return const_cast<Impl*>(this)->packedData(); }
  Impl(const float* data, std::size_t k, std::size_t n);
  void multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                std::uint8_t zp, float* output, const float* bias) const;
};
Weight::Weight(const float* data, std::size_t k, std::size_t n) : impl_(std::make_unique<const Impl>(data, k, n)) {}
Weight::~Weight() = default;
void Weight::multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                      std::uint8_t zp, float* output, const float* bias) const {
  impl_->multiply(a, rows, a_scale, zp, output, bias);
}
Weight::Impl::Impl(const float* data, std::size_t k, std::size_t n) : k_(k), n_(n), dot_(selectDot()) {
  if (!dot_) throw std::invalid_argument("Packed CPU/OS support unavailable");
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
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
  vnni_ = (features() & detail::AvxVnni) != 0;
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  sdot_ = dot_ == dotSdot;
  if (sdot_ && n % 32 == 0) { block_ = 32; dot_ = dotSdot32; }
#endif
  if (k * n > SIZE_MAX - 31) throw std::invalid_argument("Packed weight size overflow");
  packed_.resize(k * n + 31);
  sums_.resize(n);
  for (std::size_t col = 0; col < n; ++col) {
    std::int32_t sum = 0;
    for (std::size_t row = 0; row < k; ++row) {
      const auto q = static_cast<std::int8_t>(std::clamp(roundEven(data[row * n + col] / scale_), -128.f, 127.f));
#if defined(__x86_64__) || defined(_M_X64)
      const auto index = (col / 8) * k * 8 + (row / 32) * 256 + (col % 8) * 32 + row % 32;
#else
      const auto index = (col / block_) * k * block_ + (sdot_ ? (row / 4) * block_ * 4 + (col % block_) * 4 + row % 4
                                                                     : row * 8 + col % 8);
#endif
      packedData()[index] = q;
      sum += q;
    }
    sums_[col] = sum;
  }
}
void Weight::Impl::multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                            std::uint8_t zp, float* output, const float* bias) const {
  if (!a || !output || !rows || rows > SIZE_MAX / k_ || rows > SIZE_MAX / n_ ||
      reinterpret_cast<std::uintptr_t>(a) % 32 || !std::isfinite(a_scale) || a_scale <= 0)
    throw std::invalid_argument("Invalid packed activation/output");
  const float combined_scale = a_scale * scale_;
  if (!std::isfinite(combined_scale)) throw std::invalid_argument("Invalid packed output scale");
#ifdef MTRAN_SSRU_HAVE_VNNI
  if (vnni_) {
    multiplyVnni(a, packedData(), sums_.data(), rows, k_, n_, zp, combined_scale, output, bias);
    return;
  }
#endif
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < n_; col += block_) {
      std::int32_t acc[32];
      dot_(a + row * k_, packedData() + col * k_, k_, acc);
      // The K limit also bounds the signed-dot correction and its final int32 sum.
      for (std::size_t lane = 0; lane < block_; ++lane) {
        const float value = static_cast<float>(acc[lane] + (sdot_ ? 128 - std::int32_t(zp) : -std::int32_t(zp)) * sums_[col + lane]);
        output[row * n_ + col + lane] = bias ? std::fma(value, combined_scale, bias[col + lane]) : value * combined_scale;
      }
    }
  }
}
}
