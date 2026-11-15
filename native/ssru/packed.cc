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
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
void multiplyVnni(const std::uint8_t*, const std::int8_t*, const std::int32_t*,
                  std::size_t, std::size_t, std::size_t, std::uint8_t, float, float*,
                  const float*, const float*);
float scanShortlistVnni(const std::uint8_t*, float, std::uint8_t, std::size_t,
                        const std::int8_t*, const float*, const std::int32_t*,
                        const float*, const std::int64_t*, std::size_t, float*);
#endif
namespace {
float roundEven(float x) {
  const float lo = std::floor(x), fraction = x - lo;
  return fraction < .5f ? lo : fraction > .5f ? lo + 1.f : std::fmod(lo, 2.f) == 0.f ? lo : lo + 1.f;
}
void checkFinite(const float* data, std::size_t count, const char* what) {
  std::uint32_t bad = 0;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t bits;
    std::memcpy(&bits, &data[i], sizeof(bits));
    bad |= ((bits & 0x7f800000u) == 0x7f800000u);
  }
  if (bad) throw std::runtime_error(what);
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
MTRAN_AVX2 float scanShortlistAvx2(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const float* row_scales, const std::int32_t* row_sums,
    const float* output_bias, const std::int64_t* candidate_ids,
    std::size_t candidate_count, float* approx_out) {
  float best_logit = -std::numeric_limits<float>::infinity();
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
  const __m256i low = _mm256_set1_epi8(127), high = _mm256_set1_epi8(-128);
  const __m256i ones = _mm256_set1_epi16(1);
  std::size_t j = 0;
  for (; j + 8 <= candidate_count; j += 8) {
    if (j + 16 <= candidate_count) {
      for (int p = 0; p < 8; ++p)
        _mm_prefetch(reinterpret_cast<const char*>(emb_q8 + static_cast<std::size_t>(candidate_ids[j + 8 + p]) * dim), _MM_HINT_T0);
    }
    const std::size_t ts[8] = {
      static_cast<std::size_t>(candidate_ids[j + 0]), static_cast<std::size_t>(candidate_ids[j + 1]),
      static_cast<std::size_t>(candidate_ids[j + 2]), static_cast<std::size_t>(candidate_ids[j + 3]),
      static_cast<std::size_t>(candidate_ids[j + 4]), static_cast<std::size_t>(candidate_ids[j + 5]),
      static_cast<std::size_t>(candidate_ids[j + 6]), static_cast<std::size_t>(candidate_ids[j + 7]),
    };
    const std::int8_t *r0 = emb_q8 + ts[0] * dim, *r1 = emb_q8 + ts[1] * dim;
    const std::int8_t *r2 = emb_q8 + ts[2] * dim, *r3 = emb_q8 + ts[3] * dim;
    const std::int8_t *r4 = emb_q8 + ts[4] * dim, *r5 = emb_q8 + ts[5] * dim;
    const std::int8_t *r6 = emb_q8 + ts[6] * dim, *r7 = emb_q8 + ts[7] * dim;
    const std::int8_t* const rows[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
    __m256i acc[8]{};
    for (std::size_t k = 0; k < dim; k += 32) {
      const __m256i vx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x_u8 + k));
      const __m256i xl = _mm256_and_si256(vx, low), xh = _mm256_and_si256(vx, high);
      for (int lane = 0; lane < 8; ++lane) {
        const __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[lane] + k));
        const __m256i lo = _mm256_maddubs_epi16(xl, w), hi = _mm256_maddubs_epi16(xh, w);
        acc[lane] = _mm256_add_epi32(acc[lane], _mm256_add_epi32(_mm256_madd_epi16(lo, ones), _mm256_madd_epi16(hi, ones)));
      }
    }
    const __m256i h01 = _mm256_hadd_epi32(acc[0], acc[1]), h23 = _mm256_hadd_epi32(acc[2], acc[3]);
    const __m256i h45 = _mm256_hadd_epi32(acc[4], acc[5]), h67 = _mm256_hadd_epi32(acc[6], acc[7]);
    const __m256i p0 = _mm256_hadd_epi32(h01, h23), p1 = _mm256_hadd_epi32(h45, h67);
    const __m256i sums8 = _mm256_add_epi32(_mm256_permute2f128_si256(p0, p1, 0x21), _mm256_blend_epi32(p0, p1, 0xf0));
    alignas(32) std::int32_t dots[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(dots), sums8);
    for (int lane = 0; lane < 8; ++lane) {
      const auto t = ts[lane];
      const float bias = t == 1 ? -std::numeric_limits<float>::infinity() : output_bias[t];
      const float val = static_cast<float>(dots[lane] - zp_i * row_sums[t]) * (x_scale * row_scales[t]) + bias;
      approx_out[j + lane] = val;
      if (val > best_logit) best_logit = val;
    }
  }
  for (; j < candidate_count; ++j) {
    const auto t = static_cast<std::size_t>(candidate_ids[j]);
    const auto* row = emb_q8 + t * dim;
    std::int32_t acc = 0;
    for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(x_u8[k]) * row[k];
    const float bias = t == 1 ? -std::numeric_limits<float>::infinity() : output_bias[t];
    const float val = static_cast<float>(acc - zp_i * row_sums[t]) * (x_scale * row_scales[t]) + bias;
    approx_out[j] = val;
    if (val > best_logit) best_logit = val;
  }
  return best_logit;
}
MTRAN_AVX2 float dotRowAvx2(const float* x, const float* row, std::size_t dim) {
  __m256 a0 = _mm256_setzero_ps(), a1 = _mm256_setzero_ps();
  std::size_t k = 0;
  for (; k + 16 <= dim; k += 16) {
    a0 = _mm256_add_ps(a0, _mm256_mul_ps(_mm256_loadu_ps(x + k), _mm256_loadu_ps(row + k)));
    a1 = _mm256_add_ps(a1, _mm256_mul_ps(_mm256_loadu_ps(x + k + 8), _mm256_loadu_ps(row + k + 8)));
  }
  a0 = _mm256_add_ps(a0, a1);
  for (; k + 8 <= dim; k += 8)
    a0 = _mm256_add_ps(a0, _mm256_mul_ps(_mm256_loadu_ps(x + k), _mm256_loadu_ps(row + k)));
  const __m128 s4 = _mm_add_ps(_mm256_castps256_ps128(a0), _mm256_extractf128_ps(a0, 1));
  const __m128 s2 = _mm_hadd_ps(s4, s4);
  float sum = _mm_cvtss_f32(_mm_hadd_ps(s2, s2));
  for (; k < dim; ++k) sum += x[k] * row[k];
  return sum;
}
MTRAN_AVX2 std::size_t argmaxFp32Avx2(
    const float* x, std::size_t dim, const float* embedding, const float* output_bias,
    const std::int64_t* candidate_ids, std::size_t candidate_count) {
  std::size_t best = 0;
  float best_logit = -std::numeric_limits<float>::infinity();
  std::size_t j = 0;
  for (; j + 4 <= candidate_count; j += 4) {
    const std::size_t ts[4] = {
      static_cast<std::size_t>(candidate_ids[j + 0]), static_cast<std::size_t>(candidate_ids[j + 1]),
      static_cast<std::size_t>(candidate_ids[j + 2]), static_cast<std::size_t>(candidate_ids[j + 3]),
    };
    const float *r0 = embedding + ts[0] * dim, *r1 = embedding + ts[1] * dim;
    const float *r2 = embedding + ts[2] * dim, *r3 = embedding + ts[3] * dim;
    __m256 a0 = _mm256_setzero_ps(), a1 = _mm256_setzero_ps();
    __m256 a2 = _mm256_setzero_ps(), a3 = _mm256_setzero_ps();
    std::size_t k = 0;
    for (; k + 8 <= dim; k += 8) {
      const __m256 vx = _mm256_loadu_ps(x + k);
      a0 = _mm256_add_ps(a0, _mm256_mul_ps(vx, _mm256_loadu_ps(r0 + k)));
      a1 = _mm256_add_ps(a1, _mm256_mul_ps(vx, _mm256_loadu_ps(r1 + k)));
      a2 = _mm256_add_ps(a2, _mm256_mul_ps(vx, _mm256_loadu_ps(r2 + k)));
      a3 = _mm256_add_ps(a3, _mm256_mul_ps(vx, _mm256_loadu_ps(r3 + k)));
    }
    const __m256 h0 = _mm256_hadd_ps(a0, a1), h1 = _mm256_hadd_ps(a2, a3);
    const __m256 h2 = _mm256_hadd_ps(h0, h1);
    const __m128 sums = _mm_add_ps(_mm256_castps256_ps128(h2), _mm256_extractf128_ps(h2, 1));
    alignas(16) float logits[4];
    _mm_store_ps(logits, sums);
    for (int lane = 0; lane < 4; ++lane) {
      float tail = 0.0f;
      const float* row = embedding + ts[lane] * dim;
      for (std::size_t rem = k; rem < dim; ++rem) tail += x[rem] * row[rem];
      const float bias = ts[lane] == 1 ? -std::numeric_limits<float>::infinity() : output_bias[ts[lane]];
      const float val = (logits[lane] + tail) + bias;
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && ts[lane] == 1))
        throw std::runtime_error("SSRU nonfinite logit");
      if (j + lane == 0 || val > best_logit) { best_logit = val; best = j + lane; }
    }
  }
  for (; j < candidate_count; ++j) {
    const auto token = static_cast<std::size_t>(candidate_ids[j]);
    const float bias = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias[token];
    const float val = dotRowAvx2(x, embedding + token * dim, dim) + bias;
    if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
      throw std::runtime_error("SSRU nonfinite logit");
    if (j == 0 || val > best_logit) { best_logit = val; best = j; }
  }
  return best;
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
MTRAN_DOTPROD float scanShortlistSdot(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const float* row_scales, const std::int32_t* row_sums,
    const float* output_bias, const std::int64_t* candidate_ids,
    std::size_t candidate_count, float* approx_out) {
  float best_logit = -std::numeric_limits<float>::infinity();
  const std::int32_t zp_corr = 128 - static_cast<std::int32_t>(x_zp);
  const uint8x16_t sign_flip = vdupq_n_u8(0x80);
  std::size_t j = 0;
  for (; j + 4 <= candidate_count; j += 4) {
    const std::size_t ts[4] = {
      static_cast<std::size_t>(candidate_ids[j + 0]), static_cast<std::size_t>(candidate_ids[j + 1]),
      static_cast<std::size_t>(candidate_ids[j + 2]), static_cast<std::size_t>(candidate_ids[j + 3]),
    };
    const std::int8_t *r0 = emb_q8 + ts[0] * dim, *r1 = emb_q8 + ts[1] * dim;
    const std::int8_t *r2 = emb_q8 + ts[2] * dim, *r3 = emb_q8 + ts[3] * dim;
    int32x4_t a0 = vdupq_n_s32(0), a1 = a0, a2 = a0, a3 = a0;
    for (std::size_t k = 0; k < dim; k += 16) {
      const int8x16_t vx = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(x_u8 + k), sign_flip));
      a0 = vdotq_s32(a0, vld1q_s8(r0 + k), vx);
      a1 = vdotq_s32(a1, vld1q_s8(r1 + k), vx);
      a2 = vdotq_s32(a2, vld1q_s8(r2 + k), vx);
      a3 = vdotq_s32(a3, vld1q_s8(r3 + k), vx);
    }
    const int32x4_t p01 = vpaddq_s32(a0, a1), p23 = vpaddq_s32(a2, a3);
    std::int32_t dots[4];
    vst1q_s32(dots, vpaddq_s32(p01, p23));
    for (int lane = 0; lane < 4; ++lane) {
      const auto t = ts[lane];
      const float bias = t == 1 ? -std::numeric_limits<float>::infinity() : output_bias[t];
      const float val = static_cast<float>(dots[lane] + zp_corr * row_sums[t]) * (x_scale * row_scales[t]) + bias;
      approx_out[j + lane] = val;
      if (val > best_logit) best_logit = val;
    }
  }
  for (; j < candidate_count; ++j) {
    const auto t = static_cast<std::size_t>(candidate_ids[j]);
    const auto* row = emb_q8 + t * dim;
    std::int32_t acc = 0;
    for (std::size_t k = 0; k < dim; ++k)
      acc += (static_cast<std::int32_t>(x_u8[k]) - 128) * row[k];
    const float bias = t == 1 ? -std::numeric_limits<float>::infinity() : output_bias[t];
    const float val = static_cast<float>(acc + zp_corr * row_sums[t]) * (x_scale * row_scales[t]) + bias;
    approx_out[j] = val;
    if (val > best_logit) best_logit = val;
  }
  return best_logit;
}
float dotRowNeon(const float* x, const float* row, std::size_t dim) {
  float32x4_t a0 = vdupq_n_f32(0.0f), a1 = vdupq_n_f32(0.0f);
  std::size_t k = 0;
  for (; k + 8 <= dim; k += 8) {
    a0 = vaddq_f32(a0, vmulq_f32(vld1q_f32(x + k), vld1q_f32(row + k)));
    a1 = vaddq_f32(a1, vmulq_f32(vld1q_f32(x + k + 4), vld1q_f32(row + k + 4)));
  }
  a0 = vaddq_f32(a0, a1);
  for (; k + 4 <= dim; k += 4)
    a0 = vaddq_f32(a0, vmulq_f32(vld1q_f32(x + k), vld1q_f32(row + k)));
  float tmp[4];
  vst1q_f32(tmp, a0);
  float sum = (tmp[0] + tmp[1]) + (tmp[2] + tmp[3]);
  for (; k < dim; ++k) sum += x[k] * row[k];
  return sum;
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
struct Workspace::Impl {
  std::vector<std::uint8_t> activation;
  std::vector<float> floats;
};
Workspace::Workspace() : impl_(std::make_unique<Impl>()) {}
Workspace::~Workspace() = default;
std::uint8_t* Workspace::resize(std::size_t size) {
  if (size > SIZE_MAX - 31) throw std::invalid_argument("Packed workspace overflow");
  impl_->activation.resize(size + 31);
  auto* p = impl_->activation.data();
  return p + ((32 - reinterpret_cast<std::uintptr_t>(p) % 32) % 32);
}
float* Workspace::resizeFloats(std::size_t count) {
  if (count > SIZE_MAX / sizeof(float)) throw std::invalid_argument("Packed float workspace overflow");
  impl_->floats.resize(count);
  return impl_->floats.data();
}
void quantizeLinearU8(const float* input, std::size_t rows, std::size_t cols,
                      std::uint8_t* output, float& scale, std::uint8_t& zero_point) {
  if (!input || !output || !rows || !cols || rows > SIZE_MAX / cols)
    throw std::invalid_argument("Invalid DQL activation");
  const std::size_t total = rows * cols;
  checkFinite(input, total, "Nonfinite DQL activation");
  float lo = 0.0f, hi = 0.0f;
#if defined(__x86_64__) || defined(_M_X64)
  __m128 vlo = _mm_setzero_ps(), vhi = _mm_setzero_ps();
  std::size_t i = 0;
  for (; i + 4 <= total; i += 4) {
    const __m128 v = _mm_loadu_ps(input + i);
    vlo = _mm_min_ps(vlo, v);
    vhi = _mm_max_ps(vhi, v);
  }
  alignas(16) float tmp_lo[4], tmp_hi[4];
  _mm_store_ps(tmp_lo, vlo);
  _mm_store_ps(tmp_hi, vhi);
  for (int k = 0; k < 4; ++k) { lo = std::min(lo, tmp_lo[k]); hi = std::max(hi, tmp_hi[k]); }
  for (; i < total; ++i) { lo = std::min(lo, input[i]); hi = std::max(hi, input[i]); }
#elif defined(__aarch64__) || defined(_M_ARM64)
  float32x4_t vlo = vdupq_n_f32(0.0f), vhi = vdupq_n_f32(0.0f);
  std::size_t i = 0;
  for (; i + 4 <= total; i += 4) {
    const float32x4_t v = vld1q_f32(input + i);
    vlo = vminq_f32(vlo, v);
    vhi = vmaxq_f32(vhi, v);
  }
  float tmp_lo[4], tmp_hi[4];
  vst1q_f32(tmp_lo, vlo);
  vst1q_f32(tmp_hi, vhi);
  for (int k = 0; k < 4; ++k) { lo = std::min(lo, tmp_lo[k]); hi = std::max(hi, tmp_hi[k]); }
  for (; i < total; ++i) { lo = std::min(lo, input[i]); hi = std::max(hi, input[i]); }
#else
  for (std::size_t i = 0; i < total; ++i) { lo = std::min(lo, input[i]); hi = std::max(hi, input[i]); }
#endif
  scale = (hi == lo) ? 1.0f : (hi - lo) / 255.0f;
  if (!std::isfinite(scale) || scale <= 0.0f) throw std::runtime_error("Invalid DQL scale");
  const float initial_zp = std::clamp(0.0f - lo / scale, 0.0f, 255.0f);
  zero_point = static_cast<std::uint8_t>(roundEven(initial_zp));
  const float min_val = static_cast<float>(0 - static_cast<std::int32_t>(zero_point));
  const float max_val = static_cast<float>(255 - static_cast<std::int32_t>(zero_point));
#if defined(__x86_64__) || defined(_M_X64)
  const __m128 vscale = _mm_set1_ps(scale);
  const __m128 vmin = _mm_set1_ps(min_val), vmax = _mm_set1_ps(max_val);
  const __m128i vzp = _mm_set1_epi32(static_cast<int>(zero_point));
  std::size_t j = 0;
  for (; j + 16 <= total; j += 16) {
    auto q4 = [&](std::size_t off) {
      __m128 v = _mm_div_ps(_mm_loadu_ps(input + off), vscale);
      v = _mm_max_ps(v, vmin);
      v = _mm_min_ps(v, vmax);
      return _mm_add_epi32(_mm_cvtps_epi32(v), vzp);
    };
    const __m128i s01 = _mm_packs_epi32(q4(j), q4(j + 4));
    const __m128i s23 = _mm_packs_epi32(q4(j + 8), q4(j + 12));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(output + j), _mm_packus_epi16(s01, s23));
  }
  for (; j < total; ++j) {
    __m128 v = _mm_div_ss(_mm_load_ss(input + j), vscale);
    v = _mm_max_ss(v, vmin);
    v = _mm_min_ss(v, vmax);
    output[j] = static_cast<std::uint8_t>(_mm_cvtsi128_si32(_mm_cvtps_epi32(v)) + static_cast<int>(zero_point));
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  // IEEE-754 addition of 1.5 * 2^23 (12582912.0f = 0x4b400000) rounds [-255, 255] to nearest even integer.
  const float32x4_t vscale = vdupq_n_f32(scale);
  const float32x4_t vmin = vdupq_n_f32(min_val), vmax = vdupq_n_f32(max_val);
  const float32x4_t vmagic = vdupq_n_f32(12582912.0f);
  const int32x4_t voffset = vdupq_n_s32(static_cast<std::int32_t>(zero_point) - 0x4b400000);
  std::size_t j = 0;
  for (; j + 16 <= total; j += 16) {
    auto q4 = [&](std::size_t off) {
      float32x4_t v = vdivq_f32(vld1q_f32(input + off), vscale);
      v = vmaxq_f32(v, vmin);
      v = vminq_f32(v, vmax);
      return vaddq_s32(vreinterpretq_s32_f32(vaddq_f32(v, vmagic)), voffset);
    };
    const uint16x8_t lo16 = vcombine_u16(vqmovun_s32(q4(j)), vqmovun_s32(q4(j + 4)));
    const uint16x8_t hi16 = vcombine_u16(vqmovun_s32(q4(j + 8)), vqmovun_s32(q4(j + 12)));
    vst1q_u8(output + j, vcombine_u8(vqmovn_u16(lo16), vqmovn_u16(hi16)));
  }
  for (; j < total; ++j) {
    const float v = std::clamp(input[j] / scale, min_val, max_val);
    output[j] = static_cast<std::uint8_t>(static_cast<std::int32_t>(roundEven(v)) + static_cast<std::int32_t>(zero_point));
  }
#else
  for (std::size_t j = 0; j < total; ++j) {
    const float v = std::clamp(input[j] / scale, min_val, max_val);
    output[j] = static_cast<std::uint8_t>(static_cast<std::int32_t>(roundEven(v)) + static_cast<std::int32_t>(zero_point));
  }
#endif
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
                std::uint8_t zp, float* output, const float* bias, const float* separate_bias) const;
};
Weight::Weight(const float* data, std::size_t k, std::size_t n) : impl_(std::make_unique<const Impl>(data, k, n)) {}
Weight::~Weight() = default;
void Weight::multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                      std::uint8_t zp, float* output, const float* bias, const float* separate_bias) const {
  impl_->multiply(a, rows, a_scale, zp, output, bias, separate_bias);
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
                            std::uint8_t zp, float* output, const float* bias, const float* separate_bias) const {
  if (!a || !output || !rows || rows > SIZE_MAX / k_ || rows > SIZE_MAX / n_ ||
      reinterpret_cast<std::uintptr_t>(a) % 32 || !std::isfinite(a_scale) || a_scale <= 0)
    throw std::invalid_argument("Invalid packed activation/output");
  const float combined_scale = a_scale * scale_;
  if (!std::isfinite(combined_scale)) throw std::invalid_argument("Invalid packed output scale");
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
  if (vnni_) {
    multiplyVnni(a, packedData(), sums_.data(), rows, k_, n_, zp, combined_scale, output, bias, separate_bias);
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
        const float scaled = bias ? std::fma(value, combined_scale, bias[col + lane]) : value * combined_scale;
        output[row * n_ + col + lane] = separate_bias ? scaled + separate_bias[col + lane] : scaled;
      }
    }
  }
}

struct ShortlistTable::Impl {
  const float* embedding_;
  const float* output_bias_;
  std::size_t vocab_;
  std::size_t dim_;
  float max_w_l2_ = 0.0f;
  float max_dw_l2_ = 0.0f;
  float max_bias_abs_ = 0.0f;
  bool sequential_fma_ = false;
  bool use_q8_ = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool vnni_ = false;
  bool avx2_ = false;
#endif
  std::vector<std::int8_t> emb_q8_;
  std::vector<float> row_scales_;
  std::vector<std::int32_t> row_sums_;
  std::vector<float> row_w_l2_;
  std::vector<float> row_dw_l2_;

  Impl(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
       bool sequential_fma)
      : embedding_(embedding), output_bias_(output_bias), vocab_(vocab), dim_(dim),
        sequential_fma_(sequential_fma) {
    if (!embedding || !output_bias || vocab < 2 || dim < 1 || vocab > SIZE_MAX / dim)
      throw std::invalid_argument("Invalid shortlist embedding table");
    checkFinite(embedding, vocab * dim, "Nonfinite shortlist embedding");
    for (std::size_t i = 0; i < vocab; ++i) {
      if (!std::isfinite(output_bias[i]) && !(i == 1 && output_bias[i] == -std::numeric_limits<float>::infinity()))
        throw std::invalid_argument("Nonfinite shortlist output bias");
      if (i != 1) max_bias_abs_ = std::max(max_bias_abs_, std::abs(output_bias[i]));
    }
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned f = features();
    avx2_ = (f & detail::Avx2) != 0;
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    vnni_ = (f & detail::AvxVnni) != 0;
#endif
    use_q8_ = avx2_ && dim % 32 == 0 && dim <= std::size_t(INT32_MAX) / (255 * 128);
#elif defined(__aarch64__) || defined(_M_ARM64)
    use_q8_ = hasDotprod() && dim % 32 == 0 && dim <= std::size_t(INT32_MAX) / (255 * 128);
#endif
    if (!use_q8_) return;
    emb_q8_.resize(vocab * dim);
    row_scales_.resize(vocab);
    row_sums_.resize(vocab);
    row_w_l2_.resize(vocab);
    row_dw_l2_.resize(vocab);
    for (std::size_t r = 0; r < vocab; ++r) {
      const float* row = embedding + r * dim;
      float amax = 0.0f;
      double w_sq = 0.0;
      for (std::size_t k = 0; k < dim; ++k) {
        amax = std::max(amax, std::abs(row[k]));
        w_sq += static_cast<double>(row[k]) * row[k];
      }
      float scale = (amax * 2.0f) / 254.0f;
      if (scale < std::numeric_limits<float>::min()) scale = 1.0f;
      row_scales_[r] = scale;
      std::int32_t sum = 0;
      double dw_sq = 0.0;
      std::int8_t* qr = emb_q8_.data() + r * dim;
      for (std::size_t k = 0; k < dim; ++k) {
        const auto q = static_cast<std::int8_t>(std::clamp(roundEven(row[k] / scale), -128.0f, 127.0f));
        qr[k] = q;
        sum += q;
        const double dw = static_cast<double>(row[k]) - static_cast<double>(q) * scale;
        dw_sq += dw * dw;
      }
      row_sums_[r] = sum;
      const float wl2 = static_cast<float>(std::sqrt(w_sq));
      const float dwl2 = static_cast<float>(std::sqrt(dw_sq));
      row_w_l2_[r] = wl2;
      row_dw_l2_[r] = dwl2;
      max_w_l2_ = std::max(max_w_l2_, wl2);
      max_dw_l2_ = std::max(max_dw_l2_, dwl2);
    }
  }

  float dotRow(const float* x, const float* row) const {
    if (sequential_fma_) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < dim_; ++k) sum = std::fma(x[k], row[k], sum);
      return sum;
    }
#if defined(__x86_64__) || defined(_M_X64)
    if (avx2_) return dotRowAvx2(x, row, dim_);
    float sum = 0.0f;
    for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * row[k];
    return sum;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return dotRowNeon(x, row, dim_);
#else
    float sum = 0.0f;
    for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * row[k];
    return sum;
#endif
  }

  std::size_t argmaxFp32(const float* x, const std::int64_t* candidate_ids, std::size_t candidate_count) const {
#if defined(__x86_64__) || defined(_M_X64)
    if (avx2_ && !sequential_fma_) return argmaxFp32Avx2(x, dim_, embedding_, output_bias_, candidate_ids, candidate_count);
#endif
    std::size_t best = 0;
    float best_logit = -std::numeric_limits<float>::infinity();
    for (std::size_t j = 0; j < candidate_count; ++j) {
      const auto token = static_cast<std::size_t>(candidate_ids[j]);
      const float bias = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
      const float val = dotRow(x, embedding_ + token * dim_) + bias;
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
        throw std::runtime_error("SSRU nonfinite logit");
      if (j == 0 || val > best_logit) { best_logit = val; best = j; }
    }
    return best;
  }

  std::size_t argmax(const float* x, const std::int64_t* candidate_ids,
                     std::size_t candidate_count, Workspace& workspace) const {
    if (!x || !candidate_ids || !candidate_count)
      throw std::invalid_argument("Invalid shortlist arguments");
    for (std::size_t j = 0; j < candidate_count; ++j)
      if (candidate_ids[j] < 0 || static_cast<std::uint64_t>(candidate_ids[j]) >= vocab_)
        throw std::invalid_argument("Shortlist candidate outside vocabulary");
    if (!use_q8_) {
      checkFinite(x, dim_, "Nonfinite shortlist activation");
      return argmaxFp32(x, candidate_ids, candidate_count);
    }
    std::uint8_t* x_u8 = workspace.resize(dim_);
    float* approx = workspace.resizeFloats(candidate_count);
    float x_scale = 1.0f;
    std::uint8_t x_zp = 0;
    quantizeLinearU8(x, 1, dim_, x_u8, x_scale, x_zp);
    float best_approx = -std::numeric_limits<float>::infinity();
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (vnni_) {
      best_approx = scanShortlistVnni(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                                      row_scales_.data(), row_sums_.data(), output_bias_,
                                      candidate_ids, candidate_count, approx);
    } else
#endif
    {
      best_approx = scanShortlistAvx2(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                                      row_scales_.data(), row_sums_.data(), output_bias_,
                                      candidate_ids, candidate_count, approx);
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    best_approx = scanShortlistSdot(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                                    row_scales_.data(), row_sums_.data(), output_bias_,
                                    candidate_ids, candidate_count, approx);
#endif
    if (!std::isfinite(best_approx)) return argmaxFp32(x, candidate_ids, candidate_count);
    std::size_t best_j = 0;
    for (std::size_t j = 0; j < candidate_count; ++j) {
      if (approx[j] == best_approx) { best_j = j; break; }
    }
    const auto seed_token = static_cast<std::size_t>(candidate_ids[best_j]);
    const float seed_bias = seed_token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[seed_token];
    float best_exact = dotRow(x, embedding_ + seed_token * dim_) + seed_bias;
    if (!std::isfinite(best_exact) && !(best_exact == -std::numeric_limits<float>::infinity() && seed_token == 1))
      throw std::runtime_error("SSRU nonfinite logit");
    std::size_t best = best_j;

    float dx_sq = 0.0f, qx_sq = 0.0f;
    const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
    for (std::size_t k = 0; k < dim_; ++k) {
      const float qx = static_cast<float>(static_cast<std::int32_t>(x_u8[k]) - zp_i) * x_scale;
      const float dx = x[k] - qx;
      dx_sq += dx * dx;
      qx_sq += qx * qx;
    }
    const float dx_l2 = std::sqrt(dx_sq), qx_l2 = std::sqrt(qx_sq);
    const float eps = std::numeric_limits<float>::epsilon();
    const float slack = 32.0f * eps * (std::abs(best_approx) + max_bias_abs_) + 1e-4f;
    const float global_err = 1.01f * (dx_l2 * max_w_l2_ + qx_l2 * max_dw_l2_) + slack;
    if (!std::isfinite(global_err)) return argmaxFp32(x, candidate_ids, candidate_count);
    for (std::size_t j = 0; j < candidate_count; ++j) {
      if (j == best_j || approx[j] + global_err < best_exact) continue;
      const auto token = static_cast<std::size_t>(candidate_ids[j]);
      const float row_err = 1.01f * (dx_l2 * row_w_l2_[token] + qx_l2 * row_dw_l2_[token]) + slack;
      if (approx[j] + row_err < best_exact) continue;
      const float bias = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
      const float val = dotRow(x, embedding_ + token * dim_) + bias;
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
        throw std::runtime_error("SSRU nonfinite logit");
      if (val > best_exact || (val == best_exact && j < best)) { best_exact = val; best = j; }
    }
    return best;
  }
};

ShortlistTable::ShortlistTable(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
                               bool sequential_fma)
    : impl_(std::make_unique<const Impl>(embedding, output_bias, vocab, dim, sequential_fma)) {}
ShortlistTable::~ShortlistTable() = default;
std::size_t ShortlistTable::argmax(const float* x, const std::int64_t* candidate_ids,
                                   std::size_t candidate_count, Workspace& workspace) const {
  return impl_->argmax(x, candidate_ids, candidate_count, workspace);
}
}
