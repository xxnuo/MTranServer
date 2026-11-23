#include "ops.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_OPS_AVX2 __declspec(noinline)
#define MTRAN_OPS_AVX2_FMA __declspec(noinline)
#else
#define MTRAN_OPS_AVX2 __attribute__((target("avx2"), noinline))
#define MTRAN_OPS_AVX2_FMA __attribute__((target("avx2,fma"), noinline))
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#ifdef _MSC_VER
#pragma fp_contract(off)
#endif

namespace mtran::ssru::ops {
namespace {

#if defined(__x86_64__) || defined(_M_X64)
struct CpuFeatures {
  bool avx2 = false;
  bool avx2_fma = false;
};

CpuFeatures detectCpuFeatures() noexcept {
  CpuFeatures feat;
#ifdef _MSC_VER
  int regs[4];
  __cpuid(regs, 0);
  if (regs[0] < 7) return feat;
  __cpuidex(regs, 1, 0);
  const unsigned ecx1 = static_cast<unsigned>(regs[2]);
  if ((ecx1 & 0x18000000u) != 0x18000000u) return feat;
  if ((_xgetbv(0) & 6) != 6) return feat;
  __cpuidex(regs, 7, 0);
  feat.avx2 = (static_cast<unsigned>(regs[1]) & (1u << 5)) != 0;
  feat.avx2_fma = feat.avx2 && ((ecx1 & (1u << 12)) != 0);
  return feat;
#else
  unsigned eax, ebx, ecx, edx;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return feat;
  const unsigned ecx1 = ecx;
  if ((ecx1 & 0x18000000u) != 0x18000000u) return feat;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  if ((eax & 6) != 6) return feat;
  if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return feat;
  feat.avx2 = (ebx & (1u << 5)) != 0;
  feat.avx2_fma = feat.avx2 && ((ecx1 & (1u << 12)) != 0);
  return feat;
#endif
}

inline const CpuFeatures& cpuFeatures() noexcept {
  static const CpuFeatures cached = detectCpuFeatures();
  return cached;
}

inline bool hasAvx2() noexcept { return cpuFeatures().avx2; }
inline bool hasAvx2Fma() noexcept { return cpuFeatures().avx2_fma; }

MTRAN_OPS_AVX2 float maxRowAvx2(const float* scores, std::size_t len) noexcept {
  __m256 vmax = _mm256_set1_ps(scores[0]);
  std::size_t s = 0;
  for (; s + 8 <= len; s += 8) {
    vmax = _mm256_max_ps(vmax, _mm256_loadu_ps(scores + s));
  }
  const __m128 lo = _mm256_castps256_ps128(vmax);
  const __m128 hi = _mm256_extractf128_ps(vmax, 1);
  const __m128 m4 = _mm_max_ps(lo, hi);
  const __m128 m2 = _mm_max_ps(m4, _mm_movehl_ps(m4, m4));
  const __m128 m1 = _mm_max_ss(m2, _mm_shuffle_ps(m2, m2, 1));
  float maximum = _mm_cvtss_f32(m1);
  for (; s < len; ++s) maximum = std::max(maximum, scores[s]);
  return maximum;
}

MTRAN_OPS_AVX2 void finishSoftmaxAvx2(float* scores, std::size_t len) noexcept {
  __m256 vsum = _mm256_setzero_ps();
  std::size_t s = 0;
  for (; s + 8 <= len; s += 8) {
    vsum = _mm256_add_ps(vsum, _mm256_loadu_ps(scores + s));
  }
  if (s < len) {
    alignas(32) float rem[8] = {};
    for (std::size_t k = 0; s + k < len; ++k) rem[k] = scores[s + k];
    vsum = _mm256_add_ps(vsum, _mm256_load_ps(rem));
  }
  __m256 h = _mm256_hadd_ps(vsum, vsum);
  h = _mm256_hadd_ps(h, h);
  const float total =
      _mm_cvtss_f32(_mm_add_ss(_mm256_castps256_ps128(h), _mm256_extractf128_ps(h, 1)));
  const float inv_tot = 1.0f / total;
  const __m256 vinv = _mm256_set1_ps(inv_tot);
  s = 0;
  for (; s + 8 <= len; s += 8) {
    _mm256_storeu_ps(scores + s, _mm256_mul_ps(_mm256_loadu_ps(scores + s), vinv));
  }
  for (; s < len; ++s) scores[s] *= inv_tot;
}

MTRAN_OPS_AVX2 inline __m256 fastExpAvx2(__m256 x) noexcept {
  alignas(32) float buf[8];
  _mm256_store_ps(buf, x);
  for (int i = 0; i < 8; ++i) {
    buf[i] = std::exp(buf[i]);
  }
  return _mm256_load_ps(buf);
}

MTRAN_OPS_AVX2 inline float hsum256(__m256 v) noexcept {
  const __m128 lo = _mm256_castps256_ps128(v);
  const __m128 hi = _mm256_extractf128_ps(v, 1);
  const __m128 s4 = _mm_add_ps(lo, hi);
  const __m128 s2 = _mm_add_ps(s4, _mm_movehl_ps(s4, s4));
  const __m128 s1 = _mm_add_ss(s2, _mm_shuffle_ps(s2, s2, 1));
  return _mm_cvtss_f32(s1);
}

MTRAN_OPS_AVX2 inline float hmax256(__m256 v) noexcept {
  const __m128 lo = _mm256_castps256_ps128(v);
  const __m128 hi = _mm256_extractf128_ps(v, 1);
  const __m128 m4 = _mm_max_ps(lo, hi);
  const __m128 m2 = _mm_max_ps(m4, _mm_movehl_ps(m4, m4));
  const __m128 m1 = _mm_max_ss(m2, _mm_shuffle_ps(m2, m2, 1));
  return _mm_cvtss_f32(m1);
}

MTRAN_OPS_AVX2 inline float hmin256(__m256 v) noexcept {
  const __m128 lo = _mm256_castps256_ps128(v);
  const __m128 hi = _mm256_extractf128_ps(v, 1);
  const __m128 m4 = _mm_min_ps(lo, hi);
  const __m128 m2 = _mm_min_ps(m4, _mm_movehl_ps(m4, m4));
  const __m128 m1 = _mm_min_ss(m2, _mm_shuffle_ps(m2, m2, 1));
  return _mm_cvtss_f32(m1);
}

MTRAN_OPS_AVX2 void addScaledEmbedAvx2(const float* embed, float scale,
                                            const float* pos_vec, std::size_t dim,
                                            float* out) noexcept {
  std::size_t j = 0;
  if (embed) {
    const __m256 vscale = _mm256_set1_ps(scale);
    for (; j + 8 <= dim; j += 8) {
      const __m256 ve = _mm256_loadu_ps(embed + j);
      const __m256 vp = _mm256_loadu_ps(pos_vec + j);
      _mm256_storeu_ps(out + j, _mm256_add_ps(_mm256_mul_ps(ve, vscale), vp));
    }
    for (; j < dim; ++j) out[j] = embed[j] * scale + pos_vec[j];
  } else {
    for (; j + 8 <= dim; j += 8) {
      _mm256_storeu_ps(out + j, _mm256_loadu_ps(pos_vec + j));
    }
    for (; j < dim; ++j) out[j] = pos_vec[j];
  }
}

MTRAN_OPS_AVX2 void addScaledEmbedInt8Avx2(const std::int8_t* embed_q8, float scale,
                                                const float* pos_vec, std::size_t dim,
                                                float* out) noexcept {
  std::size_t j = 0;
  if (embed_q8) {
    const __m256 vscale = _mm256_set1_ps(scale);
    for (; j + 8 <= dim; j += 8) {
      const __m128i q8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(embed_q8 + j));
      const __m256 qf = _mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(q8));
      const __m256 vp = _mm256_loadu_ps(pos_vec + j);
      _mm256_storeu_ps(out + j, _mm256_add_ps(_mm256_mul_ps(qf, vscale), vp));
    }
    for (; j < dim; ++j) out[j] = static_cast<float>(embed_q8[j]) * scale + pos_vec[j];
  } else {
    for (; j + 8 <= dim; j += 8) {
      _mm256_storeu_ps(out + j, _mm256_loadu_ps(pos_vec + j));
    }
    for (; j < dim; ++j) out[j] = pos_vec[j];
  }
}

MTRAN_OPS_AVX2 void addScaledEmbedInt8TwoStepAvx2(const std::int8_t* embed_q8,
                                                       float q_scale, float embed_scale,
                                                       const float* pos_vec, std::size_t dim,
                                                       float* out) noexcept {
  std::size_t j = 0;
  if (embed_q8) {
    const __m256 vqs = _mm256_set1_ps(q_scale);
    const __m256 ves = _mm256_set1_ps(embed_scale);
    for (; j + 8 <= dim; j += 8) {
      const __m128i q8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(embed_q8 + j));
      const __m256 qf = _mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(q8));
      const __m256 vp = _mm256_loadu_ps(pos_vec + j);
      _mm256_storeu_ps(out + j, _mm256_add_ps(_mm256_mul_ps(_mm256_mul_ps(qf, vqs), ves), vp));
    }
    for (; j < dim; ++j) out[j] = (static_cast<float>(embed_q8[j]) * q_scale) * embed_scale + pos_vec[j];
  } else {
    for (; j + 8 <= dim; j += 8) {
      _mm256_storeu_ps(out + j, _mm256_loadu_ps(pos_vec + j));
    }
    for (; j < dim; ++j) out[j] = pos_vec[j];
  }
}

MTRAN_OPS_AVX2 void instanceNormAffineAvx2(const float* x, const float* scale,
                                                const float* bias, std::size_t rows,
                                                std::size_t dim, float* out,
                                                float* out_mins, float* out_maxs) noexcept {
  const bool track_bounds = (out_mins != nullptr || out_maxs != nullptr);
  for (std::size_t r = 0; r < rows; ++r) {
    const float* xr = x + r * dim;
    float* yr = out + r * dim;

    __m256 vsum = _mm256_setzero_ps();
    std::size_t i = 0;
    for (; i + 8 <= dim; i += 8) {
      vsum = _mm256_add_ps(vsum, _mm256_loadu_ps(xr + i));
    }
    float sum = hsum256(vsum);
    for (; i < dim; ++i) sum += xr[i];
    const float mean = sum / static_cast<float>(dim);
    const __m256 vmean = _mm256_set1_ps(mean);

    __m256 vvar = _mm256_setzero_ps();
    i = 0;
    for (; i + 8 <= dim; i += 8) {
      const __m256 d = _mm256_sub_ps(_mm256_loadu_ps(xr + i), vmean);
      vvar = _mm256_add_ps(vvar, _mm256_mul_ps(d, d));
    }
    float var = hsum256(vvar);
    for (; i < dim; ++i) {
      const float d = xr[i] - mean;
      var += d * d;
    }
    const float inv_std = 1.0f / std::sqrt(var / static_cast<float>(dim) + 1e-6f);
    const float offset = -mean * inv_std;
    const __m256 vinv = _mm256_set1_ps(inv_std);
    const __m256 voff = _mm256_set1_ps(offset);

    __m256 vmin = _mm256_setzero_ps();
    __m256 vmax = _mm256_setzero_ps();
    i = 0;
    for (; i + 8 <= dim; i += 8) {
      const __m256 norm = _mm256_add_ps(_mm256_mul_ps(_mm256_loadu_ps(xr + i), vinv), voff);
      const __m256 y = _mm256_add_ps(_mm256_mul_ps(norm, _mm256_loadu_ps(scale + i)),
                                     _mm256_loadu_ps(bias + i));
      _mm256_storeu_ps(yr + i, y);
      if (track_bounds) {
        vmin = _mm256_min_ps(vmin, y);
        vmax = _mm256_max_ps(vmax, y);
      }
    }
    float rmin = track_bounds ? hmin256(vmin) : 0.0f;
    float rmax = track_bounds ? hmax256(vmax) : 0.0f;
    for (; i < dim; ++i) {
      const float norm = xr[i] * inv_std + offset;
      const float y = norm * scale[i] + bias[i];
      yr[i] = y;
      if (track_bounds) {
        rmin = std::min(rmin, y);
        rmax = std::max(rmax, y);
      }
    }
    if (out_mins) out_mins[r] = rmin;
    if (out_maxs) out_maxs[r] = rmax;
  }
}

MTRAN_OPS_AVX2 void ssruGateUpdateAvx2(const float* wf, const float* w, const float* x,
                                            std::size_t rows, std::size_t dim,
                                            float* cell, float* out) noexcept {
  const __m256 vone = _mm256_set1_ps(1.0f);
  const __m256 vzero = _mm256_setzero_ps();
  const std::size_t total = rows * dim;
  std::size_t i = 0;
  for (; i + 8 <= total; i += 8) {
    const __m256 vwf = _mm256_loadu_ps(wf + i);
    const __m256 neg = _mm256_sub_ps(vzero, vwf);
    const __m256 exp_neg = fastExpAvx2(neg);
    __m256 gate = _mm256_div_ps(vone, _mm256_add_ps(vone, exp_neg));
    const __m256 isnan_mask = _mm256_cmp_ps(vwf, vwf, _CMP_UNORD_Q);
    gate = _mm256_blendv_ps(gate, vwf, isnan_mask);

    const __m256 c_prev = _mm256_loadu_ps(cell + i);
    const __m256 vw = _mm256_loadu_ps(w + i);
    const __m256 c_next = _mm256_add_ps(_mm256_mul_ps(gate, c_prev),
                                       _mm256_mul_ps(_mm256_sub_ps(vone, gate), vw));
    _mm256_storeu_ps(cell + i, c_next);
    const __m256 relu_c = _mm256_max_ps(vzero, c_next);
    _mm256_storeu_ps(out + i, _mm256_add_ps(_mm256_loadu_ps(x + i), relu_c));
  }
  for (; i < total; ++i) {
    const float gate = 1.0f / (1.0f + std::exp(-wf[i]));
    const float c = gate * cell[i] + (1.0f - gate) * w[i];
    cell[i] = c;
    out[i] = x[i] + std::max(0.0f, c);
  }
}

MTRAN_OPS_AVX2_FMA inline __m256 mlasExpAvx2Fma(__m256 x) noexcept {
  const __m256 vlo = _mm256_set1_ps(-88.37626266f);
  const __m256 vhi = _mm256_set1_ps(88.37626266f);
  const __m256 vlog2e = _mm256_set1_ps(1.44269504088896341f);
  const __m256 vln2_hi = _mm256_set1_ps(-0.693145751953125f);
  const __m256 vln2_lo = _mm256_set1_ps(-1.42860682030941723e-6f);
  const __m256 vc0 = _mm256_set1_ps(1.9875691500e-4f);
  const __m256 vc1 = _mm256_set1_ps(1.3981999507e-3f);
  const __m256 vc2 = _mm256_set1_ps(8.3334519073e-3f);
  const __m256 vc3 = _mm256_set1_ps(4.1665795894e-2f);
  const __m256 vc4 = _mm256_set1_ps(1.6666665459e-1f);
  const __m256 vc5 = _mm256_set1_ps(5.0000001201e-1f);
  const __m256 vone = _mm256_set1_ps(1.0f);
  const __m256 z = _mm256_min_ps(_mm256_max_ps(x, vlo), vhi);
  const __m256 m =
      _mm256_round_ps(_mm256_mul_ps(z, vlog2e), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
  __m256 r = _mm256_fmadd_ps(m, vln2_hi, z);
  r = _mm256_fmadd_ps(m, vln2_lo, r);
  __m256 p = _mm256_fmadd_ps(vc0, r, vc1);
  p = _mm256_fmadd_ps(p, r, vc2);
  p = _mm256_fmadd_ps(p, r, vc3);
  p = _mm256_fmadd_ps(p, r, vc4);
  p = _mm256_fmadd_ps(p, r, vc5);
  p = _mm256_fmadd_ps(p, _mm256_mul_ps(r, r), r);
  p = _mm256_add_ps(p, vone);
  const __m256i pow2 = _mm256_slli_epi32(
      _mm256_add_epi32(_mm256_cvtps_epi32(m), _mm256_set1_epi32(127)), 23);
  return _mm256_mul_ps(p, _mm256_castsi256_ps(pow2));
}

MTRAN_OPS_AVX2_FMA void ssruGateUpdateAvx2Fma(const float* wf, const float* w, const float* x,
                                               std::size_t rows, std::size_t dim,
                                               float* cell, float* out) noexcept {
  const __m256 vone = _mm256_set1_ps(1.0f);
  const __m256 vzero = _mm256_setzero_ps();
  const std::size_t total = rows * dim;
  std::size_t i = 0;
  for (; i + 8 <= total; i += 8) {
    const __m256 vwf = _mm256_loadu_ps(wf + i);
    const __m256 exp_neg = mlasExpAvx2Fma(_mm256_sub_ps(vzero, vwf));
    __m256 gate = _mm256_div_ps(vone, _mm256_add_ps(vone, exp_neg));
    const __m256 isnan_mask = _mm256_cmp_ps(vwf, vwf, _CMP_UNORD_Q);
    gate = _mm256_blendv_ps(gate, vwf, isnan_mask);

    const __m256 c_prev = _mm256_loadu_ps(cell + i);
    const __m256 vw = _mm256_loadu_ps(w + i);
    const __m256 c_next = _mm256_add_ps(_mm256_mul_ps(gate, c_prev),
                                       _mm256_mul_ps(_mm256_sub_ps(vone, gate), vw));
    _mm256_storeu_ps(cell + i, c_next);
    const __m256 relu_c = _mm256_max_ps(vzero, c_next);
    _mm256_storeu_ps(out + i, _mm256_add_ps(_mm256_loadu_ps(x + i), relu_c));
  }
  for (; i < total; ++i) {
    const float gate = 1.0f / (1.0f + std::exp(-wf[i]));
    const float c = gate * cell[i] + (1.0f - gate) * w[i];
    cell[i] = c;
    out[i] = x[i] + std::max(0.0f, c);
  }
}

MTRAN_OPS_AVX2 void softmaxRowAvx2(float* scores, std::size_t len) noexcept {
  const float maximum = maxRowAvx2(scores, len);
  const __m256 vm = _mm256_set1_ps(maximum);
  std::size_t s = 0;
  for (; s + 8 <= len; s += 8) {
    _mm256_storeu_ps(scores + s, fastExpAvx2(_mm256_sub_ps(_mm256_loadu_ps(scores + s), vm)));
  }
  for (; s < len; ++s) {
    scores[s] = std::exp(scores[s] - maximum);
  }
  finishSoftmaxAvx2(scores, len);
}

MTRAN_OPS_AVX2_FMA void softmaxRowAvx2Fma(float* scores, std::size_t len) noexcept {
  const float maximum = maxRowAvx2(scores, len);
  const __m256 vm = _mm256_set1_ps(maximum);
  std::size_t s = 0;
  for (; s + 8 <= len; s += 8) {
    _mm256_storeu_ps(scores + s,
                     mlasExpAvx2Fma(_mm256_sub_ps(_mm256_loadu_ps(scores + s), vm)));
  }
  if (s < len) {
    alignas(32) float rem[8] = {};
    const std::size_t rem_n = len - s;
    for (std::size_t k = 0; k < rem_n; ++k) rem[k] = scores[s + k] - maximum;
    alignas(32) float out_rem[8];
    _mm256_store_ps(out_rem, mlasExpAvx2Fma(_mm256_load_ps(rem)));
    for (std::size_t k = 0; k < rem_n; ++k) scores[s + k] = out_rem[k];
  }
  finishSoftmaxAvx2(scores, len);
}

// ORT MatMul(M=1) uses 4-row unrolled non-FMA block sums + separate tail sum; keep target("avx2") without FMA
// so compilers never contract mul+add into FMA even when built without -ffp-contract=off.
MTRAN_OPS_AVX2 void crossAttentionAvx2(const float* q, const float* k_t, const float* v,
                                         std::size_t heads, std::size_t head_dim,
                                         std::size_t src_len, float* scores_scratch, float* out,
                                         std::size_t head_begin, std::size_t head_end,
                                         bool fma_softmax) noexcept {
  const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
  const __m256 vscale = _mm256_set1_ps(scale);

  for (std::size_t h = head_begin; h < head_end; ++h) {
    const float* qh = q + h * head_dim;
    const float* kh = k_t + h * head_dim * src_len;
    const float* vh = v + h * src_len * head_dim;
    float* oh = out + h * head_dim;

    std::size_t s = 0;
    for (; s + 16 <= src_len; s += 16) {
      __m256 a0 = _mm256_setzero_ps();
      __m256 a1 = _mm256_setzero_ps();
      std::size_t k = 0;
      for (; k + 4 <= head_dim; k += 4) {
        const __m256 q0 = _mm256_set1_ps(qh[k + 0]);
        const __m256 q1 = _mm256_set1_ps(qh[k + 1]);
        const __m256 q2 = _mm256_set1_ps(qh[k + 2]);
        const __m256 q3 = _mm256_set1_ps(qh[k + 3]);
        const float* kr0 = kh + (k + 0) * src_len + s;
        const float* kr1 = kh + (k + 1) * src_len + s;
        const float* kr2 = kh + (k + 2) * src_len + s;
        const float* kr3 = kh + (k + 3) * src_len + s;
        const __m256 b0 = _mm256_add_ps(
            _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(q0, _mm256_loadu_ps(kr0)),
                                        _mm256_mul_ps(q1, _mm256_loadu_ps(kr1))),
                          _mm256_mul_ps(q2, _mm256_loadu_ps(kr2))),
            _mm256_mul_ps(q3, _mm256_loadu_ps(kr3)));
        const __m256 b1 = _mm256_add_ps(
            _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(q0, _mm256_loadu_ps(kr0 + 8)),
                                        _mm256_mul_ps(q1, _mm256_loadu_ps(kr1 + 8))),
                          _mm256_mul_ps(q2, _mm256_loadu_ps(kr2 + 8))),
            _mm256_mul_ps(q3, _mm256_loadu_ps(kr3 + 8)));
        a0 = _mm256_add_ps(a0, b0);
        a1 = _mm256_add_ps(a1, b1);
      }
      if (k < head_dim) {
        __m256 t0 = _mm256_setzero_ps();
        __m256 t1 = _mm256_setzero_ps();
        for (; k < head_dim; ++k) {
          const __m256 qk = _mm256_set1_ps(qh[k]);
          const float* kr = kh + k * src_len + s;
          t0 = _mm256_add_ps(t0, _mm256_mul_ps(qk, _mm256_loadu_ps(kr)));
          t1 = _mm256_add_ps(t1, _mm256_mul_ps(qk, _mm256_loadu_ps(kr + 8)));
        }
        a0 = _mm256_add_ps(a0, t0);
        a1 = _mm256_add_ps(a1, t1);
      }
      _mm256_storeu_ps(scores_scratch + s, _mm256_mul_ps(a0, vscale));
      _mm256_storeu_ps(scores_scratch + s + 8, _mm256_mul_ps(a1, vscale));
    }
    for (; s + 8 <= src_len; s += 8) {
      __m256 a0 = _mm256_setzero_ps();
      std::size_t k = 0;
      for (; k + 4 <= head_dim; k += 4) {
        const __m256 p0 = _mm256_mul_ps(_mm256_set1_ps(qh[k + 0]),
                                        _mm256_loadu_ps(kh + (k + 0) * src_len + s));
        const __m256 p1 = _mm256_mul_ps(_mm256_set1_ps(qh[k + 1]),
                                        _mm256_loadu_ps(kh + (k + 1) * src_len + s));
        const __m256 p2 = _mm256_mul_ps(_mm256_set1_ps(qh[k + 2]),
                                        _mm256_loadu_ps(kh + (k + 2) * src_len + s));
        const __m256 p3 = _mm256_mul_ps(_mm256_set1_ps(qh[k + 3]),
                                        _mm256_loadu_ps(kh + (k + 3) * src_len + s));
        a0 = _mm256_add_ps(a0, _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(p0, p1), p2), p3));
      }
      if (k < head_dim) {
        __m256 tail = _mm256_setzero_ps();
        for (; k < head_dim; ++k) {
          tail = _mm256_add_ps(
              tail, _mm256_mul_ps(_mm256_set1_ps(qh[k]), _mm256_loadu_ps(kh + k * src_len + s)));
        }
        a0 = _mm256_add_ps(a0, tail);
      }
      _mm256_storeu_ps(scores_scratch + s, _mm256_mul_ps(a0, vscale));
    }
    for (; s < src_len; ++s) {
      float acc = 0.0f;
      std::size_t k = 0;
      for (; k + 4 <= head_dim; k += 4) {
        const float b = ((qh[k + 0] * kh[(k + 0) * src_len + s] +
                          qh[k + 1] * kh[(k + 1) * src_len + s]) +
                         qh[k + 2] * kh[(k + 2) * src_len + s]) +
                        qh[k + 3] * kh[(k + 3) * src_len + s];
        acc += b;
      }
      float tail = 0.0f;
      for (; k < head_dim; ++k) tail += qh[k] * kh[k * src_len + s];
      scores_scratch[s] = (acc + tail) * scale;
    }

    if (fma_softmax) {
      softmaxRowAvx2Fma(scores_scratch, src_len);
    } else {
      softmaxRowAvx2(scores_scratch, src_len);
    }

    if (head_dim == 48) {
      __m256 o0 = _mm256_setzero_ps(), o1 = _mm256_setzero_ps();
      __m256 o2 = _mm256_setzero_ps(), o3 = _mm256_setzero_ps();
      __m256 o4 = _mm256_setzero_ps(), o5 = _mm256_setzero_ps();
      std::size_t i = 0;
      for (; i + 4 <= src_len; i += 4) {
        const __m256 p0 = _mm256_set1_ps(scores_scratch[i + 0]);
        const __m256 p1 = _mm256_set1_ps(scores_scratch[i + 1]);
        const __m256 p2 = _mm256_set1_ps(scores_scratch[i + 2]);
        const __m256 p3 = _mm256_set1_ps(scores_scratch[i + 3]);
        const float* vr0 = vh + (i + 0) * 48;
        const float* vr1 = vh + (i + 1) * 48;
        const float* vr2 = vh + (i + 2) * 48;
        const float* vr3 = vh + (i + 3) * 48;
#define MTRAN_ACC4_PV(o, off)                                                                   \
  (o) = _mm256_add_ps(                                                                          \
      (o), _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(p0, _mm256_loadu_ps(vr0 + (off))), \
                                                     _mm256_mul_ps(p1, _mm256_loadu_ps(vr1 + (off)))), \
                                       _mm256_mul_ps(p2, _mm256_loadu_ps(vr2 + (off)))),        \
                         _mm256_mul_ps(p3, _mm256_loadu_ps(vr3 + (off)))))
        MTRAN_ACC4_PV(o0, 0);
        MTRAN_ACC4_PV(o1, 8);
        MTRAN_ACC4_PV(o2, 16);
        MTRAN_ACC4_PV(o3, 24);
        MTRAN_ACC4_PV(o4, 32);
        MTRAN_ACC4_PV(o5, 40);
#undef MTRAN_ACC4_PV
      }
      if (i < src_len) {
        __m256 t0 = _mm256_setzero_ps(), t1 = _mm256_setzero_ps();
        __m256 t2 = _mm256_setzero_ps(), t3 = _mm256_setzero_ps();
        __m256 t4 = _mm256_setzero_ps(), t5 = _mm256_setzero_ps();
        for (; i < src_len; ++i) {
          const __m256 p = _mm256_set1_ps(scores_scratch[i]);
          const float* vr = vh + i * 48;
          t0 = _mm256_add_ps(t0, _mm256_mul_ps(p, _mm256_loadu_ps(vr + 0)));
          t1 = _mm256_add_ps(t1, _mm256_mul_ps(p, _mm256_loadu_ps(vr + 8)));
          t2 = _mm256_add_ps(t2, _mm256_mul_ps(p, _mm256_loadu_ps(vr + 16)));
          t3 = _mm256_add_ps(t3, _mm256_mul_ps(p, _mm256_loadu_ps(vr + 24)));
          t4 = _mm256_add_ps(t4, _mm256_mul_ps(p, _mm256_loadu_ps(vr + 32)));
          t5 = _mm256_add_ps(t5, _mm256_mul_ps(p, _mm256_loadu_ps(vr + 40)));
        }
        o0 = _mm256_add_ps(o0, t0);
        o1 = _mm256_add_ps(o1, t1);
        o2 = _mm256_add_ps(o2, t2);
        o3 = _mm256_add_ps(o3, t3);
        o4 = _mm256_add_ps(o4, t4);
        o5 = _mm256_add_ps(o5, t5);
      }
      _mm256_storeu_ps(oh + 0, o0);
      _mm256_storeu_ps(oh + 8, o1);
      _mm256_storeu_ps(oh + 16, o2);
      _mm256_storeu_ps(oh + 24, o3);
      _mm256_storeu_ps(oh + 32, o4);
      _mm256_storeu_ps(oh + 40, o5);
    } else {
      std::size_t k = 0;
      for (; k + 8 <= head_dim; k += 8) {
        __m256 o = _mm256_setzero_ps();
        std::size_t i = 0;
        for (; i + 4 <= src_len; i += 4) {
          const __m256 p0 = _mm256_set1_ps(scores_scratch[i + 0]);
          const __m256 p1 = _mm256_set1_ps(scores_scratch[i + 1]);
          const __m256 p2 = _mm256_set1_ps(scores_scratch[i + 2]);
          const __m256 p3 = _mm256_set1_ps(scores_scratch[i + 3]);
          const __m256 b = _mm256_add_ps(
              _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(p0, _mm256_loadu_ps(vh + (i + 0) * head_dim + k)),
                                          _mm256_mul_ps(p1, _mm256_loadu_ps(vh + (i + 1) * head_dim + k))),
                            _mm256_mul_ps(p2, _mm256_loadu_ps(vh + (i + 2) * head_dim + k))),
              _mm256_mul_ps(p3, _mm256_loadu_ps(vh + (i + 3) * head_dim + k)));
          o = _mm256_add_ps(o, b);
        }
        __m256 tail = _mm256_setzero_ps();
        for (; i < src_len; ++i) {
          tail = _mm256_add_ps(
              tail, _mm256_mul_ps(_mm256_set1_ps(scores_scratch[i]),
                                  _mm256_loadu_ps(vh + i * head_dim + k)));
        }
        _mm256_storeu_ps(oh + k, _mm256_add_ps(o, tail));
      }
      for (; k < head_dim; ++k) {
        float sum = 0.0f;
        std::size_t i = 0;
        for (; i + 4 <= src_len; i += 4) {
          const float b = ((scores_scratch[i + 0] * vh[(i + 0) * head_dim + k] +
                            scores_scratch[i + 1] * vh[(i + 1) * head_dim + k]) +
                           scores_scratch[i + 2] * vh[(i + 2) * head_dim + k]) +
                          scores_scratch[i + 3] * vh[(i + 3) * head_dim + k];
          sum += b;
        }
        float tail = 0.0f;
        for (; i < src_len; ++i) tail += scores_scratch[i] * vh[i * head_dim + k];
        oh[k] = sum + tail;
      }
    }
  }
  (void)heads;
}

#define MTRAN_NOFMA_256(a, b, c) _mm256_add_ps(_mm256_mul_ps((a), (b)), (c))
#define MTRAN_NOFMA_SCALAR(a, b, c) ((a) * (b) + (c))
#define MTRAN_DEFINE_SELF_ATTN(FN_NAME, ATTR, MADD, SCALAR_MADD, SOFTMAX_FN)                  \
  ATTR void FN_NAME(const float* q, const float* k, const float* v, std::size_t qkv_stride,   \
                    std::size_t seq_len, std::size_t heads, std::size_t head_dim,             \
                    float* scores_scratch, float* out, std::size_t head_begin,                \
                    std::size_t head_end) noexcept {                                          \
    const std::size_t out_stride = heads * head_dim;                                          \
    const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));   \
    const __m256 vscale = _mm256_set1_ps(scale);                                              \
                                                                                              \
    for (std::size_t h = head_begin; h < head_end; ++h) {                                     \
      const std::size_t h_off = h * head_dim;                                                 \
      float* sc0 = scores_scratch + h * seq_len;                                              \
                                                                                              \
      if (head_dim == 48 && seq_len <= 128) {                                                 \
        const std::size_t s_pad = (seq_len + 15) & ~std::size_t(15);                          \
        alignas(32) float k_t[48 * 128];                                                      \
        alignas(32) float s0_buf[128];                                                        \
        alignas(32) float s1_buf[128];                                                        \
        for (std::size_t s = 0; s < seq_len; ++s) {                                           \
          const float* ks = k + s * qkv_stride + h_off;                                       \
          for (std::size_t d = 0; d < 48; ++d) k_t[d * s_pad + s] = ks[d];                    \
        }                                                                                     \
        if (seq_len < s_pad) {                                                                \
          for (std::size_t d = 0; d < 48; ++d) {                                              \
            for (std::size_t s = seq_len; s < s_pad; ++s) k_t[d * s_pad + s] = 0.0f;          \
          }                                                                                   \
        }                                                                                     \
                                                                                              \
        std::size_t t = 0;                                                                    \
        for (; t + 1 < seq_len; t += 2) {                                                     \
          const float* qt0 = q + t * qkv_stride + h_off;                                      \
          const float* qt1 = q + (t + 1) * qkv_stride + h_off;                                \
          for (std::size_t s = 0; s < s_pad; s += 16) {                                       \
            __m256 a00 = _mm256_setzero_ps(), a01 = _mm256_setzero_ps();                      \
            __m256 a10 = _mm256_setzero_ps(), a11 = _mm256_setzero_ps();                      \
            for (std::size_t d = 0; d < 48; d += 4) {                                         \
              const float* kr0 = k_t + (d + 0) * s_pad + s;                                   \
              const float* kr1 = k_t + (d + 1) * s_pad + s;                                   \
              const float* kr2 = k_t + (d + 2) * s_pad + s;                                   \
              const float* kr3 = k_t + (d + 3) * s_pad + s;                                   \
              const __m256 q0_0 = _mm256_set1_ps(qt0[d + 0]);                                 \
              const __m256 q1_0 = _mm256_set1_ps(qt1[d + 0]);                                 \
              a00 = MADD(q0_0, _mm256_load_ps(kr0), a00);                                     \
              a01 = MADD(q0_0, _mm256_load_ps(kr0 + 8), a01);                                 \
              a10 = MADD(q1_0, _mm256_load_ps(kr0), a10);                                     \
              a11 = MADD(q1_0, _mm256_load_ps(kr0 + 8), a11);                                 \
              const __m256 q0_1 = _mm256_set1_ps(qt0[d + 1]);                                 \
              const __m256 q1_1 = _mm256_set1_ps(qt1[d + 1]);                                 \
              a00 = MADD(q0_1, _mm256_load_ps(kr1), a00);                                     \
              a01 = MADD(q0_1, _mm256_load_ps(kr1 + 8), a01);                                 \
              a10 = MADD(q1_1, _mm256_load_ps(kr1), a10);                                     \
              a11 = MADD(q1_1, _mm256_load_ps(kr1 + 8), a11);                                 \
              const __m256 q0_2 = _mm256_set1_ps(qt0[d + 2]);                                 \
              const __m256 q1_2 = _mm256_set1_ps(qt1[d + 2]);                                 \
              a00 = MADD(q0_2, _mm256_load_ps(kr2), a00);                                     \
              a01 = MADD(q0_2, _mm256_load_ps(kr2 + 8), a01);                                 \
              a10 = MADD(q1_2, _mm256_load_ps(kr2), a10);                                     \
              a11 = MADD(q1_2, _mm256_load_ps(kr2 + 8), a11);                                 \
              const __m256 q0_3 = _mm256_set1_ps(qt0[d + 3]);                                 \
              const __m256 q1_3 = _mm256_set1_ps(qt1[d + 3]);                                 \
              a00 = MADD(q0_3, _mm256_load_ps(kr3), a00);                                     \
              a01 = MADD(q0_3, _mm256_load_ps(kr3 + 8), a01);                                 \
              a10 = MADD(q1_3, _mm256_load_ps(kr3), a10);                                     \
              a11 = MADD(q1_3, _mm256_load_ps(kr3 + 8), a11);                                 \
            }                                                                                 \
            _mm256_store_ps(s0_buf + s, _mm256_mul_ps(a00, vscale));                          \
            _mm256_store_ps(s0_buf + s + 8, _mm256_mul_ps(a01, vscale));                      \
            _mm256_store_ps(s1_buf + s, _mm256_mul_ps(a10, vscale));                          \
            _mm256_store_ps(s1_buf + s + 8, _mm256_mul_ps(a11, vscale));                      \
          }                                                                                   \
          SOFTMAX_FN(s0_buf, seq_len);                                                        \
          SOFTMAX_FN(s1_buf, seq_len);                                                        \
                                                                                              \
          __m256 o0_0 = _mm256_setzero_ps(), o0_1 = _mm256_setzero_ps(),                      \
                 o0_2 = _mm256_setzero_ps();                                                  \
          __m256 o0_3 = _mm256_setzero_ps(), o0_4 = _mm256_setzero_ps(),                      \
                 o0_5 = _mm256_setzero_ps();                                                  \
          __m256 o1_0 = _mm256_setzero_ps(), o1_1 = _mm256_setzero_ps(),                      \
                 o1_2 = _mm256_setzero_ps();                                                  \
          __m256 o1_3 = _mm256_setzero_ps(), o1_4 = _mm256_setzero_ps(),                      \
                 o1_5 = _mm256_setzero_ps();                                                  \
          for (std::size_t s = 0; s < seq_len; ++s) {                                         \
            const __m256 p0 = _mm256_set1_ps(s0_buf[s]);                                      \
            const __m256 p1 = _mm256_set1_ps(s1_buf[s]);                                      \
            const float* vs = v + s * qkv_stride + h_off;                                     \
            const __m256 v0 = _mm256_loadu_ps(vs + 0), v1 = _mm256_loadu_ps(vs + 8);          \
            const __m256 v2 = _mm256_loadu_ps(vs + 16), v3 = _mm256_loadu_ps(vs + 24);        \
            const __m256 v4 = _mm256_loadu_ps(vs + 32), v5 = _mm256_loadu_ps(vs + 40);        \
            o0_0 = MADD(p0, v0, o0_0);                                                        \
            o1_0 = MADD(p1, v0, o1_0);                                                        \
            o0_1 = MADD(p0, v1, o0_1);                                                        \
            o1_1 = MADD(p1, v1, o1_1);                                                        \
            o0_2 = MADD(p0, v2, o0_2);                                                        \
            o1_2 = MADD(p1, v2, o1_2);                                                        \
            o0_3 = MADD(p0, v3, o0_3);                                                        \
            o1_3 = MADD(p1, v3, o1_3);                                                        \
            o0_4 = MADD(p0, v4, o0_4);                                                        \
            o1_4 = MADD(p1, v4, o1_4);                                                        \
            o0_5 = MADD(p0, v5, o0_5);                                                        \
            o1_5 = MADD(p1, v5, o1_5);                                                        \
          }                                                                                   \
          float* ot0 = out + t * out_stride + h_off;                                          \
          float* ot1 = out + (t + 1) * out_stride + h_off;                                    \
          _mm256_storeu_ps(ot0 + 0, o0_0);                                                    \
          _mm256_storeu_ps(ot0 + 8, o0_1);                                                    \
          _mm256_storeu_ps(ot0 + 16, o0_2);                                                   \
          _mm256_storeu_ps(ot0 + 24, o0_3);                                                   \
          _mm256_storeu_ps(ot0 + 32, o0_4);                                                   \
          _mm256_storeu_ps(ot0 + 40, o0_5);                                                   \
          _mm256_storeu_ps(ot1 + 0, o1_0);                                                    \
          _mm256_storeu_ps(ot1 + 8, o1_1);                                                    \
          _mm256_storeu_ps(ot1 + 16, o1_2);                                                   \
          _mm256_storeu_ps(ot1 + 24, o1_3);                                                   \
          _mm256_storeu_ps(ot1 + 32, o1_4);                                                   \
          _mm256_storeu_ps(ot1 + 40, o1_5);                                                   \
        }                                                                                     \
        if (t < seq_len) {                                                                    \
          const float* qt = q + t * qkv_stride + h_off;                                       \
          for (std::size_t s = 0; s < s_pad; s += 16) {                                       \
            __m256 a0 = _mm256_setzero_ps(), a1 = _mm256_setzero_ps();                        \
            for (std::size_t d = 0; d < 48; d += 4) {                                         \
              const __m256 q0 = _mm256_set1_ps(qt[d + 0]);                                    \
              const __m256 q1 = _mm256_set1_ps(qt[d + 1]);                                    \
              const __m256 q2 = _mm256_set1_ps(qt[d + 2]);                                    \
              const __m256 q3 = _mm256_set1_ps(qt[d + 3]);                                    \
              const float* kr0 = k_t + (d + 0) * s_pad + s;                                   \
              const float* kr1 = k_t + (d + 1) * s_pad + s;                                   \
              const float* kr2 = k_t + (d + 2) * s_pad + s;                                   \
              const float* kr3 = k_t + (d + 3) * s_pad + s;                                   \
              a0 = MADD(q0, _mm256_load_ps(kr0), a0);                                         \
              a1 = MADD(q0, _mm256_load_ps(kr0 + 8), a1);                                     \
              a0 = MADD(q1, _mm256_load_ps(kr1), a0);                                         \
              a1 = MADD(q1, _mm256_load_ps(kr1 + 8), a1);                                     \
              a0 = MADD(q2, _mm256_load_ps(kr2), a0);                                         \
              a1 = MADD(q2, _mm256_load_ps(kr2 + 8), a1);                                     \
              a0 = MADD(q3, _mm256_load_ps(kr3), a0);                                         \
              a1 = MADD(q3, _mm256_load_ps(kr3 + 8), a1);                                     \
            }                                                                                 \
            _mm256_store_ps(s0_buf + s, _mm256_mul_ps(a0, vscale));                           \
            _mm256_store_ps(s0_buf + s + 8, _mm256_mul_ps(a1, vscale));                       \
          }                                                                                   \
          SOFTMAX_FN(s0_buf, seq_len);                                                        \
          __m256 o0 = _mm256_setzero_ps(), o1 = _mm256_setzero_ps(), o2 = _mm256_setzero_ps();  \
          __m256 o3 = _mm256_setzero_ps(), o4 = _mm256_setzero_ps(), o5 = _mm256_setzero_ps();  \
          for (std::size_t s = 0; s < seq_len; ++s) {                                         \
            const __m256 p = _mm256_set1_ps(s0_buf[s]);                                       \
            const float* vs = v + s * qkv_stride + h_off;                                     \
            o0 = MADD(p, _mm256_loadu_ps(vs + 0), o0);                                        \
            o1 = MADD(p, _mm256_loadu_ps(vs + 8), o1);                                        \
            o2 = MADD(p, _mm256_loadu_ps(vs + 16), o2);                                       \
            o3 = MADD(p, _mm256_loadu_ps(vs + 24), o3);                                       \
            o4 = MADD(p, _mm256_loadu_ps(vs + 32), o4);                                       \
            o5 = MADD(p, _mm256_loadu_ps(vs + 40), o5);                                       \
          }                                                                                   \
          float* ot = out + t * out_stride + h_off;                                           \
          _mm256_storeu_ps(ot + 0, o0);                                                       \
          _mm256_storeu_ps(ot + 8, o1);                                                       \
          _mm256_storeu_ps(ot + 16, o2);                                                      \
          _mm256_storeu_ps(ot + 24, o3);                                                      \
          _mm256_storeu_ps(ot + 32, o4);                                                      \
          _mm256_storeu_ps(ot + 40, o5);                                                      \
        }                                                                                     \
      } else {                                                                                \
        for (std::size_t t = 0; t < seq_len; ++t) {                                           \
          const float* qt = q + t * qkv_stride + h_off;                                       \
          for (std::size_t s = 0; s < seq_len; ++s) {                                         \
            const float* ks = k + s * qkv_stride + h_off;                                     \
            float sum = 0.0f;                                                                 \
            for (std::size_t d_i = 0; d_i < head_dim; ++d_i) {                                \
              sum = SCALAR_MADD(qt[d_i], ks[d_i], sum);                                       \
            }                                                                                 \
            sc0[s] = sum * scale;                                                             \
          }                                                                                   \
          SOFTMAX_FN(sc0, seq_len);                                                           \
          float* ot = out + t * out_stride + h_off;                                           \
          std::size_t d_i = 0;                                                                \
          for (; d_i + 8 <= head_dim; d_i += 8) {                                             \
            __m256 o = _mm256_setzero_ps();                                                   \
            for (std::size_t s = 0; s < seq_len; ++s) {                                       \
              o = MADD(_mm256_set1_ps(sc0[s]),                                                \
                       _mm256_loadu_ps(v + s * qkv_stride + h_off + d_i), o);                 \
            }                                                                                 \
            _mm256_storeu_ps(ot + d_i, o);                                                    \
          }                                                                                   \
          for (; d_i < head_dim; ++d_i) {                                                     \
            float sum = 0.0f;                                                                 \
            for (std::size_t s = 0; s < seq_len; ++s) {                                       \
              sum = SCALAR_MADD(sc0[s], v[s * qkv_stride + h_off + d_i], sum);                \
            }                                                                                 \
            ot[d_i] = sum;                                                                    \
          }                                                                                   \
        }                                                                                     \
      }                                                                                       \
    }                                                                                         \
  }

MTRAN_DEFINE_SELF_ATTN(selfAttentionAvx2, MTRAN_OPS_AVX2, MTRAN_NOFMA_256, MTRAN_NOFMA_SCALAR,
                       softmaxRowAvx2)
MTRAN_DEFINE_SELF_ATTN(selfAttentionAvx2Fma, MTRAN_OPS_AVX2_FMA, _mm256_fmadd_ps, std::fma,
                       softmaxRowAvx2Fma)
#undef MTRAN_DEFINE_SELF_ATTN
#undef MTRAN_NOFMA_256
#undef MTRAN_NOFMA_SCALAR

#elif defined(__aarch64__) || defined(_M_ARM64)

inline float32x4_t fastExpNeon(float32x4_t x) noexcept {
  const float32x4_t vlo = vdupq_n_f32(-87.33654475f);
  const float32x4_t vhi = vdupq_n_f32(88.37626266f);
  const float32x4_t vlog2e = vdupq_n_f32(1.44269504088896341f);
  const float32x4_t vln2_hi = vdupq_n_f32(0.693145751953125f);
  const float32x4_t vln2_lo = vdupq_n_f32(1.42860682030941723e-6f);
  const float32x4_t vone = vdupq_n_f32(1.0f);
  const int32x4_t vbias = vdupq_n_s32(127);

  const float32x4_t z = vminq_f32(vmaxq_f32(x, vlo), vhi);
  const float32x4_t k = vrndnq_f32(vmulq_f32(z, vlog2e));
  float32x4_t r = vfmsq_f32(z, k, vln2_hi);
  r = vfmsq_f32(r, k, vln2_lo);

  float32x4_t p = vfmaq_f32(vdupq_n_f32(1.3981999507e-3f), vdupq_n_f32(1.9875691500e-4f), r);
  p = vfmaq_f32(vdupq_n_f32(8.3334519073e-3f), p, r);
  p = vfmaq_f32(vdupq_n_f32(4.1665795894e-2f), p, r);
  p = vfmaq_f32(vdupq_n_f32(1.6666665459e-1f), p, r);
  p = vfmaq_f32(vdupq_n_f32(5.0000001201e-1f), p, r);
  const float32x4_t r2 = vmulq_f32(r, r);
  const float32x4_t y = vaddq_f32(vfmaq_f32(r, p, r2), vone);

  const int32x4_t ki = vcvtnq_s32_f32(k);
  const float32x4_t pow2k = vreinterpretq_f32_s32(vshlq_n_s32(vaddq_s32(ki, vbias), 23));
  return vmulq_f32(y, pow2k);
}

void softmaxRowNeon(float* scores, std::size_t len) noexcept {
  float32x4_t vmax = vdupq_n_f32(scores[0]);
  std::size_t s = 0;
  for (; s + 4 <= len; s += 4) vmax = vmaxq_f32(vmax, vld1q_f32(scores + s));
  float maximum = vmaxvq_f32(vmax);
  for (; s < len; ++s) maximum = std::max(maximum, scores[s]);

  const float32x4_t vm = vdupq_n_f32(maximum);
  float32x4_t vsum = vdupq_n_f32(0.0f);
  s = 0;
  for (; s + 4 <= len; s += 4) {
    const float32x4_t e = fastExpNeon(vsubq_f32(vld1q_f32(scores + s), vm));
    vst1q_f32(scores + s, e);
    vsum = vaddq_f32(vsum, e);
  }
  float total = vaddvq_f32(vsum);
  for (; s < len; ++s) {
    const float e = std::exp(scores[s] - maximum);
    scores[s] = e;
    total += e;
  }

  const float32x4_t vtot = vdupq_n_f32(total);
  s = 0;
  for (; s + 4 <= len; s += 4) vst1q_f32(scores + s, vdivq_f32(vld1q_f32(scores + s), vtot));
  for (; s < len; ++s) scores[s] /= total;
}

#endif

}  // namespace

PosTable PosTable::create(const float* freq, const float* off,
                          std::size_t dim, std::size_t max_pos) {
  if (!freq || !off || !dim) throw std::invalid_argument("Invalid PosTable arguments");
  PosTable pt;
  pt.dim = dim;
  pt.max_pos = max_pos;
  pt.frequency.assign(freq, freq + dim);
  pt.offset.assign(off, off + dim);
  pt.table.resize(max_pos * dim);
  for (std::size_t p = 0; p < max_pos; ++p) {
    const float fp = static_cast<float>(p);
    float* dst = pt.table.data() + p * dim;
    for (std::size_t j = 0; j < dim; ++j) {
      dst[j] = std::sin(fp * freq[j] + off[j]);
    }
  }
  return pt;
}

void PosTable::addScaledEmbed(const float* embed, float scale,
                              std::size_t pos, float* out) const noexcept {
  if (pos < max_pos) {
    const float* pos_vec = table.data() + pos * dim;
#if defined(__x86_64__) || defined(_M_X64)
    if (hasAvx2()) {
      addScaledEmbedAvx2(embed, scale, pos_vec, dim, out);
      return;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    std::size_t j = 0;
    if (embed) {
      const float32x4_t vscale = vdupq_n_f32(scale);
      for (; j + 4 <= dim; j += 4) {
        vst1q_f32(out + j, vaddq_f32(vmulq_f32(vld1q_f32(embed + j), vscale), vld1q_f32(pos_vec + j)));
      }
      for (; j < dim; ++j) out[j] = embed[j] * scale + pos_vec[j];
    } else {
      for (; j + 4 <= dim; j += 4) vst1q_f32(out + j, vld1q_f32(pos_vec + j));
      for (; j < dim; ++j) out[j] = pos_vec[j];
    }
    return;
#endif
#if !defined(__aarch64__) && !defined(_M_ARM64)
    if (embed) {
      for (std::size_t j = 0; j < dim; ++j) out[j] = embed[j] * scale + pos_vec[j];
    } else {
      std::copy_n(pos_vec, dim, out);
    }
    return;
#endif
  }

  const float fp = static_cast<float>(pos);
  if (embed) {
    for (std::size_t j = 0; j < dim; ++j) {
      out[j] = embed[j] * scale + std::sin(fp * frequency[j] + offset[j]);
    }
  } else {
    for (std::size_t j = 0; j < dim; ++j) {
      out[j] = std::sin(fp * frequency[j] + offset[j]);
    }
  }
}

void PosTable::addScaledEmbedInt8(const std::int8_t* embed_q8, float combined_scale,
                                  std::size_t pos, float* out) const noexcept {
  if (pos < max_pos) {
    const float* pos_vec = table.data() + pos * dim;
#if defined(__x86_64__) || defined(_M_X64)
    if (hasAvx2()) {
      addScaledEmbedInt8Avx2(embed_q8, combined_scale, pos_vec, dim, out);
      return;
    }
#endif
    if (embed_q8) {
      for (std::size_t j = 0; j < dim; ++j) {
        out[j] = static_cast<float>(embed_q8[j]) * combined_scale + pos_vec[j];
      }
    } else {
      std::copy_n(pos_vec, dim, out);
    }
    return;
  }

  const float fp = static_cast<float>(pos);
  if (embed_q8) {
    for (std::size_t j = 0; j < dim; ++j) {
      out[j] = static_cast<float>(embed_q8[j]) * combined_scale + std::sin(fp * frequency[j] + offset[j]);
    }
  } else {
    for (std::size_t j = 0; j < dim; ++j) {
      out[j] = std::sin(fp * frequency[j] + offset[j]);
    }
  }
}

void PosTable::addScaledEmbedInt8(const std::int8_t* embed_q8, float q_scale, float embed_scale,
                                  std::size_t pos, float* out) const noexcept {
  if (pos < max_pos) {
    const float* pos_vec = table.data() + pos * dim;
#if defined(__x86_64__) || defined(_M_X64)
    if (hasAvx2()) {
      addScaledEmbedInt8TwoStepAvx2(embed_q8, q_scale, embed_scale, pos_vec, dim, out);
      return;
    }
#endif
    if (embed_q8) {
      for (std::size_t j = 0; j < dim; ++j) {
        out[j] = (static_cast<float>(embed_q8[j]) * q_scale) * embed_scale + pos_vec[j];
      }
    } else {
      std::copy_n(pos_vec, dim, out);
    }
    return;
  }

  const float fp = static_cast<float>(pos);
  if (embed_q8) {
    for (std::size_t j = 0; j < dim; ++j) {
      out[j] = (static_cast<float>(embed_q8[j]) * q_scale) * embed_scale + std::sin(fp * frequency[j] + offset[j]);
    }
  } else {
    for (std::size_t j = 0; j < dim; ++j) {
      out[j] = std::sin(fp * frequency[j] + offset[j]);
    }
  }
}

void instanceNormAffine(const float* x, const float* scale, const float* bias,
                        std::size_t rows, std::size_t dim, float* out,
                        float* out_mins, float* out_maxs) noexcept {
  if (!x || !scale || !bias || !out || !rows || !dim) return;
#if defined(__x86_64__) || defined(_M_X64)
  if (hasAvx2()) {
    instanceNormAffineAvx2(x, scale, bias, rows, dim, out, out_mins, out_maxs);
    return;
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  const float inv_dim = 1.0f / static_cast<float>(dim);
  const bool track = (out_mins != nullptr || out_maxs != nullptr);
  for (std::size_t r = 0; r < rows; ++r) {
    const float* xr = x + r * dim;
    float* yr = out + r * dim;
    float32x4_t vsum = vdupq_n_f32(0.0f);
    std::size_t i = 0;
    for (; i + 4 <= dim; i += 4) vsum = vaddq_f32(vsum, vld1q_f32(xr + i));
    float sum = vaddvq_f32(vsum);
    for (; i < dim; ++i) sum += xr[i];
    const float mean = sum * inv_dim;
    const float32x4_t vmean = vdupq_n_f32(mean);

    float32x4_t vvar = vdupq_n_f32(0.0f);
    i = 0;
    for (; i + 4 <= dim; i += 4) {
      const float32x4_t d = vsubq_f32(vld1q_f32(xr + i), vmean);
      vvar = vaddq_f32(vvar, vmulq_f32(d, d));
    }
    float var = vaddvq_f32(vvar);
    for (; i < dim; ++i) {
      const float d = xr[i] - mean;
      var += d * d;
    }
    const float inv_std = 1.0f / std::sqrt(var * inv_dim + 1e-6f);
    const float32x4_t vinv = vdupq_n_f32(inv_std);
    float32x4_t vmin = vdupq_n_f32(0.0f), vmax = vdupq_n_f32(0.0f);
    i = 0;
    for (; i + 4 <= dim; i += 4) {
      const float32x4_t norm = vmulq_f32(vsubq_f32(vld1q_f32(xr + i), vmean), vinv);
      const float32x4_t y = vaddq_f32(vmulq_f32(norm, vld1q_f32(scale + i)), vld1q_f32(bias + i));
      vst1q_f32(yr + i, y);
      if (track) {
        vmin = vminq_f32(vmin, y);
        vmax = vmaxq_f32(vmax, y);
      }
    }
    float rmin = track ? vminvq_f32(vmin) : 0.0f;
    float rmax = track ? vmaxvq_f32(vmax) : 0.0f;
    for (; i < dim; ++i) {
      const float y = ((xr[i] - mean) * inv_std) * scale[i] + bias[i];
      yr[i] = y;
      if (track) {
        rmin = std::min(rmin, y);
        rmax = std::max(rmax, y);
      }
    }
    if (out_mins) out_mins[r] = rmin;
    if (out_maxs) out_maxs[r] = rmax;
  }
  return;
#endif
#if !defined(__aarch64__) && !defined(_M_ARM64)
  const float inv_dim = 1.0f / static_cast<float>(dim);
  for (std::size_t r = 0; r < rows; ++r) {
    const float* xr = x + r * dim;
    float* yr = out + r * dim;
    float mean = 0.0f;
    for (std::size_t i = 0; i < dim; ++i) mean += xr[i];
    mean *= inv_dim;
    float var = 0.0f;
    for (std::size_t i = 0; i < dim; ++i) {
      const float d = xr[i] - mean;
      var += d * d;
    }
    const float inv_std = 1.0f / std::sqrt(var * inv_dim + 1e-6f);
    float rmin = 0.0f, rmax = 0.0f;
    for (std::size_t i = 0; i < dim; ++i) {
      const float y = ((xr[i] - mean) * inv_std) * scale[i] + bias[i];
      yr[i] = y;
      rmin = std::min(rmin, y);
      rmax = std::max(rmax, y);
    }
    if (out_mins) out_mins[r] = rmin;
    if (out_maxs) out_maxs[r] = rmax;
  }
#endif
}

void ssruGateUpdate(const float* wf, const float* w, const float* x,
                    std::size_t rows, std::size_t dim, float* cell, float* out) noexcept {
  if (!wf || !w || !x || !cell || !out || !rows || !dim) return;
#if defined(__x86_64__) || defined(_M_X64)
  if (hasAvx2Fma()) {
    ssruGateUpdateAvx2Fma(wf, w, x, rows, dim, cell, out);
    return;
  }
  if (hasAvx2()) {
    ssruGateUpdateAvx2(wf, w, x, rows, dim, cell, out);
    return;
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  const float32x4_t vone = vdupq_n_f32(1.0f);
  const float32x4_t vzero = vdupq_n_f32(0.0f);
  const std::size_t total = rows * dim;
  std::size_t i = 0;
  for (; i + 4 <= total; i += 4) {
    const float32x4_t vwf = vld1q_f32(wf + i);
    const float32x4_t exp_neg = fastExpNeon(vnegq_f32(vwf));
    float32x4_t gate = vdivq_f32(vone, vaddq_f32(vone, exp_neg));
    gate = vbslq_f32(vceqq_f32(vwf, vwf), gate, vwf);
    const float32x4_t c_next = vaddq_f32(vmulq_f32(gate, vld1q_f32(cell + i)),
                                         vmulq_f32(vsubq_f32(vone, gate), vld1q_f32(w + i)));
    vst1q_f32(cell + i, c_next);
    vst1q_f32(out + i, vaddq_f32(vld1q_f32(x + i), vmaxq_f32(vzero, c_next)));
  }
  for (; i < total; ++i) {
    const float gate = 1.0f / (1.0f + std::exp(-wf[i]));
    const float c = gate * cell[i] + (1.0f - gate) * w[i];
    cell[i] = c;
    out[i] = x[i] + std::max(0.0f, c);
  }
  return;
#endif
#if !defined(__aarch64__) && !defined(_M_ARM64)
  const std::size_t total = rows * dim;
  for (std::size_t i = 0; i < total; ++i) {
    const float gate = 1.0f / (1.0f + std::exp(-wf[i]));
    const float c = gate * cell[i] + (1.0f - gate) * w[i];
    cell[i] = c;
    out[i] = x[i] + std::max(0.0f, c);
  }
#endif
}

void crossAttention(const float* q, const float* k_t, const float* v,
                    std::size_t heads, std::size_t head_dim, std::size_t src_len,
                    float* scores_scratch, float* out,
                    std::size_t head_begin, std::size_t head_end) noexcept {
  if (!q || !k_t || !v || !scores_scratch || !out || !heads || !head_dim || !src_len) return;
  if (head_end > heads) head_end = heads;
  if (head_begin >= head_end) return;
#if defined(__x86_64__) || defined(_M_X64)
  if (hasAvx2()) {
    crossAttentionAvx2(q, k_t, v, heads, head_dim, src_len, scores_scratch, out, head_begin,
                       head_end, hasAvx2Fma());
    return;
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
  const float32x4_t vscale = vdupq_n_f32(scale);
  for (std::size_t h = head_begin; h < head_end; ++h) {
    const float* qh = q + h * head_dim;
    const float* kh = k_t + h * head_dim * src_len;
    const float* vh = v + h * src_len * head_dim;
    float* oh = out + h * head_dim;
    std::size_t s = 0;
    for (; s + 4 <= src_len; s += 4) {
      float32x4_t acc = vdupq_n_f32(0.0f);
      for (std::size_t k = 0; k < head_dim; ++k) {
        acc = vfmaq_f32(acc, vdupq_n_f32(qh[k]), vld1q_f32(kh + k * src_len + s));
      }
      vst1q_f32(scores_scratch + s, vmulq_f32(acc, vscale));
    }
    for (; s < src_len; ++s) {
      float acc = 0.0f;
      for (std::size_t k = 0; k < head_dim; ++k) acc = std::fma(qh[k], kh[k * src_len + s], acc);
      scores_scratch[s] = acc * scale;
    }
    softmaxRowNeon(scores_scratch, src_len);
    std::size_t k = 0;
    for (; k + 4 <= head_dim; k += 4) {
      float32x4_t o = vdupq_n_f32(0.0f);
      for (std::size_t i = 0; i < src_len; ++i) {
        o = vfmaq_f32(o, vdupq_n_f32(scores_scratch[i]), vld1q_f32(vh + i * head_dim + k));
      }
      vst1q_f32(oh + k, o);
    }
    for (; k < head_dim; ++k) {
      float sum = 0.0f;
      for (std::size_t i = 0; i < src_len; ++i) sum = std::fma(scores_scratch[i], vh[i * head_dim + k], sum);
      oh[k] = sum;
    }
  }
  return;
#endif
#if !defined(__aarch64__) && !defined(_M_ARM64)
  const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
  for (std::size_t h = head_begin; h < head_end; ++h) {
    const float* qh = q + h * head_dim;
    const float* kh = k_t + h * head_dim * src_len;
    const float* vh = v + h * src_len * head_dim;
    float* oh = out + h * head_dim;
    for (std::size_t s = 0; s < src_len; ++s) {
      float acc = 0.0f;
      for (std::size_t k = 0; k < head_dim; ++k) acc += qh[k] * kh[k * src_len + s];
      scores_scratch[s] = acc * scale;
    }
    float mx = scores_scratch[0];
    for (std::size_t s = 1; s < src_len; ++s) mx = std::max(mx, scores_scratch[s]);
    float total = 0.0f;
    for (std::size_t s = 0; s < src_len; ++s) {
      scores_scratch[s] = std::exp(scores_scratch[s] - mx);
      total += scores_scratch[s];
    }
    for (std::size_t s = 0; s < src_len; ++s) scores_scratch[s] /= total;
    for (std::size_t k = 0; k < head_dim; ++k) {
      float sum = 0.0f;
      for (std::size_t s = 0; s < src_len; ++s) sum += scores_scratch[s] * vh[s * head_dim + k];
      oh[k] = sum;
    }
  }
#endif
}

void selfAttentionStrided(const float* q, const float* k, const float* v, std::size_t qkv_stride,
                          std::size_t seq_len, std::size_t heads, std::size_t head_dim,
                          float* scores_scratch, float* out,
                          std::size_t head_begin, std::size_t head_end) noexcept {
  if (!q || !k || !v || !scores_scratch || !out || !seq_len || !heads || !head_dim) return;
  if (head_end > heads) head_end = heads;
  if (head_begin >= head_end) return;
#if defined(__x86_64__) || defined(_M_X64)
  if (hasAvx2Fma()) {
    selfAttentionAvx2Fma(q, k, v, qkv_stride, seq_len, heads, head_dim, scores_scratch, out,
                         head_begin, head_end);
    return;
  }
  if (hasAvx2()) {
    selfAttentionAvx2(q, k, v, qkv_stride, seq_len, heads, head_dim, scores_scratch, out,
                      head_begin, head_end);
    return;
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  const std::size_t out_stride = heads * head_dim;
  const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
  for (std::size_t h = head_begin; h < head_end; ++h) {
    const std::size_t h_off = h * head_dim;
    float* sc = scores_scratch + h * seq_len;
    for (std::size_t t = 0; t < seq_len; ++t) {
      const float* qt = q + t * qkv_stride + h_off;
      for (std::size_t s = 0; s < seq_len; ++s) {
        const float* ks = k + s * qkv_stride + h_off;
        float32x4_t acc = vdupq_n_f32(0.0f);
        std::size_t d = 0;
        for (; d + 4 <= head_dim; d += 4) acc = vfmaq_f32(acc, vld1q_f32(qt + d), vld1q_f32(ks + d));
        float sum = vaddvq_f32(acc);
        for (; d < head_dim; ++d) sum = std::fma(qt[d], ks[d], sum);
        sc[s] = sum * scale;
      }
      softmaxRowNeon(sc, seq_len);
      float* ot = out + t * out_stride + h_off;
      std::size_t d = 0;
      for (; d + 4 <= head_dim; d += 4) {
        float32x4_t o = vdupq_n_f32(0.0f);
        for (std::size_t s = 0; s < seq_len; ++s) {
          o = vfmaq_f32(o, vdupq_n_f32(sc[s]), vld1q_f32(v + s * qkv_stride + h_off + d));
        }
        vst1q_f32(ot + d, o);
      }
      for (; d < head_dim; ++d) {
        float sum = 0.0f;
        for (std::size_t s = 0; s < seq_len; ++s) sum = std::fma(sc[s], v[s * qkv_stride + h_off + d], sum);
        ot[d] = sum;
      }
    }
  }
  return;
#endif
#if !defined(__aarch64__) && !defined(_M_ARM64)
  const std::size_t out_stride = heads * head_dim;
  const float scale = static_cast<float>(1.0 / std::sqrt(static_cast<double>(head_dim)));
  for (std::size_t h = head_begin; h < head_end; ++h) {
    const std::size_t h_off = h * head_dim;
    float* sc = scores_scratch + h * seq_len;
    for (std::size_t t = 0; t < seq_len; ++t) {
      const float* qt = q + t * qkv_stride + h_off;
      for (std::size_t s = 0; s < seq_len; ++s) {
        const float* ks = k + s * qkv_stride + h_off;
        float acc = 0.0f;
        for (std::size_t d = 0; d < head_dim; ++d) acc += qt[d] * ks[d];
        sc[s] = acc * scale;
      }
      float mx = sc[0];
      for (std::size_t s = 1; s < seq_len; ++s) mx = std::max(mx, sc[s]);
      float total = 0.0f;
      for (std::size_t s = 0; s < seq_len; ++s) {
        sc[s] = std::exp(sc[s] - mx);
        total += sc[s];
      }
      for (std::size_t s = 0; s < seq_len; ++s) sc[s] /= total;
      float* ot = out + t * out_stride + h_off;
      for (std::size_t d = 0; d < head_dim; ++d) {
        float sum = 0.0f;
        for (std::size_t s = 0; s < seq_len; ++s) sum += sc[s] * v[s * qkv_stride + h_off + d];
        ot[d] = sum;
      }
    }
  }
#endif
}

void selfAttention(const float* q, const float* k, const float* v,
                   std::size_t seq_len, std::size_t heads, std::size_t head_dim,
                   float* scores_scratch, float* out,
                   std::size_t head_begin, std::size_t head_end) noexcept {
  selfAttentionStrided(q, k, v, heads * head_dim, seq_len, heads, head_dim,
                       scores_scratch, out, head_begin, head_end);
}

}  // namespace mtran::ssru::ops
