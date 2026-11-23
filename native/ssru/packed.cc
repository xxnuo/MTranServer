#include "packed.h"
#include "kleidi.h"
#include "pcore_pool.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_AVX2 __declspec(noinline)
#define MTRAN_AVX2_FMA __declspec(noinline)
#define MTRAN_DOTPROD __declspec(noinline)
#else
#define MTRAN_AVX2 __attribute__((target("avx2"), noinline))
#define MTRAN_AVX2_FMA __attribute__((target("avx2,fma"), noinline))
#if defined(__aarch64__) || defined(_M_ARM64)
#define MTRAN_DOTPROD __attribute__((target("+dotprod"), noinline))
#else
#define MTRAN_DOTPROD
#endif
#endif

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__clang__) || defined(__GNUC__)
#define MTRAN_PREFETCH(p) __builtin_prefetch((p), 0, 1)
#else
#define MTRAN_PREFETCH(p) _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T0)
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(__clang__) || defined(__GNUC__)
#define MTRAN_PREFETCH(p) __builtin_prefetch((p), 0, 1)
#else
#define MTRAN_PREFETCH(p)
#endif
#else
#define MTRAN_PREFETCH(p)
#endif

namespace mtran::ssru::packed {
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
void multiplyBatchK4Vnni(
    const std::uint8_t*, std::size_t, const float*, const std::uint8_t*,
    const std::int8_t*, const std::int32_t*, float, std::size_t, std::size_t,
    std::size_t, std::size_t, float*, std::size_t, const float*, const float*,
    bool, float*);
ShortlistTable::ScanResult scanShortlistVnni(
    const std::uint8_t*, float, std::uint8_t, std::size_t,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    const std::int64_t*, std::size_t, float*,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    std::size_t, bool, float);
void scanShortlistVnniBatch(
    const std::uint8_t*, std::size_t, const float*, const std::uint8_t*,
    std::size_t, std::size_t, const std::int8_t*, const ShortlistTable::RowMeta*,
    const std::int64_t*, const std::uint32_t*, std::size_t, float*,
    float*, std::size_t*, float*, bool*,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    std::size_t, bool, float);
bool prepareFullQ4Vnni(const float*, std::size_t, std::uint8_t*);
void scanFullQ4K4VnniRange(
    std::size_t, std::size_t, std::size_t,
    const std::uint8_t*, const float*,
    const float*, const std::uint8_t*, FullVocabQ4::TopK9&);
void scanFullQ4K4VnniBatchRange(
    std::size_t, std::size_t, std::size_t, std::size_t,
    const std::uint8_t*, const float*,
    const float*, const std::uint8_t* const*, FullVocabQ4::TopK9*);
#endif

#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
void multiplyBatchK4Avx512Vnni(
    const std::uint8_t*, std::size_t, const float*, const std::uint8_t*,
    const std::int8_t*, const std::int32_t*, float, std::size_t, std::size_t,
    std::size_t, std::size_t, float*, std::size_t, const float*, const float*,
    bool, float*);
ShortlistTable::ScanResult scanShortlistAvx512Vnni(
    const std::uint8_t*, float, std::uint8_t, std::size_t,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    const std::int64_t*, std::size_t, float*,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    std::size_t, bool, float);
void scanShortlistAvx512VnniBatch(
    const std::uint8_t*, std::size_t, const float*, const std::uint8_t*,
    std::size_t, std::size_t, const std::int8_t*, const ShortlistTable::RowMeta*,
    const std::int64_t*, const std::uint32_t*, std::size_t, float*,
    float*, std::size_t*, float*, bool*,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    std::size_t, bool, float);
bool prepareFullQ4Avx512Vnni(const float*, std::size_t, std::uint8_t*);
void scanFullQ4K4Avx512VnniRange(
    std::size_t, std::size_t, std::size_t,
    const std::uint8_t*, const float*,
    const float*, const std::uint8_t*, FullVocabQ4::TopK9&);
void scanFullQ4K4Avx512VnniBatchRange(
    std::size_t, std::size_t, std::size_t, std::size_t,
    const std::uint8_t*, const float*,
    const float*, const std::uint8_t* const*, FullVocabQ4::TopK9*);
#endif

namespace {
float roundEven(float x) {
  const float r = std::nearbyint(x);
  return std::isfinite(r) ? r : x;
}

void checkFinite(const float* data, std::size_t count, const char* what) {
  std::size_t i = 0;
#if defined(__x86_64__) || defined(_M_X64)
  const __m128i mask4 = _mm_set1_epi32(0x7f800000);
  __m128i bad4 = _mm_setzero_si128();
  for (; i + 4 <= count; i += 4) {
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    bad4 = _mm_or_si128(bad4, _mm_cmpeq_epi32(_mm_and_si128(v, mask4), mask4));
  }
  if (_mm_movemask_epi8(bad4) != 0) throw std::runtime_error(what);
#elif defined(__aarch64__) || defined(_M_ARM64)
  const uint32x4_t mask4 = vdupq_n_u32(0x7f800000u);
  uint32x4_t bad4 = vdupq_n_u32(0u);
  for (; i + 4 <= count; i += 4) {
    const uint32x4_t v = vreinterpretq_u32_f32(vld1q_f32(data + i));
    bad4 = vorrq_u32(bad4, vceqq_u32(vandq_u32(v, mask4), mask4));
  }
  if (vmaxvq_u32(bad4) != 0) throw std::runtime_error(what);
#endif
  for (; i < count; ++i) {
    std::uint32_t bits;
    std::memcpy(&bits, &data[i], sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) throw std::runtime_error(what);
  }
}

#if defined(__x86_64__) || defined(_M_X64)
unsigned computeFeatures() noexcept {
  unsigned leaf1_ecx = 0, leaf7_ebx = 0, leaf7_ecx = 0, leaf71_eax = 0;
  std::uint64_t xcr0 = 0;
#ifdef _MSC_VER
  int regs[4] = {};
  __cpuid(regs, 0);
  if (regs[0] < 7) return 0;
  __cpuidex(regs, 1, 0);
  leaf1_ecx = static_cast<unsigned>(regs[2]);
  if ((leaf1_ecx & 0x18000000u) != 0x18000000u) return 0;
  xcr0 = _xgetbv(0);
  __cpuidex(regs, 7, 0);
  const bool subleaf1 = regs[0] >= 1;
  leaf7_ebx = static_cast<unsigned>(regs[1]);
  leaf7_ecx = static_cast<unsigned>(regs[2]);
  if (subleaf1) {
    __cpuidex(regs, 7, 1);
    leaf71_eax = static_cast<unsigned>(regs[0]);
  }
#else
  unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
  leaf1_ecx = ecx;
  if ((leaf1_ecx & 0x18000000u) != 0x18000000u) return 0;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  xcr0 = (static_cast<std::uint64_t>(edx) << 32) | eax;
  if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 0;
  leaf7_ebx = ebx;
  leaf7_ecx = ecx;
  if (eax >= 1 && __get_cpuid_count(7, 1, &eax, &ebx, &ecx, &edx)) leaf71_eax = eax;
#endif
  return detail::x86Features(leaf1_ecx, leaf7_ebx, leaf7_ecx, leaf71_eax, xcr0);
}

unsigned features() noexcept {
  static const unsigned cached = computeFeatures();
  return cached;
}

bool computeFma() noexcept {
#ifdef _MSC_VER
  int regs[4] = {};
  __cpuid(regs, 0);
  if (regs[0] < 1) return false;
  __cpuidex(regs, 1, 0);
  return (static_cast<unsigned>(regs[2]) & 0x18001000u) == 0x18001000u && (_xgetbv(0) & 6) == 6;
#else
  unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
  if ((ecx & 0x18001000u) != 0x18001000u) return false;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return (eax & 6) == 6;
#endif
}

bool hasFma() noexcept {
  static const bool cached = computeFma();
  return cached;
}

template <bool ortSaturation>
MTRAN_AVX2 void dotAvx2(const std::uint8_t* a, const std::int8_t* b, std::size_t k, std::int32_t* result) {
  __m256i total = _mm256_setzero_si256();
  const __m256i ones = _mm256_set1_epi16(1);
  for (std::size_t j = 0; j < k; j += 32, b += 256) {
    const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + j));
    __m256i row[8];
    for (std::size_t lane = 0; lane < 8; ++lane) {
      const auto w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + lane * 32));
      if constexpr (ortSaturation) {
        row[lane] = _mm256_madd_epi16(_mm256_maddubs_epi16(x, w), ones);
      } else {
        const auto m0 = _mm256_maddubs_epi16(_mm256_cvtepu8_epi16(_mm256_castsi256_si128(x)),
                                             _mm256_cvtepi8_epi16(_mm256_castsi256_si128(w)));
        const auto m1 = _mm256_maddubs_epi16(_mm256_cvtepu8_epi16(_mm256_extracti128_si256(x, 1)),
                                             _mm256_cvtepi8_epi16(_mm256_extracti128_si256(w, 1)));
        row[lane] = _mm256_add_epi32(_mm256_madd_epi16(m0, ones), _mm256_madd_epi16(m1, ones));
      }
    }
    const auto s01 = _mm256_hadd_epi32(row[0], row[1]), s23 = _mm256_hadd_epi32(row[2], row[3]);
    const auto s45 = _mm256_hadd_epi32(row[4], row[5]), s67 = _mm256_hadd_epi32(row[6], row[7]);
    const auto lo = _mm256_hadd_epi32(s01, s23), hi = _mm256_hadd_epi32(s45, s67);
    const auto sum = _mm256_add_epi32(_mm256_permute2x128_si256(lo, hi, 0x20), _mm256_permute2x128_si256(lo, hi, 0x31));
    total = _mm256_add_epi32(total, sum);
  }
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(result), total);
}

MTRAN_AVX2 void quantizeLinearU8RangeAvx2(
    const float* input, std::size_t total, std::uint8_t* output,
    float& scale, std::uint8_t& zero_point, float* dx_sq_out, float* qx_sq_out) {
  const __m256i exp_mask = _mm256_set1_epi32(0x7f800000);
  __m256i bad = _mm256_setzero_si256();
  __m256 vlo = _mm256_setzero_ps(), vhi = _mm256_setzero_ps();
  std::size_t i = 0;
  for (; i + 8 <= total; i += 8) {
    const __m256 v = _mm256_loadu_ps(input + i);
    const __m256i vi = _mm256_castps_si256(v);
    bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(vi, exp_mask), exp_mask));
    vlo = _mm256_min_ps(vlo, v);
    vhi = _mm256_max_ps(vhi, v);
  }
  if (_mm256_movemask_epi8(bad) != 0) throw std::runtime_error("Nonfinite DQL activation");
  __m128 l4 = _mm_min_ps(_mm256_castps256_ps128(vlo), _mm256_extractf128_ps(vlo, 1));
  __m128 h4 = _mm_max_ps(_mm256_castps256_ps128(vhi), _mm256_extractf128_ps(vhi, 1));
  l4 = _mm_min_ps(l4, _mm_movehl_ps(l4, l4));
  h4 = _mm_max_ps(h4, _mm_movehl_ps(h4, h4));
  l4 = _mm_min_ss(l4, _mm_shuffle_ps(l4, l4, 1));
  h4 = _mm_max_ss(h4, _mm_shuffle_ps(h4, h4, 1));
  float lo = _mm_cvtss_f32(l4);
  float hi = _mm_cvtss_f32(h4);
  for (; i < total; ++i) {
    std::uint32_t bits;
    std::memcpy(&bits, &input[i], sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) throw std::runtime_error("Nonfinite DQL activation");
    lo = std::min(lo, input[i]);
    hi = std::max(hi, input[i]);
  }
  scale = (hi == lo) ? 1.0f : (hi - lo) / 255.0f;
  if (!std::isfinite(scale) || scale <= 0.0f) throw std::runtime_error("Invalid DQL scale");
  const float initial_zp = std::clamp(0.0f - lo / scale, 0.0f, 255.0f);
  zero_point = static_cast<std::uint8_t>(roundEven(initial_zp));
  const float min_val = static_cast<float>(0 - static_cast<std::int32_t>(zero_point));
  const float max_val = static_cast<float>(255 - static_cast<std::int32_t>(zero_point));

  const __m256 vscale = _mm256_set1_ps(scale);
  const __m256 vmin = _mm256_set1_ps(min_val), vmax = _mm256_set1_ps(max_val);
  const __m256i vzp = _mm256_set1_epi32(static_cast<int>(zero_point));
  const __m256i perm_idx = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
  __m256 vdx = _mm256_setzero_ps(), vqx = _mm256_setzero_ps();
  const bool want_dx = dx_sq_out != nullptr;
  const bool want_qx = qx_sq_out != nullptr;

  std::size_t j = 0;
  for (; j + 32 <= total; j += 32) {
    __m256i acc_q[4];
    for (int grp = 0; grp < 4; ++grp) {
      const __m256 x = _mm256_loadu_ps(input + j + grp * 8);
      __m256 v = _mm256_div_ps(x, vscale);
      v = _mm256_max_ps(v, vmin);
      v = _mm256_min_ps(v, vmax);
      const __m256i qi = _mm256_cvtps_epi32(v);
      if (want_dx || want_qx) {
        const __m256 qf = _mm256_mul_ps(_mm256_cvtepi32_ps(qi), vscale);
        if (want_dx) {
          const __m256 d = _mm256_sub_ps(x, qf);
          vdx = _mm256_add_ps(vdx, _mm256_mul_ps(d, d));
        }
        if (want_qx) {
          vqx = _mm256_add_ps(vqx, _mm256_mul_ps(qf, qf));
        }
      }
      acc_q[grp] = _mm256_add_epi32(qi, vzp);
    }
    const __m256i s01 = _mm256_packs_epi32(acc_q[0], acc_q[1]);
    const __m256i s23 = _mm256_packs_epi32(acc_q[2], acc_q[3]);
    const __m256i u8 = _mm256_packus_epi16(s01, s23);
    // In-lane packus splits [0..3, 8..11, 16..19, 24..27 | 4..7, 12..15, 20..23, 28..31]
    // Permute 32-bit blocks by [0, 4, 1, 5, 2, 6, 3, 7] -> contiguous [0..31]!
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output + j), _mm256_permutevar8x32_epi32(u8, perm_idx));
  }
  for (; j + 8 <= total; j += 8) {
    const __m256 x = _mm256_loadu_ps(input + j);
    __m256 v = _mm256_div_ps(x, vscale);
    v = _mm256_max_ps(v, vmin);
    v = _mm256_min_ps(v, vmax);
    const __m256i qi = _mm256_cvtps_epi32(v);
    if (want_dx || want_qx) {
      const __m256 qf = _mm256_mul_ps(_mm256_cvtepi32_ps(qi), vscale);
      if (want_dx) {
        const __m256 d = _mm256_sub_ps(x, qf);
        vdx = _mm256_add_ps(vdx, _mm256_mul_ps(d, d));
      }
      if (want_qx) {
        vqx = _mm256_add_ps(vqx, _mm256_mul_ps(qf, qf));
      }
    }
    const __m256i uz = _mm256_add_epi32(qi, vzp);
    const __m128i s16 = _mm_packs_epi32(_mm256_castsi256_si128(uz), _mm256_extracti128_si256(uz, 1));
    const __m128i u8 = _mm_packus_epi16(s16, s16);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(output + j), u8);
  }
  float tail_dx = 0.0f, tail_qx = 0.0f;
  for (; j < total; ++j) {
    __m128 v = _mm_div_ss(_mm_load_ss(input + j), _mm256_castps256_ps128(vscale));
    v = _mm_max_ss(v, _mm256_castps256_ps128(vmin));
    v = _mm_min_ss(v, _mm256_castps256_ps128(vmax));
    const std::int32_t qi = _mm_cvtsi128_si32(_mm_cvtps_epi32(v));
    output[j] = static_cast<std::uint8_t>(qi + static_cast<std::int32_t>(zero_point));
    if (want_dx || want_qx) {
      const float qf = static_cast<float>(qi) * scale;
      if (want_dx) { const float d = input[j] - qf; tail_dx += d * d; }
      if (want_qx) { tail_qx += qf * qf; }
    }
  }
  if (want_dx) {
    const __m128 s = _mm_add_ps(_mm256_castps256_ps128(vdx), _mm256_extractf128_ps(vdx, 1));
    const __m128 s2 = _mm_add_ps(s, _mm_movehdup_ps(s));
    *dx_sq_out = _mm_cvtss_f32(_mm_add_ss(s2, _mm_movehl_ps(s2, s2))) + tail_dx;
  }
  if (want_qx) {
    const __m128 s = _mm_add_ps(_mm256_castps256_ps128(vqx), _mm256_extractf128_ps(vqx, 1));
    const __m128 s2 = _mm_add_ps(s, _mm_movehdup_ps(s));
    *qx_sq_out = _mm_cvtss_f32(_mm_add_ss(s2, _mm_movehl_ps(s2, s2))) + tail_qx;
  }
}

MTRAN_AVX2 void quantizeLinearU8FromMinMaxRowAvx2(
    const float* input, std::size_t total, float lo_in, float hi_in,
    std::uint8_t* output, float& scale, std::uint8_t& zero_point) {
  if (!std::isfinite(lo_in) || !std::isfinite(hi_in)) throw std::runtime_error("Nonfinite DQL activation");
  const float lo = std::min(0.0f, lo_in);
  const float hi = std::max(0.0f, hi_in);
  scale = (hi == lo) ? 1.0f : (hi - lo) / 255.0f;
  if (!std::isfinite(scale) || scale <= 0.0f) throw std::runtime_error("Invalid DQL scale");
  const float initial_zp = std::clamp(0.0f - lo / scale, 0.0f, 255.0f);
  zero_point = static_cast<std::uint8_t>(roundEven(initial_zp));
  const float min_val = static_cast<float>(0 - static_cast<std::int32_t>(zero_point));
  const float max_val = static_cast<float>(255 - static_cast<std::int32_t>(zero_point));

  const __m256 vscale = _mm256_set1_ps(scale);
  const __m256 vmin = _mm256_set1_ps(min_val), vmax = _mm256_set1_ps(max_val);
  const __m256i vzp = _mm256_set1_epi32(static_cast<int>(zero_point));
  const __m256i perm_idx = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
  const __m256i exp_mask = _mm256_set1_epi32(0x7f800000);
  __m256i bad = _mm256_setzero_si256();
  std::size_t j = 0;
  for (; j + 32 <= total; j += 32) {
    __m256i acc_q[4];
    for (int grp = 0; grp < 4; ++grp) {
      const __m256 x = _mm256_loadu_ps(input + j + grp * 8);
      bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(x), exp_mask), exp_mask));
      __m256 v = _mm256_div_ps(x, vscale);
      v = _mm256_max_ps(v, vmin);
      v = _mm256_min_ps(v, vmax);
      acc_q[grp] = _mm256_add_epi32(_mm256_cvtps_epi32(v), vzp);
    }
    const __m256i u8 = _mm256_packus_epi16(_mm256_packs_epi32(acc_q[0], acc_q[1]),
                                           _mm256_packs_epi32(acc_q[2], acc_q[3]));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output + j), _mm256_permutevar8x32_epi32(u8, perm_idx));
  }
  for (; j + 8 <= total; j += 8) {
    const __m256 x = _mm256_loadu_ps(input + j);
    bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(x), exp_mask), exp_mask));
    __m256 v = _mm256_div_ps(x, vscale);
    v = _mm256_max_ps(v, vmin);
    v = _mm256_min_ps(v, vmax);
    const __m256i uz = _mm256_add_epi32(_mm256_cvtps_epi32(v), vzp);
    const __m128i s16 = _mm_packs_epi32(_mm256_castsi256_si128(uz), _mm256_extracti128_si256(uz, 1));
    _mm_storel_epi64(reinterpret_cast<__m128i*>(output + j), _mm_packus_epi16(s16, s16));
  }
  if (_mm256_movemask_epi8(bad) != 0) throw std::runtime_error("Nonfinite DQL activation");
  for (; j < total; ++j) {
    std::uint32_t bits;
    std::memcpy(&bits, &input[j], sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) throw std::runtime_error("Nonfinite DQL activation");
    __m128 v = _mm_div_ss(_mm_load_ss(input + j), _mm256_castps256_ps128(vscale));
    v = _mm_max_ss(v, _mm256_castps256_ps128(vmin));
    v = _mm_min_ss(v, _mm256_castps256_ps128(vmax));
    output[j] = static_cast<std::uint8_t>(_mm_cvtsi128_si32(_mm_cvtps_epi32(v)) + static_cast<std::int32_t>(zero_point));
  }
}

MTRAN_AVX2 ShortlistTable::ScanResult scanShortlistAvx2(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out,
    const std::int8_t* hot_q8, const ShortlistTable::RowMeta* hot_meta,
    std::size_t hot_prefix_len, bool exact_int8, float exact_scale) {
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
  const __m256i vzp = _mm256_set1_epi32(zp_i);
  const __m256 vxscale = _mm256_set1_ps(x_scale);
  const __m256 vexact = _mm256_set1_ps(x_scale * exact_scale);
  const __m256i ones = _mm256_set1_epi16(1);
  (void)hot_q8;
  (void)hot_meta;
  (void)hot_prefix_len;

  std::size_t j = 0;
  for (; j + 8 <= candidate_count; j += 8) {
    if (j + 16 <= candidate_count) {
      for (int p = 0; p < 8; ++p) {
        const auto nt = static_cast<std::size_t>(candidate_ids[j + 8 + p]);
        _mm_prefetch(reinterpret_cast<const char*>(emb_q8 + nt * dim), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(meta + nt), _MM_HINT_T0);
      }
    }
    const std::size_t ts[8] = {
      static_cast<std::size_t>(candidate_ids[j + 0]), static_cast<std::size_t>(candidate_ids[j + 1]),
      static_cast<std::size_t>(candidate_ids[j + 2]), static_cast<std::size_t>(candidate_ids[j + 3]),
      static_cast<std::size_t>(candidate_ids[j + 4]), static_cast<std::size_t>(candidate_ids[j + 5]),
      static_cast<std::size_t>(candidate_ids[j + 6]), static_cast<std::size_t>(candidate_ids[j + 7]),
    };
    const std::int8_t* rows[8] = {
      emb_q8 + ts[0] * dim, emb_q8 + ts[1] * dim, emb_q8 + ts[2] * dim, emb_q8 + ts[3] * dim,
      emb_q8 + ts[4] * dim, emb_q8 + ts[5] * dim, emb_q8 + ts[6] * dim, emb_q8 + ts[7] * dim,
    };
    __m256i total = _mm256_setzero_si256();
    for (std::size_t k = 0; k < dim; k += 32) {
      const __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x_u8 + k));
      __m256i r[8];
      for (int lane = 0; lane < 8; ++lane) {
        const __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[lane] + k));
        const __m256i m0 = _mm256_maddubs_epi16(_mm256_cvtepu8_epi16(_mm256_castsi256_si128(x)),
                                                _mm256_cvtepi8_epi16(_mm256_castsi256_si128(w)));
        const __m256i m1 = _mm256_maddubs_epi16(_mm256_cvtepu8_epi16(_mm256_extracti128_si256(x, 1)),
                                                _mm256_cvtepi8_epi16(_mm256_extracti128_si256(w, 1)));
        r[lane] = _mm256_add_epi32(_mm256_madd_epi16(m0, ones), _mm256_madd_epi16(m1, ones));
      }
      const __m256i s01 = _mm256_hadd_epi32(r[0], r[1]), s23 = _mm256_hadd_epi32(r[2], r[3]);
      const __m256i s45 = _mm256_hadd_epi32(r[4], r[5]), s67 = _mm256_hadd_epi32(r[6], r[7]);
      const __m256i lo = _mm256_hadd_epi32(s01, s23), hi = _mm256_hadd_epi32(s45, s67);
      total = _mm256_add_epi32(total, _mm256_add_epi32(_mm256_permute2x128_si256(lo, hi, 0x20),
                                                       _mm256_permute2x128_si256(lo, hi, 0x31)));
    }
    const __m256i rsums = _mm256_setr_epi32(
        meta[ts[0]].sum, meta[ts[1]].sum, meta[ts[2]].sum, meta[ts[3]].sum,
        meta[ts[4]].sum, meta[ts[5]].sum, meta[ts[6]].sum, meta[ts[7]].sum);
    const __m256 rscales = exact_int8 ? vexact : _mm256_mul_ps(vxscale, _mm256_setr_ps(
        meta[ts[0]].scale, meta[ts[1]].scale, meta[ts[2]].scale, meta[ts[3]].scale,
        meta[ts[4]].scale, meta[ts[5]].scale, meta[ts[6]].scale, meta[ts[7]].scale));
    const __m256 rbias = _mm256_setr_ps(
        meta[ts[0]].bias, meta[ts[1]].bias, meta[ts[2]].bias, meta[ts[3]].bias,
        meta[ts[4]].bias, meta[ts[5]].bias, meta[ts[6]].bias, meta[ts[7]].bias);
    const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(total, _mm256_mullo_epi32(vzp, rsums)));
    const __m256 vals = _mm256_add_ps(_mm256_mul_ps(corr, rscales), rbias);
    _mm256_storeu_ps(approx_out + j, vals);
    if (_mm256_movemask_ps(_mm256_cmp_ps(vals, _mm256_set1_ps(second_logit), _CMP_GE_OQ)) != 0) {
      for (int lane = 0; lane < 8; ++lane) {
        const float val = approx_out[j + lane];
        if (val > best_logit) {
          second_logit = best_logit;
          best_logit = val;
          best_j = j + lane;
        } else if (val > second_logit) {
          second_logit = val;
        }
      }
    }
  }
  for (; j < candidate_count; ++j) {
    const auto t = static_cast<std::size_t>(candidate_ids[j]);
    const auto* row = emb_q8 + t * dim;
    std::int32_t acc = 0;
    for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(x_u8[k]) * row[k];
    const auto& m = meta[t];
    const float sc = exact_int8 ? (x_scale * exact_scale) : (x_scale * m.scale);
    const float val = static_cast<float>(acc - zp_i * m.sum) * sc + m.bias;
    approx_out[j] = val;
    if (val > best_logit) {
      second_logit = best_logit;
      best_logit = val;
      best_j = j;
    } else if (val > second_logit) {
      second_logit = val;
    }
  }
  return {best_logit, best_j, second_logit};
}

MTRAN_AVX2 void scanShortlistAvx2Batch(
    const std::uint8_t* x_u8, std::size_t x_stride, const float* x_scales,
    const std::uint8_t* x_zps, std::size_t rows, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* union_ids, const std::uint32_t* slot_masks,
    std::size_t union_count, float* approx_out, float* best_logits,
    std::size_t* best_js, float* second_logits, bool* has_any,
    bool exact_int8, float exact_scale) {
  for (std::size_t m = 0; m < rows; ++m) {
    best_logits[m] = -std::numeric_limits<float>::infinity();
    second_logits[m] = -std::numeric_limits<float>::infinity();
    best_js[m] = 0;
    has_any[m] = false;
  }
  const __m256i ones = _mm256_set1_epi16(1);
  std::size_t j = 0;
  for (; j + 8 <= union_count; j += 8) {
    const std::size_t ts[8] = {
      static_cast<std::size_t>(union_ids[j + 0]), static_cast<std::size_t>(union_ids[j + 1]),
      static_cast<std::size_t>(union_ids[j + 2]), static_cast<std::size_t>(union_ids[j + 3]),
      static_cast<std::size_t>(union_ids[j + 4]), static_cast<std::size_t>(union_ids[j + 5]),
      static_cast<std::size_t>(union_ids[j + 6]), static_cast<std::size_t>(union_ids[j + 7]),
    };
    const std::int8_t* erows[8] = {
      emb_q8 + ts[0] * dim, emb_q8 + ts[1] * dim, emb_q8 + ts[2] * dim, emb_q8 + ts[3] * dim,
      emb_q8 + ts[4] * dim, emb_q8 + ts[5] * dim, emb_q8 + ts[6] * dim, emb_q8 + ts[7] * dim,
    };
    const __m256i rsums = _mm256_setr_epi32(
        meta[ts[0]].sum, meta[ts[1]].sum, meta[ts[2]].sum, meta[ts[3]].sum,
        meta[ts[4]].sum, meta[ts[5]].sum, meta[ts[6]].sum, meta[ts[7]].sum);
    const __m256 raw_rscales = _mm256_setr_ps(
        meta[ts[0]].scale, meta[ts[1]].scale, meta[ts[2]].scale, meta[ts[3]].scale,
        meta[ts[4]].scale, meta[ts[5]].scale, meta[ts[6]].scale, meta[ts[7]].scale);
    const __m256 rbias = _mm256_setr_ps(
        meta[ts[0]].bias, meta[ts[1]].bias, meta[ts[2]].bias, meta[ts[3]].bias,
        meta[ts[4]].bias, meta[ts[5]].bias, meta[ts[6]].bias, meta[ts[7]].bias);

    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* xm = x_u8 + m * x_stride;
      __m256i total = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 32) {
        const __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(xm + k));
        const __m256i xu_lo = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(x));
        const __m256i xu_hi = _mm256_cvtepu8_epi16(_mm256_extracti128_si256(x, 1));
        __m256i r[8];
        for (int lane = 0; lane < 8; ++lane) {
          const __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(erows[lane] + k));
          const __m256i m0 = _mm256_maddubs_epi16(xu_lo, _mm256_cvtepi8_epi16(_mm256_castsi256_si128(w)));
          const __m256i m1 = _mm256_maddubs_epi16(xu_hi, _mm256_cvtepi8_epi16(_mm256_extracti128_si256(w, 1)));
          r[lane] = _mm256_add_epi32(_mm256_madd_epi16(m0, ones), _mm256_madd_epi16(m1, ones));
        }
        const __m256i s01 = _mm256_hadd_epi32(r[0], r[1]), s23 = _mm256_hadd_epi32(r[2], r[3]);
        const __m256i s45 = _mm256_hadd_epi32(r[4], r[5]), s67 = _mm256_hadd_epi32(r[6], r[7]);
        const __m256i lo = _mm256_hadd_epi32(s01, s23), hi = _mm256_hadd_epi32(s45, s67);
        total = _mm256_add_epi32(total, _mm256_add_epi32(_mm256_permute2x128_si256(lo, hi, 0x20),
                                                         _mm256_permute2x128_si256(lo, hi, 0x31)));
      }
      const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
      const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m] * exact_scale)
                                    : _mm256_mul_ps(_mm256_set1_ps(x_scales[m]), raw_rscales);
      const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(total, _mm256_mullo_epi32(vzp, rsums)));
      const __m256 vals = _mm256_add_ps(_mm256_mul_ps(corr, rsc), rbias);
      _mm256_storeu_ps(approx_out + m * union_count + j, vals);
      for (int l = 0; l < 8; ++l) {
        if ((slot_masks[j + l] & (1u << m)) == 0) continue;
        const float val = approx_out[m * union_count + j + l];
        if (!has_any[m]) {
          has_any[m] = true;
          best_logits[m] = val;
          best_js[m] = j + l;
        } else if (val > best_logits[m]) {
          second_logits[m] = best_logits[m];
          best_logits[m] = val;
          best_js[m] = j + l;
        } else if (val > second_logits[m]) {
          second_logits[m] = val;
        }
      }
    }
  }
  for (; j < union_count; ++j) {
    const auto t = static_cast<std::size_t>(union_ids[j]);
    const auto* row = emb_q8 + t * dim;
    const auto& rm = meta[t];
    const std::uint32_t mask_j = slot_masks[j];
    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* xm = x_u8 + m * x_stride;
      std::int32_t acc = 0;
      for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(xm[k]) * row[k];
      const std::int32_t zp_i = static_cast<std::int32_t>(x_zps[m]);
      const float sc = exact_int8 ? (x_scales[m] * exact_scale) : (x_scales[m] * rm.scale);
      const float val = static_cast<float>(acc - zp_i * rm.sum) * sc + rm.bias;
      approx_out[m * union_count + j] = val;
      if ((mask_j & (1u << m)) == 0) continue;
      if (!has_any[m]) {
        has_any[m] = true;
        best_logits[m] = val;
        best_js[m] = j;
      } else if (val > best_logits[m]) {
        second_logits[m] = best_logits[m];
        best_logits[m] = val;
        best_js[m] = j;
      } else if (val > second_logits[m]) {
        second_logits[m] = val;
      }
    }
  }
}

MTRAN_AVX2 float dotRowAvx2(const float* x, const float* row, std::size_t dim) {
  __m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps();
  std::size_t k = 0;
  for (; k + 16 <= dim; k += 16) {
    s0 = _mm256_add_ps(s0, _mm256_mul_ps(_mm256_loadu_ps(x + k), _mm256_loadu_ps(row + k)));
    s1 = _mm256_add_ps(s1, _mm256_mul_ps(_mm256_loadu_ps(x + k + 8), _mm256_loadu_ps(row + k + 8)));
  }
  const __m256 vsum = _mm256_add_ps(s0, s1);
  __m128 sum4 = _mm_add_ps(_mm256_castps256_ps128(vsum), _mm256_extractf128_ps(vsum, 1));
  for (; k + 8 <= dim; k += 8) {
    const __m256 v = _mm256_mul_ps(_mm256_loadu_ps(x + k), _mm256_loadu_ps(row + k));
    sum4 = _mm_add_ps(sum4, _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)));
  }
  sum4 = _mm_add_ps(sum4, _mm_movehl_ps(sum4, sum4));
  float sum = _mm_cvtss_f32(_mm_add_ss(sum4, _mm_shuffle_ps(sum4, sum4, 1)));
  for (; k < dim; ++k) sum += x[k] * row[k];
  return sum;
}

MTRAN_AVX2 float dotRowInt8Avx2(const float* x, const std::int8_t* qrow, float min_pos, std::size_t dim) {
  const __m256 vmin = _mm256_set1_ps(min_pos);
  __m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps();
  std::size_t k = 0;
  for (; k + 16 <= dim; k += 16) {
    const __m128i q16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(qrow + k));
    const __m256 w0 = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(q16)), vmin);
    const __m256 w1 = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(_mm_srli_si128(q16, 8))), vmin);
    s0 = _mm256_add_ps(s0, _mm256_mul_ps(_mm256_loadu_ps(x + k), w0));
    s1 = _mm256_add_ps(s1, _mm256_mul_ps(_mm256_loadu_ps(x + k + 8), w1));
  }
  const __m256 vsum = _mm256_add_ps(s0, s1);
  __m128 sum4 = _mm_add_ps(_mm256_castps256_ps128(vsum), _mm256_extractf128_ps(vsum, 1));
  for (; k + 8 <= dim; k += 8) {
    const __m128i q8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(qrow + k));
    const __m256 w = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(q8)), vmin);
    const __m256 v = _mm256_mul_ps(_mm256_loadu_ps(x + k), w);
    sum4 = _mm_add_ps(sum4, _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)));
  }
  sum4 = _mm_add_ps(sum4, _mm_movehl_ps(sum4, sum4));
  float sum = _mm_cvtss_f32(_mm_add_ss(sum4, _mm_shuffle_ps(sum4, sum4, 1)));
  for (; k < dim; ++k) sum += x[k] * (static_cast<float>(qrow[k]) * min_pos);
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
    const float* rows[4] = {
      embedding + ts[0] * dim, embedding + ts[1] * dim,
      embedding + ts[2] * dim, embedding + ts[3] * dim,
    };
    __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
    std::size_t k = 0;
    for (; k + 8 <= dim; k += 8) {
      const __m256 xv = _mm256_loadu_ps(x + k);
      a0 = _mm256_add_ps(a0, _mm256_mul_ps(xv, _mm256_loadu_ps(rows[0] + k)));
      a1 = _mm256_add_ps(a1, _mm256_mul_ps(xv, _mm256_loadu_ps(rows[1] + k)));
      a2 = _mm256_add_ps(a2, _mm256_mul_ps(xv, _mm256_loadu_ps(rows[2] + k)));
      a3 = _mm256_add_ps(a3, _mm256_mul_ps(xv, _mm256_loadu_ps(rows[3] + k)));
    }
    const __m256 h01 = _mm256_hadd_ps(a0, a1), h23 = _mm256_hadd_ps(a2, a3);
    const __m256 h = _mm256_hadd_ps(h01, h23);
    const __m128 sums = _mm_add_ps(_mm256_castps256_ps128(h), _mm256_extractf128_ps(h, 1));
    alignas(16) float logits[4];
    _mm_store_ps(logits, sums);
    for (int lane = 0; lane < 4; ++lane) {
      float tail = 0.0f;
      const float* row = rows[lane];
      for (std::size_t rem = k; rem < dim; ++rem) tail += x[rem] * row[rem];
      const float bias = ts[lane] == 1 ? -std::numeric_limits<float>::infinity() : output_bias[ts[lane]];
      const float val = logits[lane] + tail + bias;
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

MTRAN_AVX2_FMA void prepareFullQ4K4Avx2(const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  auto* neg_corr = reinterpret_cast<std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const __m256 sign_mask = _mm256_set1_ps(-0.0f);
  const __m256i eight = _mm256_set1_epi8(8);
  const __m256i ones16 = _mm256_set1_epi16(1);
  for (std::size_t b = 0; b < blocks; ++b) {
    __m256 maxv = _mm256_setzero_ps();
    for (std::size_t j = 0; j < 64; j += 8)
      maxv = _mm256_max_ps(maxv, _mm256_andnot_ps(sign_mask, _mm256_loadu_ps(x + b * 64 + j)));
    __m128 max4 = _mm_max_ps(_mm256_extractf128_ps(maxv, 1), _mm256_castps256_ps128(maxv));
    max4 = _mm_max_ps(max4, _mm_movehl_ps(max4, max4));
    max4 = _mm_max_ss(max4, _mm_shuffle_ps(max4, max4, 1));
    const float mx = _mm_cvtss_f32(max4);
    sa[b] = mx / 127.0f;
    const __m256 inv = _mm256_set1_ps(mx != 0.0f ? 127.0f / mx : 0.0f);
    for (std::size_t j = 0; j < 64; j += 16) {
      const __m256 v0 = _mm256_round_ps(_mm256_mul_ps(_mm256_loadu_ps(x + b * 64 + j), inv), _MM_ROUND_NEAREST);
      const __m256 v1 = _mm256_round_ps(_mm256_mul_ps(_mm256_loadu_ps(x + b * 64 + j + 8), inv), _MM_ROUND_NEAREST);
      const __m256i i0 = _mm256_cvtps_epi32(v0), i1 = _mm256_cvtps_epi32(v1);
      const __m128i p0 = _mm_packs_epi32(_mm256_castsi256_si128(i0), _mm256_extracti128_si256(i0, 1));
      const __m128i p1 = _mm_packs_epi32(_mm256_castsi256_si128(i1), _mm256_extracti128_si256(i1, 1));
      _mm_store_si128(reinterpret_cast<__m128i*>(qa + b * 64 + j), _mm_packs_epi16(p0, p1));
    }
    const __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(qa + b * 64));
    const __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(qa + b * 64 + 32));
    const __m256i c16 = _mm256_add_epi16(_mm256_maddubs_epi16(eight, a0), _mm256_maddubs_epi16(eight, a1));
    const __m256i csum = _mm256_madd_epi16(c16, ones16);
    const __m128i c128 = _mm_add_epi32(_mm256_castsi256_si128(csum), _mm256_extracti128_si256(csum, 1));
    const __m128i c64 = _mm_add_epi32(c128, _mm_srli_si128(c128, 8));
    const __m128i c32 = _mm_add_epi32(c64, _mm_srli_si128(c64, 4));
    neg_corr[b] = -_mm_cvtsi128_si32(c32);
    const __m256i p0 = _mm256_unpacklo_epi32(a0, a1);
    const __m256i p1 = _mm256_unpackhi_epi32(a0, a1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(qa + b * 64 + 0), _mm256_permute2x128_si256(p0, p1, 0x20));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(qa + b * 64 + 32), _mm256_permute2x128_si256(p0, p1, 0x31));
  }
}

// AVX2 fallback sharing N x K4 Q4 layout [vocab/8][blocks][8_chunks][8_cols][4_k_nibbles].
MTRAN_AVX2_FMA void scanFullQ4K4Avx2Range(
    std::size_t dim, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4, const float* packed_scales,
    const float* bias, const std::uint8_t* work_buf, FullVocabQ4::TopK9& top) {
  const std::size_t blocks = dim / 64;
  const auto* qa = reinterpret_cast<const std::int8_t*>(work_buf);
  const auto* sa = reinterpret_cast<const float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const auto* neg_corr = reinterpret_cast<const std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const auto* qa32 = reinterpret_cast<const std::int32_t*>(qa);
  top.init();
  const __m256i mask = _mm256_set1_epi8(15);
  const __m256i ones16 = _mm256_set1_epi16(1);
  const std::size_t group_q4_bytes = blocks * 256;
  const std::size_t group_sc_floats = blocks * 8;

  for (std::size_t g = g_begin; g < g_end; ++g) {
    __m256 acc0 = _mm256_setzero_ps();
    const std::uint8_t* gp0 = packed_q4 + g * group_q4_bytes;
    const float* gs0 = packed_scales + g * group_sc_floats;
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 256, gs0 += 8) {
      __m256i s0 = _mm256_set1_epi32(neg_corr[b]);
      const std::int32_t* qab = qa32 + b * 16;
      for (std::size_t i = 0; i < 8; ++i) {
        const __m256i a_lo = _mm256_set1_epi32(qab[2 * i + 0]);
        const __m256i a_hi = _mm256_set1_epi32(qab[2 * i + 1]);
        const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gp0 + i * 32));
        const __m256i m0 = _mm256_maddubs_epi16(_mm256_and_si256(p, mask), a_lo);
        const __m256i m1 = _mm256_maddubs_epi16(_mm256_and_si256(_mm256_srli_epi16(p, 4), mask), a_hi);
        s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(_mm256_add_epi16(m0, m1), ones16));
      }
      acc0 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s0), _mm256_mul_ps(_mm256_set1_ps(sa[b]), _mm256_loadu_ps(gs0)), acc0);
    }
    const __m256 v0 = _mm256_add_ps(acc0, _mm256_loadu_ps(bias + g * 8));
    if (top.count < 9 || _mm256_movemask_ps(_mm256_cmp_ps(v0, _mm256_set1_ps(top.scores[8]), _CMP_GT_OQ)) != 0) {
      alignas(32) float out8[8];
      _mm256_store_ps(out8, v0);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 8);
      for (int c = 0; c < 8; ++c) top.push(out8[c], base_id + c);
    }
  }
}

MTRAN_AVX2_FMA void scanFullQ4K4Avx2BatchRange(
    std::size_t dim, std::size_t rows, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4, const float* packed_scales,
    const float* bias, const std::uint8_t* const* work_bufs, FullVocabQ4::TopK9* tops) {
  const std::size_t blocks = dim / 64;
  const std::size_t qa_bytes = ((dim + 63) & ~std::size_t(63));
  const std::size_t sa_off = qa_bytes;
  const std::size_t nc_off = sa_off + ((blocks + 7) & ~std::size_t(7)) * sizeof(float);
  for (std::size_t m = 0; m < rows; ++m) tops[m].init();
  const __m256i mask = _mm256_set1_epi8(15);
  const __m256i ones16 = _mm256_set1_epi16(1);
  const std::size_t group_q4_bytes = blocks * 256;
  const std::size_t group_sc_floats = blocks * 8;

  for (std::size_t m0 = 0; m0 < rows; m0 += 4) {
    const std::size_t mb = std::min<std::size_t>(4, rows - m0);
    for (std::size_t g = g_begin; g < g_end; ++g) {
      const std::uint8_t* gp = packed_q4 + g * group_q4_bytes;
      const float* gs = packed_scales + g * group_sc_floats;
      __m256 acc[4] = {_mm256_setzero_ps(), _mm256_setzero_ps(), _mm256_setzero_ps(), _mm256_setzero_ps()};
      for (std::size_t b = 0; b < blocks; ++b, gp += 256, gs += 8) {
        __m256i s[4];
        const std::int32_t* qabs[4];
        for (std::size_t i = 0; i < mb; ++i) {
          const auto* nc = reinterpret_cast<const std::int32_t*>(work_bufs[m0 + i] + nc_off);
          s[i] = _mm256_set1_epi32(nc[b]);
          qabs[i] = reinterpret_cast<const std::int32_t*>(work_bufs[m0 + i]) + b * 16;
        }
        for (std::size_t k = 0; k < 8; ++k) {
          const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gp + k * 32));
          const __m256i qlo = _mm256_and_si256(p, mask);
          const __m256i qhi = _mm256_and_si256(_mm256_srli_epi16(p, 4), mask);
          for (std::size_t i = 0; i < mb; ++i) {
            const __m256i m0i = _mm256_maddubs_epi16(qlo, _mm256_set1_epi32(qabs[i][2 * k + 0]));
            const __m256i m1i = _mm256_maddubs_epi16(qhi, _mm256_set1_epi32(qabs[i][2 * k + 1]));
            s[i] = _mm256_add_epi32(s[i], _mm256_madd_epi16(_mm256_add_epi16(m0i, m1i), ones16));
          }
        }
        const __m256 vgs = _mm256_loadu_ps(gs);
        for (std::size_t i = 0; i < mb; ++i) {
          const auto* sa_i = reinterpret_cast<const float*>(work_bufs[m0 + i] + sa_off);
          acc[i] = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s[i]), _mm256_mul_ps(_mm256_set1_ps(sa_i[b]), vgs), acc[i]);
        }
      }
      const __m256 vbias = _mm256_loadu_ps(bias + g * 8);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 8);
      for (std::size_t i = 0; i < mb; ++i) {
        const __m256 v = _mm256_add_ps(acc[i], vbias);
        if (tops[m0 + i].count < 9 ||
            _mm256_movemask_ps(_mm256_cmp_ps(v, _mm256_set1_ps(tops[m0 + i].scores[8]), _CMP_GT_OQ)) != 0) {
          alignas(32) float out8[8];
          _mm256_store_ps(out8, v);
          for (int c = 0; c < 8; ++c) tops[m0 + i].push(out8[c], base_id + c);
        }
      }
    }
  }
}
#elif defined(__aarch64__) || defined(_M_ARM64)
bool hasDotprod() noexcept {
#ifdef MTRAN_SSRU_NO_DOTPROD
  return false;
#elif defined(_WIN32)
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
  auto s0 = vdupq_n_s32(0), s1 = s0;
  for (std::size_t j = 0; j < k; j += 4, b += 32) {
    std::int32_t bytes;
    std::memcpy(&bytes, a + j, 4);
    const auto x = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes), vdupq_n_s32(-2139062144)));
    s0 = vdotq_s32(s0, vld1q_s8(b), x);
    s1 = vdotq_s32(s1, vld1q_s8(b + 16), x);
  }
  vst1q_s32(result, s0);
  vst1q_s32(result + 4, s1);
}

MTRAN_DOTPROD void multiplyBatchSdot(
    const std::uint8_t* a_q, std::size_t a_stride, const float* a_scales,
    const std::uint8_t* a_zps, const std::int8_t* b_sdot, const std::int32_t* sums,
    float w_scale, std::size_t rows, std::size_t k, std::size_t col_begin,
    std::size_t col_end, std::size_t block, float* out, std::size_t out_stride,
    const float* fused_bias, const float* separate_bias, bool relu_minmax,
    float* out_maxs) {
  if (relu_minmax && out_maxs) {
    for (std::size_t m = 0; m < rows; ++m) out_maxs[m] = 0.0f;
  }
  const int32x4_t xorb = vdupq_n_s32(-2139062144);
  for (std::size_t col = col_begin; col < col_end; col += 8) {
    const std::size_t blk_idx = col / block;
    const std::size_t blk_off = col % block;
    const std::int8_t* bp_base = b_sdot + blk_idx * k * block + blk_off * 4;
    const std::size_t b_step = block * 4;
    const int32x4_t vsum0 = vld1q_s32(sums + col);
    const int32x4_t vsum1 = vld1q_s32(sums + col + 4);
    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* am = a_q + m * a_stride;
      int32x4_t acc0 = vdupq_n_s32(0), acc1 = vdupq_n_s32(0);
      const std::int8_t* bp = bp_base;
      for (std::size_t j = 0; j < k; j += 4, bp += b_step) {
        std::int32_t bytes;
        std::memcpy(&bytes, am + j, 4);
        const int8x16_t xv = vreinterpretq_s8_s32(veorq_s32(vdupq_n_s32(bytes), xorb));
        acc0 = vdotq_s32(acc0, vld1q_s8(bp), xv);
        acc1 = vdotq_s32(acc1, vld1q_s8(bp + 16), xv);
      }
      const int32x4_t vcorr = vdupq_n_s32(128 - static_cast<std::int32_t>(a_zps[m]));
      const float32x4_t f0 = vcvtq_f32_s32(vmlaq_s32(acc0, vsum0, vcorr));
      const float32x4_t f1 = vcvtq_f32_s32(vmlaq_s32(acc1, vsum1, vcorr));
      const float32x4_t vsc = vdupq_n_f32(a_scales[m] * w_scale);
      float32x4_t r0, r1;
      if (fused_bias) {
        r0 = vfmaq_f32(vld1q_f32(fused_bias + col), f0, vsc);
        r1 = vfmaq_f32(vld1q_f32(fused_bias + col + 4), f1, vsc);
      } else if (separate_bias) {
        r0 = vaddq_f32(vmulq_f32(f0, vsc), vld1q_f32(separate_bias + col));
        r1 = vaddq_f32(vmulq_f32(f1, vsc), vld1q_f32(separate_bias + col + 4));
      } else {
        r0 = vmulq_f32(f0, vsc);
        r1 = vmulq_f32(f1, vsc);
      }
      if (relu_minmax) {
        const float32x4_t z = vdupq_n_f32(0.0f);
        r0 = vmaxq_f32(r0, z);
        r1 = vmaxq_f32(r1, z);
        if (out_maxs) out_maxs[m] = std::max(out_maxs[m], vmaxvq_f32(vmaxq_f32(r0, r1)));
      }
      vst1q_f32(out + m * out_stride + col, r0);
      vst1q_f32(out + m * out_stride + col + 4, r1);
    }
  }
}

void dotNeon(const std::uint8_t* a, const std::int8_t* b, std::size_t k, std::int32_t* result) {
  auto l0 = vdupq_n_s32(0), l1 = l0, h0 = l0, h1 = l0;
  const int16x8_t bias = vdupq_n_s16(128);
  for (std::size_t j = 0; j < k; j += 8, b += 64) {
    const int16x8_t av = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(a + j)));
    const int8x8_t al = vmovn_s16(vsubq_s16(av, bias));
    for (int p = 0; p < 8; ++p) {
      const int16x8_t prod = vmull_s8(al, vld1_s8(b + p * 8));
      auto& acc = p < 4 ? (p & 1 ? l1 : l0) : (p & 1 ? h1 : h0);
      if (p < 2 || (p >= 4 && p < 6)) acc = vaddw_s16(acc, vget_low_s16(prod));
      else acc = vaddw_s16(acc, vget_high_s16(prod));
    }
  }
  const int32x4_t zero = vdupq_n_s32(0);
  const auto lo = vpaddq_s32(vpaddq_s32(l0, l1), zero), hi = vpaddq_s32(vpaddq_s32(h0, h1), zero);
  vst1q_s32(result, vdupq_n_s32(0)); vst1q_s32(result + 4, vdupq_n_s32(0));
  std::int32_t s[8];
  (void)lo; (void)hi;
  std::fill_n(s, 8, 0);
  for (std::size_t row = 0; row < k; ++row)
    for (std::size_t lane = 0; lane < 8; ++lane)
      s[lane] += (static_cast<std::int32_t>(a[row]) - 128) * b[-static_cast<std::ptrdiff_t>(k * 8) + row * 8 + lane];
  std::copy_n(s, 8, result);
}

MTRAN_DOTPROD ShortlistTable::ScanResult scanShortlistSdot(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out,
    std::int8_t* x_s8, bool exact_int8, float exact_scale) {
  const uint8x16_t bias128 = vdupq_n_u8(0x80);
  for (std::size_t k = 0; k < dim; k += 16) {
    vst1q_s8(x_s8 + k, vreinterpretq_s8_u8(veorq_u8(vld1q_u8(x_u8 + k), bias128)));
  }
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const int32_t zp_corr = 128 - static_cast<int32_t>(x_zp);
  const int32x4_t vzp = vdupq_n_s32(zp_corr);
  const float32x4_t vxscale = vdupq_n_f32(x_scale);
  const float32x4_t vexact = vdupq_n_f32(x_scale * exact_scale);

  std::size_t j = 0;
  for (; j + 4 <= candidate_count; j += 4) {
    if (j + 8 <= candidate_count) {
      for (int p = 0; p < 4; ++p) {
        const auto nt = static_cast<std::size_t>(candidate_ids[j + 4 + p]);
        MTRAN_PREFETCH(emb_q8 + nt * dim);
        MTRAN_PREFETCH(meta + nt);
      }
    }
    const std::size_t ts[4] = {
      static_cast<std::size_t>(candidate_ids[j + 0]), static_cast<std::size_t>(candidate_ids[j + 1]),
      static_cast<std::size_t>(candidate_ids[j + 2]), static_cast<std::size_t>(candidate_ids[j + 3]),
    };
    const std::int8_t* r0 = emb_q8 + ts[0] * dim;
    const std::int8_t* r1 = emb_q8 + ts[1] * dim;
    const std::int8_t* r2 = emb_q8 + ts[2] * dim;
    const std::int8_t* r3 = emb_q8 + ts[3] * dim;
    int32x4_t a0 = vdupq_n_s32(0), a1 = a0, a2 = a0, a3 = a0;
    for (std::size_t k = 0; k < dim; k += 16) {
      const int8x16_t xv = vld1q_s8(x_s8 + k);
      a0 = vdotq_s32(a0, vld1q_s8(r0 + k), xv);
      a1 = vdotq_s32(a1, vld1q_s8(r1 + k), xv);
      a2 = vdotq_s32(a2, vld1q_s8(r2 + k), xv);
      a3 = vdotq_s32(a3, vld1q_s8(r3 + k), xv);
    }
    const int32x4_t sum01 = vpaddq_s32(a0, a1);
    const int32x4_t sum23 = vpaddq_s32(a2, a3);
    const int32x4_t sum4 = vpaddq_s32(sum01, sum23);
    const std::int32_t rsums_arr[4] = {meta[ts[0]].sum, meta[ts[1]].sum, meta[ts[2]].sum, meta[ts[3]].sum};
    const float raw_rsc_arr[4] = {meta[ts[0]].scale, meta[ts[1]].scale, meta[ts[2]].scale, meta[ts[3]].scale};
    const float rbias_arr[4] = {meta[ts[0]].bias, meta[ts[1]].bias, meta[ts[2]].bias, meta[ts[3]].bias};
    const int32x4_t rsums = vld1q_s32(rsums_arr);
    const float32x4_t raw_rsc = vld1q_f32(raw_rsc_arr);
    const float32x4_t rscales = exact_int8 ? vexact : vmulq_f32(vxscale, raw_rsc);
    const float32x4_t rbias = vld1q_f32(rbias_arr);
    const int32x4_t corr = vmlaq_s32(sum4, rsums, vzp);
    const float32x4_t vals = vfmaq_f32(rbias, vcvtq_f32_s32(corr), rscales);
    vst1q_f32(approx_out + j, vals);
    for (int lane = 0; lane < 4; ++lane) {
      const float val = approx_out[j + lane];
      if (val > best_logit) {
        second_logit = best_logit;
        best_logit = val;
        best_j = j + lane;
      } else if (val > second_logit) {
        second_logit = val;
      }
    }
  }
  const std::int32_t zp_u = static_cast<std::int32_t>(x_zp);
  for (; j < candidate_count; ++j) {
    const auto t = static_cast<std::size_t>(candidate_ids[j]);
    const auto* row = emb_q8 + t * dim;
    std::int32_t acc = 0;
    for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(x_u8[k]) * row[k];
    const auto& m = meta[t];
    const float sc = exact_int8 ? (x_scale * exact_scale) : (x_scale * m.scale);
    const float val = static_cast<float>(acc - zp_u * m.sum) * sc + m.bias;
    approx_out[j] = val;
    if (val > best_logit) {
      second_logit = best_logit;
      best_logit = val;
      best_j = j;
    } else if (val > second_logit) {
      second_logit = val;
    }
  }
  return {best_logit, best_j, second_logit};
}

MTRAN_DOTPROD void scanShortlistSdotBatch(
    const std::uint8_t* x_u8, std::size_t x_stride, const float* x_scales,
    const std::uint8_t* x_zps, std::size_t rows, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* union_ids, const std::uint32_t* slot_masks,
    std::size_t union_count, float* approx_out, float* best_logits,
    std::size_t* best_js, float* second_logits, bool* has_any,
    std::int8_t* x_s8_buf, bool exact_int8, float exact_scale) {
  const uint8x16_t bias128 = vdupq_n_u8(0x80);
  for (std::size_t m = 0; m < rows; ++m) {
    best_logits[m] = -std::numeric_limits<float>::infinity();
    second_logits[m] = -std::numeric_limits<float>::infinity();
    best_js[m] = 0;
    has_any[m] = false;
    const std::uint8_t* xm = x_u8 + m * x_stride;
    std::int8_t* sm = x_s8_buf + m * dim;
    for (std::size_t k = 0; k < dim; k += 16) {
      vst1q_s8(sm + k, vreinterpretq_s8_u8(veorq_u8(vld1q_u8(xm + k), bias128)));
    }
  }
  std::size_t j = 0;
  for (; j + 4 <= union_count; j += 4) {
    const std::size_t ts[4] = {
      static_cast<std::size_t>(union_ids[j + 0]), static_cast<std::size_t>(union_ids[j + 1]),
      static_cast<std::size_t>(union_ids[j + 2]), static_cast<std::size_t>(union_ids[j + 3]),
    };
    const std::int8_t* r0 = emb_q8 + ts[0] * dim;
    const std::int8_t* r1 = emb_q8 + ts[1] * dim;
    const std::int8_t* r2 = emb_q8 + ts[2] * dim;
    const std::int8_t* r3 = emb_q8 + ts[3] * dim;
    const std::int32_t rsums_arr[4] = {meta[ts[0]].sum, meta[ts[1]].sum, meta[ts[2]].sum, meta[ts[3]].sum};
    const float raw_rsc_arr[4] = {meta[ts[0]].scale, meta[ts[1]].scale, meta[ts[2]].scale, meta[ts[3]].scale};
    const float rbias_arr[4] = {meta[ts[0]].bias, meta[ts[1]].bias, meta[ts[2]].bias, meta[ts[3]].bias};
    const int32x4_t rsums = vld1q_s32(rsums_arr);
    const float32x4_t raw_rsc = vld1q_f32(raw_rsc_arr);
    const float32x4_t rbias = vld1q_f32(rbias_arr);
    for (std::size_t m = 0; m < rows; ++m) {
      const std::int8_t* sm = x_s8_buf + m * dim;
      int32x4_t a0 = vdupq_n_s32(0), a1 = a0, a2 = a0, a3 = a0;
      for (std::size_t k = 0; k < dim; k += 16) {
        const int8x16_t xv = vld1q_s8(sm + k);
        a0 = vdotq_s32(a0, vld1q_s8(r0 + k), xv);
        a1 = vdotq_s32(a1, vld1q_s8(r1 + k), xv);
        a2 = vdotq_s32(a2, vld1q_s8(r2 + k), xv);
        a3 = vdotq_s32(a3, vld1q_s8(r3 + k), xv);
      }
      const int32x4_t sum4 = vpaddq_s32(vpaddq_s32(a0, a1), vpaddq_s32(a2, a3));
      const int32x4_t corr = vmlaq_s32(sum4, rsums, vdupq_n_s32(128 - static_cast<std::int32_t>(x_zps[m])));
      const float32x4_t rsc = exact_int8 ? vdupq_n_f32(x_scales[m] * exact_scale)
                                         : vmulq_f32(vdupq_n_f32(x_scales[m]), raw_rsc);
      const float32x4_t vals = vfmaq_f32(rbias, vcvtq_f32_s32(corr), rsc);
      vst1q_f32(approx_out + m * union_count + j, vals);
      for (int lane = 0; lane < 4; ++lane) {
        if ((slot_masks[j + lane] & (1u << m)) == 0) continue;
        const float val = approx_out[m * union_count + j + lane];
        if (!has_any[m]) {
          has_any[m] = true;
          best_logits[m] = val;
          best_js[m] = j + lane;
        } else if (val > best_logits[m]) {
          second_logits[m] = best_logits[m];
          best_logits[m] = val;
          best_js[m] = j + lane;
        } else if (val > second_logits[m]) {
          second_logits[m] = val;
        }
      }
    }
  }
  for (; j < union_count; ++j) {
    const auto t = static_cast<std::size_t>(union_ids[j]);
    const auto* row = emb_q8 + t * dim;
    const auto& rm = meta[t];
    const std::uint32_t mask_j = slot_masks[j];
    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* xm = x_u8 + m * x_stride;
      std::int32_t acc = 0;
      for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(xm[k]) * row[k];
      const std::int32_t zp_u = static_cast<std::int32_t>(x_zps[m]);
      const float sc = exact_int8 ? (x_scales[m] * exact_scale) : (x_scales[m] * rm.scale);
      const float val = static_cast<float>(acc - zp_u * rm.sum) * sc + rm.bias;
      approx_out[m * union_count + j] = val;
      if ((mask_j & (1u << m)) == 0) continue;
      if (!has_any[m]) {
        has_any[m] = true;
        best_logits[m] = val;
        best_js[m] = j;
      } else if (val > best_logits[m]) {
        second_logits[m] = best_logits[m];
        best_logits[m] = val;
        best_js[m] = j;
      } else if (val > second_logits[m]) {
        second_logits[m] = val;
      }
    }
  }
}

float dotRowNeon(const float* x, const float* row, std::size_t dim) {
  float32x4_t acc = vdupq_n_f32(0.0f);
  std::size_t k = 0;
  for (; k + 4 <= dim; k += 4) acc = vfmaq_f32(acc, vld1q_f32(x + k), vld1q_f32(row + k));
  float sum = vaddvq_f32(acc);
  for (; k < dim; ++k) sum = std::fma(x[k], row[k], sum);
  return sum;
}

float dotRowInt8Neon(const float* x, const std::int8_t* qrow, float min_pos, std::size_t dim) {
  float32x4_t acc = vdupq_n_f32(0.0f);
  const float32x4_t vmin = vdupq_n_f32(min_pos);
  std::size_t k = 0;
  for (; k + 8 <= dim; k += 8) {
    const int16x8_t q16 = vmovl_s8(vld1_s8(qrow + k));
    const float32x4_t w0 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(q16))), vmin);
    const float32x4_t w1 = vmulq_f32(vcvtq_f32_s32(vmovl_high_s16(q16)), vmin);
    acc = vfmaq_f32(acc, vld1q_f32(x + k), w0);
    acc = vfmaq_f32(acc, vld1q_f32(x + k + 4), w1);
  }
  float sum = vaddvq_f32(acc);
  for (; k < dim; ++k) sum = std::fma(x[k], static_cast<float>(qrow[k]) * min_pos, sum);
  return sum;
}

MTRAN_DOTPROD void prepareFullQ4Sdot(const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  auto* neg_corr = reinterpret_cast<std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  auto* u8_buf = reinterpret_cast<std::uint8_t*>(qa);
  std::uint8_t zero_u8 = 128;
  const float scale = mtran::ssru::kleidi::quantize(x, dim, u8_buf, zero_u8);
  const int zp_s8 = static_cast<int>(zero_u8) - 128;
  const uint8x16_t bias128 = vdupq_n_u8(0x80);
  for (std::size_t k = 0; k < dim; k += 16)
    vst1q_s8(qa + k, vreinterpretq_s8_u8(veorq_u8(vld1q_u8(u8_buf + k), bias128)));
  sa[0] = scale;
  sa[1] = -scale * static_cast<float>(zp_s8);
  const int8x16_t neg8 = vdupq_n_s8(-8);
  const int32x4_t zero = vdupq_n_s32(0);
  for (std::size_t b = 0; b < blocks; ++b) {
    const int8x16_t av0 = vld1q_s8(qa + b * 64 + 0);
    const int8x16_t av1 = vld1q_s8(qa + b * 64 + 16);
    const int8x16_t av2 = vld1q_s8(qa + b * 64 + 32);
    const int8x16_t av3 = vld1q_s8(qa + b * 64 + 48);
    vst1q_s32(neg_corr + b * 4, vdotq_s32(vdotq_s32(vdotq_s32(vdotq_s32(zero, av0, neg8), av1, neg8), av2, neg8), av3, neg8));
  }
}

MTRAN_DOTPROD void scanFullQ4SdotRange(
    std::size_t dim, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4, const float* packed_scales,
    const float* kai_wsum, const float* bias, const std::uint8_t* work_buf, FullVocabQ4::TopK9& top) {
  const std::size_t blocks = dim / 64;
  const auto* qa = reinterpret_cast<const std::int8_t*>(work_buf);
  const auto* sa = reinterpret_cast<const float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const auto* neg_corr = reinterpret_cast<const std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const float sa_b = sa[0];
  const float32x4_t vneg_zp = vdupq_n_f32(sa[1]);
  top.init();
  const uint8x16_t low_mask = vdupq_n_u8(0x0F);
  #define MTRAN_SDOT_Q4_STEP(cp, sb, c_acc) do { \
    const uint8x16_t p0_ = vld1q_u8(cp), p1_ = vld1q_u8((cp) + 16); \
    const int32x4_t d_ = vdotq_s32(vdotq_s32(vdotq_s32(vdotq_s32(nc, av0, vreinterpretq_s8_u8(vandq_u8(p0_, low_mask))), \
      av1, vreinterpretq_s8_u8(vshrq_n_u8(p0_, 4))), av2, vreinterpretq_s8_u8(vandq_u8(p1_, low_mask))), \
      av3, vreinterpretq_s8_u8(vshrq_n_u8(p1_, 4))); \
    c_acc = vfmaq_f32(c_acc, vcvtq_f32_s32(d_), vdupq_n_f32(sa_b * (sb))); \
  } while (0)
  std::size_t g = g_begin;
  for (; g + 2 <= g_end; g += 2) {
    float32x4_t acc0 = vdupq_n_f32(0.0f), acc1 = acc0, acc2 = acc0, acc3 = acc0;
    float32x4_t acc4 = acc0, acc5 = acc0, acc6 = acc0, acc7 = acc0;
    const std::uint8_t* gp0 = packed_q4 + g * blocks * 128;
    const std::uint8_t* gp1 = gp0 + blocks * 128;
    const float* gs0 = packed_scales + g * blocks * 4;
    const float* gs1 = gs0 + blocks * 4;
    MTRAN_PREFETCH(gp1 + blocks * 128);
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 128, gp1 += 128, gs0 += 4, gs1 += 4) {
      const int8x16_t av0 = vld1q_s8(qa + b * 64 + 0), av1 = vld1q_s8(qa + b * 64 + 16);
      const int8x16_t av2 = vld1q_s8(qa + b * 64 + 32), av3 = vld1q_s8(qa + b * 64 + 48);
      const int32x4_t nc = vld1q_s32(neg_corr + b * 4);
      MTRAN_SDOT_Q4_STEP(gp0 + 0, gs0[0], acc0); MTRAN_SDOT_Q4_STEP(gp0 + 32, gs0[1], acc1);
      MTRAN_SDOT_Q4_STEP(gp0 + 64, gs0[2], acc2); MTRAN_SDOT_Q4_STEP(gp0 + 96, gs0[3], acc3);
      MTRAN_SDOT_Q4_STEP(gp1 + 0, gs1[0], acc4); MTRAN_SDOT_Q4_STEP(gp1 + 32, gs1[1], acc5);
      MTRAN_SDOT_Q4_STEP(gp1 + 64, gs1[2], acc6); MTRAN_SDOT_Q4_STEP(gp1 + 96, gs1[3], acc7);
    }
    const float32x4_t v0 = vfmaq_f32(vaddq_f32(vpaddq_f32(vpaddq_f32(acc0, acc1), vpaddq_f32(acc2, acc3)), vld1q_f32(bias + g * 4)), vneg_zp, vld1q_f32(kai_wsum + g * 4));
    const float32x4_t v1 = vfmaq_f32(vaddq_f32(vpaddq_f32(vpaddq_f32(acc4, acc5), vpaddq_f32(acc6, acc7)), vld1q_f32(bias + g * 4 + 4)), vneg_zp, vld1q_f32(kai_wsum + g * 4 + 4));
    const float32x4_t vth = vdupq_n_f32(top.scores[8]);
    if (top.count < 9 || vmaxvq_u32(vorrq_u32(vcgtq_f32(v0, vth), vcgtq_f32(v1, vth))) != 0) {
      float out8[8];
      vst1q_f32(out8, v0);
      vst1q_f32(out8 + 4, v1);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 4);
      for (int c = 0; c < 8; ++c) top.push(out8[c], base_id + c);
    }
  }
  for (; g < g_end; ++g) {
    float32x4_t acc0 = vdupq_n_f32(0.0f), acc1 = acc0, acc2 = acc0, acc3 = acc0;
    const std::uint8_t* gp = packed_q4 + g * blocks * 128;
    const float* gs = packed_scales + g * blocks * 4;
    for (std::size_t b = 0; b < blocks; ++b, gp += 128, gs += 4) {
      const int8x16_t av0 = vld1q_s8(qa + b * 64 + 0);
      const int8x16_t av1 = vld1q_s8(qa + b * 64 + 16);
      const int8x16_t av2 = vld1q_s8(qa + b * 64 + 32);
      const int8x16_t av3 = vld1q_s8(qa + b * 64 + 48);
      const int32x4_t nc = vld1q_s32(neg_corr + b * 4);
      MTRAN_SDOT_Q4_STEP(gp + 0, gs[0], acc0);
      MTRAN_SDOT_Q4_STEP(gp + 32, gs[1], acc1);
      MTRAN_SDOT_Q4_STEP(gp + 64, gs[2], acc2);
      MTRAN_SDOT_Q4_STEP(gp + 96, gs[3], acc3);
    }
    const float32x4_t v0 = vfmaq_f32(vaddq_f32(vpaddq_f32(vpaddq_f32(acc0, acc1), vpaddq_f32(acc2, acc3)), vld1q_f32(bias + g * 4)), vneg_zp, vld1q_f32(kai_wsum + g * 4));
    if (top.count < 9 || vmaxvq_u32(vcgtq_f32(v0, vdupq_n_f32(top.scores[8]))) != 0) {
      float out4[4];
      vst1q_f32(out4, v0);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 4);
      for (int c = 0; c < 4; ++c) top.push(out4[c], base_id + c);
    }
  }
  #undef MTRAN_SDOT_Q4_STEP
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
  return (f & (detail::AvxVnni | detail::Avx512Vnni)) ? dotAvx2<false> : dotAvx2<true>;
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  return hasDotprod() ? dotSdot : dotNeon;
#else
  return nullptr;
#endif
}

std::size_t computeHotPrefix(
    const std::int64_t* ids, std::size_t count, std::size_t hot_count, std::size_t block_w) noexcept {
  const std::size_t limit = std::min(count, hot_count) & ~(block_w - 1);
  std::size_t matched = 0;
  while (matched < limit && ids[matched] == static_cast<std::int64_t>(matched)) ++matched;
  return matched & ~(block_w - 1);
}

template <typename TopSlot>
struct alignas(64) PaddedTopK9 {
  TopSlot top;
};

}  // namespace

const char* backend() noexcept {
  const auto dot = selectDot();
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
  if ((features() & detail::Avx512Vnni) && hasFma()) return "packed-avx512vnni";
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
  if ((features() & detail::AvxVnni) && hasFma()) return "packed-avxvnni";
#endif
  if (dot == dotAvx2<true>) return "packed-avx2-ort";
  return dot ? "packed-avx2" : nullptr;
#elif defined(__aarch64__) || defined(_M_ARM64)
  return dot == dotSdot ? "packed-sdot" : "packed-neon";
#else
  return nullptr;
#endif
}

struct Workspace::Impl {
  std::vector<std::uint8_t> buffer;
  std::vector<float> floats;
};
Workspace::Workspace() : impl_(std::make_unique<Impl>()) {}
Workspace::~Workspace() = default;
std::uint8_t* Workspace::resize(std::size_t size) {
  if (size > SIZE_MAX - 63) throw std::invalid_argument("Packed workspace overflow");
  impl_->buffer.resize(size + 63);
  auto* p = impl_->buffer.data();
  return p + ((64 - reinterpret_cast<std::uintptr_t>(p) % 64) % 64);
}
float* Workspace::resizeFloats(std::size_t count) {
  if (count > SIZE_MAX / sizeof(float)) throw std::invalid_argument("Packed float workspace overflow");
  impl_->floats.resize(count);
  return impl_->floats.data();
}

static void quantizeLinearU8ScalarRange(
    const float* input, std::size_t total, std::uint8_t* output,
    float& scale, std::uint8_t& zero_point, float* dx_sq_out, float* qx_sq_out) {
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
  float dx_sq = 0.0f, qx_sq = 0.0f;
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
  if (dx_sq_out || qx_sq_out) {
    for (std::size_t k = 0; k < total; ++k) {
      const float qf = static_cast<float>(static_cast<std::int32_t>(output[k]) - static_cast<std::int32_t>(zero_point)) * scale;
      if (dx_sq_out) { const float d = input[k] - qf; dx_sq += d * d; }
      if (qx_sq_out) { qx_sq += qf * qf; }
    }
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  const float32x4_t vscale = vdupq_n_f32(scale);
  const float32x4_t vmin = vdupq_n_f32(min_val), vmax = vdupq_n_f32(max_val);
  const float32x4_t vmagic = vdupq_n_f32(12582912.0f);
  const int32x4_t voffset = vdupq_n_s32(static_cast<std::int32_t>(zero_point) - 0x4b400000);
  float32x4_t vdx = vdupq_n_f32(0.0f), vqx = vdupq_n_f32(0.0f);
  const bool want_dx = dx_sq_out != nullptr, want_qx = qx_sq_out != nullptr;
  std::size_t j = 0;
  for (; j + 16 <= total; j += 16) {
    auto q4 = [&](std::size_t off) {
      const float32x4_t x = vld1q_f32(input + off);
      float32x4_t v = vdivq_f32(x, vscale);
      v = vmaxq_f32(v, vmin);
      v = vminq_f32(v, vmax);
      const int32x4_t uz = vaddq_s32(vreinterpretq_s32_f32(vaddq_f32(v, vmagic)), voffset);
      if (want_dx || want_qx) {
        const float32x4_t qf = vmulq_f32(vcvtq_f32_s32(vsubq_s32(uz, vdupq_n_s32(static_cast<std::int32_t>(zero_point)))), vscale);
        if (want_dx) {
          const float32x4_t d = vsubq_f32(x, qf);
          vdx = vfmaq_f32(vdx, d, d);
        }
        if (want_qx) vqx = vfmaq_f32(vqx, qf, qf);
      }
      return uz;
    };
    const uint16x8_t lo16 = vcombine_u16(vqmovun_s32(q4(j)), vqmovun_s32(q4(j + 4)));
    const uint16x8_t hi16 = vcombine_u16(vqmovun_s32(q4(j + 8)), vqmovun_s32(q4(j + 12)));
    vst1q_u8(output + j, vcombine_u8(vqmovn_u16(lo16), vqmovn_u16(hi16)));
  }
  dx_sq = vaddvq_f32(vdx);
  qx_sq = vaddvq_f32(vqx);
  for (; j < total; ++j) {
    const float v = std::clamp(input[j] / scale, min_val, max_val);
    const auto qi = static_cast<std::int32_t>(roundEven(v));
    output[j] = static_cast<std::uint8_t>(qi + static_cast<std::int32_t>(zero_point));
    if (want_dx || want_qx) {
      const float qf = static_cast<float>(qi) * scale;
      if (want_dx) { const float d = input[j] - qf; dx_sq += d * d; }
      if (want_qx) qx_sq += qf * qf;
    }
  }
#else
  for (std::size_t j = 0; j < total; ++j) {
    const float v = std::clamp(input[j] / scale, min_val, max_val);
    const auto qi = static_cast<std::int32_t>(roundEven(v));
    output[j] = static_cast<std::uint8_t>(qi + static_cast<std::int32_t>(zero_point));
    if (dx_sq_out || qx_sq_out) {
      const float qf = static_cast<float>(qi) * scale;
      if (dx_sq_out) { const float d = input[j] - qf; dx_sq += d * d; }
      if (qx_sq_out) qx_sq += qf * qf;
    }
  }
#endif
  if (dx_sq_out) *dx_sq_out = dx_sq;
  if (qx_sq_out) *qx_sq_out = qx_sq;
}

void quantizeLinearU8(const float* input, std::size_t rows, std::size_t cols,
                      std::uint8_t* output, float& scale, std::uint8_t& zero_point) {
  if (!input || !output || !rows || !cols || rows > SIZE_MAX / cols)
    throw std::invalid_argument("Invalid DQL activation");
  const std::size_t total = rows * cols;
#if defined(__x86_64__) || defined(_M_X64)
  if ((features() & detail::Avx2) && total >= 16) {
    quantizeLinearU8RangeAvx2(input, total, output, scale, zero_point, nullptr, nullptr);
    return;
  }
#endif
  quantizeLinearU8ScalarRange(input, total, output, scale, zero_point, nullptr, nullptr);
}

void quantizeLinearU8Batch(const float* input, std::size_t rows, std::size_t cols,
                           std::size_t input_stride, std::uint8_t* output,
                           std::size_t output_stride, float* scales, std::uint8_t* zero_points) {
  if (!input || !output || !scales || !zero_points || !cols || input_stride < cols || output_stride < cols)
    throw std::invalid_argument("Invalid DQL batch activation");
#if defined(__x86_64__) || defined(_M_X64)
  const bool use_avx2 = (features() & detail::Avx2) != 0 && cols >= 16;
#endif
  for (std::size_t m = 0; m < rows; ++m) {
    const float* in_row = input + m * input_stride;
    std::uint8_t* out_row = output + m * output_stride;
#if defined(__x86_64__) || defined(_M_X64)
    if (use_avx2) {
      quantizeLinearU8RangeAvx2(in_row, cols, out_row, scales[m], zero_points[m], nullptr, nullptr);
      continue;
    }
#endif
    quantizeLinearU8ScalarRange(in_row, cols, out_row, scales[m], zero_points[m], nullptr, nullptr);
  }
}

void quantizeLinearU8FromMinMax(const float* input, std::size_t rows, std::size_t cols,
                                std::size_t input_stride, const float* mins, const float* maxs,
                                std::uint8_t* output, std::size_t output_stride,
                                float* scales, std::uint8_t* zero_points) {
  if (!input || !mins || !maxs || !output || !scales || !zero_points || !cols ||
      input_stride < cols || output_stride < cols)
    throw std::invalid_argument("Invalid DQL minmax batch activation");
#if defined(__x86_64__) || defined(_M_X64)
  const bool use_avx2 = (features() & detail::Avx2) != 0 && cols >= 8;
#endif
  for (std::size_t m = 0; m < rows; ++m) {
    const float* in_row = input + m * input_stride;
    std::uint8_t* out_row = output + m * output_stride;
#if defined(__x86_64__) || defined(_M_X64)
    if (use_avx2) {
      quantizeLinearU8FromMinMaxRowAvx2(in_row, cols, mins[m], maxs[m], out_row, scales[m], zero_points[m]);
      continue;
    }
#endif
    if (!std::isfinite(mins[m]) || !std::isfinite(maxs[m])) throw std::runtime_error("Nonfinite DQL activation");
    checkFinite(in_row, cols, "Nonfinite DQL activation");
    const float lo = std::min(0.0f, mins[m]);
    const float hi = std::max(0.0f, maxs[m]);
    const float sc = (hi == lo) ? 1.0f : (hi - lo) / 255.0f;
    if (!std::isfinite(sc) || sc <= 0.0f) throw std::runtime_error("Invalid DQL scale");
    scales[m] = sc;
    const std::uint8_t zp = static_cast<std::uint8_t>(roundEven(std::clamp(0.0f - lo / sc, 0.0f, 255.0f)));
    zero_points[m] = zp;
    const float min_val = static_cast<float>(0 - static_cast<std::int32_t>(zp));
    const float max_val = static_cast<float>(255 - static_cast<std::int32_t>(zp));
    for (std::size_t j = 0; j < cols; ++j) {
      const float v = std::clamp(in_row[j] / sc, min_val, max_val);
      out_row[j] = static_cast<std::uint8_t>(static_cast<std::int32_t>(roundEven(v)) + static_cast<std::int32_t>(zp));
    }
  }
}

void quantizeLinearU8WithNorm(const float* x, std::size_t dim, std::uint8_t* output,
                              float& scale, std::uint8_t& zero_point,
                              float& dx_sq, float* qx_sq) {
  if (!x || !output || !dim) throw std::invalid_argument("Invalid DQL norm activation");
#if defined(__x86_64__) || defined(_M_X64)
  if ((features() & detail::Avx2) && dim >= 8) {
    quantizeLinearU8RangeAvx2(x, dim, output, scale, zero_point, &dx_sq, qx_sq);
    return;
  }
#endif
  quantizeLinearU8ScalarRange(x, dim, output, scale, zero_point, &dx_sq, qx_sq);
}

struct Weight::Impl {
  enum class Layout { K32Avx2, K4Vnni8, K4Vnni512_16, K4Sdot4, K8Neon };
  std::size_t k_, n_, block_ = 8;
  float scale_;
  Dot dot_;
  Layout layout_ = Layout::K32Avx2;
  bool sdot_ = false;
  std::vector<std::int8_t> packed_;
  std::vector<std::int32_t> sums_;
  std::vector<float> fused_bias_;
  std::vector<float> separate_bias_;

  std::int8_t* packedData() {
    auto* p = packed_.data();
    return p + ((64 - reinterpret_cast<std::uintptr_t>(p) % 64) % 64);
  }
  const std::int8_t* packedData() const { return const_cast<Impl*>(this)->packedData(); }

  void initLayout(bool enable_vnni) {
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned f = features();
    if (enable_vnni && hasFma()) {
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
      if (f & detail::Avx512Vnni) {
        layout_ = Layout::K4Vnni512_16;
        block_ = 16;
        return;
      }
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
      if (f & detail::AvxVnni) {
        layout_ = Layout::K4Vnni8;
        block_ = 8;
        return;
      }
#endif
    }
    (void)f;
    layout_ = Layout::K32Avx2;
    block_ = 8;
#elif defined(__aarch64__) || defined(_M_ARM64)
    (void)enable_vnni;
    sdot_ = dot_ == dotSdot;
    if (sdot_) {
      layout_ = Layout::K4Sdot4;
      block_ = (n_ % 32 == 0) ? 32 : 8;
      if (block_ == 32) dot_ = dotSdot32;
    } else {
      layout_ = Layout::K8Neon;
      block_ = 8;
    }
#else
    (void)enable_vnni;
    layout_ = Layout::K32Avx2;
    block_ = 8;
#endif
  }

  template <typename ReadWeight>
  void packWeights(ReadWeight&& read_q) {
    const std::size_t n_padded = (layout_ == Layout::K4Vnni512_16) ? ((n_ + 15) & ~std::size_t(15)) : n_;
    if (k_ > (SIZE_MAX - 63) / n_padded) throw std::invalid_argument("Packed weight size overflow");
    packed_.assign(k_ * n_padded + 63, 0);
    sums_.assign(n_padded, 0);
    std::int8_t* dst = packedData();
    for (std::size_t col = 0; col < n_; ++col) {
      std::int32_t sum = 0;
      for (std::size_t row = 0; row < k_; ++row) {
        const std::int8_t q = read_q(row, col);
        std::size_t index = 0;
        switch (layout_) {
          case Layout::K4Vnni512_16:
            index = (col / 16) * k_ * 16 + (row / 4) * 64 + (col % 16) * 4 + (row % 4);
            break;
          case Layout::K4Vnni8:
            index = (col / 8) * k_ * 8 + (row / 4) * 32 + (col % 8) * 4 + (row % 4);
            break;
          case Layout::K32Avx2:
            index = (col / 8) * k_ * 8 + (row / 32) * 256 + (col % 8) * 32 + (row % 32);
            break;
          case Layout::K4Sdot4:
            index = (col / block_) * k_ * block_ + (row / 4) * block_ * 4 + (col % block_) * 4 + (row % 4);
            break;
          case Layout::K8Neon:
            index = (col / 8) * k_ * 8 + row * 8 + (col % 8);
            break;
        }
        dst[index] = q;
        sum += q;
      }
      sums_[col] = sum;
    }
  }

  Impl(const float* data, std::size_t k, std::size_t n) : k_(k), n_(n), dot_(selectDot()) {
    if (!dot_) throw std::invalid_argument("Packed CPU/OS support unavailable");
    if (!data || !k || !n || k % 32 || n % 8 || k > std::size_t(INT32_MAX) / (255 * 128) || n > SIZE_MAX / k)
      throw std::invalid_argument("Unsupported packed matrix dimensions");
    float maximum = 0.0f;
    for (std::size_t i = 0; i < k * n; ++i) {
      if (!std::isfinite(data[i])) throw std::invalid_argument("Non-finite packed weight");
      maximum = std::max(maximum, std::abs(data[i]));
    }
    scale_ = (maximum * 2.f) / 254.f;
    if (scale_ < std::numeric_limits<float>::min()) scale_ = 1.f;
    if (!std::isfinite(scale_)) throw std::invalid_argument("Invalid packed weight scale");
    initLayout(true);
    packWeights([&](std::size_t row, std::size_t col) -> std::int8_t {
      return static_cast<std::int8_t>(std::clamp(roundEven(data[row * n + col] / scale_), -128.f, 127.f));
    });
  }

  Impl(const std::int8_t* w_q8, float w_scale, std::size_t rows, std::size_t cols,
       const float* bias, const float* separate_bias, bool enable_vnni)
      : k_(rows), n_(cols), scale_(w_scale), dot_(selectDot()) {
    if (!dot_) throw std::invalid_argument("Packed CPU/OS support unavailable");
    if (!w_q8 || !rows || !cols || rows % 32 || cols % 8 ||
        rows > std::size_t(INT32_MAX) / (255 * 128) || cols > SIZE_MAX / rows ||
        !std::isfinite(w_scale) || w_scale <= 0.0f)
      throw std::invalid_argument("Unsupported packed INT8 matrix dimensions");
    if (bias) {
      checkFinite(bias, cols, "Non-finite packed weight fused bias");
      fused_bias_.assign(bias, bias + cols);
    }
    if (separate_bias) {
      checkFinite(separate_bias, cols, "Non-finite packed weight separate bias");
      separate_bias_.assign(separate_bias, separate_bias + cols);
    }
    initLayout(enable_vnni);
    packWeights([&](std::size_t row, std::size_t col) -> std::int8_t {
      return w_q8[row * cols + col];
    });
  }

  void runMultiplyRange(const std::uint8_t* a, std::size_t a_stride, const float* a_scales,
                        const std::uint8_t* a_zps, std::size_t rows, std::size_t col_begin,
                        std::size_t col_end, float* output, std::size_t out_stride,
                        const float* fused_bias, const float* separate_bias,
                        bool relu_minmax, float* out_maxs) const {
    if (col_begin >= col_end) return;
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    if (layout_ == Layout::K4Vnni512_16) {
      multiplyBatchK4Avx512Vnni(a, a_stride, a_scales, a_zps, packedData(), sums_.data(),
                                scale_, rows, k_, col_begin, col_end, output, out_stride,
                                fused_bias, separate_bias, relu_minmax, out_maxs);
      return;
    }
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (layout_ == Layout::K4Vnni8) {
      multiplyBatchK4Vnni(a, a_stride, a_scales, a_zps, packedData(), sums_.data(),
                          scale_, rows, k_, col_begin, col_end, output, out_stride,
                          fused_bias, separate_bias, relu_minmax, out_maxs);
      return;
    }
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    if (layout_ == Layout::K4Sdot4) {
      multiplyBatchSdot(a, a_stride, a_scales, a_zps, packedData(), sums_.data(),
                        scale_, rows, k_, col_begin, col_end, block_, output, out_stride,
                        fused_bias, separate_bias, relu_minmax, out_maxs);
      return;
    }
#endif
    if (relu_minmax && out_maxs) {
      for (std::size_t m = 0; m < rows; ++m) out_maxs[m] = 0.0f;
    }
    // K32 / fallback: iterate 8-col weight strips outer so 3-12 KB stays in L1 across rows.
    for (std::size_t col = col_begin; col < col_end; col += 8) {
      const std::int8_t* wcol = packedData() + (col / 8) * k_ * 8;
      for (std::size_t row = 0; row < rows; ++row) {
        std::int32_t acc[8];
        dot_(a + row * a_stride, wcol, k_, acc);
        const float combined_scale = a_scales[row] * scale_;
        const std::int32_t corr_mul = sdot_ ? (128 - static_cast<std::int32_t>(a_zps[row]))
                                            : (-static_cast<std::int32_t>(a_zps[row]));
        float row_max = 0.0f;
        for (std::size_t lane = 0; lane < 8; ++lane) {
          const float value = static_cast<float>(acc[lane] + corr_mul * sums_[col + lane]);
          const float scaled = fused_bias ? std::fma(value, combined_scale, fused_bias[col + lane])
                                          : value * combined_scale;
          float out_v = separate_bias ? scaled + separate_bias[col + lane] : scaled;
          if (relu_minmax) {
            out_v = out_v > 0.0f ? out_v : 0.0f;
            if (out_v > row_max) row_max = out_v;
          }
          output[row * out_stride + col + lane] = out_v;
        }
        if (relu_minmax && out_maxs) out_maxs[row] = std::max(out_maxs[row], row_max);
      }
    }
  }

  void multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                std::uint8_t zp, float* output, const float* bias, const float* separate_bias) const {
    if (!a || !output || !rows || rows > SIZE_MAX / k_ || rows > SIZE_MAX / n_ ||
        reinterpret_cast<std::uintptr_t>(a) % 32 || !std::isfinite(a_scale) || a_scale <= 0)
      throw std::invalid_argument("Invalid packed activation/output");
    if (!std::isfinite(a_scale * scale_)) throw std::invalid_argument("Invalid packed output scale");
    if (rows <= 32) {
      float scales[32];
      std::uint8_t zps[32];
      for (std::size_t m = 0; m < rows; ++m) { scales[m] = a_scale; zps[m] = zp; }
      runMultiplyRange(a, k_, scales, zps, rows, 0, n_, output, n_, bias, separate_bias, false, nullptr);
    } else {
      std::vector<float> scales(rows, a_scale);
      std::vector<std::uint8_t> zps(rows, zp);
      runMultiplyRange(a, k_, scales.data(), zps.data(), rows, 0, n_, output, n_, bias, separate_bias, false, nullptr);
    }
  }

  void multiplyBatch(const std::uint8_t* a_q, const float* a_scales, const std::uint8_t* a_zps,
                     std::size_t rows, float* out, std::size_t out_stride,
                     std::size_t col_begin, std::size_t col_end, bool force_separate_bias) const {
    if (col_end == ~std::size_t(0)) col_end = n_;
    if (!rows || !a_q || !a_scales || !a_zps || !out ||
        col_begin > col_end || col_end > n_ || (col_begin % 8) != 0 || (col_end % 8) != 0 ||
        out_stride < col_end)
      throw std::invalid_argument("Invalid packed batch multiply arguments");
    if (col_begin == col_end) return;
    for (std::size_t m = 0; m < rows; ++m) {
      if (!std::isfinite(a_scales[m]) || a_scales[m] <= 0.0f || !std::isfinite(a_scales[m] * scale_))
        throw std::invalid_argument("Invalid packed batch activation scale");
    }
    const float* fbias = (!force_separate_bias && !fused_bias_.empty()) ? fused_bias_.data() : nullptr;
    const float* sbias = !separate_bias_.empty()
        ? separate_bias_.data()
        : (force_separate_bias && !fused_bias_.empty() ? fused_bias_.data() : nullptr);
    runMultiplyRange(a_q, k_, a_scales, a_zps, rows, col_begin, col_end, out, out_stride, fbias, sbias, false, nullptr);
  }

  void multiplyBatchReluMinMax(const std::uint8_t* a_q, const float* a_scales, const std::uint8_t* a_zps,
                               std::size_t rows, float* out, std::size_t out_stride,
                               float* out_maxs, std::size_t col_begin, std::size_t col_end) const {
    if (col_end == ~std::size_t(0)) col_end = n_;
    if (!rows || !a_q || !a_scales || !a_zps || !out || !out_maxs ||
        col_begin > col_end || col_end > n_ || (col_begin % 8) != 0 || (col_end % 8) != 0 ||
        out_stride < col_end)
      throw std::invalid_argument("Invalid packed batch relu minmax arguments");
    if (col_begin == col_end) return;
    for (std::size_t m = 0; m < rows; ++m) {
      if (!std::isfinite(a_scales[m]) || a_scales[m] <= 0.0f || !std::isfinite(a_scales[m] * scale_))
        throw std::invalid_argument("Invalid packed batch activation scale");
    }
    const float* fbias = !fused_bias_.empty() ? fused_bias_.data() : nullptr;
    const float* sbias = !separate_bias_.empty() ? separate_bias_.data() : nullptr;
    runMultiplyRange(a_q, k_, a_scales, a_zps, rows, col_begin, col_end, out, out_stride, fbias, sbias, true, out_maxs);
  }
};

Weight::Weight(std::unique_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}
Weight::Weight(const float* data, std::size_t k, std::size_t n) : impl_(std::make_unique<const Impl>(data, k, n)) {}
Weight Weight::packInt8(const std::int8_t* w_q8, float w_scale, std::size_t rows, std::size_t cols,
                        const float* bias, const float* separate_bias, bool enable_vnni) {
  return Weight(std::make_unique<const Impl>(w_q8, w_scale, rows, cols, bias, separate_bias, enable_vnni));
}
Weight::~Weight() = default;
Weight::Weight(Weight&&) noexcept = default;
Weight& Weight::operator=(Weight&&) noexcept = default;

void Weight::multiply(const std::uint8_t* a, std::size_t rows, float a_scale,
                      std::uint8_t zp, float* output, const float* bias, const float* separate_bias) const {
  impl_->multiply(a, rows, a_scale, zp, output, bias, separate_bias);
}
void Weight::multiplyBatch(const std::uint8_t* a_q, const float* a_scales, const std::uint8_t* a_zps,
                           std::size_t rows, float* out, std::size_t out_stride,
                           std::size_t col_begin, std::size_t col_end, bool force_separate_bias) const {
  impl_->multiplyBatch(a_q, a_scales, a_zps, rows, out, out_stride, col_begin, col_end, force_separate_bias);
}
void Weight::multiplyBatchReluMinMax(const std::uint8_t* a_q, const float* a_scales, const std::uint8_t* a_zps,
                                     std::size_t rows, float* out, std::size_t out_stride,
                                     float* out_maxs, std::size_t col_begin, std::size_t col_end) const {
  impl_->multiplyBatchReluMinMax(a_q, a_scales, a_zps, rows, out, out_stride, out_maxs, col_begin, col_end);
}

struct ShortlistTable::Impl {
  const float* embedding_ = nullptr;
  const std::int8_t* emb_q8_ptr_ = nullptr;
  const float* output_bias_ = nullptr;
  std::size_t vocab_ = 0;
  std::size_t dim_ = 0;
  float min_pos_ = 0.0f;
  float max_w_l2_ = 0.0f;
  float max_dw_l2_ = 0.0f;
  float max_bias_abs_ = 0.0f;
  bool sequential_fma_ = false;
  bool use_q8_ = false;
  bool exact_int8_ = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool avx512vnni_ = false;
  bool vnni_ = false;
  bool avx2_ = false;
#endif
  std::vector<std::int8_t> emb_q8_;
  std::vector<RowMeta> meta_;
  std::vector<float> row_dw_l2_;
  std::size_t hot_count_ = 0;
  std::size_t hot_block_ = 8;
  std::vector<std::int8_t> hot_q8_k4_;

  void buildHotPrefix() {
#if defined(__x86_64__) || defined(_M_X64)
    if (!use_q8_) return;
    if (avx512vnni_) hot_block_ = 16;
    else if (vnni_) hot_block_ = 8;
    else return;
    hot_count_ = std::min<std::size_t>(vocab_, 4096) & ~(hot_block_ - 1);
    if (hot_count_ == 0) return;
    hot_q8_k4_.resize(hot_count_ * dim_);
    const std::int8_t* src = emb_q8_.data();
    for (std::size_t g = 0; g < hot_count_; g += hot_block_) {
      for (std::size_t k4 = 0; k4 < dim_; k4 += 4) {
        std::int8_t* dst = hot_q8_k4_.data() + g * dim_ + k4 * hot_block_;
        for (std::size_t l = 0; l < hot_block_; ++l) {
          const std::int8_t* er = src + (g + l) * dim_ + k4;
          dst[l * 4 + 0] = er[0];
          dst[l * 4 + 1] = er[1];
          dst[l * 4 + 2] = er[2];
          dst[l * 4 + 3] = er[3];
        }
      }
    }
#endif
  }

  void initFlags() {
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned f = features();
    const bool fma = hasFma();
    avx2_ = (f & detail::Avx2) != 0;
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    avx512vnni_ = (f & detail::Avx512Vnni) != 0 && fma;
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    vnni_ = (f & detail::AvxVnni) != 0 && fma;
#endif
    (void)fma;
    use_q8_ = avx2_ && dim_ % 32 == 0 && dim_ <= std::size_t(INT32_MAX) / (255 * 128);
#elif defined(__aarch64__) || defined(_M_ARM64)
    use_q8_ = hasDotprod() && dim_ % 32 == 0 && dim_ <= std::size_t(INT32_MAX) / (255 * 128);
#endif
  }

  Impl(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim, bool sequential_fma)
      : embedding_(embedding), output_bias_(output_bias), vocab_(vocab), dim_(dim), sequential_fma_(sequential_fma) {
    if (!embedding || !output_bias || vocab < 2 || dim < 1 || vocab > SIZE_MAX / dim)
      throw std::invalid_argument("Invalid shortlist embedding table");
    checkFinite(embedding, vocab * dim, "Nonfinite shortlist embedding");
    for (std::size_t i = 0; i < vocab; ++i) {
      if (!std::isfinite(output_bias[i]) && !(i == 1 && output_bias[i] == -std::numeric_limits<float>::infinity()))
        throw std::invalid_argument("Nonfinite shortlist output bias");
      if (i != 1) max_bias_abs_ = std::max(max_bias_abs_, std::abs(output_bias[i]));
    }
    initFlags();
    if (!use_q8_) return;
    emb_q8_.resize(vocab * dim);
    meta_.resize(vocab);

    float min_pos = std::numeric_limits<float>::infinity();
    const std::size_t total = vocab * dim;
    const std::size_t sample_limit = std::min<std::size_t>(total, 65536);
    for (std::size_t i = 0; i < sample_limit; ++i) {
      const float av = std::abs(embedding[i]);
      if (av > 0.0f && av < min_pos) min_pos = av;
    }
    if (std::isfinite(min_pos) && min_pos >= std::numeric_limits<float>::min()) {
      bool exact = true;
      for (std::size_t i = 0; i < total; ++i) {
        const float qf = roundEven(embedding[i] / min_pos);
        if (qf < -128.0f || qf > 127.0f) { exact = false; break; }
        const auto qi = static_cast<std::int8_t>(qf);
        if (static_cast<float>(qi) * min_pos != embedding[i]) { exact = false; break; }
        emb_q8_[i] = qi;
      }
      if (exact) {
        exact_int8_ = true;
        min_pos_ = min_pos;
        for (std::size_t r = 0; r < vocab; ++r) {
          const float* row = embedding + r * dim;
          const std::int8_t* qr = emb_q8_.data() + r * dim;
          double w_sq = 0.0;
          std::int32_t sum = 0;
          for (std::size_t k = 0; k < dim; ++k) {
            w_sq += static_cast<double>(row[k]) * row[k];
            sum += qr[k];
          }
          const float wl2 = static_cast<float>(std::sqrt(w_sq));
          meta_[r] = RowMeta{min_pos, r == 1 ? -std::numeric_limits<float>::infinity() : output_bias[r], wl2, sum};
          max_w_l2_ = std::max(max_w_l2_, wl2);
        }
        buildHotPrefix();
        return;
      }
    }

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
      const float wl2 = static_cast<float>(std::sqrt(w_sq));
      const float dwl2 = static_cast<float>(std::sqrt(dw_sq));
      meta_[r] = RowMeta{scale, r == 1 ? -std::numeric_limits<float>::infinity() : output_bias[r], wl2, sum};
      row_dw_l2_[r] = dwl2;
      max_w_l2_ = std::max(max_w_l2_, wl2);
      max_dw_l2_ = std::max(max_dw_l2_, dwl2);
    }
    buildHotPrefix();
  }

  Impl(const std::int8_t* emb_q8, float min_pos, const float* bias, std::size_t vocab, std::size_t dim,
       bool sequential_fma)
      : emb_q8_ptr_(emb_q8), output_bias_(bias), vocab_(vocab), dim_(dim),
        min_pos_(min_pos), sequential_fma_(sequential_fma), exact_int8_(true) {
    if (!emb_q8 || !bias || vocab < 2 || dim < 1 || vocab > SIZE_MAX / dim ||
        !std::isfinite(min_pos) || min_pos < 0.0f)
      throw std::invalid_argument("Invalid INT8 shortlist embedding table");
    for (std::size_t i = 0; i < vocab; ++i) {
      if (!std::isfinite(bias[i]) && !(i == 1 && bias[i] == -std::numeric_limits<float>::infinity()))
        throw std::invalid_argument("Nonfinite shortlist output bias");
      if (i != 1) max_bias_abs_ = std::max(max_bias_abs_, std::abs(bias[i]));
    }
    initFlags();
    if (!use_q8_) return;
    meta_.resize(vocab);
    for (std::size_t r = 0; r < vocab; ++r) {
      const std::int8_t* qr = emb_q8 + r * dim;
      std::int64_t q_sq = 0;
      std::int32_t sum = 0;
      for (std::size_t k = 0; k < dim; ++k) {
        const std::int32_t qv = qr[k];
        sum += qv;
        q_sq += static_cast<std::int64_t>(qv) * qv;
      }
      const float wl2 = static_cast<float>(std::sqrt(static_cast<double>(q_sq)) * static_cast<double>(min_pos));
      meta_[r] = RowMeta{min_pos, r == 1 ? -std::numeric_limits<float>::infinity() : bias[r], wl2, sum};
      max_w_l2_ = std::max(max_w_l2_, wl2);
    }
    emb_q8_.assign(emb_q8, emb_q8 + vocab * dim);
    buildHotPrefix();
  }

  float dotRowToken(const float* x, std::size_t token) const {
    if (embedding_) {
      const float* row = embedding_ + token * dim_;
      if (sequential_fma_) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < dim_; ++k) sum = std::fma(x[k], row[k], sum);
        return sum;
      }
#if defined(__x86_64__) || defined(_M_X64)
      if (avx2_) return dotRowAvx2(x, row, dim_);
#elif defined(__aarch64__) || defined(_M_ARM64)
      return dotRowNeon(x, row, dim_);
#endif
      float sum = 0.0f;
      for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * row[k];
      return sum;
    }
    const std::int8_t* qrow = (emb_q8_.empty() ? emb_q8_ptr_ : emb_q8_.data()) + token * dim_;
    if (sequential_fma_) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < dim_; ++k) sum = std::fma(x[k], static_cast<float>(qrow[k]) * min_pos_, sum);
      return sum;
    }
#if defined(__x86_64__) || defined(_M_X64)
    if (avx2_) return dotRowInt8Avx2(x, qrow, min_pos_, dim_);
#elif defined(__aarch64__) || defined(_M_ARM64)
    return dotRowInt8Neon(x, qrow, min_pos_, dim_);
#endif
    float sum = 0.0f;
    for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * (static_cast<float>(qrow[k]) * min_pos_);
    return sum;
  }

  std::size_t argmaxFp32(const float* x, const std::int64_t* candidate_ids,
                         std::size_t candidate_count, float* out_best_logit = nullptr) const {
#if defined(__x86_64__) || defined(_M_X64)
    if (embedding_ && avx2_ && !sequential_fma_) {
      const std::size_t best =
          argmaxFp32Avx2(x, dim_, embedding_, output_bias_, candidate_ids, candidate_count);
      if (out_best_logit) {
        const auto token = static_cast<std::size_t>(candidate_ids[best]);
        const float bias = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
        *out_best_logit = dotRowToken(x, token) + bias;
      }
      return best;
    }
#endif
    std::size_t best = 0;
    float best_logit = -std::numeric_limits<float>::infinity();
    for (std::size_t j = 0; j < candidate_count; ++j) {
      const auto token = static_cast<std::size_t>(candidate_ids[j]);
      const float bias = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
      const float val = dotRowToken(x, token) + bias;
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
        throw std::runtime_error("SSRU nonfinite logit");
      if (j == 0 || val > best_logit) { best_logit = val; best = j; }
    }
    if (out_best_logit) *out_best_logit = best_logit;
    return best;
  }

  std::int64_t argmaxFp32Masked(const float* x, const std::int64_t* union_ids,
                                const std::uint32_t* slot_masks, std::size_t union_count,
                                std::size_t slot) const {
    bool found = false;
    std::int64_t best_id = 0;
    float best_logit = -std::numeric_limits<float>::infinity();
    const std::uint32_t bit = 1u << slot;
    for (std::size_t j = 0; j < union_count; ++j) {
      if ((slot_masks[j] & bit) == 0) continue;
      const auto token = static_cast<std::size_t>(union_ids[j]);
      const float bias = token == 1 ? -std::numeric_limits<float>::infinity() : output_bias_[token];
      const float val = dotRowToken(x, token) + bias;
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
        throw std::runtime_error("SSRU nonfinite logit");
      if (!found || val > best_logit) {
        found = true;
        best_logit = val;
        best_id = union_ids[j];
      }
    }
    return best_id;
  }

  std::size_t argmax(const float* x, const std::int64_t* candidate_ids,
                     std::size_t candidate_count, Workspace& workspace, bool prevalidated,
                     float* out_best_logit = nullptr) const {
    if (!x || !candidate_ids || !candidate_count)
      throw std::invalid_argument("Invalid shortlist arguments");
    if (!prevalidated) {
      for (std::size_t j = 0; j < candidate_count; ++j)
        if (candidate_ids[j] < 0 || static_cast<std::uint64_t>(candidate_ids[j]) >= vocab_)
          throw std::invalid_argument("Shortlist candidate outside vocabulary");
    }
    if (!use_q8_) {
      checkFinite(x, dim_, "Nonfinite shortlist activation");
      return argmaxFp32(x, candidate_ids, candidate_count, out_best_logit);
    }
#if defined(__aarch64__) || defined(_M_ARM64)
    std::uint8_t* x_u8 = workspace.resize(dim_ * 2);
    auto* x_s8 = reinterpret_cast<std::int8_t*>(x_u8 + dim_);
#else
    std::uint8_t* x_u8 = workspace.resize(dim_);
#endif
    float* approx = workspace.resizeFloats(candidate_count);
    float x_scale = 1.0f, dx_sq = 0.0f, qx_sq = 0.0f;
    std::uint8_t x_zp = 0;
    quantizeLinearU8WithNorm(x, dim_, x_u8, x_scale, x_zp, dx_sq, exact_int8_ ? nullptr : &qx_sq);
    ScanResult scan{-std::numeric_limits<float>::infinity(), 0, -std::numeric_limits<float>::infinity()};
    const std::size_t hot_prefix = computeHotPrefix(candidate_ids, candidate_count, hot_count_, hot_block_);
    (void)hot_prefix;
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    if (avx512vnni_) {
      scan = scanShortlistAvx512Vnni(
          x_u8, x_scale, x_zp, dim_, emb_q8_.data(), meta_.data(), candidate_ids,
          candidate_count, approx, hot_q8_k4_.data(), meta_.data(), hot_prefix,
          exact_int8_, min_pos_);
    } else
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (vnni_) {
      scan = scanShortlistVnni(
          x_u8, x_scale, x_zp, dim_, emb_q8_.data(), meta_.data(), candidate_ids,
          candidate_count, approx, hot_q8_k4_.data(), meta_.data(), hot_prefix,
          exact_int8_, min_pos_);
    } else
#endif
    {
      scan = scanShortlistAvx2(
          x_u8, x_scale, x_zp, dim_, emb_q8_.data(), meta_.data(), candidate_ids,
          candidate_count, approx, nullptr, nullptr, 0, exact_int8_, min_pos_);
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    (void)hot_prefix;
    scan = scanShortlistSdot(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                             meta_.data(), candidate_ids, candidate_count, approx, x_s8,
                             exact_int8_, min_pos_);
#endif
    if (!std::isfinite(scan.best_logit)) return argmaxFp32(x, candidate_ids, candidate_count, out_best_logit);
    const std::size_t best_j = scan.best_j;
    const auto seed_token = static_cast<std::size_t>(candidate_ids[best_j]);
    float best_exact = dotRowToken(x, seed_token) + meta_[seed_token].bias;
    if (!std::isfinite(best_exact) && !(best_exact == -std::numeric_limits<float>::infinity() && seed_token == 1))
      throw std::runtime_error("SSRU nonfinite logit");

    const float dx_l2 = std::sqrt(dx_sq);
    const float qx_l2 = exact_int8_ ? 0.0f : std::sqrt(qx_sq);
    const float eps = std::numeric_limits<float>::epsilon();
    const float slack = 32.0f * eps * (std::abs(scan.best_logit) + max_bias_abs_) + 1e-4f;
    const float global_err = 1.01f * (dx_l2 * max_w_l2_ + qx_l2 * max_dw_l2_) + slack;
    if (!std::isfinite(global_err)) return argmaxFp32(x, candidate_ids, candidate_count, out_best_logit);
    if (scan.second_logit + global_err < best_exact) {
      if (out_best_logit) *out_best_logit = best_exact;
      return best_j;
    }

    std::size_t best = best_j;
    const float dx_scaled = 1.01f * dx_l2;
    const float qx_scaled = 1.01f * qx_l2;
    for (std::size_t j = 0; j < candidate_count; ++j) {
      if (j == best_j || approx[j] + global_err < best_exact) continue;
      const auto token = static_cast<std::size_t>(candidate_ids[j]);
      const float row_err = (exact_int8_ ? dx_scaled * meta_[token].w_l2
                                         : dx_scaled * meta_[token].w_l2 + qx_scaled * row_dw_l2_[token]) + slack;
      if (approx[j] + row_err < best_exact) continue;
      const float val = dotRowToken(x, token) + meta_[token].bias;
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
        throw std::runtime_error("SSRU nonfinite logit");
      if (val > best_exact || (val == best_exact && j < best)) { best_exact = val; best = j; }
    }
    if (out_best_logit) *out_best_logit = best_exact;
    return best;
  }

  void argMaxBatch(const float* x_rows, std::size_t rows, std::size_t x_stride,
                   const std::int64_t* union_ids, const std::uint32_t* slot_masks,
                   std::size_t union_count, std::int64_t* out_tokens) const {
    if (!x_rows || !union_ids || !slot_masks || !out_tokens || !rows || rows > 32 ||
        !union_count || x_stride < dim_)
      throw std::invalid_argument("Invalid shortlist batch arguments");
    for (std::size_t j = 0; j < union_count; ++j) {
      if (union_ids[j] < 0 || static_cast<std::uint64_t>(union_ids[j]) >= vocab_)
        throw std::invalid_argument("Shortlist batch candidate outside vocabulary");
    }
    if (!use_q8_) {
      for (std::size_t m = 0; m < rows; ++m) {
        const float* xm = x_rows + m * x_stride;
        checkFinite(xm, dim_, "Nonfinite shortlist activation");
        out_tokens[m] = argmaxFp32Masked(xm, union_ids, slot_masks, union_count, m);
      }
      return;
    }

    thread_local Workspace tls_ws;
    const std::size_t stride_q8 = (dim_ + 31) & ~std::size_t(31);
#if defined(__aarch64__) || defined(_M_ARM64)
    std::uint8_t* raw = tls_ws.resize(rows * stride_q8 + rows * dim_);
    std::uint8_t* x_u8 = raw;
    auto* x_s8 = reinterpret_cast<std::int8_t*>(raw + rows * stride_q8);
#else
    std::uint8_t* x_u8 = tls_ws.resize(rows * stride_q8);
#endif
    float* approx = tls_ws.resizeFloats(rows * union_count);
    float x_scales[32], dx_sqs[32], qx_sqs[32];
    std::uint8_t x_zps[32];
    float best_logits[32], second_logits[32];
    std::size_t best_js[32];
    bool has_any[32];

    for (std::size_t m = 0; m < rows; ++m) {
      qx_sqs[m] = 0.0f;
      quantizeLinearU8WithNorm(x_rows + m * x_stride, dim_, x_u8 + m * stride_q8,
                               x_scales[m], x_zps[m], dx_sqs[m], exact_int8_ ? nullptr : &qx_sqs[m]);
    }
    const std::size_t hot_prefix = computeHotPrefix(union_ids, union_count, hot_count_, hot_block_);
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    if (avx512vnni_) {
      scanShortlistAvx512VnniBatch(
          x_u8, stride_q8, x_scales, x_zps, rows, dim_, emb_q8_.data(), meta_.data(),
          union_ids, slot_masks, union_count, approx, best_logits, best_js,
          second_logits, has_any, hot_q8_k4_.data(), meta_.data(), hot_prefix,
          exact_int8_, min_pos_);
    } else
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (vnni_) {
      scanShortlistVnniBatch(
          x_u8, stride_q8, x_scales, x_zps, rows, dim_, emb_q8_.data(), meta_.data(),
          union_ids, slot_masks, union_count, approx, best_logits, best_js,
          second_logits, has_any, hot_q8_k4_.data(), meta_.data(), hot_prefix,
          exact_int8_, min_pos_);
    } else
#endif
    {
      (void)hot_prefix;
      scanShortlistAvx2Batch(
          x_u8, stride_q8, x_scales, x_zps, rows, dim_, emb_q8_.data(), meta_.data(),
          union_ids, slot_masks, union_count, approx, best_logits, best_js,
          second_logits, has_any, exact_int8_, min_pos_);
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    (void)hot_prefix;
    scanShortlistSdotBatch(
        x_u8, stride_q8, x_scales, x_zps, rows, dim_, emb_q8_.data(), meta_.data(),
        union_ids, slot_masks, union_count, approx, best_logits, best_js,
        second_logits, has_any, x_s8, exact_int8_, min_pos_);
#endif

    for (std::size_t m = 0; m < rows; ++m) {
      if (!has_any[m]) {
        out_tokens[m] = 0;
        continue;
      }
      const float* xm = x_rows + m * x_stride;
      if (!std::isfinite(best_logits[m])) {
        out_tokens[m] = argmaxFp32Masked(xm, union_ids, slot_masks, union_count, m);
        continue;
      }
      const std::size_t best_j = best_js[m];
      const auto seed_token = static_cast<std::size_t>(union_ids[best_j]);
      float best_exact = dotRowToken(xm, seed_token) + meta_[seed_token].bias;
      if (!std::isfinite(best_exact) && !(best_exact == -std::numeric_limits<float>::infinity() && seed_token == 1))
        throw std::runtime_error("SSRU nonfinite logit");

      const float dx_l2 = std::sqrt(dx_sqs[m]);
      const float qx_l2 = exact_int8_ ? 0.0f : std::sqrt(qx_sqs[m]);
      const float eps = std::numeric_limits<float>::epsilon();
      const float slack = 32.0f * eps * (std::abs(best_logits[m]) + max_bias_abs_) + 1e-4f;
      const float global_err = 1.01f * (dx_l2 * max_w_l2_ + qx_l2 * max_dw_l2_) + slack;
      if (!std::isfinite(global_err)) {
        out_tokens[m] = argmaxFp32Masked(xm, union_ids, slot_masks, union_count, m);
        continue;
      }
      if (second_logits[m] + global_err < best_exact) {
        out_tokens[m] = union_ids[best_j];
        continue;
      }

      std::size_t best = best_j;
      const float dx_scaled = 1.01f * dx_l2;
      const float qx_scaled = 1.01f * qx_l2;
      const float* row_approx = approx + m * union_count;
      const std::uint32_t bit = 1u << m;
      for (std::size_t j = 0; j < union_count; ++j) {
        if ((slot_masks[j] & bit) == 0 || j == best_j || row_approx[j] + global_err < best_exact) continue;
        const auto token = static_cast<std::size_t>(union_ids[j]);
        const float row_err = (exact_int8_ ? dx_scaled * meta_[token].w_l2
                                           : dx_scaled * meta_[token].w_l2 + qx_scaled * row_dw_l2_[token]) + slack;
        if (row_approx[j] + row_err < best_exact) continue;
        const float val = dotRowToken(xm, token) + meta_[token].bias;
        if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && token == 1))
          throw std::runtime_error("SSRU nonfinite logit");
        if (val > best_exact || (val == best_exact && j < best)) { best_exact = val; best = j; }
      }
      out_tokens[m] = union_ids[best];
    }
  }
};

ShortlistTable::ShortlistTable(std::unique_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}
ShortlistTable::ShortlistTable(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
                               bool sequential_fma)
    : impl_(std::make_unique<const Impl>(embedding, output_bias, vocab, dim, sequential_fma)) {}
ShortlistTable ShortlistTable::createInt8(const std::int8_t* emb_q8, float min_pos, const float* bias,
                                          std::size_t vocab, std::size_t dim, bool sequential_fma) {
  return ShortlistTable(std::make_unique<const Impl>(emb_q8, min_pos, bias, vocab, dim, sequential_fma));
}
ShortlistTable::~ShortlistTable() = default;
ShortlistTable::ShortlistTable(ShortlistTable&&) noexcept = default;
ShortlistTable& ShortlistTable::operator=(ShortlistTable&&) noexcept = default;

std::size_t ShortlistTable::argmax(const float* x, const std::int64_t* candidate_ids,
                                   std::size_t candidate_count, Workspace& workspace, bool prevalidated,
                                   float* out_best_logit) const {
  return impl_->argmax(x, candidate_ids, candidate_count, workspace, prevalidated, out_best_logit);
}
void ShortlistTable::argMaxBatch(const float* x_rows, std::size_t rows, std::size_t x_stride,
                                 const std::int64_t* union_ids, const std::uint32_t* slot_masks,
                                 std::size_t union_count, std::int64_t* out_tokens) const {
  impl_->argMaxBatch(x_rows, rows, x_stride, union_ids, slot_masks, union_count, out_tokens);
}

struct FullVocabQ4::BatchScratch::Impl {
  Workspace work;
  std::uint8_t* raw_work = nullptr;
  std::size_t work_bytes = 0;
  std::vector<const std::uint8_t*> work_ptrs;
  std::vector<TopK9> tops;
  std::vector<TopK9> partial;
  std::vector<int> paths;
  std::size_t max_rows = 0;
  std::size_t max_threads = 0;
};

struct FullVocabQ4::Impl {
  const float* embedding_ = nullptr;
  const std::int8_t* emb_q8_ptr_ = nullptr;
  float min_pos_ = 0.0f;
  std::size_t vocab_ = 0;
  std::size_t dim_ = 0;
  std::size_t blocks_ = 0;
  std::size_t group_width_ = 8;
  bool sequential_fma_ = false;
  bool simd_ready_ = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool avx512vnni_ = false;
  bool vnni_ = false;
  bool avx2_ = false;
#endif
  std::vector<float> bias_;
  std::vector<std::uint8_t> raw_q4_;
  std::vector<float> raw_scales_;
  std::vector<std::uint8_t> packed_q4_;
  std::vector<float> packed_scales_;
  std::vector<float> kai_wsum_;

  void packQ4Layout() {
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned f = features();
    const bool fma = hasFma();
    avx2_ = (f & detail::Avx2) != 0;
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    avx512vnni_ = (f & detail::Avx512Vnni) != 0 && fma;
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    vnni_ = (f & detail::AvxVnni) != 0 && fma;
#endif
    group_width_ = avx512vnni_ ? 16 : 8;
    simd_ready_ = avx2_ && fma && (dim_ % 64 == 0) && vocab_ >= group_width_;
    if (simd_ready_) {
      const std::size_t groups = vocab_ / group_width_;
      // [vocab/gw][blocks][8_chunks][gw_cols][4_k_nibbles]
      packed_q4_.resize(groups * blocks_ * 8 * group_width_ * 4);
      packed_scales_.resize(groups * blocks_ * group_width_);
      for (std::size_t g = 0; g < groups; ++g) {
        for (std::size_t b = 0; b < blocks_; ++b) {
          for (std::size_t c = 0; c < group_width_; ++c) {
            const std::size_t col = g * group_width_ + c;
            const std::uint8_t* sq = raw_q4_.data() + (col * blocks_ + b) * 32;
            auto nib = [&](std::size_t j) -> std::uint8_t { return (sq[j >> 1] >> ((j & 1) * 4)) & 0x0Fu; };
            for (std::size_t i = 0; i < 8; ++i) {
              std::uint8_t* dq = packed_q4_.data() + ((g * blocks_ + b) * 8 + i) * group_width_ * 4 + c * 4;
              for (std::size_t r = 0; r < 4; ++r) {
                const std::size_t j = i * 4 + r;
                dq[r] = static_cast<std::uint8_t>(nib(j) | (nib(j + 32) << 4));
              }
            }
            packed_scales_[(g * blocks_ + b) * group_width_ + c] = raw_scales_[col * blocks_ + b];
          }
        }
      }
      if (vocab_ % group_width_ == 0) {
        raw_q4_.clear(); raw_q4_.shrink_to_fit();
        raw_scales_.clear(); raw_scales_.shrink_to_fit();
      }
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    group_width_ = 4;
    simd_ready_ = hasDotprod() && (dim_ % 64 == 0) && vocab_ >= 4;
    if (simd_ready_) {
      const std::size_t groups = vocab_ / 4;
      packed_q4_.resize(groups * blocks_ * 128);
      packed_scales_.resize(groups * blocks_ * 4);
      kai_wsum_.assign(groups * 4, 0.0f);
      for (std::size_t g = 0; g < groups; ++g) {
        for (std::size_t b = 0; b < blocks_; ++b) {
          for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t col = g * 4 + c;
            const std::uint8_t* sq = raw_q4_.data() + (col * blocks_ + b) * 32;
            std::uint8_t* dq = packed_q4_.data() + ((g * blocks_ + b) * 4 + c) * 32;
            auto nib = [&](std::size_t j) -> std::uint8_t { return (sq[j >> 1] >> ((j & 1) * 4)) & 0x0Fu; };
            for (std::size_t j = 0; j < 16; ++j) {
              dq[j] = static_cast<std::uint8_t>(nib(j) | (nib(j + 32) << 4));
              dq[j + 16] = static_cast<std::uint8_t>(nib(j + 16) | (nib(j + 48) << 4));
            }
            float ws = raw_scales_[col * blocks_ + b];
            std::uint32_t bits;
            std::memcpy(&bits, &ws, sizeof(bits));
            bits = (bits + 0x7FFFu + ((bits >> 16) & 1u)) & 0xFFFF0000u;
            std::memcpy(&ws, &bits, sizeof(bits));
            packed_scales_[(g * blocks_ + b) * 4 + c] = ws;
            std::int32_t bsum = 0;
            for (std::size_t j = 0; j < 64; ++j) bsum += static_cast<int>(nib(j)) - 8;
            kai_wsum_[g * 4 + c] += static_cast<float>(bsum) * ws;
          }
        }
      }
      if (vocab_ % 4 == 0) {
        raw_q4_.clear(); raw_q4_.shrink_to_fit();
        raw_scales_.clear(); raw_scales_.shrink_to_fit();
      }
    }
#endif
  }

  Impl(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim, bool sequential_fma)
      : embedding_(embedding), vocab_(vocab), dim_(dim), blocks_((dim + 63) / 64), sequential_fma_(sequential_fma) {
    if (!embedding || !output_bias || vocab < 9 || dim < 4 || vocab > SIZE_MAX / dim || vocab > 0x7fffffffu)
      throw std::invalid_argument("Invalid full-vocab Q4 embedding table");
    checkFinite(embedding, vocab * dim, "Nonfinite full-vocab embedding");
    bias_.resize(vocab);
    for (std::size_t i = 0; i < vocab; ++i) {
      if (!std::isfinite(output_bias[i]) && !(i == 1 && output_bias[i] == -std::numeric_limits<float>::infinity()))
        throw std::invalid_argument("Nonfinite full-vocab output bias");
      bias_[i] = i == 1 ? -std::numeric_limits<float>::infinity() : output_bias[i];
    }

    raw_q4_.assign(vocab * blocks_ * 32, 0);
    raw_scales_.resize(vocab * blocks_);
    for (std::size_t col = 0; col < vocab; ++col) {
      const float* row = embedding + col * dim;
      for (std::size_t b = 0; b < blocks_; ++b) {
        float lo = 0.0f, hi = 0.0f;
        for (std::size_t j = 0; j < 64 && b * 64 + j < dim; ++j) {
          const float v = row[b * 64 + j];
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
        const float scale = -(std::abs(hi) > std::abs(lo) ? hi : lo) / 8.0f;
        raw_scales_[col * blocks_ + b] = scale;
        const float inv = scale == 0.0f ? 0.0f : 1.0f / scale;
        std::uint8_t* dst_q = raw_q4_.data() + (col * blocks_ + b) * 32;
        for (std::size_t j = 0; j < 64; ++j) {
          const float v = (b * 64 + j < dim) ? row[b * 64 + j] : 0.0f;
          const float y = (v * inv) + 8.0f;
          const int qi = std::max(0, std::min(15, static_cast<int>(std::floor(y + 0.5f))));
          dst_q[j >> 1] |= static_cast<std::uint8_t>(qi << ((j & 1) * 4));
        }
      }
    }
    packQ4Layout();
  }

  Impl(const std::int8_t* emb_q8, float min_pos, const float* bias, std::size_t vocab, std::size_t dim,
       bool sequential_fma)
      : emb_q8_ptr_(emb_q8), min_pos_(min_pos), vocab_(vocab), dim_(dim), blocks_((dim + 63) / 64),
        sequential_fma_(sequential_fma) {
    if (!emb_q8 || !bias || vocab < 9 || dim < 4 || vocab > SIZE_MAX / dim || vocab > 0x7fffffffu ||
        !std::isfinite(min_pos) || min_pos < 0.0f)
      throw std::invalid_argument("Invalid INT8 full-vocab Q4 embedding table");
    bias_.resize(vocab);
    for (std::size_t i = 0; i < vocab; ++i) {
      if (!std::isfinite(bias[i]) && !(i == 1 && bias[i] == -std::numeric_limits<float>::infinity()))
        throw std::invalid_argument("Nonfinite full-vocab output bias");
      bias_[i] = i == 1 ? -std::numeric_limits<float>::infinity() : bias[i];
    }

    raw_q4_.assign(vocab * blocks_ * 32, 0);
    raw_scales_.resize(vocab * blocks_);
    for (std::size_t col = 0; col < vocab; ++col) {
      const std::int8_t* qrow = emb_q8 + col * dim;
      for (std::size_t b = 0; b < blocks_; ++b) {
        float lo = 0.0f, hi = 0.0f;
        for (std::size_t j = 0; j < 64 && b * 64 + j < dim; ++j) {
          const float v = static_cast<float>(qrow[b * 64 + j]) * min_pos;
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
        const float scale = -(std::abs(hi) > std::abs(lo) ? hi : lo) / 8.0f;
        raw_scales_[col * blocks_ + b] = scale;
        const float inv = scale == 0.0f ? 0.0f : 1.0f / scale;
        std::uint8_t* dst_q = raw_q4_.data() + (col * blocks_ + b) * 32;
        for (std::size_t j = 0; j < 64; ++j) {
          const float v = (b * 64 + j < dim) ? (static_cast<float>(qrow[b * 64 + j]) * min_pos) : 0.0f;
          const float y = (v * inv) + 8.0f;
          const int qi = std::max(0, std::min(15, static_cast<int>(std::floor(y + 0.5f))));
          dst_q[j >> 1] |= static_cast<std::uint8_t>(qi << ((j & 1) * 4));
        }
      }
    }
    packQ4Layout();
  }

  float dotRowToken(const float* x, std::size_t token) const {
    if (embedding_) {
      const float* row = embedding_ + token * dim_;
      if (sequential_fma_) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < dim_; ++k) sum = std::fma(x[k], row[k], sum);
        return sum;
      }
#if defined(__x86_64__) || defined(_M_X64)
      if (avx2_) return dotRowAvx2(x, row, dim_);
#elif defined(__aarch64__) || defined(_M_ARM64)
      return dotRowNeon(x, row, dim_);
#endif
      float sum = 0.0f;
      for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * row[k];
      return sum;
    }
    const std::int8_t* qrow = emb_q8_ptr_ + token * dim_;
    if (sequential_fma_) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < dim_; ++k) sum = std::fma(x[k], static_cast<float>(qrow[k]) * min_pos_, sum);
      return sum;
    }
#if defined(__x86_64__) || defined(_M_X64)
    if (avx2_) return dotRowInt8Avx2(x, qrow, min_pos_, dim_);
#elif defined(__aarch64__) || defined(_M_ARM64)
    return dotRowInt8Neon(x, qrow, min_pos_, dim_);
#endif
    float sum = 0.0f;
    for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * (static_cast<float>(qrow[k]) * min_pos_);
    return sum;
  }

  float scalarCoarseCol(const std::int8_t* qa, const float* sa, std::size_t col) const {
    float acc = 0.0f;
    const std::uint8_t* sq = raw_q4_.data() + col * blocks_ * 32;
    const float* ss = raw_scales_.data() + col * blocks_;
    for (std::size_t b = 0; b < blocks_; ++b) {
      std::int32_t isum = 0;
      const std::uint8_t* bq = sq + b * 32;
      for (std::size_t j = 0; j < 64 && b * 64 + j < dim_; ++j) {
        const int nib = (bq[j >> 1] >> ((j & 1) * 4)) & 0x0F;
        isum += static_cast<int>(qa[b * 64 + j]) * (nib - 8);
      }
      acc += static_cast<float>(isum) * (sa[b] * ss[b]);
    }
    return acc + bias_[col];
  }

  enum class Q4Path { None, Avx512Vnni, AvxVnni, Avx2, Sdot };

  Q4Path prepareInput(const float* x, std::uint8_t* work) const {
    if (!simd_ready_) return Q4Path::None;
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    if (avx512vnni_) {
      if (prepareFullQ4Avx512Vnni(x, dim_, work)) return Q4Path::Avx512Vnni;
      return Q4Path::None;
    }
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (vnni_) {
      if (prepareFullQ4Vnni(x, dim_, work)) return Q4Path::AvxVnni;
    }
#endif
    prepareFullQ4K4Avx2(x, dim_, work);
    return Q4Path::Avx2;
#elif defined(__aarch64__) || defined(_M_ARM64)
    prepareFullQ4Sdot(x, dim_, work);
    return Q4Path::Sdot;
#else
    (void)x; (void)work;
    return Q4Path::None;
#endif
  }

  void scanRange(std::size_t g_begin, std::size_t g_end, const std::uint8_t* work, Q4Path path, TopK9& out) const {
    if (g_begin >= g_end) { out.init(); return; }
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    if (path == Q4Path::Avx512Vnni) {
      scanFullQ4K4Avx512VnniRange(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(),
                                  bias_.data(), work, out);
      return;
    }
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (path == Q4Path::AvxVnni) {
      scanFullQ4K4VnniRange(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(),
                            bias_.data(), work, out);
      return;
    }
#endif
    if (path == Q4Path::Avx2) {
      scanFullQ4K4Avx2Range(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(),
                            bias_.data(), work, out);
      return;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (path == Q4Path::Sdot) {
      scanFullQ4SdotRange(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(),
                          kai_wsum_.data(), bias_.data(), work, out);
      return;
    }
#else
    (void)work;
    (void)path;
#endif
    out.init();
  }

  void scanBatchRange(std::size_t rows, std::size_t g_begin, std::size_t g_end,
                      const std::uint8_t* const* work_bufs, Q4Path path, TopK9* tops) const {
    if (g_begin >= g_end) {
      for (std::size_t m = 0; m < rows; ++m) tops[m].init();
      return;
    }
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_AVX512VNNI) && !defined(MTRAN_SSRU_NO_VNNI) && !defined(MTRAN_SSRU_NO_AVX512VNNI)
    if (path == Q4Path::Avx512Vnni) {
      scanFullQ4K4Avx512VnniBatchRange(dim_, rows, g_begin, g_end, packed_q4_.data(),
                                       packed_scales_.data(), bias_.data(), work_bufs, tops);
      return;
    }
#endif
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (path == Q4Path::AvxVnni) {
      scanFullQ4K4VnniBatchRange(dim_, rows, g_begin, g_end, packed_q4_.data(),
                                 packed_scales_.data(), bias_.data(), work_bufs, tops);
      return;
    }
#endif
    if (path == Q4Path::Avx2) {
      scanFullQ4K4Avx2BatchRange(dim_, rows, g_begin, g_end, packed_q4_.data(),
                                 packed_scales_.data(), bias_.data(), work_bufs, tops);
      return;
    }
#endif
    for (std::size_t m = 0; m < rows; ++m) {
      scanRange(g_begin, g_end, work_bufs[m], path, tops[m]);
    }
  }

  void appendScalarTail(const float* x, std::uint8_t* work, std::size_t start_col, TopK9& top) const {
    const std::size_t qa_bytes = ((dim_ + 63) & ~std::size_t(63));
    auto* qa = reinterpret_cast<std::int8_t*>(work);
    auto* sa = reinterpret_cast<float*>(qa + qa_bytes);
    for (std::size_t b = 0; b < blocks_; ++b) {
      float mx = 0.0f;
      for (std::size_t j = 0; j < 64 && b * 64 + j < dim_; ++j) mx = std::max(mx, std::abs(x[b * 64 + j]));
      sa[b] = mx / 127.0f;
      const float inv = mx != 0.0f ? 127.0f / mx : 0.0f;
      for (std::size_t j = 0; j < 64; ++j) {
        const float v = (b * 64 + j < dim_) ? x[b * 64 + j] : 0.0f;
        qa[b * 64 + j] = static_cast<std::int8_t>(std::clamp(roundEven(v * inv), -128.0f, 127.0f));
      }
    }
    for (std::size_t col = start_col; col < vocab_; ++col) {
      top.push(scalarCoarseCol(qa, sa, col), static_cast<std::uint32_t>(col));
    }
  }

  std::size_t rerankTopK(const float* x, const TopK9& top) const {
    std::size_t best_id = top.ids[0];
    float best_exact = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < top.count; ++i) {
      const std::size_t id = top.ids[i];
      const float val = dotRowToken(x, id) + bias_[id];
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && id == 1))
        throw std::runtime_error("SSRU nonfinite full-vocab logit");
      if (i == 0 || val > best_exact) {
        best_exact = val;
        best_id = id;
      }
    }
    return best_id;
  }

  std::size_t perRowWorkBytes() const noexcept {
    const std::size_t qa_bytes = ((dim_ + 63) & ~std::size_t(63));
    const std::size_t sa_bytes = ((blocks_ + 7) & ~std::size_t(7)) * sizeof(float);
    const std::size_t corr_bytes = blocks_ * 8 * sizeof(std::int32_t);
    return (qa_bytes + sa_bytes + corr_bytes + 64 + 63) & ~std::size_t(63);
  }

  void reserveBatchScratch(FullVocabQ4::BatchScratch::Impl& bs, std::size_t max_rows,
                           std::size_t max_threads) const {
    const std::size_t rows_need = std::max(bs.max_rows, std::max<std::size_t>(1, max_rows));
    const std::size_t thr_need =
        std::max(bs.max_threads, std::max<std::size_t>(1, max_threads));
    const std::size_t row_bytes = perRowWorkBytes();
    const std::size_t need_work = rows_need * row_bytes;
    if (need_work > bs.work_bytes || !bs.raw_work) {
      bs.raw_work = bs.work.resize(need_work);
      bs.work_bytes = need_work;
    }
    if (bs.work_ptrs.size() < rows_need) bs.work_ptrs.resize(rows_need);
    if (bs.tops.size() < rows_need) bs.tops.resize(rows_need);
    if (bs.paths.size() < rows_need) bs.paths.resize(rows_need);
    const std::size_t row_stride = ((rows_need + 15) / 16) * 16;
    const std::size_t need_partial = thr_need * row_stride;
    if (bs.partial.size() < need_partial) bs.partial.resize(need_partial);
    bs.max_rows = rows_need;
    bs.max_threads = thr_need;
  }

  void argMaxBatchInTeam(FullVocabQ4::BatchScratch::Impl& bs, const float* x_rows,
                         std::size_t rows, std::size_t x_stride, std::int64_t* out_tokens,
                         mtran::ssru::TeamBarrier* barrier, std::size_t tid,
                         std::size_t nt) const {
    if (!x_rows || !out_tokens || !rows || x_stride < dim_ ||
        rows > bs.max_rows || nt > bs.max_threads) {
      throw std::invalid_argument("Invalid full-vocab Q4 batch arguments");
    }
    const std::size_t per_row_work = perRowWorkBytes();
    const std::size_t row_stride = ((rows + 15) / 16) * 16;
    std::uint8_t* raw_work = bs.raw_work;

    for (std::size_t m = tid; m < rows; m += nt) {
      const float* xm = x_rows + m * x_stride;
      checkFinite(xm, dim_, "Nonfinite full-vocab activation");
      std::uint8_t* wm = raw_work + m * per_row_work;
      bs.work_ptrs[m] = wm;
      bs.paths[m] = static_cast<int>(prepareInput(xm, wm));
      bs.tops[m].init();
    }
    if (nt > 1 && barrier && !barrier->arriveAndWait()) return;

    Q4Path path = static_cast<Q4Path>(bs.paths[0]);
    for (std::size_t m = 1; m < rows; ++m) {
      if (static_cast<Q4Path>(bs.paths[m]) != path) {
        path = Q4Path::None;
        break;
      }
    }
    const bool ran_simd = (path != Q4Path::None);
    const std::size_t groups = vocab_ / group_width_;
    if (ran_simd) {
      if (nt >= 2) {
        const std::size_t gb = (groups * tid) / nt;
        const std::size_t ge = (groups * (tid + 1)) / nt;
        scanBatchRange(rows, gb, ge, bs.work_ptrs.data(), path,
                       bs.partial.data() + tid * row_stride);
        if (barrier && !barrier->arriveAndWait()) return;
      } else {
        scanBatchRange(rows, 0, groups, bs.work_ptrs.data(), path, bs.tops.data());
      }
    }

    const std::size_t start_col = ran_simd ? (groups * group_width_) : 0;
    const bool need_tail = (!ran_simd || (vocab_ % group_width_ != 0));
    for (std::size_t m = tid; m < rows; m += nt) {
      if (ran_simd && nt >= 2) {
        for (std::size_t p = 0; p < nt; ++p) {
          const TopK9& pt = bs.partial[p * row_stride + m];
          for (int k = 0; k < pt.count; ++k) bs.tops[m].push(pt.scores[k], pt.ids[k]);
        }
      }
      if (need_tail) {
        appendScalarTail(x_rows + m * x_stride, raw_work + m * per_row_work,
                         start_col, bs.tops[m]);
      }
      out_tokens[m] = static_cast<std::int64_t>(rerankTopK(x_rows + m * x_stride, bs.tops[m]));
    }
  }

  std::size_t argmax(const float* x, Workspace& workspace, bool allow_parallel) const {
    if (!x) throw std::invalid_argument("Invalid full-vocab activation");
    checkFinite(x, dim_, "Nonfinite full-vocab activation");
    const std::size_t qa_bytes = ((dim_ + 63) & ~std::size_t(63));
    const std::size_t sa_bytes = ((blocks_ + 7) & ~std::size_t(7)) * sizeof(float);
    const std::size_t corr_bytes = blocks_ * 8 * sizeof(std::int32_t);
    std::uint8_t* work = workspace.resize(qa_bytes + sa_bytes + corr_bytes + 64);

    TopK9 top;
    top.init();
    const Q4Path path = prepareInput(x, work);
    const bool ran_simd = (path != Q4Path::None);
    if (ran_simd) {
      const std::size_t groups = vocab_ / group_width_;
      auto& pool = mtran::ssru::PcorePool::instance();
      const std::size_t nthreads = (allow_parallel && vocab_ >= 4096)
          ? std::min<std::size_t>(pool.pcoreLogicalCount(), std::max<std::size_t>(1, groups / 16))
          : 1;
      if (nthreads >= 2) {
        thread_local std::vector<PaddedTopK9<TopK9>> tls_partial;
        if (tls_partial.size() < nthreads) tls_partial.resize(nthreads);
        auto* partial_ptr = tls_partial.data();
        std::size_t actual_nt = 1;
        pool.parallelFor(nthreads, [&](std::size_t tid, std::size_t nthr) {
          if (tid == 0) actual_nt = nthr;
          const std::size_t gb = (groups * tid) / nthr;
          const std::size_t ge = (groups * (tid + 1)) / nthr;
          scanRange(gb, ge, work, path, partial_ptr[tid].top);
        });
        for (std::size_t p = 0; p < actual_nt; ++p) {
          for (int k = 0; k < partial_ptr[p].top.count; ++k) {
            top.push(partial_ptr[p].top.scores[k], partial_ptr[p].top.ids[k]);
          }
        }
      } else {
        scanRange(0, groups, work, path, top);
      }
    }
    if (!ran_simd || (vocab_ % group_width_ != 0)) {
      const std::size_t start_col = ran_simd ? ((vocab_ / group_width_) * group_width_) : 0;
      if (!ran_simd) top.init();
      appendScalarTail(x, work, start_col, top);
    }
    return rerankTopK(x, top);
  }

  void argMaxBatch(const float* x_rows, std::size_t rows, std::size_t x_stride,
                   std::int64_t* out_tokens, std::size_t max_threads) const {
    if (!x_rows || !out_tokens || !rows || x_stride < dim_)
      throw std::invalid_argument("Invalid full-vocab Q4 batch arguments");
    if (rows == 1 && max_threads <= 1) {
      thread_local Workspace tls_single;
      out_tokens[0] = static_cast<std::int64_t>(argmax(x_rows, tls_single, false));
      return;
    }

    const std::size_t groups = vocab_ / group_width_;
    auto& pool = mtran::ssru::PcorePool::instance();
    const std::size_t cap = std::max<std::size_t>(1, max_threads);
    const std::size_t nthreads = (cap > 1 && groups >= 4)
        ? std::min<std::size_t>({cap, pool.pcoreLogicalCount(), groups / 4})
        : 1;

    thread_local FullVocabQ4::BatchScratch tls_bs;
    auto& bs = *tls_bs.impl_;
    reserveBatchScratch(bs, rows, nthreads);
    if (nthreads <= 1) {
      argMaxBatchInTeam(bs, x_rows, rows, x_stride, out_tokens, nullptr, 0, 1);
      return;
    }
    if (rows == 1) {
      checkFinite(x_rows, dim_, "Nonfinite full-vocab activation");
      std::uint8_t* w0 = bs.raw_work;
      const Q4Path path = prepareInput(x_rows, w0);
      TopK9& top = bs.tops[0];
      top.init();
      const bool ran_simd = (path != Q4Path::None);
      if (ran_simd) {
        auto* partial_ptr = bs.partial.data();
        std::size_t actual_nt = 1;
        pool.parallelFor(nthreads, [&](std::size_t tid, std::size_t nt) {
          if (tid == 0) actual_nt = nt;
          const std::size_t gb = (groups * tid) / nt;
          const std::size_t ge = (groups * (tid + 1)) / nt;
          scanRange(gb, ge, w0, path, partial_ptr[tid * 16]);
        });
        for (std::size_t p = 0; p < actual_nt; ++p) {
          const TopK9& pt = partial_ptr[p * 16];
          for (int k = 0; k < pt.count; ++k) top.push(pt.scores[k], pt.ids[k]);
        }
      }
      if (!ran_simd || (vocab_ % group_width_ != 0)) {
        const std::size_t start_col = ran_simd ? (groups * group_width_) : 0;
        appendScalarTail(x_rows, w0, start_col, top);
      }
      out_tokens[0] = static_cast<std::int64_t>(rerankTopK(x_rows, top));
      return;
    }

    mtran::ssru::TeamBarrier barrier(nthreads);
    pool.parallelFor(nthreads, [&](std::size_t tid, std::size_t nt) {
      struct CancelGuard {
        mtran::ssru::TeamBarrier& b;
        bool active = false;
        bool done = false;
        ~CancelGuard() { if (!done && active) b.cancel(); }
      } cancel_guard{barrier, nt > 1};
      argMaxBatchInTeam(bs, x_rows, rows, x_stride, out_tokens,
                        (nt > 1) ? &barrier : nullptr, tid, nt);
      cancel_guard.done = true;
    });
  }
};

FullVocabQ4::BatchScratch::BatchScratch() : impl_(std::make_unique<Impl>()) {}
FullVocabQ4::BatchScratch::~BatchScratch() = default;
FullVocabQ4::BatchScratch::BatchScratch(BatchScratch&&) noexcept = default;
FullVocabQ4::BatchScratch& FullVocabQ4::BatchScratch::operator=(BatchScratch&&) noexcept = default;

void FullVocabQ4::reserveBatchScratch(BatchScratch& scratch, std::size_t max_rows,
                                      std::size_t max_threads) const {
  impl_->reserveBatchScratch(*scratch.impl_, max_rows, max_threads);
}
void FullVocabQ4::argMaxBatchInTeam(BatchScratch& scratch, const float* x_rows,
                                    std::size_t rows, std::size_t x_stride,
                                    std::int64_t* out_tokens,
                                    mtran::ssru::TeamBarrier* barrier,
                                    std::size_t tid, std::size_t nt) const {
  impl_->argMaxBatchInTeam(*scratch.impl_, x_rows, rows, x_stride, out_tokens,
                           barrier, tid, nt);
}

FullVocabQ4::FullVocabQ4(std::unique_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}
FullVocabQ4::FullVocabQ4(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
                         bool sequential_fma)
    : impl_(std::make_unique<const Impl>(embedding, output_bias, vocab, dim, sequential_fma)) {}
FullVocabQ4 FullVocabQ4::createInt8(const std::int8_t* emb_q8, float min_pos, const float* bias,
                                    std::size_t vocab, std::size_t dim, bool sequential_fma) {
  return FullVocabQ4(std::make_unique<const Impl>(emb_q8, min_pos, bias, vocab, dim, sequential_fma));
}
FullVocabQ4::~FullVocabQ4() = default;
FullVocabQ4::FullVocabQ4(FullVocabQ4&&) noexcept = default;
FullVocabQ4& FullVocabQ4::operator=(FullVocabQ4&&) noexcept = default;

std::size_t FullVocabQ4::argmax(const float* x, Workspace& workspace, bool allow_parallel) const {
  return impl_->argmax(x, workspace, allow_parallel);
}
void FullVocabQ4::argMaxBatch(const float* x_rows, std::size_t rows, std::size_t x_stride,
                              std::int64_t* out_tokens, std::size_t max_threads) const {
  impl_->argMaxBatch(x_rows, rows, x_stride, out_tokens, max_threads);
}
}
