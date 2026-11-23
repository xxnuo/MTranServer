// Compile this translation unit only with -mavx2 -mavxvnni (or /arch:AVX2 on MSVC);
// callers stay baseline.
#include "packed.h"
#include <immintrin.h>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#ifdef _MSC_VER
#pragma fp_contract(off)
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_VNNI_FMA __declspec(noinline)
#else
#define MTRAN_VNNI_FMA __attribute__((target("avx2,avxvnni,fma"), noinline))
#endif

namespace mtran::ssru::packed {
namespace {

MTRAN_VNNI_FMA inline float hmax256Vnni(__m256 v) noexcept {
  const __m128 lo = _mm256_castps256_ps128(v);
  const __m128 hi = _mm256_extractf128_ps(v, 1);
  const __m128 m4 = _mm_max_ps(lo, hi);
  const __m128 m2 = _mm_max_ps(m4, _mm_movehl_ps(m4, m4));
  const __m128 m1 = _mm_max_ss(m2, _mm_shuffle_ps(m2, m2, 1));
  return _mm_cvtss_f32(m1);
}

MTRAN_VNNI_FMA inline void store8Vnni(
    __m256i acc, __m256i vzp, __m256i vsum, __m256 vscale,
    const float* fused_bias, const float* separate_bias, std::size_t col,
    float* dst, bool relu_minmax, __m256& vmax) noexcept {
  const __m256i corr = _mm256_sub_epi32(acc, _mm256_mullo_epi32(vzp, vsum));
  const __m256 f = _mm256_cvtepi32_ps(corr);
  __m256 res;
  if (fused_bias) {
    res = _mm256_fmadd_ps(f, vscale, _mm256_loadu_ps(fused_bias + col));
  } else if (separate_bias) {
    res = _mm256_add_ps(_mm256_mul_ps(f, vscale), _mm256_loadu_ps(separate_bias + col));
  } else {
    res = _mm256_mul_ps(f, vscale);
  }
  if (relu_minmax) {
    res = _mm256_max_ps(res, _mm256_setzero_ps());
    vmax = _mm256_max_ps(vmax, res);
  }
  _mm256_storeu_ps(dst, res);
}

}  // namespace

MTRAN_VNNI_FMA void multiplyBatchK4Vnni(
    const std::uint8_t* a_q, std::size_t a_stride, const float* a_scales,
    const std::uint8_t* a_zps, const std::int8_t* b_k4, const std::int32_t* sums,
    float w_scale, std::size_t rows, std::size_t k, std::size_t col_begin,
    std::size_t col_end, float* out, std::size_t out_stride,
    const float* fused_bias, const float* separate_bias,
    bool relu_minmax, float* out_maxs) {
  if (relu_minmax && out_maxs) {
    for (std::size_t r = 0; r < rows; ++r) out_maxs[r] = 0.0f;
  }
  if (col_begin >= col_end || rows == 0) return;

  if (rows == 1) {
    const std::uint8_t* a = a_q;
    const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(a_zps[0]));
    const __m256 vscale = _mm256_set1_ps(a_scales[0] * w_scale);
    __m256 vmax = _mm256_setzero_ps();
    std::size_t col = col_begin;
    for (; col + 32 <= col_end; col += 32) {
      const std::int8_t* b0 = b_k4 + col * k;
      const std::int8_t* b1 = b0 + 8 * k;
      const std::int8_t* b2 = b0 + 16 * k;
      const std::int8_t* b3 = b0 + 24 * k;
      __m256i c0a = _mm256_setzero_si256(), c1a = _mm256_setzero_si256();
      __m256i c2a = _mm256_setzero_si256(), c3a = _mm256_setzero_si256();
      __m256i c0b = _mm256_setzero_si256(), c1b = _mm256_setzero_si256();
      __m256i c2b = _mm256_setzero_si256(), c3b = _mm256_setzero_si256();
      for (std::size_t r = 0; r < k; r += 8, b0 += 64, b1 += 64, b2 += 64, b3 += 64) {
        std::int32_t v0, v1;
        std::memcpy(&v0, a + r, 4);
        std::memcpy(&v1, a + r + 4, 4);
        const __m256i va0 = _mm256_set1_epi32(v0);
        const __m256i va1 = _mm256_set1_epi32(v1);
        c0a = _mm256_dpbusd_avx_epi32(c0a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0)));
        c1a = _mm256_dpbusd_avx_epi32(c1a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1)));
        c2a = _mm256_dpbusd_avx_epi32(c2a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b2)));
        c3a = _mm256_dpbusd_avx_epi32(c3a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b3)));
        c0b = _mm256_dpbusd_avx_epi32(c0b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0 + 32)));
        c1b = _mm256_dpbusd_avx_epi32(c1b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1 + 32)));
        c2b = _mm256_dpbusd_avx_epi32(c2b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b2 + 32)));
        c3b = _mm256_dpbusd_avx_epi32(c3b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b3 + 32)));
      }
      store8Vnni(_mm256_add_epi32(c0a, c0b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col)), vscale,
                 fused_bias, separate_bias, col, out + col, relu_minmax, vmax);
      store8Vnni(_mm256_add_epi32(c1a, c1b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col + 8)), vscale,
                 fused_bias, separate_bias, col + 8, out + col + 8, relu_minmax, vmax);
      store8Vnni(_mm256_add_epi32(c2a, c2b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col + 16)), vscale,
                 fused_bias, separate_bias, col + 16, out + col + 16, relu_minmax, vmax);
      store8Vnni(_mm256_add_epi32(c3a, c3b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col + 24)), vscale,
                 fused_bias, separate_bias, col + 24, out + col + 24, relu_minmax, vmax);
    }
    for (; col + 16 <= col_end; col += 16) {
      const std::int8_t* b0 = b_k4 + col * k;
      const std::int8_t* b1 = b0 + 8 * k;
      __m256i c0a = _mm256_setzero_si256(), c1a = _mm256_setzero_si256();
      __m256i c0b = _mm256_setzero_si256(), c1b = _mm256_setzero_si256();
      for (std::size_t r = 0; r < k; r += 8, b0 += 64, b1 += 64) {
        std::int32_t v0, v1;
        std::memcpy(&v0, a + r, 4);
        std::memcpy(&v1, a + r + 4, 4);
        const __m256i va0 = _mm256_set1_epi32(v0);
        const __m256i va1 = _mm256_set1_epi32(v1);
        c0a = _mm256_dpbusd_avx_epi32(c0a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0)));
        c1a = _mm256_dpbusd_avx_epi32(c1a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1)));
        c0b = _mm256_dpbusd_avx_epi32(c0b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0 + 32)));
        c1b = _mm256_dpbusd_avx_epi32(c1b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1 + 32)));
      }
      store8Vnni(_mm256_add_epi32(c0a, c0b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col)), vscale,
                 fused_bias, separate_bias, col, out + col, relu_minmax, vmax);
      store8Vnni(_mm256_add_epi32(c1a, c1b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col + 8)), vscale,
                 fused_bias, separate_bias, col + 8, out + col + 8, relu_minmax, vmax);
    }
    for (; col + 8 <= col_end; col += 8) {
      const std::int8_t* b0 = b_k4 + col * k;
      __m256i c0a = _mm256_setzero_si256(), c0b = _mm256_setzero_si256();
      for (std::size_t r = 0; r < k; r += 8, b0 += 64) {
        std::int32_t v0, v1;
        std::memcpy(&v0, a + r, 4);
        std::memcpy(&v1, a + r + 4, 4);
        c0a = _mm256_dpbusd_avx_epi32(c0a, _mm256_set1_epi32(v0), _mm256_load_si256(reinterpret_cast<const __m256i*>(b0)));
        c0b = _mm256_dpbusd_avx_epi32(c0b, _mm256_set1_epi32(v1), _mm256_load_si256(reinterpret_cast<const __m256i*>(b0 + 32)));
      }
      store8Vnni(_mm256_add_epi32(c0a, c0b), vzp, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col)), vscale,
                 fused_bias, separate_bias, col, out + col, relu_minmax, vmax);
    }
    if (relu_minmax && out_maxs) out_maxs[0] = hmax256Vnni(vmax);
    return;
  }

  // Batched GEMM (rows >= 2): 16-col outer strips keep B in L1; 4x16 (8 accs + 4 va + 2 vb = 14 YMM) zero-spill.
  std::size_t col = col_begin;
  const std::size_t m = 0;
  for (; col + 16 <= col_end; col += 16) {
    const std::int8_t* b0_base = b_k4 + col * k;
    const std::int8_t* b1_base = b0_base + 8 * k;
    const __m256i s0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col));
    const __m256i s1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col + 8));
    std::size_t mr = m;
    for (; mr + 4 <= rows; mr += 4) {
      const std::uint8_t* a0 = a_q + (mr + 0) * a_stride;
      const std::uint8_t* a1 = a_q + (mr + 1) * a_stride;
      const std::uint8_t* a2 = a_q + (mr + 2) * a_stride;
      const std::uint8_t* a3 = a_q + (mr + 3) * a_stride;
      __m256i c00 = _mm256_setzero_si256(), c01 = _mm256_setzero_si256();
      __m256i c10 = _mm256_setzero_si256(), c11 = _mm256_setzero_si256();
      __m256i c20 = _mm256_setzero_si256(), c21 = _mm256_setzero_si256();
      __m256i c30 = _mm256_setzero_si256(), c31 = _mm256_setzero_si256();
      const std::int8_t* b0 = b0_base;
      const std::int8_t* b1 = b1_base;
      for (std::size_t r = 0; r < k; r += 4, b0 += 32, b1 += 32) {
        const __m256i vb0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(b0));
        const __m256i vb1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(b1));
        std::int32_t x0, x1, x2, x3;
        std::memcpy(&x0, a0 + r, 4); std::memcpy(&x1, a1 + r, 4);
        std::memcpy(&x2, a2 + r, 4); std::memcpy(&x3, a3 + r, 4);
        const __m256i va0 = _mm256_set1_epi32(x0), va1 = _mm256_set1_epi32(x1);
        const __m256i va2 = _mm256_set1_epi32(x2), va3 = _mm256_set1_epi32(x3);
        c00 = _mm256_dpbusd_avx_epi32(c00, va0, vb0); c01 = _mm256_dpbusd_avx_epi32(c01, va0, vb1);
        c10 = _mm256_dpbusd_avx_epi32(c10, va1, vb0); c11 = _mm256_dpbusd_avx_epi32(c11, va1, vb1);
        c20 = _mm256_dpbusd_avx_epi32(c20, va2, vb0); c21 = _mm256_dpbusd_avx_epi32(c21, va2, vb1);
        c30 = _mm256_dpbusd_avx_epi32(c30, va3, vb0); c31 = _mm256_dpbusd_avx_epi32(c31, va3, vb1);
      }
      const __m256i accs[4][2] = {{c00, c01}, {c10, c11}, {c20, c21}, {c30, c31}};
      for (std::size_t i = 0; i < 4; ++i) {
        const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(a_zps[mr + i]));
        const __m256 vsc = _mm256_set1_ps(a_scales[mr + i] * w_scale);
        __m256 vmax = relu_minmax && out_maxs ? _mm256_set1_ps(out_maxs[mr + i]) : _mm256_setzero_ps();
        float* dst = out + (mr + i) * out_stride + col;
        store8Vnni(accs[i][0], vzp, s0, vsc, fused_bias, separate_bias, col, dst, relu_minmax, vmax);
        store8Vnni(accs[i][1], vzp, s1, vsc, fused_bias, separate_bias, col + 8, dst + 8, relu_minmax, vmax);
        if (relu_minmax && out_maxs) out_maxs[mr + i] = hmax256Vnni(vmax);
      }
    }
    for (; mr + 2 <= rows; mr += 2) {
      const std::uint8_t* a0 = a_q + (mr + 0) * a_stride;
      const std::uint8_t* a1 = a_q + (mr + 1) * a_stride;
      __m256i c00 = _mm256_setzero_si256(), c01 = _mm256_setzero_si256();
      __m256i c10 = _mm256_setzero_si256(), c11 = _mm256_setzero_si256();
      const std::int8_t* b0 = b0_base;
      const std::int8_t* b1 = b1_base;
      for (std::size_t r = 0; r < k; r += 4, b0 += 32, b1 += 32) {
        const __m256i vb0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(b0));
        const __m256i vb1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(b1));
        std::int32_t x0, x1;
        std::memcpy(&x0, a0 + r, 4); std::memcpy(&x1, a1 + r, 4);
        const __m256i va0 = _mm256_set1_epi32(x0), va1 = _mm256_set1_epi32(x1);
        c00 = _mm256_dpbusd_avx_epi32(c00, va0, vb0); c01 = _mm256_dpbusd_avx_epi32(c01, va0, vb1);
        c10 = _mm256_dpbusd_avx_epi32(c10, va1, vb0); c11 = _mm256_dpbusd_avx_epi32(c11, va1, vb1);
      }
      const __m256i accs[2][2] = {{c00, c01}, {c10, c11}};
      for (std::size_t i = 0; i < 2; ++i) {
        const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(a_zps[mr + i]));
        const __m256 vsc = _mm256_set1_ps(a_scales[mr + i] * w_scale);
        __m256 vmax = relu_minmax && out_maxs ? _mm256_set1_ps(out_maxs[mr + i]) : _mm256_setzero_ps();
        float* dst = out + (mr + i) * out_stride + col;
        store8Vnni(accs[i][0], vzp, s0, vsc, fused_bias, separate_bias, col, dst, relu_minmax, vmax);
        store8Vnni(accs[i][1], vzp, s1, vsc, fused_bias, separate_bias, col + 8, dst + 8, relu_minmax, vmax);
        if (relu_minmax && out_maxs) out_maxs[mr + i] = hmax256Vnni(vmax);
      }
    }
    if (mr < rows) {
      const std::uint8_t* a0 = a_q + mr * a_stride;
      __m256i c0 = _mm256_setzero_si256(), c1 = _mm256_setzero_si256();
      const std::int8_t* b0 = b0_base;
      const std::int8_t* b1 = b1_base;
      for (std::size_t r = 0; r < k; r += 4, b0 += 32, b1 += 32) {
        std::int32_t x0;
        std::memcpy(&x0, a0 + r, 4);
        const __m256i va0 = _mm256_set1_epi32(x0);
        c0 = _mm256_dpbusd_avx_epi32(c0, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0)));
        c1 = _mm256_dpbusd_avx_epi32(c1, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1)));
      }
      const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(a_zps[mr]));
      const __m256 vsc = _mm256_set1_ps(a_scales[mr] * w_scale);
      __m256 vmax = relu_minmax && out_maxs ? _mm256_set1_ps(out_maxs[mr]) : _mm256_setzero_ps();
      float* dst = out + mr * out_stride + col;
      store8Vnni(c0, vzp, s0, vsc, fused_bias, separate_bias, col, dst, relu_minmax, vmax);
      store8Vnni(c1, vzp, s1, vsc, fused_bias, separate_bias, col + 8, dst + 8, relu_minmax, vmax);
      if (relu_minmax && out_maxs) out_maxs[mr] = hmax256Vnni(vmax);
    }
  }
  for (; col + 8 <= col_end; col += 8) {
    const std::int8_t* b0_base = b_k4 + col * k;
    const __m256i s0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums + col));
    for (std::size_t mr = m; mr < rows; ++mr) {
      const std::uint8_t* a0 = a_q + mr * a_stride;
      __m256i c0 = _mm256_setzero_si256();
      const std::int8_t* b0 = b0_base;
      for (std::size_t r = 0; r < k; r += 4, b0 += 32) {
        std::int32_t x0;
        std::memcpy(&x0, a0 + r, 4);
        c0 = _mm256_dpbusd_avx_epi32(c0, _mm256_set1_epi32(x0), _mm256_load_si256(reinterpret_cast<const __m256i*>(b0)));
      }
      const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(a_zps[mr]));
      const __m256 vsc = _mm256_set1_ps(a_scales[mr] * w_scale);
      __m256 vmax = relu_minmax && out_maxs ? _mm256_set1_ps(out_maxs[mr]) : _mm256_setzero_ps();
      store8Vnni(c0, vzp, s0, vsc, fused_bias, separate_bias, col, out + mr * out_stride + col, relu_minmax, vmax);
      if (relu_minmax && out_maxs) out_maxs[mr] = hmax256Vnni(vmax);
    }
  }
}

MTRAN_VNNI_FMA ShortlistTable::ScanResult scanShortlistVnni(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out,
    const std::int8_t* hot_q8_k4, const ShortlistTable::RowMeta* hot_meta,
    std::size_t hot_prefix_len, bool exact_int8, float exact_scale) {
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
  const __m256i vzp = _mm256_set1_epi32(zp_i);
  const __m256 vxscale = _mm256_set1_ps(x_scale);
  const __m256 vexact_scale = _mm256_set1_ps(x_scale * exact_scale);

  std::size_t j = 0;
  if (hot_q8_k4 && hot_prefix_len > 0) {
    auto record_hot8 = [&](std::size_t idx, __m256i acc) {
      const auto* hm = hot_meta + idx;
      const __m256i rsums = _mm256_setr_epi32(hm[0].sum, hm[1].sum, hm[2].sum, hm[3].sum,
                                               hm[4].sum, hm[5].sum, hm[6].sum, hm[7].sum);
      const __m256 rscales = exact_int8
        ? vexact_scale
        : _mm256_mul_ps(vxscale, _mm256_setr_ps(hm[0].scale, hm[1].scale, hm[2].scale, hm[3].scale,
                                                hm[4].scale, hm[5].scale, hm[6].scale, hm[7].scale));
      const __m256 rbias = _mm256_setr_ps(hm[0].bias, hm[1].bias, hm[2].bias, hm[3].bias,
                                          hm[4].bias, hm[5].bias, hm[6].bias, hm[7].bias);
      const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(acc, _mm256_mullo_epi32(vzp, rsums)));
      const __m256 vals = _mm256_add_ps(_mm256_mul_ps(corr, rscales), rbias);
      _mm256_storeu_ps(approx_out + idx, vals);
      if (_mm256_movemask_ps(_mm256_cmp_ps(vals, _mm256_set1_ps(second_logit), _CMP_GE_OQ)) != 0) {
        for (int lane = 0; lane < 8; ++lane) {
          const float val = approx_out[idx + lane];
          if (val > best_logit) {
            second_logit = best_logit;
            best_logit = val;
            best_j = idx + lane;
          } else if (val > second_logit) {
            second_logit = val;
          }
        }
      }
    };
    for (; j + 32 <= hot_prefix_len; j += 32) {
      const std::int8_t* b0 = hot_q8_k4 + j * dim;
      const std::int8_t* b1 = b0 + 8 * dim;
      const std::int8_t* b2 = b0 + 16 * dim;
      const std::int8_t* b3 = b0 + 24 * dim;
      __m256i c0a = _mm256_setzero_si256(), c1a = _mm256_setzero_si256();
      __m256i c2a = _mm256_setzero_si256(), c3a = _mm256_setzero_si256();
      __m256i c0b = _mm256_setzero_si256(), c1b = _mm256_setzero_si256();
      __m256i c2b = _mm256_setzero_si256(), c3b = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 8, b0 += 64, b1 += 64, b2 += 64, b3 += 64) {
        std::int32_t v0, v1;
        std::memcpy(&v0, x_u8 + k, 4);
        std::memcpy(&v1, x_u8 + k + 4, 4);
        const __m256i va0 = _mm256_set1_epi32(v0);
        const __m256i va1 = _mm256_set1_epi32(v1);
        c0a = _mm256_dpbusd_avx_epi32(c0a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0)));
        c1a = _mm256_dpbusd_avx_epi32(c1a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1)));
        c2a = _mm256_dpbusd_avx_epi32(c2a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b2)));
        c3a = _mm256_dpbusd_avx_epi32(c3a, va0, _mm256_load_si256(reinterpret_cast<const __m256i*>(b3)));
        c0b = _mm256_dpbusd_avx_epi32(c0b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b0 + 32)));
        c1b = _mm256_dpbusd_avx_epi32(c1b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b1 + 32)));
        c2b = _mm256_dpbusd_avx_epi32(c2b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b2 + 32)));
        c3b = _mm256_dpbusd_avx_epi32(c3b, va1, _mm256_load_si256(reinterpret_cast<const __m256i*>(b3 + 32)));
      }
      record_hot8(j + 0, _mm256_add_epi32(c0a, c0b));
      record_hot8(j + 8, _mm256_add_epi32(c1a, c1b));
      record_hot8(j + 16, _mm256_add_epi32(c2a, c2b));
      record_hot8(j + 24, _mm256_add_epi32(c3a, c3b));
    }
    for (; j + 8 <= hot_prefix_len; j += 8) {
      const std::int8_t* bp = hot_q8_k4 + j * dim;
      __m256i c0a = _mm256_setzero_si256(), c0b = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 8, bp += 64) {
        std::int32_t v0, v1;
        std::memcpy(&v0, x_u8 + k, 4);
        std::memcpy(&v1, x_u8 + k + 4, 4);
        c0a = _mm256_dpbusd_avx_epi32(c0a, _mm256_set1_epi32(v0), _mm256_load_si256(reinterpret_cast<const __m256i*>(bp)));
        c0b = _mm256_dpbusd_avx_epi32(c0b, _mm256_set1_epi32(v1), _mm256_load_si256(reinterpret_cast<const __m256i*>(bp + 32)));
      }
      record_hot8(j, _mm256_add_epi32(c0a, c0b));
    }
  }

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
    __m256i a0 = _mm256_setzero_si256(), a1 = _mm256_setzero_si256();
    __m256i a2 = _mm256_setzero_si256(), a3 = _mm256_setzero_si256();
    __m256i a4 = _mm256_setzero_si256(), a5 = _mm256_setzero_si256();
    __m256i a6 = _mm256_setzero_si256(), a7 = _mm256_setzero_si256();
    for (std::size_t k = 0; k < dim; k += 32) {
      const __m256i vx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x_u8 + k));
      a0 = _mm256_dpbusd_avx_epi32(a0, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r0 + k)));
      a1 = _mm256_dpbusd_avx_epi32(a1, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r1 + k)));
      a2 = _mm256_dpbusd_avx_epi32(a2, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r2 + k)));
      a3 = _mm256_dpbusd_avx_epi32(a3, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r3 + k)));
      a4 = _mm256_dpbusd_avx_epi32(a4, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r4 + k)));
      a5 = _mm256_dpbusd_avx_epi32(a5, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r5 + k)));
      a6 = _mm256_dpbusd_avx_epi32(a6, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r6 + k)));
      a7 = _mm256_dpbusd_avx_epi32(a7, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(r7 + k)));
    }
    const __m256i h01 = _mm256_hadd_epi32(a0, a1), h23 = _mm256_hadd_epi32(a2, a3);
    const __m256i h45 = _mm256_hadd_epi32(a4, a5), h67 = _mm256_hadd_epi32(a6, a7);
    const __m256i p0 = _mm256_hadd_epi32(h01, h23), p1 = _mm256_hadd_epi32(h45, h67);
    const __m256i sums8 = _mm256_add_epi32(_mm256_permute2f128_si256(p0, p1, 0x21), _mm256_blend_epi32(p0, p1, 0xf0));
    const auto& m0 = meta[ts[0]]; const auto& m1 = meta[ts[1]];
    const auto& m2 = meta[ts[2]]; const auto& m3 = meta[ts[3]];
    const auto& m4 = meta[ts[4]]; const auto& m5 = meta[ts[5]];
    const auto& m6 = meta[ts[6]]; const auto& m7 = meta[ts[7]];
    const __m256i rsums = _mm256_setr_epi32(m0.sum, m1.sum, m2.sum, m3.sum, m4.sum, m5.sum, m6.sum, m7.sum);
    const __m256 rscales = exact_int8
      ? vexact_scale
      : _mm256_mul_ps(vxscale, _mm256_setr_ps(m0.scale, m1.scale, m2.scale, m3.scale, m4.scale, m5.scale, m6.scale, m7.scale));
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

MTRAN_VNNI_FMA void scanShortlistVnniBatch(
    const std::uint8_t* x_u8, std::size_t x_stride, const float* x_scales,
    const std::uint8_t* x_zps, std::size_t rows, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* union_ids, const std::uint32_t* slot_masks,
    std::size_t union_count, float* approx_out, float* best_logits,
    std::size_t* best_js, float* second_logits, bool* has_any,
    const std::int8_t* hot_q8_k4, const ShortlistTable::RowMeta* hot_meta,
    std::size_t hot_prefix_len, bool exact_int8, float exact_scale) {
  for (std::size_t m = 0; m < rows; ++m) {
    best_logits[m] = -std::numeric_limits<float>::infinity();
    second_logits[m] = -std::numeric_limits<float>::infinity();
    best_js[m] = 0;
    has_any[m] = false;
  }

  auto update8 = [&](std::size_t m, std::size_t j, __m256 vals) {
    _mm256_storeu_ps(approx_out + m * union_count + j, vals);
    std::uint32_t active = 0;
    const std::uint32_t bit = 1u << m;
    for (int l = 0; l < 8; ++l) active |= ((slot_masks[j + l] & bit) != 0) ? (1u << l) : 0u;
    if (!active) return;
    std::uint32_t hit = has_any[m]
      ? (static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_cmp_ps(vals, _mm256_set1_ps(second_logits[m]), _CMP_GE_OQ))) & active)
      : active;
    while (hit) {
      const int lane = std::countr_zero(hit);
      hit &= (hit - 1);
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
  };

  std::size_t j = 0;
  if (hot_q8_k4 && hot_prefix_len > 0) {
    for (; j + 8 <= hot_prefix_len; j += 8) {
      const auto* hm = hot_meta + j;
      const __m256i rsums = _mm256_setr_epi32(hm[0].sum, hm[1].sum, hm[2].sum, hm[3].sum,
                                               hm[4].sum, hm[5].sum, hm[6].sum, hm[7].sum);
      const __m256 raw_rscales = _mm256_setr_ps(hm[0].scale, hm[1].scale, hm[2].scale, hm[3].scale,
                                               hm[4].scale, hm[5].scale, hm[6].scale, hm[7].scale);
      const __m256 rbias = _mm256_setr_ps(hm[0].bias, hm[1].bias, hm[2].bias, hm[3].bias,
                                          hm[4].bias, hm[5].bias, hm[6].bias, hm[7].bias);
      std::size_t m = 0;
      for (; m + 8 <= rows; m += 8) {
        const std::uint8_t* x0 = x_u8 + (m + 0) * x_stride;
        const std::uint8_t* x1 = x_u8 + (m + 1) * x_stride;
        const std::uint8_t* x2 = x_u8 + (m + 2) * x_stride;
        const std::uint8_t* x3 = x_u8 + (m + 3) * x_stride;
        const std::uint8_t* x4 = x_u8 + (m + 4) * x_stride;
        const std::uint8_t* x5 = x_u8 + (m + 5) * x_stride;
        const std::uint8_t* x6 = x_u8 + (m + 6) * x_stride;
        const std::uint8_t* x7 = x_u8 + (m + 7) * x_stride;
        const std::int8_t* bp = hot_q8_k4 + j * dim;
        __m256i c0 = _mm256_setzero_si256(), c1 = _mm256_setzero_si256();
        __m256i c2 = _mm256_setzero_si256(), c3 = _mm256_setzero_si256();
        __m256i c4 = _mm256_setzero_si256(), c5 = _mm256_setzero_si256();
        __m256i c6 = _mm256_setzero_si256(), c7 = _mm256_setzero_si256();
        for (std::size_t k = 0; k < dim; k += 8, bp += 64) {
          const __m256i wb0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(bp));
          std::int32_t v0, v1, v2, v3, v4, v5, v6, v7;
          std::memcpy(&v0, x0 + k, 4); std::memcpy(&v1, x1 + k, 4);
          std::memcpy(&v2, x2 + k, 4); std::memcpy(&v3, x3 + k, 4);
          std::memcpy(&v4, x4 + k, 4); std::memcpy(&v5, x5 + k, 4);
          std::memcpy(&v6, x6 + k, 4); std::memcpy(&v7, x7 + k, 4);
          c0 = _mm256_dpbusd_avx_epi32(c0, _mm256_set1_epi32(v0), wb0);
          c1 = _mm256_dpbusd_avx_epi32(c1, _mm256_set1_epi32(v1), wb0);
          c2 = _mm256_dpbusd_avx_epi32(c2, _mm256_set1_epi32(v2), wb0);
          c3 = _mm256_dpbusd_avx_epi32(c3, _mm256_set1_epi32(v3), wb0);
          c4 = _mm256_dpbusd_avx_epi32(c4, _mm256_set1_epi32(v4), wb0);
          c5 = _mm256_dpbusd_avx_epi32(c5, _mm256_set1_epi32(v5), wb0);
          c6 = _mm256_dpbusd_avx_epi32(c6, _mm256_set1_epi32(v6), wb0);
          c7 = _mm256_dpbusd_avx_epi32(c7, _mm256_set1_epi32(v7), wb0);
          const __m256i wb1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(bp + 32));
          std::memcpy(&v0, x0 + k + 4, 4); std::memcpy(&v1, x1 + k + 4, 4);
          std::memcpy(&v2, x2 + k + 4, 4); std::memcpy(&v3, x3 + k + 4, 4);
          std::memcpy(&v4, x4 + k + 4, 4); std::memcpy(&v5, x5 + k + 4, 4);
          std::memcpy(&v6, x6 + k + 4, 4); std::memcpy(&v7, x7 + k + 4, 4);
          c0 = _mm256_dpbusd_avx_epi32(c0, _mm256_set1_epi32(v0), wb1);
          c1 = _mm256_dpbusd_avx_epi32(c1, _mm256_set1_epi32(v1), wb1);
          c2 = _mm256_dpbusd_avx_epi32(c2, _mm256_set1_epi32(v2), wb1);
          c3 = _mm256_dpbusd_avx_epi32(c3, _mm256_set1_epi32(v3), wb1);
          c4 = _mm256_dpbusd_avx_epi32(c4, _mm256_set1_epi32(v4), wb1);
          c5 = _mm256_dpbusd_avx_epi32(c5, _mm256_set1_epi32(v5), wb1);
          c6 = _mm256_dpbusd_avx_epi32(c6, _mm256_set1_epi32(v6), wb1);
          c7 = _mm256_dpbusd_avx_epi32(c7, _mm256_set1_epi32(v7), wb1);
        }
        const __m256i cs[8] = {c0, c1, c2, c3, c4, c5, c6, c7};
        for (std::size_t i = 0; i < 8; ++i) {
          const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m + i]));
          const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m + i] * exact_scale)
                                        : _mm256_mul_ps(_mm256_set1_ps(x_scales[m + i]), raw_rscales);
          const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(cs[i], _mm256_mullo_epi32(vzp, rsums)));
          update8(m + i, j, _mm256_add_ps(_mm256_mul_ps(corr, rsc), rbias));
        }
      }
      for (; m + 4 <= rows; m += 4) {
        const std::uint8_t* x0 = x_u8 + (m + 0) * x_stride;
        const std::uint8_t* x1 = x_u8 + (m + 1) * x_stride;
        const std::uint8_t* x2 = x_u8 + (m + 2) * x_stride;
        const std::uint8_t* x3 = x_u8 + (m + 3) * x_stride;
        const std::int8_t* bp = hot_q8_k4 + j * dim;
        __m256i c0 = _mm256_setzero_si256(), c1 = _mm256_setzero_si256();
        __m256i c2 = _mm256_setzero_si256(), c3 = _mm256_setzero_si256();
        for (std::size_t k = 0; k < dim; k += 4, bp += 32) {
          const __m256i wb = _mm256_load_si256(reinterpret_cast<const __m256i*>(bp));
          std::int32_t v0, v1, v2, v3;
          std::memcpy(&v0, x0 + k, 4); std::memcpy(&v1, x1 + k, 4);
          std::memcpy(&v2, x2 + k, 4); std::memcpy(&v3, x3 + k, 4);
          c0 = _mm256_dpbusd_avx_epi32(c0, _mm256_set1_epi32(v0), wb);
          c1 = _mm256_dpbusd_avx_epi32(c1, _mm256_set1_epi32(v1), wb);
          c2 = _mm256_dpbusd_avx_epi32(c2, _mm256_set1_epi32(v2), wb);
          c3 = _mm256_dpbusd_avx_epi32(c3, _mm256_set1_epi32(v3), wb);
        }
        const __m256i cs[4] = {c0, c1, c2, c3};
        for (std::size_t i = 0; i < 4; ++i) {
          const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m + i]));
          const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m + i] * exact_scale)
                                        : _mm256_mul_ps(_mm256_set1_ps(x_scales[m + i]), raw_rscales);
          const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(cs[i], _mm256_mullo_epi32(vzp, rsums)));
          update8(m + i, j, _mm256_add_ps(_mm256_mul_ps(corr, rsc), rbias));
        }
      }
      for (; m < rows; ++m) {
        const std::uint8_t* xm = x_u8 + m * x_stride;
        const std::int8_t* bp = hot_q8_k4 + j * dim;
        __m256i c = _mm256_setzero_si256();
        for (std::size_t k = 0; k < dim; k += 4, bp += 32) {
          std::int32_t v0;
          std::memcpy(&v0, xm + k, 4);
          c = _mm256_dpbusd_avx_epi32(c, _mm256_set1_epi32(v0), _mm256_load_si256(reinterpret_cast<const __m256i*>(bp)));
        }
        const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
        const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m] * exact_scale)
                                      : _mm256_mul_ps(_mm256_set1_ps(x_scales[m]), raw_rscales);
        const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(c, _mm256_mullo_epi32(vzp, rsums)));
        update8(m, j, _mm256_add_ps(_mm256_mul_ps(corr, rsc), rbias));
      }
    }
  }

  for (; j + 8 <= union_count; j += 8) {
    if (j + 16 <= union_count) {
      for (int p = 0; p < 8; ++p) {
        const auto nt = static_cast<std::size_t>(union_ids[j + 8 + p]);
        _mm_prefetch(reinterpret_cast<const char*>(emb_q8 + nt * dim), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(meta + nt), _MM_HINT_T0);
      }
    }
    const std::size_t ts[8] = {
      static_cast<std::size_t>(union_ids[j + 0]), static_cast<std::size_t>(union_ids[j + 1]),
      static_cast<std::size_t>(union_ids[j + 2]), static_cast<std::size_t>(union_ids[j + 3]),
      static_cast<std::size_t>(union_ids[j + 4]), static_cast<std::size_t>(union_ids[j + 5]),
      static_cast<std::size_t>(union_ids[j + 6]), static_cast<std::size_t>(union_ids[j + 7]),
    };
    const std::int8_t* rp[8] = {
      emb_q8 + ts[0] * dim, emb_q8 + ts[1] * dim, emb_q8 + ts[2] * dim, emb_q8 + ts[3] * dim,
      emb_q8 + ts[4] * dim, emb_q8 + ts[5] * dim, emb_q8 + ts[6] * dim, emb_q8 + ts[7] * dim,
    };
    const auto& m0 = meta[ts[0]]; const auto& m1 = meta[ts[1]];
    const auto& m2 = meta[ts[2]]; const auto& m3 = meta[ts[3]];
    const auto& m4 = meta[ts[4]]; const auto& m5 = meta[ts[5]];
    const auto& m6 = meta[ts[6]]; const auto& m7 = meta[ts[7]];
    const __m256i rsums = _mm256_setr_epi32(m0.sum, m1.sum, m2.sum, m3.sum, m4.sum, m5.sum, m6.sum, m7.sum);
    const __m256 raw_rscales = _mm256_setr_ps(m0.scale, m1.scale, m2.scale, m3.scale, m4.scale, m5.scale, m6.scale, m7.scale);
    const __m256 rbias = _mm256_setr_ps(m0.bias, m1.bias, m2.bias, m3.bias, m4.bias, m5.bias, m6.bias, m7.bias);

    std::size_t m = 0;
    for (; m + 2 <= rows; m += 2) {
      const std::uint8_t* x0 = x_u8 + m * x_stride;
      const std::uint8_t* x1 = x_u8 + (m + 1) * x_stride;
      __m256i a0 = _mm256_setzero_si256(), a1 = _mm256_setzero_si256();
      __m256i a2 = _mm256_setzero_si256(), a3 = _mm256_setzero_si256();
      __m256i b0 = _mm256_setzero_si256(), b1 = _mm256_setzero_si256();
      __m256i b2 = _mm256_setzero_si256(), b3 = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 32) {
        const __m256i vx0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x0 + k));
        const __m256i vx1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x1 + k));
        const __m256i w0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[0] + k));
        const __m256i w1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[1] + k));
        const __m256i w2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[2] + k));
        const __m256i w3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[3] + k));
        a0 = _mm256_dpbusd_avx_epi32(a0, vx0, w0); b0 = _mm256_dpbusd_avx_epi32(b0, vx1, w0);
        a1 = _mm256_dpbusd_avx_epi32(a1, vx0, w1); b1 = _mm256_dpbusd_avx_epi32(b1, vx1, w1);
        a2 = _mm256_dpbusd_avx_epi32(a2, vx0, w2); b2 = _mm256_dpbusd_avx_epi32(b2, vx1, w2);
        a3 = _mm256_dpbusd_avx_epi32(a3, vx0, w3); b3 = _mm256_dpbusd_avx_epi32(b3, vx1, w3);
      }
      __m256i a4 = _mm256_setzero_si256(), a5 = _mm256_setzero_si256();
      __m256i a6 = _mm256_setzero_si256(), a7 = _mm256_setzero_si256();
      __m256i b4 = _mm256_setzero_si256(), b5 = _mm256_setzero_si256();
      __m256i b6 = _mm256_setzero_si256(), b7 = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 32) {
        const __m256i vx0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x0 + k));
        const __m256i vx1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x1 + k));
        const __m256i w4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[4] + k));
        const __m256i w5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[5] + k));
        const __m256i w6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[6] + k));
        const __m256i w7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[7] + k));
        a4 = _mm256_dpbusd_avx_epi32(a4, vx0, w4); b4 = _mm256_dpbusd_avx_epi32(b4, vx1, w4);
        a5 = _mm256_dpbusd_avx_epi32(a5, vx0, w5); b5 = _mm256_dpbusd_avx_epi32(b5, vx1, w5);
        a6 = _mm256_dpbusd_avx_epi32(a6, vx0, w6); b6 = _mm256_dpbusd_avx_epi32(b6, vx1, w6);
        a7 = _mm256_dpbusd_avx_epi32(a7, vx0, w7); b7 = _mm256_dpbusd_avx_epi32(b7, vx1, w7);
      }
      auto reduce8 = [](__m256i r0, __m256i r1, __m256i r2, __m256i r3,
                        __m256i r4, __m256i r5, __m256i r6, __m256i r7) {
        const __m256i h01 = _mm256_hadd_epi32(r0, r1), h23 = _mm256_hadd_epi32(r2, r3);
        const __m256i h45 = _mm256_hadd_epi32(r4, r5), h67 = _mm256_hadd_epi32(r6, r7);
        const __m256i p0 = _mm256_hadd_epi32(h01, h23), p1 = _mm256_hadd_epi32(h45, h67);
        return _mm256_add_epi32(_mm256_permute2f128_si256(p0, p1, 0x21), _mm256_blend_epi32(p0, p1, 0xf0));
      };
      const __m256i sums_m0 = reduce8(a0, a1, a2, a3, a4, a5, a6, a7);
      const __m256i sums_m1 = reduce8(b0, b1, b2, b3, b4, b5, b6, b7);
      {
        const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
        const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m] * exact_scale)
                                      : _mm256_mul_ps(_mm256_set1_ps(x_scales[m]), raw_rscales);
        update8(m, j, _mm256_add_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(sums_m0, _mm256_mullo_epi32(vzp, rsums))), rsc), rbias));
      }
      {
        const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m + 1]));
        const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m + 1] * exact_scale)
                                      : _mm256_mul_ps(_mm256_set1_ps(x_scales[m + 1]), raw_rscales);
        update8(m + 1, j, _mm256_add_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(sums_m1, _mm256_mullo_epi32(vzp, rsums))), rsc), rbias));
      }
    }
    if (m < rows) {
      const std::uint8_t* xm = x_u8 + m * x_stride;
      __m256i a0 = _mm256_setzero_si256(), a1 = _mm256_setzero_si256();
      __m256i a2 = _mm256_setzero_si256(), a3 = _mm256_setzero_si256();
      __m256i a4 = _mm256_setzero_si256(), a5 = _mm256_setzero_si256();
      __m256i a6 = _mm256_setzero_si256(), a7 = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 32) {
        const __m256i vx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(xm + k));
        a0 = _mm256_dpbusd_avx_epi32(a0, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[0] + k)));
        a1 = _mm256_dpbusd_avx_epi32(a1, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[1] + k)));
        a2 = _mm256_dpbusd_avx_epi32(a2, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[2] + k)));
        a3 = _mm256_dpbusd_avx_epi32(a3, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[3] + k)));
        a4 = _mm256_dpbusd_avx_epi32(a4, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[4] + k)));
        a5 = _mm256_dpbusd_avx_epi32(a5, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[5] + k)));
        a6 = _mm256_dpbusd_avx_epi32(a6, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[6] + k)));
        a7 = _mm256_dpbusd_avx_epi32(a7, vx, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp[7] + k)));
      }
      const __m256i h01 = _mm256_hadd_epi32(a0, a1), h23 = _mm256_hadd_epi32(a2, a3);
      const __m256i h45 = _mm256_hadd_epi32(a4, a5), h67 = _mm256_hadd_epi32(a6, a7);
      const __m256i p0 = _mm256_hadd_epi32(h01, h23), p1 = _mm256_hadd_epi32(h45, h67);
      const __m256i sums8 = _mm256_add_epi32(_mm256_permute2f128_si256(p0, p1, 0x21), _mm256_blend_epi32(p0, p1, 0xf0));
      const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
      const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m] * exact_scale)
                                    : _mm256_mul_ps(_mm256_set1_ps(x_scales[m]), raw_rscales);
      update8(m, j, _mm256_add_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_sub_epi32(sums8, _mm256_mullo_epi32(vzp, rsums))), rsc), rbias));
    }
  }
  for (; j < union_count; ++j) {
    const auto t = static_cast<std::size_t>(union_ids[j]);
    const auto* row = emb_q8 + t * dim;
    const auto& rm = meta[t];
    const std::uint32_t mask = slot_masks[j];
    for (std::size_t m = 0; m < rows; ++m) {
      if ((mask & (1u << m)) == 0) continue;
      const std::uint8_t* xm = x_u8 + m * x_stride;
      std::int32_t acc = 0;
      for (std::size_t k = 0; k < dim; ++k) acc += static_cast<std::int32_t>(xm[k]) * row[k];
      const float sc = exact_int8 ? (x_scales[m] * exact_scale) : (x_scales[m] * rm.scale);
      const float val = static_cast<float>(acc - static_cast<std::int32_t>(x_zps[m]) * rm.sum) * sc + rm.bias;
      approx_out[m * union_count + j] = val;
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

MTRAN_VNNI_FMA bool prepareFullQ4Vnni(
    const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  auto* neg_corr = reinterpret_cast<std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const __m256 sign_mask = _mm256_set1_ps(-0.0f);
  const __m256i eight = _mm256_set1_epi8(8);
  const __m256i zero = _mm256_setzero_si256();
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
    const __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64));
    const __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64 + 32));
    const __m256i csum = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(zero, eight, a0), eight, a1);
    const __m128i c128 = _mm_add_epi32(_mm256_castsi256_si128(csum), _mm256_extracti128_si256(csum, 1));
    const __m128i c64 = _mm_add_epi32(c128, _mm_srli_si128(c128, 8));
    const __m128i c32 = _mm_add_epi32(c64, _mm_srli_si128(c64, 4));
    neg_corr[b] = -_mm_cvtsi128_si32(c32);
    const __m256i p0 = _mm256_unpacklo_epi32(a0, a1);
    const __m256i p1 = _mm256_unpackhi_epi32(a0, a1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(qa + b * 64 + 0),
                        _mm256_permute2x128_si256(p0, p1, 0x20));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(qa + b * 64 + 32),
                        _mm256_permute2x128_si256(p0, p1, 0x31));
  }
  return true;
}

// Column-parallel N x K4 layout: [vocab/8][blocks][8_chunks][8_cols][4_k_nibbles]
// 1 YMM accumulator per 8 vocab columns, zero horizontal adds.
MTRAN_VNNI_FMA void scanFullQ4K4VnniRange(
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
  const std::size_t group_q4_bytes = blocks * 256;
  const std::size_t group_sc_floats = blocks * 8;

  std::size_t g = g_begin;
  for (; g + 4 <= g_end; g += 4) {
    __m256 acc0 = _mm256_setzero_ps(), acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps(), acc3 = _mm256_setzero_ps();
    const std::uint8_t* gp0 = packed_q4 + g * group_q4_bytes;
    const std::uint8_t* gp1 = gp0 + group_q4_bytes;
    const std::uint8_t* gp2 = gp1 + group_q4_bytes;
    const std::uint8_t* gp3 = gp2 + group_q4_bytes;
    const float* gs0 = packed_scales + g * group_sc_floats;
    const float* gs1 = gs0 + group_sc_floats;
    const float* gs2 = gs1 + group_sc_floats;
    const float* gs3 = gs2 + group_sc_floats;
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 256, gp1 += 256, gp2 += 256, gp3 += 256,
                                           gs0 += 8, gs1 += 8, gs2 += 8, gs3 += 8) {
      const __m256i ncv = _mm256_set1_epi32(neg_corr[b]);
      __m256i s0 = ncv, s1 = ncv, s2 = ncv, s3 = ncv;
      const std::int32_t* qab = qa32 + b * 16;
      for (std::size_t i = 0; i < 8; ++i) {
        const __m256i a_lo = _mm256_set1_epi32(qab[2 * i + 0]);
        const __m256i a_hi = _mm256_set1_epi32(qab[2 * i + 1]);
        #define MTRAN_Q4_K4_COL(gp, sb_acc) do { \
          const __m256i p_ = _mm256_loadu_si256(reinterpret_cast<const __m256i*>((gp) + i * 32)); \
          sb_acc = _mm256_dpbusd_avx_epi32(sb_acc, _mm256_and_si256(p_, mask), a_lo); \
          sb_acc = _mm256_dpbusd_avx_epi32(sb_acc, _mm256_and_si256(_mm256_srli_epi16(p_, 4), mask), a_hi); \
        } while (0)
        MTRAN_Q4_K4_COL(gp0, s0);
        MTRAN_Q4_K4_COL(gp1, s1);
        MTRAN_Q4_K4_COL(gp2, s2);
        MTRAN_Q4_K4_COL(gp3, s3);
      }
      const __m256 vsa = _mm256_set1_ps(sa[b]);
      acc0 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s0), _mm256_mul_ps(vsa, _mm256_loadu_ps(gs0)), acc0);
      acc1 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s1), _mm256_mul_ps(vsa, _mm256_loadu_ps(gs1)), acc1);
      acc2 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s2), _mm256_mul_ps(vsa, _mm256_loadu_ps(gs2)), acc2);
      acc3 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s3), _mm256_mul_ps(vsa, _mm256_loadu_ps(gs3)), acc3);
    }
    const float* bp = bias + g * 8;
    const __m256 v0 = _mm256_add_ps(acc0, _mm256_loadu_ps(bp + 0));
    const __m256 v1 = _mm256_add_ps(acc1, _mm256_loadu_ps(bp + 8));
    const __m256 v2 = _mm256_add_ps(acc2, _mm256_loadu_ps(bp + 16));
    const __m256 v3 = _mm256_add_ps(acc3, _mm256_loadu_ps(bp + 24));
    const __m256 vmx = _mm256_max_ps(_mm256_max_ps(v0, v1), _mm256_max_ps(v2, v3));
    if (top.count < 9 || _mm256_movemask_ps(_mm256_cmp_ps(vmx, _mm256_set1_ps(top.scores[8]), _CMP_GT_OQ)) != 0) {
      alignas(32) float out32[32];
      _mm256_store_ps(out32 + 0, v0);
      _mm256_store_ps(out32 + 8, v1);
      _mm256_store_ps(out32 + 16, v2);
      _mm256_store_ps(out32 + 24, v3);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 8);
      for (int c = 0; c < 32; ++c) top.push(out32[c], base_id + c);
    }
  }
  for (; g < g_end; ++g) {
    __m256 acc0 = _mm256_setzero_ps();
    const std::uint8_t* gp0 = packed_q4 + g * group_q4_bytes;
    const float* gs0 = packed_scales + g * group_sc_floats;
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 256, gs0 += 8) {
      __m256i s0 = _mm256_set1_epi32(neg_corr[b]);
      const std::int32_t* qab = qa32 + b * 16;
      for (std::size_t i = 0; i < 8; ++i) {
        const __m256i a_lo = _mm256_set1_epi32(qab[2 * i + 0]);
        const __m256i a_hi = _mm256_set1_epi32(qab[2 * i + 1]);
        MTRAN_Q4_K4_COL(gp0, s0);
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
  #undef MTRAN_Q4_K4_COL
}

MTRAN_VNNI_FMA void scanFullQ4K4VnniBatchRange(
    std::size_t dim, std::size_t rows, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4, const float* packed_scales,
    const float* bias, const std::uint8_t* const* work_bufs, FullVocabQ4::TopK9* tops) {
  const std::size_t blocks = dim / 64;
  const std::size_t qa_bytes = ((dim + 63) & ~std::size_t(63));
  const std::size_t sa_off = qa_bytes;
  const std::size_t nc_off = sa_off + ((blocks + 7) & ~std::size_t(7)) * sizeof(float);
  for (std::size_t m = 0; m < rows; ++m) tops[m].init();

  const __m256i mask = _mm256_set1_epi8(15);
  const std::size_t group_q4_bytes = blocks * 256;
  const std::size_t group_sc_floats = blocks * 8;

  // Outer loop over vocab group g keeps packed_q4 block in L1 across all rows m.
  for (std::size_t g = g_begin; g < g_end; ++g) {
    const std::uint8_t* gp_base = packed_q4 + g * group_q4_bytes;
    const float* gs_base = packed_scales + g * group_sc_floats;
    const __m256 vbias = _mm256_loadu_ps(bias + g * 8);
    const std::uint32_t base_id = static_cast<std::uint32_t>(g * 8);

    std::size_t m = 0;
    for (; m + 4 <= rows; m += 4) {
      const auto* qa0 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 0]);
      const auto* qa1 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 1]);
      const auto* qa2 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 2]);
      const auto* qa3 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 3]);
      const auto* sa0 = reinterpret_cast<const float*>(work_bufs[m + 0] + sa_off);
      const auto* sa1 = reinterpret_cast<const float*>(work_bufs[m + 1] + sa_off);
      const auto* sa2 = reinterpret_cast<const float*>(work_bufs[m + 2] + sa_off);
      const auto* sa3 = reinterpret_cast<const float*>(work_bufs[m + 3] + sa_off);
      const auto* nc0 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 0] + nc_off);
      const auto* nc1 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 1] + nc_off);
      const auto* nc2 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 2] + nc_off);
      const auto* nc3 = reinterpret_cast<const std::int32_t*>(work_bufs[m + 3] + nc_off);
      __m256 acc0 = _mm256_setzero_ps(), acc1 = _mm256_setzero_ps();
      __m256 acc2 = _mm256_setzero_ps(), acc3 = _mm256_setzero_ps();
      const std::uint8_t* gp = gp_base;
      const float* gs = gs_base;
      for (std::size_t b = 0; b < blocks; ++b, gp += 256, gs += 8) {
        __m256i s0 = _mm256_set1_epi32(nc0[b]);
        __m256i s1 = _mm256_set1_epi32(nc1[b]);
        __m256i s2 = _mm256_set1_epi32(nc2[b]);
        __m256i s3 = _mm256_set1_epi32(nc3[b]);
        const std::size_t qoff = b * 16;
        for (std::size_t i = 0; i < 8; ++i) {
          const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gp + i * 32));
          const __m256i lo = _mm256_and_si256(p, mask);
          const __m256i hi = _mm256_and_si256(_mm256_srli_epi16(p, 4), mask);
          const __m256i a0_l = _mm256_set1_epi32(qa0[qoff + 2 * i + 0]);
          const __m256i a0_h = _mm256_set1_epi32(qa0[qoff + 2 * i + 1]);
          const __m256i a1_l = _mm256_set1_epi32(qa1[qoff + 2 * i + 0]);
          const __m256i a1_h = _mm256_set1_epi32(qa1[qoff + 2 * i + 1]);
          const __m256i a2_l = _mm256_set1_epi32(qa2[qoff + 2 * i + 0]);
          const __m256i a2_h = _mm256_set1_epi32(qa2[qoff + 2 * i + 1]);
          const __m256i a3_l = _mm256_set1_epi32(qa3[qoff + 2 * i + 0]);
          const __m256i a3_h = _mm256_set1_epi32(qa3[qoff + 2 * i + 1]);
          s0 = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(s0, lo, a0_l), hi, a0_h);
          s1 = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(s1, lo, a1_l), hi, a1_h);
          s2 = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(s2, lo, a2_l), hi, a2_h);
          s3 = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(s3, lo, a3_l), hi, a3_h);
        }
        const __m256 vgs = _mm256_loadu_ps(gs);
        acc0 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s0), _mm256_mul_ps(_mm256_set1_ps(sa0[b]), vgs), acc0);
        acc1 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s1), _mm256_mul_ps(_mm256_set1_ps(sa1[b]), vgs), acc1);
        acc2 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s2), _mm256_mul_ps(_mm256_set1_ps(sa2[b]), vgs), acc2);
        acc3 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s3), _mm256_mul_ps(_mm256_set1_ps(sa3[b]), vgs), acc3);
      }
      const __m256 vals[4] = {
        _mm256_add_ps(acc0, vbias), _mm256_add_ps(acc1, vbias),
        _mm256_add_ps(acc2, vbias), _mm256_add_ps(acc3, vbias),
      };
      for (std::size_t i = 0; i < 4; ++i) {
        auto& top = tops[m + i];
        if (top.count < 9 || _mm256_movemask_ps(_mm256_cmp_ps(vals[i], _mm256_set1_ps(top.scores[8]), _CMP_GT_OQ)) != 0) {
          alignas(32) float out8[8];
          _mm256_store_ps(out8, vals[i]);
          for (int c = 0; c < 8; ++c) top.push(out8[c], base_id + c);
        }
      }
    }
    for (; m < rows; ++m) {
      const auto* qam = reinterpret_cast<const std::int32_t*>(work_bufs[m]);
      const auto* sam = reinterpret_cast<const float*>(work_bufs[m] + sa_off);
      const auto* ncm = reinterpret_cast<const std::int32_t*>(work_bufs[m] + nc_off);
      __m256 acc = _mm256_setzero_ps();
      const std::uint8_t* gp = gp_base;
      const float* gs = gs_base;
      for (std::size_t b = 0; b < blocks; ++b, gp += 256, gs += 8) {
        __m256i s = _mm256_set1_epi32(ncm[b]);
        const std::size_t qoff = b * 16;
        for (std::size_t i = 0; i < 8; ++i) {
          const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gp + i * 32));
          s = _mm256_dpbusd_avx_epi32(s, _mm256_and_si256(p, mask), _mm256_set1_epi32(qam[qoff + 2 * i + 0]));
          s = _mm256_dpbusd_avx_epi32(s, _mm256_and_si256(_mm256_srli_epi16(p, 4), mask), _mm256_set1_epi32(qam[qoff + 2 * i + 1]));
        }
        acc = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s), _mm256_mul_ps(_mm256_set1_ps(sam[b]), _mm256_loadu_ps(gs)), acc);
      }
      const __m256 val = _mm256_add_ps(acc, vbias);
      auto& top = tops[m];
      if (top.count < 9 || _mm256_movemask_ps(_mm256_cmp_ps(val, _mm256_set1_ps(top.scores[8]), _CMP_GT_OQ)) != 0) {
        alignas(32) float out8[8];
        _mm256_store_ps(out8, val);
        for (int c = 0; c < 8; ++c) top.push(out8[c], base_id + c);
      }
    }
  }
}

}  // namespace mtran::ssru::packed
