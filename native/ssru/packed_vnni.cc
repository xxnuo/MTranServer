// Compile this translation unit only with -mavx2 -mavxvnni; callers stay baseline.
#include "packed.h"
#include "third_party/gemmology/gemmology.h"
#include <immintrin.h>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>

namespace mtran::ssru::packed {
void multiplyVnni(const std::uint8_t* a, const std::int8_t* b, const std::int32_t* sums,
                  std::size_t rows, std::size_t k, std::size_t n,
                  std::uint8_t zp, float scale, float* output,
                  const float* fused_bias, const float* separate_bias) {
  gemmology::SequentialExecutionEngine engine;
  gemmology::Engine<xsimd::avxvnni>::Shift::Multiply(a, b, rows, k, n,
    [&](xsimd::batch<std::int32_t, xsimd::avxvnni> acc, std::size_t row, std::size_t col, std::size_t) {
      using I = xsimd::batch<std::int32_t, xsimd::avxvnni>;
      using F = xsimd::batch<float, xsimd::avxvnni>;
      acc = acc - I(zp) * I::load_unaligned(sums + col);
      if (fused_bias) {
        alignas(32) float values[8];
        xsimd::batch_cast<float>(acc).store_aligned(values);
        for (std::size_t lane = 0; lane < 8; ++lane) output[row * n + col + lane] = std::fma(values[lane], scale, fused_bias[col + lane]);
      } else if (separate_bias) {
        ((xsimd::batch_cast<float>(acc) * scale) + F::load_unaligned(separate_bias + col)).store_unaligned(output + row * n + col);
      } else {
        (xsimd::batch_cast<float>(acc) * scale).store_unaligned(output + row * n + col);
      }
    }, engine);
}

ShortlistTable::ScanResult scanShortlistVnni(
    const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
    const std::int8_t* emb_q8, const ShortlistTable::RowMeta* meta,
    const std::int64_t* candidate_ids, std::size_t candidate_count, float* approx_out) {
  float best_logit = -std::numeric_limits<float>::infinity();
  float second_logit = -std::numeric_limits<float>::infinity();
  std::size_t best_j = 0;
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
  const __m256i vzp = _mm256_set1_epi32(zp_i);
  const __m256 vxscale = _mm256_set1_ps(x_scale);
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

#if defined(_MSC_VER) && !defined(__clang__)
#define MTRAN_VNNI_FMA
#else
#define MTRAN_VNNI_FMA __attribute__((target("avx2,avxvnni,fma")))
#endif

MTRAN_VNNI_FMA bool prepareFullQ4Vnni(
    const float* x, std::size_t dim, std::uint8_t* work_buf) {
  const std::size_t blocks = dim / 64;
  auto* qa = reinterpret_cast<std::int8_t*>(work_buf);
  auto* sa = reinterpret_cast<float*>(qa + ((dim + 63) & ~std::size_t(63)));
  auto* neg_corr = reinterpret_cast<std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  bool signed_overflow = false;
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
      const __m128i q = _mm_packs_epi16(p0, p1);
      _mm_store_si128(reinterpret_cast<__m128i*>(qa + b * 64 + j), q);
      signed_overflow |= _mm_movemask_epi8(_mm_cmpeq_epi8(q, _mm_set1_epi8(-128))) != 0;
    }
    if (signed_overflow) return false;
    const __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64));
    const __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64 + 32));
    const __m256i csum = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(zero, eight, a0), eight, a1);
    _mm256_store_si256(reinterpret_cast<__m256i*>(neg_corr + b * 8), _mm256_sub_epi32(zero, csum));
  }
  return true;
}

MTRAN_VNNI_FMA void scanFullQ4VnniRange(
    std::size_t dim, std::size_t g_begin, std::size_t g_end,
    const std::uint8_t* packed_q4, const float* packed_scales,
    const float* bias, const std::uint8_t* work_buf, FullVocabQ4::TopK9& top) {
  const std::size_t blocks = dim / 64;
  const auto* qa = reinterpret_cast<const std::int8_t*>(work_buf);
  const auto* sa = reinterpret_cast<const float*>(qa + ((dim + 63) & ~std::size_t(63)));
  const auto* neg_corr = reinterpret_cast<const std::int32_t*>(sa + ((blocks + 7) & ~std::size_t(7)));
  top.init();
  const __m256i mask = _mm256_set1_epi8(15);
  std::size_t g = g_begin;
  for (; g + 2 <= g_end; g += 2) {
    __m256 acc0 = _mm256_setzero_ps(), acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps(), acc3 = _mm256_setzero_ps();
    __m256 acc4 = _mm256_setzero_ps(), acc5 = _mm256_setzero_ps();
    __m256 acc6 = _mm256_setzero_ps(), acc7 = _mm256_setzero_ps();
    const std::uint8_t* gp0 = packed_q4 + g * blocks * 128;
    const std::uint8_t* gp1 = gp0 + blocks * 128;
    const float* gs0 = packed_scales + g * blocks * 4;
    const float* gs1 = gs0 + blocks * 4;
    for (std::size_t b = 0; b < blocks; ++b, gp0 += 128, gp1 += 128, gs0 += 4, gs1 += 4) {
      const __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64));
      const __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64 + 32));
      const __m256i ncv = _mm256_load_si256(reinterpret_cast<const __m256i*>(neg_corr + b * 8));
      const float sa_b = sa[b];
      #define MTRAN_Q4_STEP(cp, sb, acc) do { \
        const __m256i p_ = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cp)); \
        const __m256i s_ = _mm256_dpbusd_avx_epi32(_mm256_dpbusd_avx_epi32(ncv, _mm256_and_si256(p_, mask), a0), _mm256_and_si256(_mm256_srli_epi16(p_, 4), mask), a1); \
        acc = _mm256_fmadd_ps(_mm256_cvtepi32_ps(s_), _mm256_set1_ps(sa_b * (sb)), acc); \
      } while (0)
      MTRAN_Q4_STEP(gp0 + 0, gs0[0], acc0); MTRAN_Q4_STEP(gp0 + 32, gs0[1], acc1);
      MTRAN_Q4_STEP(gp0 + 64, gs0[2], acc2); MTRAN_Q4_STEP(gp0 + 96, gs0[3], acc3);
      MTRAN_Q4_STEP(gp1 + 0, gs1[0], acc4); MTRAN_Q4_STEP(gp1 + 32, gs1[1], acc5);
      MTRAN_Q4_STEP(gp1 + 64, gs1[2], acc6); MTRAN_Q4_STEP(gp1 + 96, gs1[3], acc7);
    }
    const __m256 h01 = _mm256_hadd_ps(acc0, acc1), h23 = _mm256_hadd_ps(acc2, acc3);
    const __m256 h45 = _mm256_hadd_ps(acc4, acc5), h67 = _mm256_hadd_ps(acc6, acc7);
    const __m256 ha = _mm256_hadd_ps(h01, h23), hb = _mm256_hadd_ps(h45, h67);
    const __m128 v0 = _mm_add_ps(_mm256_castps256_ps128(ha), _mm256_extractf128_ps(ha, 1));
    const __m128 v1 = _mm_add_ps(_mm256_castps256_ps128(hb), _mm256_extractf128_ps(hb, 1));
    const __m256 vals8 = _mm256_add_ps(_mm256_set_m128(v1, v0), _mm256_loadu_ps(bias + g * 4));
    if (top.count < 9 || _mm256_movemask_ps(_mm256_cmp_ps(vals8, _mm256_set1_ps(top.scores[8]), _CMP_GT_OQ)) != 0) {
      alignas(32) float out8[8];
      _mm256_store_ps(out8, vals8);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 4);
      for (int c = 0; c < 8; ++c) top.push(out8[c], base_id + c);
    }
  }
  for (; g < g_end; ++g) {
    __m256 acc0 = _mm256_setzero_ps(), acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps(), acc3 = _mm256_setzero_ps();
    const std::uint8_t* gp = packed_q4 + g * blocks * 128;
    const float* gs = packed_scales + g * blocks * 4;
    for (std::size_t b = 0; b < blocks; ++b, gp += 128, gs += 4) {
      const __m256i a0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64));
      const __m256i a1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(qa + b * 64 + 32));
      const __m256i ncv = _mm256_load_si256(reinterpret_cast<const __m256i*>(neg_corr + b * 8));
      const float sa_b = sa[b];
      MTRAN_Q4_STEP(gp + 0, gs[0], acc0); MTRAN_Q4_STEP(gp + 32, gs[1], acc1);
      MTRAN_Q4_STEP(gp + 64, gs[2], acc2); MTRAN_Q4_STEP(gp + 96, gs[3], acc3);
      #undef MTRAN_Q4_STEP
    }
    const __m256 h01 = _mm256_hadd_ps(acc0, acc1), h23 = _mm256_hadd_ps(acc2, acc3);
    const __m256 h = _mm256_hadd_ps(h01, h23);
    const __m128 vals = _mm_add_ps(_mm_add_ps(_mm256_castps256_ps128(h), _mm256_extractf128_ps(h, 1)), _mm_loadu_ps(bias + g * 4));
    if (top.count < 9 || _mm_movemask_ps(_mm_cmpgt_ps(vals, _mm_set1_ps(top.scores[8]))) != 0) {
      alignas(16) float out4[4];
      _mm_store_ps(out4, vals);
      const std::uint32_t base_id = static_cast<std::uint32_t>(g * 4);
      for (int c = 0; c < 4; ++c) top.push(out4[c], base_id + c);
    }
  }
}
}
