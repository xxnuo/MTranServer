#include "packed.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
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
#include <intrin.h>
#define MTRAN_PREFETCH(addr) __prefetch(addr)
#else
#include <arm_neon.h>
#define MTRAN_PREFETCH(addr) __builtin_prefetch((addr), 0, 3)
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
#ifdef _MSC_VER
#pragma fp_contract(off)
#endif

namespace mtran::ssru::packed {
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
void multiplyVnni(const std::uint8_t*, const std::int8_t*, const std::int32_t*,
                  std::size_t, std::size_t, std::size_t, std::uint8_t, float, float*,
                  const float*, const float*);
ShortlistTable::ScanResult scanShortlistVnni(
    const std::uint8_t*, float, std::uint8_t, std::size_t,
    const std::int8_t*, const ShortlistTable::RowMeta*,
    const std::int64_t*, std::size_t, float*);
bool prepareFullQ4Vnni(const float*, std::size_t, std::uint8_t*);
void scanFullQ4VnniRange(
    std::size_t, std::size_t, std::size_t,
    const std::uint8_t*, const float*,
    const float*, const std::uint8_t*, FullVocabQ4::TopK9&);
#endif
namespace {
float roundEven(float x) {
  const float lo = std::floor(x), fraction = x - lo;
  return fraction < .5f ? lo : fraction > .5f ? lo + 1.f : std::fmod(lo, 2.f) == 0.f ? lo : lo + 1.f;
}
void checkFinite(const float* data, std::size_t count, const char* what) {
  std::size_t i = 0;
#if defined(__x86_64__) || defined(_M_X64)
  const __m128i mask = _mm_set1_epi32(0x7f800000);
  __m128i bad4 = _mm_setzero_si128();
  for (; i + 4 <= count; i += 4) {
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
    bad4 = _mm_or_si128(bad4, _mm_cmpeq_epi32(_mm_and_si128(v, mask), mask));
  }
  if (_mm_movemask_epi8(bad4) != 0) throw std::runtime_error(what);
#elif defined(__aarch64__) || defined(_M_ARM64)
  const uint32x4_t mask = vdupq_n_u32(0x7f800000u);
  uint32x4_t bad4 = vdupq_n_u32(0);
  for (; i + 4 <= count; i += 4) {
    const uint32x4_t v = vreinterpretq_u32_f32(vld1q_f32(data + i));
    bad4 = vorrq_u32(bad4, vceqq_u32(vandq_u32(v, mask), mask));
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
MTRAN_AVX2 ShortlistTable::ScanResult scanShortlistAvx2(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out) {
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
  const __m256i vzp = _mm256_set1_epi32(zp_i);
  const __m256 vxscale = _mm256_set1_ps(x_scale);
  const __m256i low = _mm256_set1_epi8(127), high = _mm256_set1_epi8(-128);
  const __m256i ones = _mm256_set1_epi16(1);
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
    const auto& m0 = meta[ts[0]]; const auto& m1 = meta[ts[1]];
    const auto& m2 = meta[ts[2]]; const auto& m3 = meta[ts[3]];
    const auto& m4 = meta[ts[4]]; const auto& m5 = meta[ts[5]];
    const auto& m6 = meta[ts[6]]; const auto& m7 = meta[ts[7]];
    const __m256i rsums = _mm256_setr_epi32(m0.sum, m1.sum, m2.sum, m3.sum, m4.sum, m5.sum, m6.sum, m7.sum);
    const __m256 rscales = _mm256_mul_ps(vxscale, _mm256_setr_ps(m0.scale, m1.scale, m2.scale, m3.scale, m4.scale, m5.scale, m6.scale, m7.scale));
    const __m256 rbias = _mm256_setr_ps(m0.bias, m1.bias, m2.bias, m3.bias, m4.bias, m5.bias, m6.bias, m7.bias);
    const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(sums8, _mm256_mullo_epi32(vzp, rsums)));
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
    const float val = static_cast<float>(acc - zp_i * m.sum) * (x_scale * m.scale) + m.bias;
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

bool hasFma() noexcept {
#ifdef _MSC_VER
  int regs[4];
  __cpuid(regs, 0);
  if (regs[0] < 1) return false;
  __cpuidex(regs, 1, 0);
  return (static_cast<unsigned>(regs[2]) & 0x18001000u) == 0x18001000u && (_xgetbv(0) & 6) == 6;
#else
  unsigned eax, ebx, ecx, edx;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
  if ((ecx & 0x18001000u) != 0x18001000u) return false;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return (eax & 6) == 6;
#endif
}
#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_AVX2_FMA __declspec(noinline)
#else
#define MTRAN_AVX2_FMA __attribute__((target("avx2,fma"), noinline))
#endif
MTRAN_AVX2_FMA void prepareFullQ4Avx2(
    const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const __m256 sign_mask = _mm256_set1_ps(-0.0f);
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
  }
}
MTRAN_AVX2_FMA void scanFullQ4Avx2Range(
    std::size_t dim, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4, const float* packed_scales,
    const float* bias, const std::uint8_t* work_buf, FullVocabQ4::TopK9& top) {
  const std::size_t blocks = dim / 64;
  const auto* qa = reinterpret_cast<const std::int8_t*>(work_buf);
  const auto* sa = reinterpret_cast<const float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const __m256i eight = _mm256_set1_epi8(8);
  const __m256i ones = _mm256_set1_epi16(1);
  const __m256i mask = _mm256_set1_epi8(15);
  top.init();
  for (std::size_t g = g_begin; g < g_end; ++g) {
    __m256 acc[4] = {_mm256_setzero_ps(), _mm256_setzero_ps(), _mm256_setzero_ps(), _mm256_setzero_ps()};
    const std::uint8_t* gp = packed_q4 + g * blocks * 128;
    const float* gs = packed_scales + g * blocks * 4;
    for (std::size_t b = 0; b < blocks; ++b, gp += 128, gs += 4) {
      const __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64));
      const __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64 + 32));
      const __m256 vsa = _mm256_set1_ps(sa[b]);
      for (std::size_t c = 0; c < 4; ++c) {
        const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gp + c * 32));
        const __m256i lo = _mm256_sub_epi8(_mm256_and_si256(p, mask), eight);
        const __m256i hi = _mm256_sub_epi8(_mm256_and_si256(_mm256_srli_epi16(p, 4), mask), eight);
        const __m256i d0 = _mm256_maddubs_epi16(_mm256_sign_epi8(lo, lo), _mm256_sign_epi8(a0, lo));
        const __m256i d1 = _mm256_maddubs_epi16(_mm256_sign_epi8(hi, hi), _mm256_sign_epi8(a1, hi));
        const __m256i sum = _mm256_madd_epi16(ones, _mm256_hadd_epi16(d0, d1));
        acc[c] = _mm256_fmadd_ps(_mm256_cvtepi32_ps(sum), _mm256_mul_ps(vsa, _mm256_set1_ps(gs[c])), acc[c]);
      }
    }
    const __m256 lo01 = _mm256_unpacklo_ps(acc[0], acc[1]), hi01 = _mm256_unpackhi_ps(acc[0], acc[1]);
    const __m256 lo23 = _mm256_unpacklo_ps(acc[2], acc[3]), hi23 = _mm256_unpackhi_ps(acc[2], acc[3]);
    __m256 pf = _mm256_castpd_ps(_mm256_unpacklo_pd(_mm256_castps_pd(lo01), _mm256_castps_pd(lo23)));
    pf = _mm256_add_ps(pf, _mm256_castpd_ps(_mm256_unpackhi_pd(_mm256_castps_pd(lo01), _mm256_castps_pd(lo23))));
    pf = _mm256_add_ps(pf, _mm256_castpd_ps(_mm256_unpacklo_pd(_mm256_castps_pd(hi01), _mm256_castps_pd(hi23))));
    pf = _mm256_add_ps(pf, _mm256_castpd_ps(_mm256_unpackhi_pd(_mm256_castps_pd(hi01), _mm256_castps_pd(hi23))));
    const __m128 vals = _mm_add_ps(_mm_add_ps(_mm256_castps256_ps128(pf), _mm256_extractf128_ps(pf, 1)), _mm_loadu_ps(bias + g * 4));
    if (top.count < 9 || _mm_movemask_ps(_mm_cmpgt_ps(vals, _mm_set1_ps(top.scores[8]))) != 0) {
      alignas(16) float out4[4];
      _mm_store_ps(out4, vals);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 4);
      for (int c = 0; c < 4; ++c) top.push(out4[c], base_id + c);
    }
  }
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
MTRAN_DOTPROD ShortlistTable::ScanResult scanShortlistSdot(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out,
    std::int8_t* x_s8_buf) {
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const std::int32_t zp_corr = 128 - static_cast<std::int32_t>(x_zp);
  const uint8x16_t sign_flip = vdupq_n_u8(0x80);
  for (std::size_t k = 0; k < dim; k += 16) {
    vst1q_s8(x_s8_buf + k, vreinterpretq_s8_u8(veorq_u8(vld1q_u8(x_u8 + k), sign_flip)));
  }
  const int32x4_t vzp = vdupq_n_s32(zp_corr);
  const float32x4_t vxscale = vdupq_n_f32(x_scale);
  std::size_t j = 0;
  for (; j + 8 <= candidate_count; j += 8) {
    if (j + 16 <= candidate_count) {
      for (int p = 0; p < 8; ++p) {
        const auto nt = static_cast<std::size_t>(candidate_ids[j + 8 + p]);
        MTRAN_PREFETCH(emb_q8 + nt * dim);
        MTRAN_PREFETCH(meta + nt);
      }
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
    int32x4_t a0 = vdupq_n_s32(0), a1 = a0, a2 = a0, a3 = a0;
    int32x4_t a4 = a0, a5 = a0, a6 = a0, a7 = a0;
    for (std::size_t k = 0; k < dim; k += 16) {
      const int8x16_t vx = vld1q_s8(x_s8_buf + k);
      a0 = vdotq_s32(a0, vld1q_s8(r0 + k), vx);
      a1 = vdotq_s32(a1, vld1q_s8(r1 + k), vx);
      a2 = vdotq_s32(a2, vld1q_s8(r2 + k), vx);
      a3 = vdotq_s32(a3, vld1q_s8(r3 + k), vx);
      a4 = vdotq_s32(a4, vld1q_s8(r4 + k), vx);
      a5 = vdotq_s32(a5, vld1q_s8(r5 + k), vx);
      a6 = vdotq_s32(a6, vld1q_s8(r6 + k), vx);
      a7 = vdotq_s32(a7, vld1q_s8(r7 + k), vx);
    }
    const int32x4_t dots0 = vpaddq_s32(vpaddq_s32(a0, a1), vpaddq_s32(a2, a3));
    const int32x4_t dots1 = vpaddq_s32(vpaddq_s32(a4, a5), vpaddq_s32(a6, a7));
    const auto& m0 = meta[ts[0]]; const auto& m1 = meta[ts[1]];
    const auto& m2 = meta[ts[2]]; const auto& m3 = meta[ts[3]];
    const auto& m4 = meta[ts[4]]; const auto& m5 = meta[ts[5]];
    const auto& m6 = meta[ts[6]]; const auto& m7 = meta[ts[7]];
    const int32_t s0_arr[4] = {m0.sum, m1.sum, m2.sum, m3.sum};
    const int32_t s1_arr[4] = {m4.sum, m5.sum, m6.sum, m7.sum};
    const float sc0_arr[4] = {m0.scale, m1.scale, m2.scale, m3.scale};
    const float sc1_arr[4] = {m4.scale, m5.scale, m6.scale, m7.scale};
    const float b0_arr[4] = {m0.bias, m1.bias, m2.bias, m3.bias};
    const float b1_arr[4] = {m4.bias, m5.bias, m6.bias, m7.bias};
    const float32x4_t v0 = vaddq_f32(vmulq_f32(vcvtq_f32_s32(vmlaq_s32(dots0, vzp, vld1q_s32(s0_arr))), vmulq_f32(vxscale, vld1q_f32(sc0_arr))), vld1q_f32(b0_arr));
    const float32x4_t v1 = vaddq_f32(vmulq_f32(vcvtq_f32_s32(vmlaq_s32(dots1, vzp, vld1q_s32(s1_arr))), vmulq_f32(vxscale, vld1q_f32(sc1_arr))), vld1q_f32(b1_arr));
    vst1q_f32(approx_out + j, v0);
    vst1q_f32(approx_out + j + 4, v1);
    if (vmaxvq_f32(vmaxq_f32(v0, v1)) >= second_logit) {
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
    for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(x_s8_buf[k]) * row[k];
    const auto& m = meta[t];
    const float val = static_cast<float>(acc + zp_corr * m.sum) * (x_scale * m.scale) + m.bias;
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

MTRAN_DOTPROD void prepareFullQ4Sdot(
    const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  auto* neg_corr = reinterpret_cast<std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const int8x16_t neg8 = vdupq_n_s8(-8);
  const int32x4_t zero = vdupq_n_s32(0);
  for (std::size_t b = 0; b < blocks; ++b) {
    float amax = 0.0f;
    for (std::size_t k = 0; k < 64; k += 16) {
      const float32x4_t a0 = vabsq_f32(vld1q_f32(x + b * 64 + k + 0));
      const float32x4_t a1 = vabsq_f32(vld1q_f32(x + b * 64 + k + 4));
      const float32x4_t a2 = vabsq_f32(vld1q_f32(x + b * 64 + k + 8));
      const float32x4_t a3 = vabsq_f32(vld1q_f32(x + b * 64 + k + 12));
      amax = std::max(amax, vmaxvq_f32(vmaxq_f32(vmaxq_f32(a0, a2), vmaxq_f32(a1, a3))));
    }
    const float scale = amax / 127.0f;
    const float inv = scale != 0.0f ? 1.0f / scale : 0.0f;
    sa[b] = scale;
    for (std::size_t k = 0; k < 64; k += 16) {
      for (std::size_t u = 0; u < 4; ++u) {
        const int32x4_t qi = vcvtaq_s32_f32(vmulq_n_f32(vld1q_f32(x + b * 64 + k + u * 4), inv));
        qa[b * 64 + k + u * 4 + 0] = static_cast<std::int8_t>(vgetq_lane_s32(qi, 0));
        qa[b * 64 + k + u * 4 + 1] = static_cast<std::int8_t>(vgetq_lane_s32(qi, 1));
        qa[b * 64 + k + u * 4 + 2] = static_cast<std::int8_t>(vgetq_lane_s32(qi, 2));
        qa[b * 64 + k + u * 4 + 3] = static_cast<std::int8_t>(vgetq_lane_s32(qi, 3));
      }
    }
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
    const float* bias, const std::uint8_t* work_buf, FullVocabQ4::TopK9& top) {
  const std::size_t blocks = dim / 64;
  const auto* qa = reinterpret_cast<const std::int8_t*>(work_buf);
  const auto* sa = reinterpret_cast<const float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const auto* neg_corr = reinterpret_cast<const std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
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
      const float sa_b = sa[b];
      MTRAN_SDOT_Q4_STEP(gp0 + 0, gs0[0], acc0); MTRAN_SDOT_Q4_STEP(gp0 + 32, gs0[1], acc1);
      MTRAN_SDOT_Q4_STEP(gp0 + 64, gs0[2], acc2); MTRAN_SDOT_Q4_STEP(gp0 + 96, gs0[3], acc3);
      MTRAN_SDOT_Q4_STEP(gp1 + 0, gs1[0], acc4); MTRAN_SDOT_Q4_STEP(gp1 + 32, gs1[1], acc5);
      MTRAN_SDOT_Q4_STEP(gp1 + 64, gs1[2], acc6); MTRAN_SDOT_Q4_STEP(gp1 + 96, gs1[3], acc7);
    }
    const float32x4_t v0 = vaddq_f32(vpaddq_f32(vpaddq_f32(acc0, acc1), vpaddq_f32(acc2, acc3)), vld1q_f32(bias + g * 4));
    const float32x4_t v1 = vaddq_f32(vpaddq_f32(vpaddq_f32(acc4, acc5), vpaddq_f32(acc6, acc7)), vld1q_f32(bias + g * 4 + 4));
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
      const float sa_b = sa[b];
      MTRAN_SDOT_Q4_STEP(gp + 0, gs[0], acc0);
      MTRAN_SDOT_Q4_STEP(gp + 32, gs[1], acc1);
      MTRAN_SDOT_Q4_STEP(gp + 64, gs[2], acc2);
      MTRAN_SDOT_Q4_STEP(gp + 96, gs[3], acc3);
    }
    const float32x4_t v0 = vaddq_f32(vpaddq_f32(vpaddq_f32(acc0, acc1), vpaddq_f32(acc2, acc3)), vld1q_f32(bias + g * 4));
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
  bool exact_int8_ = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool vnni_ = false;
  bool avx2_ = false;
#endif
  std::vector<std::int8_t> emb_q8_;
  std::vector<RowMeta> meta_;
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
    meta_.resize(vocab);

    // Detect 0x4101 tensor-wide exact int8 * scalar_inverse storage (zero weight quantization residual).
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
                     std::size_t candidate_count, Workspace& workspace, bool prevalidated) const {
    if (!x || !candidate_ids || !candidate_count)
      throw std::invalid_argument("Invalid shortlist arguments");
    if (!prevalidated) {
      for (std::size_t j = 0; j < candidate_count; ++j)
        if (candidate_ids[j] < 0 || static_cast<std::uint64_t>(candidate_ids[j]) >= vocab_)
          throw std::invalid_argument("Shortlist candidate outside vocabulary");
    }
    if (!use_q8_) {
      checkFinite(x, dim_, "Nonfinite shortlist activation");
      return argmaxFp32(x, candidate_ids, candidate_count);
    }
#if defined(__aarch64__) || defined(_M_ARM64)
    std::uint8_t* x_u8 = workspace.resize(dim_ * 2);
    auto* x_s8 = reinterpret_cast<std::int8_t*>(x_u8 + dim_);
#else
    std::uint8_t* x_u8 = workspace.resize(dim_);
#endif
    float* approx = workspace.resizeFloats(candidate_count);
    float x_scale = 1.0f;
    std::uint8_t x_zp = 0;
    quantizeLinearU8(x, 1, dim_, x_u8, x_scale, x_zp);
    ScanResult scan{-std::numeric_limits<float>::infinity(), 0, -std::numeric_limits<float>::infinity()};
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (vnni_) {
      scan = scanShortlistVnni(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                               meta_.data(), candidate_ids, candidate_count, approx);
    } else
#endif
    {
      scan = scanShortlistAvx2(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                               meta_.data(), candidate_ids, candidate_count, approx);
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    scan = scanShortlistSdot(x_u8, x_scale, x_zp, dim_, emb_q8_.data(),
                             meta_.data(), candidate_ids, candidate_count, approx, x_s8);
#endif
    if (!std::isfinite(scan.best_logit)) return argmaxFp32(x, candidate_ids, candidate_count);
    const std::size_t best_j = scan.best_j;
    const auto seed_token = static_cast<std::size_t>(candidate_ids[best_j]);
    const float seed_bias = meta_[seed_token].bias;
    float best_exact = dotRow(x, embedding_ + seed_token * dim_) + seed_bias;
    if (!std::isfinite(best_exact) && !(best_exact == -std::numeric_limits<float>::infinity() && seed_token == 1))
      throw std::runtime_error("SSRU nonfinite logit");

    float dx_sq = 0.0f, qx_sq = 0.0f;
    const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
    std::size_t k = 0;
#if defined(__x86_64__) || defined(_M_X64)
    {
      const __m128i vzp = _mm_set1_epi32(zp_i);
      const __m128 vsc = _mm_set1_ps(x_scale);
      const __m128i zero128 = _mm_setzero_si128();
      __m128 vdx = _mm_setzero_ps(), vqx = _mm_setzero_ps();
      for (; k + 8 <= dim_; k += 8) {
        const __m128i u8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(x_u8 + k));
        const __m128i u16 = _mm_unpacklo_epi8(u8, zero128);
        const __m128 q0 = _mm_mul_ps(_mm_cvtepi32_ps(_mm_sub_epi32(_mm_unpacklo_epi16(u16, zero128), vzp)), vsc);
        const __m128 q1 = _mm_mul_ps(_mm_cvtepi32_ps(_mm_sub_epi32(_mm_unpackhi_epi16(u16, zero128), vzp)), vsc);
        const __m128 d0 = _mm_sub_ps(_mm_loadu_ps(x + k), q0);
        const __m128 d1 = _mm_sub_ps(_mm_loadu_ps(x + k + 4), q1);
        vdx = _mm_add_ps(vdx, _mm_add_ps(_mm_mul_ps(d0, d0), _mm_mul_ps(d1, d1)));
        if (!exact_int8_) vqx = _mm_add_ps(vqx, _mm_add_ps(_mm_mul_ps(q0, q0), _mm_mul_ps(q1, q1)));
      }
      alignas(16) float td[4], tq[4];
      _mm_store_ps(td, vdx);
      dx_sq = (td[0] + td[1]) + (td[2] + td[3]);
      if (!exact_int8_) {
        _mm_store_ps(tq, vqx);
        qx_sq = (tq[0] + tq[1]) + (tq[2] + tq[3]);
      }
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    {
      const int32x4_t vzp = vdupq_n_s32(zp_i);
      const float32x4_t vsc = vdupq_n_f32(x_scale);
      float32x4_t vdx = vdupq_n_f32(0.0f), vqx = vdupq_n_f32(0.0f);
      for (; k + 8 <= dim_; k += 8) {
        const uint16x8_t u16 = vmovl_u8(vld1_u8(x_u8 + k));
        const float32x4_t q0 = vmulq_f32(vcvtq_f32_s32(vsubq_s32(vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(u16))), vzp)), vsc);
        const float32x4_t q1 = vmulq_f32(vcvtq_f32_s32(vsubq_s32(vreinterpretq_s32_u32(vmovl_high_u16(u16)), vzp)), vsc);
        const float32x4_t d0 = vsubq_f32(vld1q_f32(x + k), q0);
        const float32x4_t d1 = vsubq_f32(vld1q_f32(x + k + 4), q1);
        vdx = vaddq_f32(vdx, vaddq_f32(vmulq_f32(d0, d0), vmulq_f32(d1, d1)));
        if (!exact_int8_) vqx = vaddq_f32(vqx, vaddq_f32(vmulq_f32(q0, q0), vmulq_f32(q1, q1)));
      }
      dx_sq = vaddvq_f32(vdx);
      if (!exact_int8_) qx_sq = vaddvq_f32(vqx);
    }
#endif
    for (; k < dim_; ++k) {
      const float qx = static_cast<float>(static_cast<std::int32_t>(x_u8[k]) - zp_i) * x_scale;
      const float dx = x[k] - qx;
      dx_sq += dx * dx;
      if (!exact_int8_) qx_sq += qx * qx;
    }
    const float dx_l2 = std::sqrt(dx_sq);
    const float qx_l2 = exact_int8_ ? 0.0f : std::sqrt(qx_sq);
    const float eps = std::numeric_limits<float>::epsilon();
    const float slack = 32.0f * eps * (std::abs(scan.best_logit) + max_bias_abs_) + 1e-4f;
    const float global_err = 1.01f * (dx_l2 * max_w_l2_ + qx_l2 * max_dw_l2_) + slack;
    if (!std::isfinite(global_err)) return argmaxFp32(x, candidate_ids, candidate_count);
    // O(1) Stage-2 early exit when even the runner-up cannot surpass best_exact.
    if (scan.second_logit + global_err < best_exact) return best_j;

    std::size_t best = best_j;
    const float dx_scaled = 1.01f * dx_l2;
    const float qx_scaled = 1.01f * qx_l2;
    for (std::size_t j = 0; j < candidate_count; ++j) {
      if (j == best_j || approx[j] + global_err < best_exact) continue;
      const auto token = static_cast<std::size_t>(candidate_ids[j]);
      const float row_err = (exact_int8_ ? dx_scaled * meta_[token].w_l2
                                         : dx_scaled * meta_[token].w_l2 + qx_scaled * row_dw_l2_[token]) + slack;
      if (approx[j] + row_err < best_exact) continue;
      const float bias = meta_[token].bias;
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
                                   std::size_t candidate_count, Workspace& workspace, bool prevalidated) const {
  return impl_->argmax(x, candidate_ids, candidate_count, workspace, prevalidated);
}

struct FullVocabQ4::Impl {
  const float* embedding_;
  std::size_t vocab_;
  std::size_t dim_;
  std::size_t blocks_;
  bool sequential_fma_ = false;
  bool simd_ready_ = false;
#if defined(__x86_64__) || defined(_M_X64)
  bool vnni_ = false;
  bool avx2_ = false;
#endif
  std::vector<float> bias_;
  std::vector<std::uint8_t> raw_q4_;
  std::vector<float> raw_scales_;
  std::vector<std::uint8_t> packed_q4_;
  std::vector<float> packed_scales_;

  // Claimable 4-way worker team used only when a single request is active (c=1).
  mutable std::atomic<bool> team_busy_{false};
  mutable std::mutex team_mu_;
  mutable std::condition_variable team_cv_;
  mutable std::atomic<std::uint64_t> epoch_{0};
  mutable std::atomic<int> remaining_{0};
  mutable std::atomic<int> sleeping_{0};
  mutable std::atomic<bool> stop_{false};
  mutable const std::uint8_t* task_work_ = nullptr;
  mutable bool task_vnni_ = false;
  mutable TopK9 partial_tops_[4];
  mutable std::vector<std::thread> workers_;

  static void cpuRelax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
    __yield();
#else
    asm volatile("yield" ::: "memory");
#endif
#else
    std::this_thread::yield();
#endif
  }

  void scanRange(std::size_t g_begin, std::size_t g_end, const std::uint8_t* work, bool use_vnni, TopK9& out) const {
    if (g_begin >= g_end) { out.init(); return; }
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    if (use_vnni) {
      scanFullQ4VnniRange(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(), bias_.data(), work, out);
      return;
    }
#else
    (void)use_vnni;
#endif
    scanFullQ4Avx2Range(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(), bias_.data(), work, out);
#elif defined(__aarch64__) || defined(_M_ARM64)
    (void)use_vnni;
    scanFullQ4SdotRange(dim_, g_begin, g_end, packed_q4_.data(), packed_scales_.data(), bias_.data(), work, out);
#else
    (void)work; (void)use_vnni; out.init();
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

#if defined(__x86_64__) || defined(_M_X64)
    const unsigned f = features();
    avx2_ = (f & detail::Avx2) != 0;
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
    vnni_ = (f & detail::AvxVnni) != 0;
#endif
    simd_ready_ = avx2_ && hasFma() && (dim % 64 == 0) && vocab >= 4;
    if (simd_ready_) {
      const std::size_t groups = vocab / 4;
      packed_q4_.resize(groups * blocks_ * 128);
      packed_scales_.resize(groups * blocks_ * 4);
      for (std::size_t g = 0; g < groups; ++g) {
        for (std::size_t b = 0; b < blocks_; ++b) {
          for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t col = g * 4 + c;
            const std::uint8_t* sq = raw_q4_.data() + (col * blocks_ + b) * 32;
            std::uint8_t* dq = packed_q4_.data() + ((g * blocks_ + b) * 4 + c) * 32;
            auto nib = [&](std::size_t j) -> std::uint8_t { return (sq[j >> 1] >> ((j & 1) * 4)) & 0x0Fu; };
            for (std::size_t j = 0; j < 32; ++j) dq[j] = static_cast<std::uint8_t>(nib(j) | (nib(j + 32) << 4));
            packed_scales_[(g * blocks_ + b) * 4 + c] = raw_scales_[col * blocks_ + b];
          }
        }
      }
      if (vocab % 4 == 0) {
        raw_q4_.clear(); raw_q4_.shrink_to_fit();
        raw_scales_.clear(); raw_scales_.shrink_to_fit();
      }
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    simd_ready_ = hasDotprod() && (dim % 64 == 0) && vocab >= 4;
    if (simd_ready_) {
      const std::size_t groups = vocab / 4;
      packed_q4_.resize(groups * blocks_ * 128);
      packed_scales_.resize(groups * blocks_ * 4);
      for (std::size_t g = 0; g < groups; ++g) {
        for (std::size_t b = 0; b < blocks_; ++b) {
          for (std::size_t c = 0; c < 4; ++c) {
            const std::size_t col = g * 4 + c;
            const std::uint8_t* sq = raw_q4_.data() + (col * blocks_ + b) * 32;
            std::uint8_t* dq = packed_q4_.data() + ((g * blocks_ + b) * 4 + c) * 32;
            auto nib = [&](std::size_t j) -> std::uint8_t { return (sq[j >> 1] >> ((j & 1) * 4)) & 0x0Fu; };
            for (std::size_t j = 0; j < 16; ++j) {
              dq[j] = static_cast<std::uint8_t>(nib(j) | (nib(j + 16) << 4));
              dq[j + 16] = static_cast<std::uint8_t>(nib(j + 32) | (nib(j + 48) << 4));
            }
            packed_scales_[(g * blocks_ + b) * 4 + c] = raw_scales_[col * blocks_ + b];
          }
        }
      }
      if (vocab % 4 == 0) {
        raw_q4_.clear(); raw_q4_.shrink_to_fit();
        raw_scales_.clear(); raw_scales_.shrink_to_fit();
      }
    }
#endif
    if (simd_ready_ && vocab >= 4096) {
      const unsigned hw = std::thread::hardware_concurrency();
      const unsigned helpers = hw >= 4 ? 3u : (hw >= 2 ? hw - 1u : 0u);
      const std::size_t groups = vocab / 4;
      const std::size_t parts = helpers + 1;
      workers_.reserve(helpers);
      for (unsigned w = 0; w < helpers; ++w) {
        const std::size_t idx = w + 1;
        const std::size_t gb = (groups * idx) / parts;
        const std::size_t ge = (groups * (idx + 1)) / parts;
        workers_.emplace_back([this, idx, gb, ge] {
          std::uint64_t seen = 0;
          for (;;) {
            std::uint64_t cur = epoch_.load(std::memory_order_acquire);
            if (cur == seen && !stop_.load(std::memory_order_acquire)) {
              for (int spin = 0; spin < 32768; ++spin) {
                cpuRelax();
                cur = epoch_.load(std::memory_order_acquire);
                if (cur != seen || stop_.load(std::memory_order_acquire)) break;
              }
            }
            if (cur == seen) {
              std::unique_lock<std::mutex> lk(team_mu_);
              sleeping_.fetch_add(1, std::memory_order_seq_cst);
              team_cv_.wait(lk, [&] {
                cur = epoch_.load(std::memory_order_seq_cst);
                return stop_.load(std::memory_order_seq_cst) || cur != seen;
              });
              sleeping_.fetch_sub(1, std::memory_order_seq_cst);
            }
            if (stop_.load(std::memory_order_acquire)) return;
            seen = cur;
            scanRange(gb, ge, task_work_, task_vnni_, partial_tops_[idx]);
            remaining_.fetch_sub(1, std::memory_order_release);
          }
        });
      }
    }
  }

  ~Impl() {
    if (!workers_.empty()) {
      {
        std::lock_guard<std::mutex> lk(team_mu_);
        stop_.store(true, std::memory_order_seq_cst);
      }
      team_cv_.notify_all();
      for (auto& t : workers_) if (t.joinable()) t.join();
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
#elif defined(__aarch64__) || defined(_M_ARM64)
    return dotRowNeon(x, row, dim_);
#endif
    float sum = 0.0f;
    for (std::size_t k = 0; k < dim_; ++k) sum += x[k] * row[k];
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

  std::size_t argmax(const float* x, Workspace& workspace, bool allow_parallel) const {
    if (!x) throw std::invalid_argument("Invalid full-vocab activation");
    checkFinite(x, dim_, "Nonfinite full-vocab activation");
    const std::size_t qa_bytes = ((dim_ + 63) & ~std::size_t(63));
    const std::size_t sa_bytes = ((blocks_ + 7) & ~std::size_t(7)) * sizeof(float);
    const std::size_t corr_bytes = blocks_ * 8 * sizeof(std::int32_t);
    std::uint8_t* work = workspace.resize(qa_bytes + sa_bytes + corr_bytes + 64);

    TopK9 top;
    bool ran_simd = false;
    if (simd_ready_) {
      bool use_vnni = false;
#if defined(__x86_64__) || defined(_M_X64)
#if defined(MTRAN_SSRU_HAVE_VNNI) && !defined(MTRAN_SSRU_NO_VNNI)
      if (vnni_) use_vnni = prepareFullQ4Vnni(x, dim_, work);
#endif
      if (!use_vnni) prepareFullQ4Avx2(x, dim_, work);
#elif defined(__aarch64__) || defined(_M_ARM64)
      prepareFullQ4Sdot(x, dim_, work);
#endif
      const std::size_t groups = vocab_ / 4;
      if (allow_parallel && !workers_.empty() && !team_busy_.exchange(true, std::memory_order_acquire)) {
        struct ReleaseTeam {
          std::atomic<bool>& flag;
          ~ReleaseTeam() { flag.store(false, std::memory_order_release); }
        } guard{team_busy_};
        const std::size_t parts = workers_.size() + 1;
        task_work_ = work;
        task_vnni_ = use_vnni;
        remaining_.store(static_cast<int>(workers_.size()), std::memory_order_relaxed);
        epoch_.fetch_add(1, std::memory_order_seq_cst);
        if (sleeping_.load(std::memory_order_seq_cst) > 0) {
          std::lock_guard<std::mutex> lk(team_mu_);
          team_cv_.notify_all();
        }
        scanRange(0, groups / parts, work, use_vnni, top);
        while (remaining_.load(std::memory_order_acquire) != 0) {
          cpuRelax();
        }
        for (std::size_t p = 1; p < parts; ++p) {
          for (int k = 0; k < partial_tops_[p].count; ++k) {
            top.push(partial_tops_[p].scores[k], partial_tops_[p].ids[k]);
          }
        }
      } else {
        scanRange(0, groups, work, use_vnni, top);
      }
      ran_simd = true;
    }

    if (!ran_simd || (vocab_ % 4 != 0)) {
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
      std::size_t start_col = ran_simd ? (vocab_ & ~std::size_t(3)) : 0;
      if (!ran_simd) top.init();
      for (std::size_t col = start_col; col < vocab_; ++col) {
        top.push(scalarCoarseCol(qa, sa, col), static_cast<std::uint32_t>(col));
      }
    }

    std::size_t best_id = top.ids[0];
    float best_exact = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < top.count; ++i) {
      const std::size_t id = top.ids[i];
      const float val = dotRow(x, embedding_ + id * dim_) + bias_[id];
      if (!std::isfinite(val) && !(val == -std::numeric_limits<float>::infinity() && id == 1))
        throw std::runtime_error("SSRU nonfinite full-vocab logit");
      if (i == 0 || val > best_exact) {
        best_exact = val;
        best_id = id;
      }
    }
    return best_id;
  }
};

FullVocabQ4::FullVocabQ4(const float* embedding, const float* output_bias, std::size_t vocab, std::size_t dim,
                         bool sequential_fma)
    : impl_(std::make_unique<const Impl>(embedding, output_bias, vocab, dim, sequential_fma)) {}
FullVocabQ4::~FullVocabQ4() = default;
std::size_t FullVocabQ4::argmax(const float* x, Workspace& workspace, bool allow_parallel) const {
  return impl_->argmax(x, workspace, allow_parallel);
}
}
