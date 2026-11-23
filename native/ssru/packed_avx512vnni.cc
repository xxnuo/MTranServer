// Compile this translation unit only with -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni
// (or /arch:AVX512 on MSVC); callers stay baseline.
#include "packed.h"
#include <immintrin.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#ifdef _MSC_VER
#pragma fp_contract(off)
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_AVX512_VNNI __declspec(noinline)
#else
#define MTRAN_AVX512_VNNI __attribute__((target("avx512f,avx512bw,avx512dq,avx512vl,avx512vnni"), noinline))
#endif

namespace mtran::ssru::packed {
namespace {

MTRAN_AVX512_VNNI inline float hmax256(__m256 v) noexcept {
  const __m128 lo = _mm256_castps256_ps128(v);
  const __m128 hi = _mm256_extractf128_ps(v, 1);
  const __m128 m4 = _mm_max_ps(lo, hi);
  const __m128 m2 = _mm_max_ps(m4, _mm_movehl_ps(m4, m4));
  const __m128 m1 = _mm_max_ss(m2, _mm_shuffle_ps(m2, m2, 1));
  return _mm_cvtss_f32(m1);
}

MTRAN_AVX512_VNNI inline float hmax512(__m512 v) noexcept {
  const __m256 lo = _mm512_castps512_ps256(v);
  const __m256 hi = _mm512_extractf32x8_ps(v, 1);
  return hmax256(_mm256_max_ps(lo, hi));
}

MTRAN_AVX512_VNNI inline void store16Avx512(
    __m512i acc, __m512i vzp, __m512i vsum, __m512 vscale,
    const float* fused_bias, const float* separate_bias, std::size_t col,
    float* dst, bool relu_minmax, __m512& vmax) noexcept {
  const __m512i corr = _mm512_sub_epi32(acc, _mm512_mullo_epi32(vzp, vsum));
  const __m512 f = _mm512_cvtepi32_ps(corr);
  __m512 res;
  if (fused_bias) {
    res = _mm512_fmadd_ps(f, vscale, _mm512_loadu_ps(fused_bias + col));
  } else if (separate_bias) {
    res = _mm512_add_ps(_mm512_mul_ps(f, vscale), _mm512_loadu_ps(separate_bias + col));
  } else {
    res = _mm512_mul_ps(f, vscale);
  }
  if (relu_minmax) {
    res = _mm512_max_ps(res, _mm512_setzero_ps());
    vmax = _mm512_max_ps(vmax, res);
  }
  _mm512_storeu_ps(dst, res);
}

MTRAN_AVX512_VNNI inline void store8Half512(
    __m256i acc8, std::int32_t zp_i, const std::int32_t* sums_ptr, float rscale,
    const float* fused_bias, const float* separate_bias, std::size_t col,
    float* dst, bool relu_minmax, float& max_scalar) noexcept {
  const __m256i vsum = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sums_ptr));
  const __m256i vzp = _mm256_set1_epi32(zp_i);
  const __m256i corr = _mm256_sub_epi32(acc8, _mm256_mullo_epi32(vzp, vsum));
  const __m256 f = _mm256_cvtepi32_ps(corr);
  const __m256 vs = _mm256_set1_ps(rscale);
  __m256 res;
  if (fused_bias) {
    res = _mm256_fmadd_ps(f, vs, _mm256_loadu_ps(fused_bias + col));
  } else if (separate_bias) {
    res = _mm256_add_ps(_mm256_mul_ps(f, vs), _mm256_loadu_ps(separate_bias + col));
  } else {
    res = _mm256_mul_ps(f, vs);
  }
  if (relu_minmax) {
    res = _mm256_max_ps(res, _mm256_setzero_ps());
    max_scalar = std::max(max_scalar, hmax256(res));
  }
  _mm256_storeu_ps(dst, res);
}

MTRAN_AVX512_VNNI inline void gather16Rows8K(
    const std::int8_t* const rows[16], std::size_t k, __m512i& wb0, __m512i& wb1) noexcept {
  auto load64 = [&](std::size_t r) noexcept -> std::int64_t {
    std::int64_t v = 0;
    std::memcpy(&v, rows[r] + k, 8);
    return v;
  };
  const __m512i p0 = _mm512_set_epi64(
      load64(7), load64(6), load64(5), load64(4),
      load64(3), load64(2), load64(1), load64(0));
  const __m512i p1 = _mm512_set_epi64(
      load64(15), load64(14), load64(13), load64(12),
      load64(11), load64(10), load64(9), load64(8));
  const __m512i even_idx = _mm512_set_epi32(
      30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
  const __m512i odd_idx = _mm512_set_epi32(
      31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);
  wb0 = _mm512_permutex2var_epi32(p0, even_idx, p1);
  wb1 = _mm512_permutex2var_epi32(p0, odd_idx, p1);
}

MTRAN_AVX512_VNNI inline void gather8Rows8K(
    const std::int8_t* const rows[8], std::size_t k, __m256i& wb0, __m256i& wb1) noexcept {
  auto load64 = [&](std::size_t r) noexcept -> std::int64_t {
    std::int64_t v = 0;
    std::memcpy(&v, rows[r] + k, 8);
    return v;
  };
  const __m256i lo = _mm256_set_epi64x(load64(3), load64(2), load64(1), load64(0));
  const __m256i hi = _mm256_set_epi64x(load64(7), load64(6), load64(5), load64(4));
  const __m256i even_idx = _mm256_set_epi32(14, 12, 10, 8, 6, 4, 2, 0);
  const __m256i odd_idx = _mm256_set_epi32(15, 13, 11, 9, 7, 5, 3, 1);
  wb0 = _mm256_permutex2var_epi32(lo, even_idx, hi);
  wb1 = _mm256_permutex2var_epi32(lo, odd_idx, hi);
}

MTRAN_AVX512_VNNI inline float hmax512ps(__m512 v) noexcept {
  return hmax512(v);
}

}  // namespace

MTRAN_AVX512_VNNI void multiplyBatchK4Avx512Vnni(
    const std::uint8_t* a_q, std::size_t a_stride, const float* a_scales,
    const std::uint8_t* a_zps, const std::int8_t* b_k4_512, const std::int32_t* sums,
    float w_scale, std::size_t rows, std::size_t k, std::size_t col_begin,
    std::size_t col_end, float* out, std::size_t out_stride,
    const float* fused_bias, const float* separate_bias, bool relu_minmax,
    float* out_maxs) {
  if (rows == 0 || col_begin >= col_end) return;
  if (relu_minmax && out_maxs) {
    for (std::size_t m = 0; m < rows; ++m) out_maxs[m] = 0.0f;
  }

  std::size_t col = col_begin;

  // Handle leading unaligned 8-column strip when col_begin % 16 == 8.
  if ((col & 15u) != 0 && col + 8 <= col_end) {
    const std::size_t tile = col & ~std::size_t(15);
    const std::int8_t* bt = b_k4_512 + tile * k;
    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* am = a_q + m * a_stride;
      __m512i acc = _mm512_setzero_si512();
      const std::int8_t* bp = bt;
      for (std::size_t p = 0; p < k; p += 4, bp += 64) {
        std::int32_t av = 0;
        std::memcpy(&av, am + p, 4);
        acc = _mm512_dpbusd_epi32(acc, _mm512_set1_epi32(av),
                                  _mm512_loadu_si512(bp));
      }
      const __m256i acc8 = _mm512_extracti32x8_epi32(acc, 1);
      float dummy_max = 0.0f;
      float& row_max = (relu_minmax && out_maxs) ? out_maxs[m] : dummy_max;
      store8Half512(acc8, static_cast<std::int32_t>(a_zps[m]), sums + col,
                   a_scales[m] * w_scale, fused_bias, separate_bias, col,
                   out + m * out_stride + col, relu_minmax, row_max);
    }
    col += 8;
  }

  if (rows == 1) {
    const std::int32_t zp_i = static_cast<std::int32_t>(a_zps[0]);
    const __m512i vzp = _mm512_set1_epi32(zp_i);
    const __m512 vscale = _mm512_set1_ps(a_scales[0] * w_scale);
    __m512 vmax = _mm512_setzero_ps();
    const std::uint8_t* a0 = a_q;
    float* out0 = out;

    for (; col + 64 <= col_end; col += 64) {
      const std::int8_t* b0 = b_k4_512 + (col + 0) * k;
      const std::int8_t* b1 = b_k4_512 + (col + 16) * k;
      const std::int8_t* b2 = b_k4_512 + (col + 32) * k;
      const std::int8_t* b3 = b_k4_512 + (col + 48) * k;
      __m512i acc0 = _mm512_setzero_si512();
      __m512i acc1 = _mm512_setzero_si512();
      __m512i acc2 = _mm512_setzero_si512();
      __m512i acc3 = _mm512_setzero_si512();
      for (std::size_t p = 0; p < k; p += 8, b0 += 128, b1 += 128, b2 += 128, b3 += 128) {
        std::int32_t av0 = 0, av1 = 0;
        std::memcpy(&av0, a0 + p, 4);
        std::memcpy(&av1, a0 + p + 4, 4);
        const __m512i va0 = _mm512_set1_epi32(av0);
        const __m512i va1 = _mm512_set1_epi32(av1);
        acc0 = _mm512_dpbusd_epi32(acc0, va0, _mm512_loadu_si512(b0));
        acc1 = _mm512_dpbusd_epi32(acc1, va0, _mm512_loadu_si512(b1));
        acc2 = _mm512_dpbusd_epi32(acc2, va0, _mm512_loadu_si512(b2));
        acc3 = _mm512_dpbusd_epi32(acc3, va0, _mm512_loadu_si512(b3));
        acc0 = _mm512_dpbusd_epi32(acc0, va1, _mm512_loadu_si512(b0 + 64));
        acc1 = _mm512_dpbusd_epi32(acc1, va1, _mm512_loadu_si512(b1 + 64));
        acc2 = _mm512_dpbusd_epi32(acc2, va1, _mm512_loadu_si512(b2 + 64));
        acc3 = _mm512_dpbusd_epi32(acc3, va1, _mm512_loadu_si512(b3 + 64));
      }
      store16Avx512(acc0, vzp, _mm512_loadu_si512(sums + col + 0), vscale, fused_bias,
                    separate_bias, col + 0, out0 + col + 0, relu_minmax, vmax);
      store16Avx512(acc1, vzp, _mm512_loadu_si512(sums + col + 16), vscale, fused_bias,
                    separate_bias, col + 16, out0 + col + 16, relu_minmax, vmax);
      store16Avx512(acc2, vzp, _mm512_loadu_si512(sums + col + 32), vscale, fused_bias,
                    separate_bias, col + 32, out0 + col + 32, relu_minmax, vmax);
      store16Avx512(acc3, vzp, _mm512_loadu_si512(sums + col + 48), vscale, fused_bias,
                    separate_bias, col + 48, out0 + col + 48, relu_minmax, vmax);
    }
    for (; col + 16 <= col_end; col += 16) {
      const std::int8_t* b0 = b_k4_512 + col * k;
      __m512i acc0 = _mm512_setzero_si512();
      for (std::size_t p = 0; p < k; p += 8, b0 += 128) {
        std::int32_t av0 = 0, av1 = 0;
        std::memcpy(&av0, a0 + p, 4);
        std::memcpy(&av1, a0 + p + 4, 4);
        acc0 = _mm512_dpbusd_epi32(acc0, _mm512_set1_epi32(av0), _mm512_loadu_si512(b0));
        acc0 = _mm512_dpbusd_epi32(acc0, _mm512_set1_epi32(av1), _mm512_loadu_si512(b0 + 64));
      }
      store16Avx512(acc0, vzp, _mm512_loadu_si512(sums + col), vscale, fused_bias,
                    separate_bias, col, out0 + col, relu_minmax, vmax);
    }
    if (relu_minmax && out_maxs) {
      out_maxs[0] = std::max(out_maxs[0], hmax512(vmax));
    }
    if (col + 8 <= col_end) {
      const std::int8_t* b0 = b_k4_512 + col * k;
      __m512i acc0 = _mm512_setzero_si512();
      for (std::size_t p = 0; p < k; p += 4, b0 += 64) {
        std::int32_t av = 0;
        std::memcpy(&av, a0 + p, 4);
        acc0 = _mm512_dpbusd_epi32(acc0, _mm512_set1_epi32(av), _mm512_loadu_si512(b0));
      }
      float dummy_max = 0.0f;
      float& row_max = (relu_minmax && out_maxs) ? out_maxs[0] : dummy_max;
      store8Half512(_mm512_castsi512_si256(acc0), zp_i, sums + col, a_scales[0] * w_scale,
                   fused_bias, separate_bias, col, out0 + col, relu_minmax, row_max);
    }
    return;
  }

  // Batched rows >= 2: col outer (16 cols * <= 2048 K = <= 32 KB fits in L1D), m inner.
  for (; col + 16 <= col_end; col += 16) {
    const std::int8_t* b_tile = b_k4_512 + col * k;
    const __m512i vsum = _mm512_loadu_si512(sums + col);
    std::size_t m = 0;

    for (; m + 8 <= rows; m += 8) {
      const std::uint8_t* a0 = a_q + (m + 0) * a_stride;
      const std::uint8_t* a1 = a_q + (m + 1) * a_stride;
      const std::uint8_t* a2 = a_q + (m + 2) * a_stride;
      const std::uint8_t* a3 = a_q + (m + 3) * a_stride;
      const std::uint8_t* a4 = a_q + (m + 4) * a_stride;
      const std::uint8_t* a5 = a_q + (m + 5) * a_stride;
      const std::uint8_t* a6 = a_q + (m + 6) * a_stride;
      const std::uint8_t* a7 = a_q + (m + 7) * a_stride;
      __m512i c0 = _mm512_setzero_si512(), c1 = _mm512_setzero_si512();
      __m512i c2 = _mm512_setzero_si512(), c3 = _mm512_setzero_si512();
      __m512i c4 = _mm512_setzero_si512(), c5 = _mm512_setzero_si512();
      __m512i c6 = _mm512_setzero_si512(), c7 = _mm512_setzero_si512();
      const std::int8_t* bp = b_tile;
      for (std::size_t p = 0; p < k; p += 4, bp += 64) {
        const __m512i wb = _mm512_loadu_si512(bp);
        std::int32_t v0, v1, v2, v3, v4, v5, v6, v7;
        std::memcpy(&v0, a0 + p, 4); std::memcpy(&v1, a1 + p, 4);
        std::memcpy(&v2, a2 + p, 4); std::memcpy(&v3, a3 + p, 4);
        std::memcpy(&v4, a4 + p, 4); std::memcpy(&v5, a5 + p, 4);
        std::memcpy(&v6, a6 + p, 4); std::memcpy(&v7, a7 + p, 4);
        c0 = _mm512_dpbusd_epi32(c0, _mm512_set1_epi32(v0), wb);
        c1 = _mm512_dpbusd_epi32(c1, _mm512_set1_epi32(v1), wb);
        c2 = _mm512_dpbusd_epi32(c2, _mm512_set1_epi32(v2), wb);
        c3 = _mm512_dpbusd_epi32(c3, _mm512_set1_epi32(v3), wb);
        c4 = _mm512_dpbusd_epi32(c4, _mm512_set1_epi32(v4), wb);
        c5 = _mm512_dpbusd_epi32(c5, _mm512_set1_epi32(v5), wb);
        c6 = _mm512_dpbusd_epi32(c6, _mm512_set1_epi32(v6), wb);
        c7 = _mm512_dpbusd_epi32(c7, _mm512_set1_epi32(v7), wb);
      }
      const __m512i cs[8] = {c0, c1, c2, c3, c4, c5, c6, c7};
      for (std::size_t i = 0; i < 8; ++i) {
        __m512 vmax = _mm512_setzero_ps();
        store16Avx512(cs[i], _mm512_set1_epi32(static_cast<std::int32_t>(a_zps[m + i])), vsum,
                      _mm512_set1_ps(a_scales[m + i] * w_scale), fused_bias, separate_bias, col,
                      out + (m + i) * out_stride + col, relu_minmax, vmax);
        if (relu_minmax && out_maxs) {
          out_maxs[m + i] = std::max(out_maxs[m + i], hmax512(vmax));
        }
      }
    }
    for (; m + 4 <= rows; m += 4) {
      const std::uint8_t* a0 = a_q + (m + 0) * a_stride;
      const std::uint8_t* a1 = a_q + (m + 1) * a_stride;
      const std::uint8_t* a2 = a_q + (m + 2) * a_stride;
      const std::uint8_t* a3 = a_q + (m + 3) * a_stride;
      __m512i c0 = _mm512_setzero_si512(), c1 = _mm512_setzero_si512();
      __m512i c2 = _mm512_setzero_si512(), c3 = _mm512_setzero_si512();
      const std::int8_t* bp = b_tile;
      for (std::size_t p = 0; p < k; p += 4, bp += 64) {
        const __m512i wb = _mm512_loadu_si512(bp);
        std::int32_t v0, v1, v2, v3;
        std::memcpy(&v0, a0 + p, 4); std::memcpy(&v1, a1 + p, 4);
        std::memcpy(&v2, a2 + p, 4); std::memcpy(&v3, a3 + p, 4);
        c0 = _mm512_dpbusd_epi32(c0, _mm512_set1_epi32(v0), wb);
        c1 = _mm512_dpbusd_epi32(c1, _mm512_set1_epi32(v1), wb);
        c2 = _mm512_dpbusd_epi32(c2, _mm512_set1_epi32(v2), wb);
        c3 = _mm512_dpbusd_epi32(c3, _mm512_set1_epi32(v3), wb);
      }
      const __m512i cs[4] = {c0, c1, c2, c3};
      for (std::size_t i = 0; i < 4; ++i) {
        __m512 vmax = _mm512_setzero_ps();
        store16Avx512(cs[i], _mm512_set1_epi32(static_cast<std::int32_t>(a_zps[m + i])), vsum,
                      _mm512_set1_ps(a_scales[m + i] * w_scale), fused_bias, separate_bias, col,
                      out + (m + i) * out_stride + col, relu_minmax, vmax);
        if (relu_minmax && out_maxs) {
          out_maxs[m + i] = std::max(out_maxs[m + i], hmax512(vmax));
        }
      }
    }
    for (; m < rows; ++m) {
      const std::uint8_t* am = a_q + m * a_stride;
      __m512i c = _mm512_setzero_si512();
      const std::int8_t* bp = b_tile;
      for (std::size_t p = 0; p < k; p += 4, bp += 64) {
        std::int32_t v0 = 0;
        std::memcpy(&v0, am + p, 4);
        c = _mm512_dpbusd_epi32(c, _mm512_set1_epi32(v0), _mm512_loadu_si512(bp));
      }
      __m512 vmax = _mm512_setzero_ps();
      store16Avx512(c, _mm512_set1_epi32(static_cast<std::int32_t>(a_zps[m])), vsum,
                    _mm512_set1_ps(a_scales[m] * w_scale), fused_bias, separate_bias, col,
                    out + m * out_stride + col, relu_minmax, vmax);
      if (relu_minmax && out_maxs) {
        out_maxs[m] = std::max(out_maxs[m], hmax512(vmax));
      }
    }
  }

  if (col + 8 <= col_end) {
    const std::int8_t* b_tile = b_k4_512 + col * k;
    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* am = a_q + m * a_stride;
      __m512i acc = _mm512_setzero_si512();
      const std::int8_t* bp = b_tile;
      for (std::size_t p = 0; p < k; p += 4, bp += 64) {
        std::int32_t av = 0;
        std::memcpy(&av, am + p, 4);
        acc = _mm512_dpbusd_epi32(acc, _mm512_set1_epi32(av), _mm512_loadu_si512(bp));
      }
      float dummy_max = 0.0f;
      float& row_max = (relu_minmax && out_maxs) ? out_maxs[m] : dummy_max;
      store8Half512(_mm512_castsi512_si256(acc), static_cast<std::int32_t>(a_zps[m]), sums + col,
                   a_scales[m] * w_scale, fused_bias, separate_bias, col,
                   out + m * out_stride + col, relu_minmax, row_max);
    }
  }
}

MTRAN_AVX512_VNNI ShortlistTable::ScanResult scanShortlistAvx512Vnni(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out,
    const std::int8_t* hot_q8_k4_512, const ShortlistTable::RowMeta* hot_meta,
    std::size_t hot_prefix_len, bool exact_int8, float exact_scale) {
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
  const __m512i vzp512 = _mm512_set1_epi32(zp_i);
  const __m512 vxscale512 = _mm512_set1_ps(x_scale);
  const __m512 vexact512 = _mm512_set1_ps(x_scale * exact_scale);

  auto record16 = [&](std::size_t j_base, __m512 vals) {
    _mm512_storeu_ps(approx_out + j_base, vals);
    if (_mm512_cmp_ps_mask(vals, _mm512_set1_ps(second_logit), _CMP_GE_OQ) != 0) {
      for (int lane = 0; lane < 16; ++lane) {
        const float val = approx_out[j_base + lane];
        if (val > best_logit) {
          second_logit = best_logit;
          best_logit = val;
          best_j = j_base + lane;
        } else if (val > second_logit) {
          second_logit = val;
        }
      }
    }
  };

  std::size_t j = 0;
  if (hot_q8_k4_512 && hot_prefix_len > 0) {
    for (; j + 16 <= hot_prefix_len; j += 16) {
      const std::int8_t* bp = hot_q8_k4_512 + j * dim;
      __m512i acc = _mm512_setzero_si512();
      for (std::size_t k = 0; k < dim; k += 8, bp += 128) {
        std::int32_t v0 = 0, v1 = 0;
        std::memcpy(&v0, x_u8 + k, 4);
        std::memcpy(&v1, x_u8 + k + 4, 4);
        acc = _mm512_dpbusd_epi32(acc, _mm512_set1_epi32(v0), _mm512_loadu_si512(bp));
        acc = _mm512_dpbusd_epi32(acc, _mm512_set1_epi32(v1), _mm512_loadu_si512(bp + 64));
      }
      const auto* hm = hot_meta + j;
      const __m512i rsums = _mm512_set_epi32(
          hm[15].sum, hm[14].sum, hm[13].sum, hm[12].sum,
          hm[11].sum, hm[10].sum, hm[9].sum, hm[8].sum,
          hm[7].sum, hm[6].sum, hm[5].sum, hm[4].sum,
          hm[3].sum, hm[2].sum, hm[1].sum, hm[0].sum);
      const __m512 rscales = exact_int8
          ? vexact512
          : _mm512_mul_ps(vxscale512, _mm512_set_ps(
                hm[15].scale, hm[14].scale, hm[13].scale, hm[12].scale,
                hm[11].scale, hm[10].scale, hm[9].scale, hm[8].scale,
                hm[7].scale, hm[6].scale, hm[5].scale, hm[4].scale,
                hm[3].scale, hm[2].scale, hm[1].scale, hm[0].scale));
      const __m512 rbias = _mm512_set_ps(
          hm[15].bias, hm[14].bias, hm[13].bias, hm[12].bias,
          hm[11].bias, hm[10].bias, hm[9].bias, hm[8].bias,
          hm[7].bias, hm[6].bias, hm[5].bias, hm[4].bias,
          hm[3].bias, hm[2].bias, hm[1].bias, hm[0].bias);
      const __m512 corr = _mm512_cvtepi32_ps(_mm512_sub_epi32(acc, _mm512_mullo_epi32(vzp512, rsums)));
      record16(j, _mm512_add_ps(_mm512_mul_ps(corr, rscales), rbias));
    }
  }

  for (; j + 16 <= candidate_count; j += 16) {
    if (j + 32 <= candidate_count) {
      for (int p = 0; p < 16; ++p) {
        const auto nt = static_cast<std::size_t>(candidate_ids[j + 16 + p]);
        _mm_prefetch(reinterpret_cast<const char*>(emb_q8 + nt * dim), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(meta + nt), _MM_HINT_T0);
      }
    }
    const std::int8_t* rows[16];
    std::size_t ts[16];
    for (int l = 0; l < 16; ++l) {
      ts[l] = static_cast<std::size_t>(candidate_ids[j + l]);
      rows[l] = emb_q8 + ts[l] * dim;
    }
    __m512i acc = _mm512_setzero_si512();
    for (std::size_t k = 0; k < dim; k += 8) {
      __m512i wb0, wb1;
      gather16Rows8K(rows, k, wb0, wb1);
      std::int32_t v0 = 0, v1 = 0;
      std::memcpy(&v0, x_u8 + k, 4);
      std::memcpy(&v1, x_u8 + k + 4, 4);
      acc = _mm512_dpbusd_epi32(acc, _mm512_set1_epi32(v0), wb0);
      acc = _mm512_dpbusd_epi32(acc, _mm512_set1_epi32(v1), wb1);
    }
    const __m512i rsums = _mm512_set_epi32(
        meta[ts[15]].sum, meta[ts[14]].sum, meta[ts[13]].sum, meta[ts[12]].sum,
        meta[ts[11]].sum, meta[ts[10]].sum, meta[ts[9]].sum, meta[ts[8]].sum,
        meta[ts[7]].sum, meta[ts[6]].sum, meta[ts[5]].sum, meta[ts[4]].sum,
        meta[ts[3]].sum, meta[ts[2]].sum, meta[ts[1]].sum, meta[ts[0]].sum);
    const __m512 rscales = exact_int8
        ? vexact512
        : _mm512_mul_ps(vxscale512, _mm512_set_ps(
              meta[ts[15]].scale, meta[ts[14]].scale, meta[ts[13]].scale, meta[ts[12]].scale,
              meta[ts[11]].scale, meta[ts[10]].scale, meta[ts[9]].scale, meta[ts[8]].scale,
              meta[ts[7]].scale, meta[ts[6]].scale, meta[ts[5]].scale, meta[ts[4]].scale,
              meta[ts[3]].scale, meta[ts[2]].scale, meta[ts[1]].scale, meta[ts[0]].scale));
    const __m512 rbias = _mm512_set_ps(
        meta[ts[15]].bias, meta[ts[14]].bias, meta[ts[13]].bias, meta[ts[12]].bias,
        meta[ts[11]].bias, meta[ts[10]].bias, meta[ts[9]].bias, meta[ts[8]].bias,
        meta[ts[7]].bias, meta[ts[6]].bias, meta[ts[5]].bias, meta[ts[4]].bias,
        meta[ts[3]].bias, meta[ts[2]].bias, meta[ts[1]].bias, meta[ts[0]].bias);
    const __m512 corr = _mm512_cvtepi32_ps(_mm512_sub_epi32(acc, _mm512_mullo_epi32(vzp512, rsums)));
    record16(j, _mm512_add_ps(_mm512_mul_ps(corr, rscales), rbias));
  }

  for (; j + 8 <= candidate_count; j += 8) {
    const std::int8_t* rows[8];
    std::size_t ts[8];
    for (int l = 0; l < 8; ++l) {
      ts[l] = static_cast<std::size_t>(candidate_ids[j + l]);
      rows[l] = emb_q8 + ts[l] * dim;
    }
    __m256i acc = _mm256_setzero_si256();
    for (std::size_t k = 0; k < dim; k += 8) {
      __m256i wb0, wb1;
      gather8Rows8K(rows, k, wb0, wb1);
      std::int32_t v0 = 0, v1 = 0;
      std::memcpy(&v0, x_u8 + k, 4);
      std::memcpy(&v1, x_u8 + k + 4, 4);
      acc = _mm256_dpbusd_epi32(acc, _mm256_set1_epi32(v0), wb0);
      acc = _mm256_dpbusd_epi32(acc, _mm256_set1_epi32(v1), wb1);
    }
    const __m256i rsums = _mm256_setr_epi32(
        meta[ts[0]].sum, meta[ts[1]].sum, meta[ts[2]].sum, meta[ts[3]].sum,
        meta[ts[4]].sum, meta[ts[5]].sum, meta[ts[6]].sum, meta[ts[7]].sum);
    const __m256 rscales = exact_int8
        ? _mm256_set1_ps(x_scale * exact_scale)
        : _mm256_mul_ps(_mm256_set1_ps(x_scale), _mm256_setr_ps(
              meta[ts[0]].scale, meta[ts[1]].scale, meta[ts[2]].scale, meta[ts[3]].scale,
              meta[ts[4]].scale, meta[ts[5]].scale, meta[ts[6]].scale, meta[ts[7]].scale));
    const __m256 rbias = _mm256_setr_ps(
        meta[ts[0]].bias, meta[ts[1]].bias, meta[ts[2]].bias, meta[ts[3]].bias,
        meta[ts[4]].bias, meta[ts[5]].bias, meta[ts[6]].bias, meta[ts[7]].bias);
    const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(acc, _mm256_mullo_epi32(_mm256_set1_epi32(zp_i), rsums)));
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
    const std::int8_t* erow = emb_q8 + t * dim;
    std::int32_t acc = 0;
    for (std::size_t k = 0; k < dim; ++k) {
      acc += static_cast<std::int32_t>(x_u8[k]) * static_cast<std::int32_t>(erow[k]);
    }
    const auto& rm = meta[t];
    const float sc = exact_int8 ? (x_scale * exact_scale) : (x_scale * rm.scale);
    const float val = static_cast<float>(acc - zp_i * rm.sum) * sc + rm.bias;
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

MTRAN_AVX512_VNNI void scanShortlistAvx512VnniBatch(
    const std::uint8_t* x_u8, std::size_t x_stride, const float* x_scales,
    const std::uint8_t* x_zps, std::size_t rows, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* union_ids, const std::uint32_t* slot_masks,
    std::size_t union_count, float* approx_out, float* best_logits,
    std::size_t* best_js, float* second_logits, bool* has_any,
    const std::int8_t* hot_q8_k4_512, const ShortlistTable::RowMeta* hot_meta,
    std::size_t hot_prefix_len, bool exact_int8, float exact_scale) {
  for (std::size_t m = 0; m < rows; ++m) {
    best_logits[m] = -std::numeric_limits<float>::infinity();
    second_logits[m] = -std::numeric_limits<float>::infinity();
    best_js[m] = 0;
    has_any[m] = false;
  }

  auto update16 = [&](std::size_t m, std::size_t j, __m512 vals) {
    _mm512_storeu_ps(approx_out + m * union_count + j, vals);
    std::uint32_t active = 0;
    const std::uint32_t bit = 1u << m;
    for (int l = 0; l < 16; ++l) active |= ((slot_masks[j + l] & bit) != 0) ? (1u << l) : 0u;
    if (!active) return;
    std::uint32_t hit = has_any[m]
        ? (static_cast<std::uint32_t>(_mm512_cmp_ps_mask(vals, _mm512_set1_ps(second_logits[m]), _CMP_GE_OQ)) & active)
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
  if (hot_q8_k4_512 && hot_prefix_len > 0) {
    for (; j + 16 <= hot_prefix_len; j += 16) {
      const auto* hm = hot_meta + j;
      const __m512i rsums = _mm512_set_epi32(
          hm[15].sum, hm[14].sum, hm[13].sum, hm[12].sum,
          hm[11].sum, hm[10].sum, hm[9].sum, hm[8].sum,
          hm[7].sum, hm[6].sum, hm[5].sum, hm[4].sum,
          hm[3].sum, hm[2].sum, hm[1].sum, hm[0].sum);
      const __m512 raw_rscales = _mm512_set_ps(
          hm[15].scale, hm[14].scale, hm[13].scale, hm[12].scale,
          hm[11].scale, hm[10].scale, hm[9].scale, hm[8].scale,
          hm[7].scale, hm[6].scale, hm[5].scale, hm[4].scale,
          hm[3].scale, hm[2].scale, hm[1].scale, hm[0].scale);
      const __m512 rbias = _mm512_set_ps(
          hm[15].bias, hm[14].bias, hm[13].bias, hm[12].bias,
          hm[11].bias, hm[10].bias, hm[9].bias, hm[8].bias,
          hm[7].bias, hm[6].bias, hm[5].bias, hm[4].bias,
          hm[3].bias, hm[2].bias, hm[1].bias, hm[0].bias);
      std::size_t m = 0;
      for (; m + 4 <= rows; m += 4) {
        const std::uint8_t* x0 = x_u8 + (m + 0) * x_stride;
        const std::uint8_t* x1 = x_u8 + (m + 1) * x_stride;
        const std::uint8_t* x2 = x_u8 + (m + 2) * x_stride;
        const std::uint8_t* x3 = x_u8 + (m + 3) * x_stride;
        const std::int8_t* bp = hot_q8_k4_512 + j * dim;
        __m512i c0 = _mm512_setzero_si512(), c1 = _mm512_setzero_si512();
        __m512i c2 = _mm512_setzero_si512(), c3 = _mm512_setzero_si512();
        for (std::size_t k = 0; k < dim; k += 4, bp += 64) {
          const __m512i wb = _mm512_loadu_si512(bp);
          std::int32_t v0, v1, v2, v3;
          std::memcpy(&v0, x0 + k, 4); std::memcpy(&v1, x1 + k, 4);
          std::memcpy(&v2, x2 + k, 4); std::memcpy(&v3, x3 + k, 4);
          c0 = _mm512_dpbusd_epi32(c0, _mm512_set1_epi32(v0), wb);
          c1 = _mm512_dpbusd_epi32(c1, _mm512_set1_epi32(v1), wb);
          c2 = _mm512_dpbusd_epi32(c2, _mm512_set1_epi32(v2), wb);
          c3 = _mm512_dpbusd_epi32(c3, _mm512_set1_epi32(v3), wb);
        }
        const __m512i cs[4] = {c0, c1, c2, c3};
        for (std::size_t i = 0; i < 4; ++i) {
          const __m512i vzp = _mm512_set1_epi32(static_cast<std::int32_t>(x_zps[m + i]));
          const __m512 rsc = exact_int8 ? _mm512_set1_ps(x_scales[m + i] * exact_scale)
                                        : _mm512_mul_ps(_mm512_set1_ps(x_scales[m + i]), raw_rscales);
          const __m512 corr = _mm512_cvtepi32_ps(_mm512_sub_epi32(cs[i], _mm512_mullo_epi32(vzp, rsums)));
          update16(m + i, j, _mm512_add_ps(_mm512_mul_ps(corr, rsc), rbias));
        }
      }
      for (; m < rows; ++m) {
        const std::uint8_t* xm = x_u8 + m * x_stride;
        const std::int8_t* bp = hot_q8_k4_512 + j * dim;
        __m512i c = _mm512_setzero_si512();
        for (std::size_t k = 0; k < dim; k += 4, bp += 64) {
          std::int32_t v0 = 0;
          std::memcpy(&v0, xm + k, 4);
          c = _mm512_dpbusd_epi32(c, _mm512_set1_epi32(v0), _mm512_loadu_si512(bp));
        }
        const __m512i vzp = _mm512_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
        const __m512 rsc = exact_int8 ? _mm512_set1_ps(x_scales[m] * exact_scale)
                                      : _mm512_mul_ps(_mm512_set1_ps(x_scales[m]), raw_rscales);
        const __m512 corr = _mm512_cvtepi32_ps(_mm512_sub_epi32(c, _mm512_mullo_epi32(vzp, rsums)));
        update16(m, j, _mm512_add_ps(_mm512_mul_ps(corr, rsc), rbias));
      }
    }
  }

  for (; j + 16 <= union_count; j += 16) {
    if (j + 32 <= union_count) {
      for (int p = 0; p < 16; ++p) {
        const auto nt = static_cast<std::size_t>(union_ids[j + 16 + p]);
        _mm_prefetch(reinterpret_cast<const char*>(emb_q8 + nt * dim), _MM_HINT_T0);
        _mm_prefetch(reinterpret_cast<const char*>(meta + nt), _MM_HINT_T0);
      }
    }
    const std::int8_t* row_ptrs[16];
    std::size_t ts[16];
    for (int l = 0; l < 16; ++l) {
      ts[l] = static_cast<std::size_t>(union_ids[j + l]);
      row_ptrs[l] = emb_q8 + ts[l] * dim;
    }
    const __m512i rsums = _mm512_set_epi32(
        meta[ts[15]].sum, meta[ts[14]].sum, meta[ts[13]].sum, meta[ts[12]].sum,
        meta[ts[11]].sum, meta[ts[10]].sum, meta[ts[9]].sum, meta[ts[8]].sum,
        meta[ts[7]].sum, meta[ts[6]].sum, meta[ts[5]].sum, meta[ts[4]].sum,
        meta[ts[3]].sum, meta[ts[2]].sum, meta[ts[1]].sum, meta[ts[0]].sum);
    const __m512 raw_rscales = _mm512_set_ps(
        meta[ts[15]].scale, meta[ts[14]].scale, meta[ts[13]].scale, meta[ts[12]].scale,
        meta[ts[11]].scale, meta[ts[10]].scale, meta[ts[9]].scale, meta[ts[8]].scale,
        meta[ts[7]].scale, meta[ts[6]].scale, meta[ts[5]].scale, meta[ts[4]].scale,
        meta[ts[3]].scale, meta[ts[2]].scale, meta[ts[1]].scale, meta[ts[0]].scale);
    const __m512 rbias = _mm512_set_ps(
        meta[ts[15]].bias, meta[ts[14]].bias, meta[ts[13]].bias, meta[ts[12]].bias,
        meta[ts[11]].bias, meta[ts[10]].bias, meta[ts[9]].bias, meta[ts[8]].bias,
        meta[ts[7]].bias, meta[ts[6]].bias, meta[ts[5]].bias, meta[ts[4]].bias,
        meta[ts[3]].bias, meta[ts[2]].bias, meta[ts[1]].bias, meta[ts[0]].bias);

    std::size_t m = 0;
    for (; m + 4 <= rows; m += 4) {
      const std::uint8_t* x0 = x_u8 + (m + 0) * x_stride;
      const std::uint8_t* x1 = x_u8 + (m + 1) * x_stride;
      const std::uint8_t* x2 = x_u8 + (m + 2) * x_stride;
      const std::uint8_t* x3 = x_u8 + (m + 3) * x_stride;
      __m512i c0 = _mm512_setzero_si512(), c1 = _mm512_setzero_si512();
      __m512i c2 = _mm512_setzero_si512(), c3 = _mm512_setzero_si512();
      for (std::size_t k = 0; k < dim; k += 8) {
        __m512i wb0, wb1;
        gather16Rows8K(row_ptrs, k, wb0, wb1);
        std::int32_t v0, v1, v2, v3;
        std::memcpy(&v0, x0 + k, 4); std::memcpy(&v1, x1 + k, 4);
        std::memcpy(&v2, x2 + k, 4); std::memcpy(&v3, x3 + k, 4);
        c0 = _mm512_dpbusd_epi32(c0, _mm512_set1_epi32(v0), wb0);
        c1 = _mm512_dpbusd_epi32(c1, _mm512_set1_epi32(v1), wb0);
        c2 = _mm512_dpbusd_epi32(c2, _mm512_set1_epi32(v2), wb0);
        c3 = _mm512_dpbusd_epi32(c3, _mm512_set1_epi32(v3), wb0);
        std::memcpy(&v0, x0 + k + 4, 4); std::memcpy(&v1, x1 + k + 4, 4);
        std::memcpy(&v2, x2 + k + 4, 4); std::memcpy(&v3, x3 + k + 4, 4);
        c0 = _mm512_dpbusd_epi32(c0, _mm512_set1_epi32(v0), wb1);
        c1 = _mm512_dpbusd_epi32(c1, _mm512_set1_epi32(v1), wb1);
        c2 = _mm512_dpbusd_epi32(c2, _mm512_set1_epi32(v2), wb1);
        c3 = _mm512_dpbusd_epi32(c3, _mm512_set1_epi32(v3), wb1);
      }
      const __m512i cs[4] = {c0, c1, c2, c3};
      for (std::size_t i = 0; i < 4; ++i) {
        const __m512i vzp = _mm512_set1_epi32(static_cast<std::int32_t>(x_zps[m + i]));
        const __m512 rsc = exact_int8 ? _mm512_set1_ps(x_scales[m + i] * exact_scale)
                                      : _mm512_mul_ps(_mm512_set1_ps(x_scales[m + i]), raw_rscales);
        const __m512 corr = _mm512_cvtepi32_ps(_mm512_sub_epi32(cs[i], _mm512_mullo_epi32(vzp, rsums)));
        update16(m + i, j, _mm512_add_ps(_mm512_mul_ps(corr, rsc), rbias));
      }
    }
    for (; m < rows; ++m) {
      const std::uint8_t* xm = x_u8 + m * x_stride;
      __m512i c = _mm512_setzero_si512();
      for (std::size_t k = 0; k < dim; k += 8) {
        __m512i wb0, wb1;
        gather16Rows8K(row_ptrs, k, wb0, wb1);
        std::int32_t v0 = 0, v1 = 0;
        std::memcpy(&v0, xm + k, 4);
        std::memcpy(&v1, xm + k + 4, 4);
        c = _mm512_dpbusd_epi32(c, _mm512_set1_epi32(v0), wb0);
        c = _mm512_dpbusd_epi32(c, _mm512_set1_epi32(v1), wb1);
      }
      const __m512i vzp = _mm512_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
      const __m512 rsc = exact_int8 ? _mm512_set1_ps(x_scales[m] * exact_scale)
                                    : _mm512_mul_ps(_mm512_set1_ps(x_scales[m]), raw_rscales);
      const __m512 corr = _mm512_cvtepi32_ps(_mm512_sub_epi32(c, _mm512_mullo_epi32(vzp, rsums)));
      update16(m, j, _mm512_add_ps(_mm512_mul_ps(corr, rsc), rbias));
    }
  }

  for (; j + 8 <= union_count; j += 8) {
    const std::int8_t* row_ptrs[8];
    std::size_t ts[8];
    for (int l = 0; l < 8; ++l) {
      ts[l] = static_cast<std::size_t>(union_ids[j + l]);
      row_ptrs[l] = emb_q8 + ts[l] * dim;
    }
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
      __m256i c = _mm256_setzero_si256();
      for (std::size_t k = 0; k < dim; k += 8) {
        __m256i wb0, wb1;
        gather8Rows8K(row_ptrs, k, wb0, wb1);
        std::int32_t v0 = 0, v1 = 0;
        std::memcpy(&v0, xm + k, 4);
        std::memcpy(&v1, xm + k + 4, 4);
        c = _mm256_dpbusd_epi32(c, _mm256_set1_epi32(v0), wb0);
        c = _mm256_dpbusd_epi32(c, _mm256_set1_epi32(v1), wb1);
      }
      const __m256i vzp = _mm256_set1_epi32(static_cast<std::int32_t>(x_zps[m]));
      const __m256 rsc = exact_int8 ? _mm256_set1_ps(x_scales[m] * exact_scale)
                                    : _mm256_mul_ps(_mm256_set1_ps(x_scales[m]), raw_rscales);
      const __m256 corr = _mm256_cvtepi32_ps(_mm256_sub_epi32(c, _mm256_mullo_epi32(vzp, rsums)));
      update8(m, j, _mm256_add_ps(_mm256_mul_ps(corr, rsc), rbias));
    }
  }

  for (; j < union_count; ++j) {
    const auto t = static_cast<std::size_t>(union_ids[j]);
    const std::int8_t* erow = emb_q8 + t * dim;
    const auto& rm = meta[t];
    const std::uint32_t mask_j = slot_masks[j];
    for (std::size_t m = 0; m < rows; ++m) {
      const std::uint8_t* xm = x_u8 + m * x_stride;
      std::int32_t acc = 0;
      for (std::size_t k = 0; k < dim; ++k) {
        acc += static_cast<std::int32_t>(xm[k]) * static_cast<std::int32_t>(erow[k]);
      }
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

MTRAN_AVX512_VNNI bool prepareFullQ4Avx512Vnni(
    const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  auto* neg_corr = reinterpret_cast<std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const __m512 abs_mask = _mm512_castsi512_ps(_mm512_set1_epi32(0x7fffffff));
  const __m256i ones8 = _mm256_set1_epi8(1);
  const __m256i ones16 = _mm256_set1_epi16(1);

  for (std::size_t b = 0; b < blocks; ++b) {
    const float* xb = x + b * 64;
    const __m512 x0 = _mm512_loadu_ps(xb + 0);
    const __m512 x1 = _mm512_loadu_ps(xb + 16);
    const __m512 x2 = _mm512_loadu_ps(xb + 32);
    const __m512 x3 = _mm512_loadu_ps(xb + 48);
    const __m512 maxv = _mm512_max_ps(
        _mm512_max_ps(_mm512_and_ps(x0, abs_mask), _mm512_and_ps(x1, abs_mask)),
        _mm512_max_ps(_mm512_and_ps(x2, abs_mask), _mm512_and_ps(x3, abs_mask)));
    const float mx = hmax512ps(maxv);
    sa[b] = mx / 127.0f;
    const __m512 inv = _mm512_set1_ps(mx != 0.0f ? 127.0f / mx : 0.0f);
    auto quant16 = [&](__m512 v) noexcept -> __m128i {
      const __m512 r = _mm512_roundscale_ps(_mm512_mul_ps(v, inv),
                                            _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
      return _mm512_cvtsepi32_epi8(_mm512_cvtps_epi32(r));
    };
    const __m128i q0 = quant16(x0);
    const __m128i q1 = quant16(x1);
    const __m128i q2 = quant16(x2);
    const __m128i q3 = quant16(x3);
    const __m256i q01 = _mm256_inserti128_si256(_mm256_castsi128_si256(q0), q1, 1);
    const __m256i q23 = _mm256_inserti128_si256(_mm256_castsi128_si256(q2), q3, 1);
    const __m256i s16 = _mm256_add_epi16(_mm256_maddubs_epi16(ones8, q01),
                                         _mm256_maddubs_epi16(ones8, q23));
    const __m256i s32 = _mm256_madd_epi16(s16, ones16);
    const __m128i s128 = _mm_add_epi32(_mm256_castsi256_si128(s32),
                                       _mm256_extracti128_si256(s32, 1));
    const __m128i s64 = _mm_hadd_epi32(s128, s128);
    const std::int32_t qsum = _mm_cvtsi128_si32(_mm_hadd_epi32(s64, s64));
    neg_corr[b] = -8 * qsum;
    const __m256i p0 = _mm256_unpacklo_epi32(q01, q23);
    const __m256i p1 = _mm256_unpackhi_epi32(q01, q23);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(qa + b * 64 + 0),
                        _mm256_permute2x128_si256(p0, p1, 0x20));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(qa + b * 64 + 32),
                        _mm256_permute2x128_si256(p0, p1, 0x31));
  }
  return true;
}

MTRAN_AVX512_VNNI void scanFullQ4K4Avx512VnniRange(
    std::size_t dim, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4_512, const float* packed_scales_512,
    const float* bias, const std::uint8_t* work_buf, FullVocabQ4::TopK9& top) {
  const std::size_t blocks = dim / 64;
  const auto* qa = reinterpret_cast<const std::int8_t*>(work_buf);
  const auto* sa = reinterpret_cast<const float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const auto* neg_corr = reinterpret_cast<const std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  const auto* qa32 = reinterpret_cast<const std::int32_t*>(qa);
  top.init();
  const __m512i mask = _mm512_set1_epi8(15);
  const std::size_t group_q4_bytes = blocks * 512;
  const std::size_t group_sc_floats = blocks * 16;

  std::size_t g = g_begin;
  for (; g + 4 <= g_end; g += 4) {
    __m512 acc0 = _mm512_setzero_ps(), acc1 = _mm512_setzero_ps();
    __m512 acc2 = _mm512_setzero_ps(), acc3 = _mm512_setzero_ps();
    const std::uint8_t* gp0 = packed_q4_512 + g * group_q4_bytes;
    const std::uint8_t* gp1 = gp0 + group_q4_bytes;
    const std::uint8_t* gp2 = gp1 + group_q4_bytes;
    const std::uint8_t* gp3 = gp2 + group_q4_bytes;
    const float* gs0 = packed_scales_512 + g * group_sc_floats;
    const float* gs1 = gs0 + group_sc_floats;
    const float* gs2 = gs1 + group_sc_floats;
    const float* gs3 = gs2 + group_sc_floats;
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 512, gp1 += 512, gp2 += 512, gp3 += 512,
                                           gs0 += 16, gs1 += 16, gs2 += 16, gs3 += 16) {
      const __m512i ncv = _mm512_set1_epi32(neg_corr[b]);
      __m512i s0 = ncv, s1 = ncv, s2 = ncv, s3 = ncv;
      const std::int32_t* qab = qa32 + b * 16;
      for (std::size_t i = 0; i < 8; ++i) {
        const __m512i a_lo = _mm512_set1_epi32(qab[2 * i + 0]);
        const __m512i a_hi = _mm512_set1_epi32(qab[2 * i + 1]);
        #define MTRAN_Q4_512_COL(gp, sb_acc) do { \
          const __m512i p_ = _mm512_loadu_si512((gp) + i * 64); \
          sb_acc = _mm512_dpbusd_epi32(sb_acc, _mm512_and_si512(p_, mask), a_lo); \
          sb_acc = _mm512_dpbusd_epi32(sb_acc, _mm512_and_si512(_mm512_srli_epi16(p_, 4), mask), a_hi); \
        } while (0)
        MTRAN_Q4_512_COL(gp0, s0);
        MTRAN_Q4_512_COL(gp1, s1);
        MTRAN_Q4_512_COL(gp2, s2);
        MTRAN_Q4_512_COL(gp3, s3);
      }
      const __m512 vsa = _mm512_set1_ps(sa[b]);
      acc0 = _mm512_fmadd_ps(_mm512_cvtepi32_ps(s0), _mm512_mul_ps(vsa, _mm512_loadu_ps(gs0)), acc0);
      acc1 = _mm512_fmadd_ps(_mm512_cvtepi32_ps(s1), _mm512_mul_ps(vsa, _mm512_loadu_ps(gs1)), acc1);
      acc2 = _mm512_fmadd_ps(_mm512_cvtepi32_ps(s2), _mm512_mul_ps(vsa, _mm512_loadu_ps(gs2)), acc2);
      acc3 = _mm512_fmadd_ps(_mm512_cvtepi32_ps(s3), _mm512_mul_ps(vsa, _mm512_loadu_ps(gs3)), acc3);
    }
    const float* bp = bias + g * 16;
    const __m512 v0 = _mm512_add_ps(acc0, _mm512_loadu_ps(bp + 0));
    const __m512 v1 = _mm512_add_ps(acc1, _mm512_loadu_ps(bp + 16));
    const __m512 v2 = _mm512_add_ps(acc2, _mm512_loadu_ps(bp + 32));
    const __m512 v3 = _mm512_add_ps(acc3, _mm512_loadu_ps(bp + 48));
    const __m512 vmx = _mm512_max_ps(_mm512_max_ps(v0, v1), _mm512_max_ps(v2, v3));
    if (top.count < 9 || _mm512_cmp_ps_mask(vmx, _mm512_set1_ps(top.scores[8]), _CMP_GT_OQ) != 0) {
      alignas(64) float out64[64];
      _mm512_store_ps(out64 + 0, v0);
      _mm512_store_ps(out64 + 16, v1);
      _mm512_store_ps(out64 + 32, v2);
      _mm512_store_ps(out64 + 48, v3);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 16);
      for (int c = 0; c < 64; ++c) top.push(out64[c], base_id + c);
    }
  }
  for (; g < g_end; ++g) {
    __m512 acc0 = _mm512_setzero_ps();
    const std::uint8_t* gp0 = packed_q4_512 + g * group_q4_bytes;
    const float* gs0 = packed_scales_512 + g * group_sc_floats;
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 512, gs0 += 16) {
      __m512i s0 = _mm512_set1_epi32(neg_corr[b]);
      const std::int32_t* qab = qa32 + b * 16;
      for (std::size_t i = 0; i < 8; ++i) {
        const __m512i a_lo = _mm512_set1_epi32(qab[2 * i + 0]);
        const __m512i a_hi = _mm512_set1_epi32(qab[2 * i + 1]);
        MTRAN_Q4_512_COL(gp0, s0);
      }
      acc0 = _mm512_fmadd_ps(_mm512_cvtepi32_ps(s0),
                             _mm512_mul_ps(_mm512_set1_ps(sa[b]), _mm512_loadu_ps(gs0)), acc0);
    }
    const __m512 v0 = _mm512_add_ps(acc0, _mm512_loadu_ps(bias + g * 16));
    if (top.count < 9 || _mm512_cmp_ps_mask(v0, _mm512_set1_ps(top.scores[8]), _CMP_GT_OQ) != 0) {
      alignas(64) float out16[16];
      _mm512_store_ps(out16, v0);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 16);
      for (int c = 0; c < 16; ++c) top.push(out16[c], base_id + c);
    }
  }
  #undef MTRAN_Q4_512_COL
}

MTRAN_AVX512_VNNI void scanFullQ4K4Avx512VnniBatchRange(
    std::size_t dim, std::size_t rows, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4_512, const float* packed_scales_512,
    const float* bias, const std::uint8_t* const* work_bufs, FullVocabQ4::TopK9* tops) {
  const std::size_t blocks = dim / 64;
  const std::size_t qa_bytes = ((dim + 63) & ~std::size_t(63));
  const std::size_t sa_off = qa_bytes;
  const std::size_t nc_off = sa_off + ((blocks + 7) & ~std::size_t(7)) * sizeof(float);
  for (std::size_t m = 0; m < rows; ++m) tops[m].init();

  const __m512i mask = _mm512_set1_epi8(15);
  const std::size_t group_q4_bytes = blocks * 512;
  const std::size_t group_sc_floats = blocks * 16;

  for (std::size_t m0 = 0; m0 < rows; m0 += 8) {
    const std::size_t mb = std::min<std::size_t>(8, rows - m0);
    if (mb >= 4) {
      for (std::size_t g = g_begin; g < g_end; ++g) {
        const std::uint8_t* gp = packed_q4_512 + g * group_q4_bytes;
        const float* gs = packed_scales_512 + g * group_sc_floats;
        const std::size_t inner_rows = mb;
        __m512 acc[8];
        const std::int32_t* qab_ptrs[8];
        const float* sa_ptrs[8];
        const std::int32_t* nc_ptrs[8];
        for (std::size_t i = 0; i < inner_rows; ++i) {
          acc[i] = _mm512_setzero_ps();
          qab_ptrs[i] = reinterpret_cast<const std::int32_t*>(work_bufs[m0 + i]);
          sa_ptrs[i] = reinterpret_cast<const float*>(work_bufs[m0 + i] + sa_off);
          nc_ptrs[i] = reinterpret_cast<const std::int32_t*>(work_bufs[m0 + i] + nc_off);
        }
        for (std::size_t b = 0; b < blocks; ++b, gp += 512, gs += 16) {
          __m512i s[8];
          for (std::size_t i = 0; i < inner_rows; ++i) s[i] = _mm512_set1_epi32(nc_ptrs[i][b]);
          for (std::size_t i_k = 0; i_k < 8; ++i_k) {
            const __m512i p = _mm512_loadu_si512(gp + i_k * 64);
            const __m512i qlo = _mm512_and_si512(p, mask);
            const __m512i qhi = _mm512_and_si512(_mm512_srli_epi16(p, 4), mask);
            for (std::size_t i = 0; i < inner_rows; ++i) {
              const std::int32_t* qab = qab_ptrs[i] + b * 16;
              s[i] = _mm512_dpbusd_epi32(s[i], qlo, _mm512_set1_epi32(qab[2 * i_k + 0]));
              s[i] = _mm512_dpbusd_epi32(s[i], qhi, _mm512_set1_epi32(qab[2 * i_k + 1]));
            }
          }
          const __m512 vgs = _mm512_loadu_ps(gs);
          for (std::size_t i = 0; i < inner_rows; ++i) {
            acc[i] = _mm512_fmadd_ps(_mm512_cvtepi32_ps(s[i]),
                                     _mm512_mul_ps(_mm512_set1_ps(sa_ptrs[i][b]), vgs), acc[i]);
          }
        }
        const __m512 vbias = _mm512_loadu_ps(bias + g * 16);
        const std::uint32_t base_id = static_cast<std::uint32_t>(g * 16);
        for (std::size_t i = 0; i < inner_rows; ++i) {
          const __m512 v = _mm512_add_ps(acc[i], vbias);
          if (tops[m0 + i].count < 9 ||
              _mm512_cmp_ps_mask(v, _mm512_set1_ps(tops[m0 + i].scores[8]), _CMP_GT_OQ) != 0) {
            alignas(64) float tmp[16];
            _mm512_store_ps(tmp, v);
            for (int c = 0; c < 16; ++c) tops[m0 + i].push(tmp[c], base_id + c);
          }
        }
      }
    } else {
      for (std::size_t i = 0; i < mb; ++i) {
        scanFullQ4K4Avx512VnniRange(dim, g_begin, g_end, packed_q4_512, packed_scales_512,
                                    bias, work_bufs[m0 + i], tops[m0 + i]);
      }
    }
  }
}

}  // namespace mtran::ssru::packed
