#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mtran::ssru::ops {

// Precomputed sinusoidal position table: table[pos * dim + j] = sin(pos * freq[j] + offset[j]).
// Falls back to computing sin(pos * freq[j] + offset[j]) for pos >= max_pos.
struct PosTable {
  std::size_t dim = 0;
  std::size_t max_pos = 0;
  std::vector<float> table;
  std::vector<float> frequency;
  std::vector<float> offset;

  static PosTable create(const float* freq, const float* offset,
                         std::size_t dim, std::size_t max_pos = 512);

  // Writes out[j] = (embed ? embed[j] * scale : 0.0f) + sin_pos[j].
  void addScaledEmbed(const float* embed, float scale, std::size_t pos, float* out) const noexcept;

  // Writes out[j] = (embed_q8 ? float(embed_q8[j]) * combined_scale : 0.0f) + sin_pos[j].
  void addScaledEmbedInt8(const std::int8_t* embed_q8, float combined_scale,
                          std::size_t pos, float* out) const noexcept;

  // Exact two-step FP32 scaling: (float(embed_q8[j]) * q_scale) * embed_scale + sin_pos[j].
  void addScaledEmbedInt8(const std::int8_t* embed_q8, float q_scale, float embed_scale,
                          std::size_t pos, float* out) const noexcept;
};

// Population-variance InstanceNormalization (eps=1e-6) followed by affine scale*norm + bias.
// Supports in-place (out == x). When out_mins / out_maxs are non-null, writes per-row bounds
// clamped to <= 0 and >= 0 respectively (matching quantizeLinearU8 min/max conventions).
void instanceNormAffine(const float* x, const float* scale, const float* bias,
                        std::size_t rows, std::size_t dim, float* out,
                        float* out_mins = nullptr, float* out_maxs = nullptr) noexcept;

// Fused SSRU recurrence:
//   f = sigmoid(wf[j])
//   cell[j] = f * cell[j] + (1 - f) * w[j]
//   out[j]  = x[j] + max(0.0f, cell[j])
// Supports in-place (out == x).
void ssruGateUpdate(const float* wf, const float* w, const float* x,
                    std::size_t rows, std::size_t dim, float* cell, float* out) noexcept;

// Multi-head cross-attention for a single decoder query step:
//   q: [heads, head_dim], k_t: [heads, head_dim, src_len], v: [heads, src_len, head_dim],
//   out: [heads, head_dim].
// scores_scratch requires at least src_len floats per calling thread.
void crossAttention(const float* q, const float* k_t, const float* v,
                    std::size_t heads, std::size_t head_dim, std::size_t src_len,
                    float* scores_scratch, float* out,
                    std::size_t head_begin = 0, std::size_t head_end = ~std::size_t(0)) noexcept;

// Multi-head encoder self-attention over untransposed [seq_len, heads * head_dim] buffers.
// scores_scratch requires at least heads * seq_len floats (head h uses scores_scratch + h * seq_len).
void selfAttention(const float* q, const float* k, const float* v,
                   std::size_t seq_len, std::size_t heads, std::size_t head_dim,
                   float* scores_scratch, float* out,
                   std::size_t head_begin = 0, std::size_t head_end = ~std::size_t(0)) noexcept;

// Variant supporting strided Q/K/V rows (e.g. qkv_stride = 3 * dim directly from packed Wqkv).
void selfAttentionStrided(const float* q, const float* k, const float* v, std::size_t qkv_stride,
                          std::size_t seq_len, std::size_t heads, std::size_t head_dim,
                          float* scores_scratch, float* out,
                          std::size_t head_begin = 0, std::size_t head_end = ~std::size_t(0)) noexcept;

}  // namespace mtran::ssru::ops
