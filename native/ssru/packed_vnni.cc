// Compile this translation unit only with -mavx2 -mavxvnni; callers stay baseline.
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

float scanShortlistVnni(const std::uint8_t* x_u8, float x_scale, std::uint8_t x_zp, std::size_t dim,
                        const std::int8_t* emb_q8, const float* row_scales, const std::int32_t* row_sums,
                        const float* output_bias, const std::int64_t* candidate_ids,
                        std::size_t candidate_count, float* approx_out) {
  float best_logit = -std::numeric_limits<float>::infinity();
  const std::int32_t zp_i = static_cast<std::int32_t>(x_zp);
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
}
