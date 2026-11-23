#include "packed.h"
#include "kleidi.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

using mtran::ssru::packed::Weight;
using mtran::ssru::packed::Workspace;
using mtran::ssru::packed::ShortlistTable;
using mtran::ssru::packed::quantizeLinearU8;

template<class F> static void rejects(F f) {
  bool threw = false;
  try { f(); } catch (const std::exception&) { threw = true; }
  assert(threw);
}
static void golden(const Weight& weight, const std::vector<float>& original,
                   std::size_t k, std::size_t n, std::size_t rows,
                   float activation_scale, std::uint8_t zero_point, bool extreme,
                   bool fused_bias = false, bool separate_bias = false) {
  float maximum = 0;
  for (float x : original) maximum = std::max(maximum, std::abs(x));
  float weight_scale = (maximum * 2.f) / 254.f;
  if (weight_scale < std::numeric_limits<float>::min()) weight_scale = 1;
  std::vector<std::int8_t> quantized(original.size());
  for (std::size_t i = 0; i < original.size(); ++i)
    quantized[i] = static_cast<std::int8_t>(std::clamp(std::nearbyint(original[i] / weight_scale), -128.f, 127.f));
  Workspace workspace;
  auto* activation = workspace.resize(rows * k);
  assert(reinterpret_cast<std::uintptr_t>(activation) % 32 == 0);
  for (std::size_t i = 0; i < rows * k; ++i) activation[i] = extreme ? 255 : (i * 137 + 51) % 256;
  std::vector<float> output(rows * n + 2, 123456.f);
  std::vector<float> bias(n);
  for (std::size_t col = 0; col < n; ++col) bias[col] = static_cast<float>(int(col % 19) - 9) / 7.f;
  weight.multiply(activation, rows, activation_scale, zero_point, output.data() + 1,
                  fused_bias ? bias.data() : nullptr, separate_bias ? bias.data() : nullptr);
  const bool ortSaturation = std::string_view(mtran::ssru::packed::backend()) == "packed-avx2-ort";
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < n; ++col) {
      std::int64_t reference = 0;
      if (ortSaturation) {
        for (std::size_t j = 0; j < k; j += 2) {
          const int w0 = quantized[j * n + col], w1 = quantized[(j + 1) * n + col];
          const int pair = int(activation[row * k + j]) * w0 + int(activation[row * k + j + 1]) * w1;
          reference += std::clamp(pair, -32768, 32767) - int(zero_point) * (w0 + w1);
        }
      } else {
        for (std::size_t j = 0; j < k; ++j)
          reference += (std::int64_t(activation[row * k + j]) - zero_point) * quantized[j * n + col];
      }
      const float expected = fused_bias ? std::fma(static_cast<float>(reference), activation_scale * weight_scale, bias[col])
                                       : separate_bias ? (static_cast<float>(reference) * (activation_scale * weight_scale)) + bias[col]
                                                       : static_cast<float>(reference) * (activation_scale * weight_scale);
      assert(std::bit_cast<std::uint32_t>(output[1 + row * n + col]) == std::bit_cast<std::uint32_t>(expected));
    }
  }
  assert(output.front() == 123456.f && output.back() == 123456.f);
}
static void cpuOsGolden() {
  using namespace mtran::ssru::packed::detail;
  constexpr unsigned fma = 1u << 12;
  constexpr unsigned avx = 0x18000000u, avx_fma = avx | fma, avx2 = 1u << 5, vnni = 1u << 4;
  constexpr unsigned avx512 = 0xc0030000u, vnni512 = 1u << 11;
  // i7-8550U / EPYC 7763: no VNNI, including CPUs with no CPUID 7.1.
  assert(x86Features(avx, avx2, 0, 0, 6) == Avx2);
  assert(x86Features(avx_fma, avx2, 0, 0, 6) == Avx2);
  assert(x86Features(avx_fma, avx2 | avx512, 0, 0, 0xe6) == Avx2);
  // Missing FMA bit disables FMA-requiring VNNI kernels.
  assert(x86Features(avx, avx2 | avx512, vnni512, vnni, 0xe6) == Avx2);
  // AVXVNNI needs only XMM/YMM state; AVX512VNNI needs all ZMM/opmask state.
  assert(x86Features(avx_fma, avx2, 0, vnni, 6) == (Avx2 | AvxVnni));
  assert(x86Features(avx_fma, avx2 | avx512, vnni512, 0, 0xe6) == (Avx2 | Avx512Vnni));
  assert(x86Features(avx_fma, avx2 | avx512, vnni512, vnni, 0xe6) == (Avx2 | AvxVnni | Avx512Vnni));
  for (unsigned bit : {16, 17, 30, 31})
    assert(x86Features(avx_fma, avx2 | (avx512 & ~(1u << bit)), vnni512, 0, 0xe6) == Avx2);
  for (std::uint64_t state : {0x6, 0x26, 0x46, 0x66, 0x86, 0xa6, 0xc6}) {
    assert(x86Features(avx_fma, avx2 | avx512, vnni512, 0, state) == Avx2);
    assert(x86Features(avx_fma, avx2 | avx512, vnni512, vnni, state) == (Avx2 | AvxVnni));
  }
  for (unsigned leaf1 : {0u, 1u << 27, 1u << 28})
    assert(x86Features(leaf1, avx2 | avx512, vnni512, vnni, 0xe6) == 0);
  for (std::uint64_t state : {0, 2, 4, 0xe0, 0xe2, 0xe4})
    assert(x86Features(avx_fma, avx2 | avx512, vnni512, vnni, state) == 0);
  assert(x86Features(avx_fma, avx512, vnni512, vnni, 0xe6) == 0);
}
static void kleidiGolden() {
  std::vector<float> input(32), output(8), original(32 * 8, .1f);
  input[0] = -3.3f; input[1] = 4.4f;
  original.back() = 12.7f;
  Workspace workspace;
  auto* activation = workspace.resize(input.size());
  std::uint8_t zero = 0;
  const float scale = mtran::ssru::kleidi::quantize(input.data(), input.size(), activation, zero);
  assert(std::bit_cast<std::uint32_t>(scale) == 0x3cf75dc5u);
  assert(zero == 109 && activation[0] == 0 && activation[1] == 255);
  assert(std::all_of(activation + 2, activation + 32, [&](auto x) { return x == zero; }));
  const Weight weight(original.data(), 32, 8);
  weight.multiply(activation, 1, scale, zero, output.data());
  for (float x : output) assert(std::bit_cast<std::uint32_t>(x + .125f) == 0x3e72682cu);
  const float ordinary = (4.4f - -3.3f) / 255.f;
  assert(std::bit_cast<std::uint32_t>((37.f * (ordinary * .1f)) + .125f) == 0x3e72682bu);
  const float ties[] = {-127.5f, 127.5f, -.5f, .5f, -1.5f, 1.5f};
  mtran::ssru::kleidi::quantize(ties, 6, activation, zero);
  const std::uint8_t expected[] = {0, 255, 127, 129, 126, 130};
  assert(zero == 128 && std::equal(activation, activation + 6, expected));
  std::fill(input.begin(), input.end(), 0.f);
  assert(mtran::ssru::kleidi::quantize(input.data(), 32, activation, zero) == 1.f);
  assert(std::all_of(activation, activation + 32, [&](auto x) { return x == zero; }));
  rejects([&] { mtran::ssru::kleidi::quantize(nullptr, 32, activation, zero); });
  rejects([&] { mtran::ssru::kleidi::quantize(input.data(), 0, activation, zero); });
  input[0] = std::numeric_limits<float>::infinity();
  rejects([&] { mtran::ssru::kleidi::quantize(input.data(), 32, activation, zero); });
  input[0] = std::numeric_limits<float>::denorm_min();
  rejects([&] { mtran::ssru::kleidi::quantize(input.data(), 32, activation, zero); });
}
static void dqlAndShortlistGolden() {
  Workspace workspace;
  for (std::size_t n : {1, 6, 16, 33, 384}) {
    std::vector<float> input(n);
    for (std::size_t i = 0; i < n; ++i) input[i] = static_cast<float>(int(i * 37 % 51) - 25) * 0.125f;
    auto* out = workspace.resize(n);
    float scale = 0; std::uint8_t zp = 0;
    quantizeLinearU8(input.data(), 1, n, out, scale, zp);
    float lo = 0, hi = 0;
    for (float v : input) { lo = std::min(lo, v); hi = std::max(hi, v); }
    const float ref_scale = hi == lo ? 1.f : (hi - lo) / 255.f;
    const auto ref_zp = static_cast<std::uint8_t>(std::nearbyint(std::clamp(0.f - lo / ref_scale, 0.f, 255.f)));
    assert(std::bit_cast<std::uint32_t>(scale) == std::bit_cast<std::uint32_t>(ref_scale) && zp == ref_zp);
    for (std::size_t i = 0; i < n; ++i) {
      const float clamped = std::clamp(input[i] / ref_scale, float(-int(ref_zp)), float(255 - int(ref_zp)));
      const auto expected = static_cast<std::uint8_t>(int(std::nearbyint(clamped)) + int(ref_zp));
      assert(out[i] == expected);
    }
  }
  const float ties[] = {-127.5f, 127.5f, -0.5f, 0.5f, -1.5f, 1.5f};
  auto* out_ties = workspace.resize(6);
  float scale_ties = 0; std::uint8_t zp_ties = 0;
  quantizeLinearU8(ties, 1, 6, out_ties, scale_ties, zp_ties);
  const std::uint8_t exp_ties[] = {0, 255, 128, 128, 126, 130};
  assert(scale_ties == 1.f && zp_ties == 128 && std::equal(out_ties, out_ties + 6, exp_ties));

  // Test ShortlistTable across non-32 fallback (4, 20) and Q8+FP32 rerank (32, 64), including
  // adversarial [-128,-128] x [255,255] pairs, ties (earliest wins), and token-1 -inf masking.
  for (std::size_t dim : {4, 20, 32, 64}) {
    constexpr std::size_t vocab = 29;
    std::vector<float> emb(vocab * dim), bias(vocab, 0.f);
    for (std::size_t r = 0; r < vocab; ++r) {
      bias[r] = static_cast<float>(int(r % 7) - 3) * 0.03f;
      for (std::size_t k = 0; k < dim; ++k)
        emb[r * dim + k] = static_cast<float>(int((r * 19 + k * 11) % 255) - 128) * 0.25f;
    }
    bias[1] = -std::numeric_limits<float>::infinity();
    // Token 1 wants to win by huge dot product, must be masked by bias[1] = -inf.
    std::fill_n(emb.data() + dim, dim, 100.f);
    // Adversarially set rows 10 and 18 to identical vectors + biases so earlier candidate wins tie,
    // and construct extreme negative pairs that would saturate naive int16 maddubs.
    std::fill_n(emb.data() + 5 * dim, dim, -128.f);
    std::copy_n(emb.data() + 10 * dim, dim, emb.data() + 18 * dim);
    bias[18] = bias[10];
    ShortlistTable table(emb.data(), bias.data(), vocab, dim);
    std::vector<std::int64_t> cids;
    for (std::size_t r = 0; r < vocab; ++r) cids.push_back(static_cast<std::int64_t>(r));
    for (int trial = 0; trial < 8; ++trial) {
      std::vector<float> x(dim);
      for (std::size_t k = 0; k < dim; ++k)
        x[k] = trial == 0 ? 100.f : (trial == 1 ? ((k == 0) ? -1.f : 255.f) : std::sin(float(trial * 13 + k)) * 5.f);
      if (trial == 2) {
        // Force tie between candidate 10 and 18 to be global maximum.
        for (std::size_t k = 0; k < dim; ++k) x[k] = emb[10 * dim + k];
        bias[10] = bias[18] = 1e5f;
      }
      for (bool fma_mode : {false, true}) {
        ShortlistTable current(emb.data(), bias.data(), vocab, dim, fma_mode);
        const std::size_t got = current.argmax(x.data(), cids.data(), cids.size(), workspace);
        std::size_t ref_best = 0;
        float ref_logit = -std::numeric_limits<float>::infinity();
        for (std::size_t j = 0; j < cids.size(); ++j) {
          const auto t = static_cast<std::size_t>(cids[j]);
          float dot = 0.f;
          for (std::size_t k = 0; k < dim; ++k)
            dot = fma_mode ? std::fma(x[k], emb[t * dim + k], dot) : (dot + x[k] * emb[t * dim + k]);
          const float val = dot + (t == 1 ? -std::numeric_limits<float>::infinity() : bias[t]);
          if (j == 0 || val > ref_logit) { ref_logit = val; ref_best = j; }
        }
        assert(got == ref_best);
        if (trial == 2) assert(got == 10);
      }
    }
    std::vector<float> x_ok(dim, 1.f);
    std::int64_t bad_cid = static_cast<std::int64_t>(vocab);
    rejects([&] { table.argmax(x_ok.data(), &bad_cid, 1, workspace); });
    bad_cid = -1;
    rejects([&] { table.argmax(x_ok.data(), &bad_cid, 1, workspace); });
    x_ok[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { table.argmax(x_ok.data(), cids.data(), cids.size(), workspace); });
  }
  {
    using mtran::ssru::packed::FullVocabQ4;
    for (auto [vocab, dim] : {std::pair<std::size_t, std::size_t>{9, 8}, {13, 64}, {20, 384}, {4100, 64}}) {
      std::vector<float> emb(vocab * dim), bias(vocab), x(dim);
      for (std::size_t k = 0; k < dim; ++k) x[k] = ((k & 1) ? -0.75f : 0.75f) * float((k % 5) + 1);
      for (std::size_t r = 0; r < vocab; ++r) {
        bias[r] = float(int(r % 11) - 5) * 0.25f;
        for (std::size_t k = 0; k < dim; ++k) {
          emb[r * dim + k] = (r == 7) ? 4.5f * x[k] : std::sin(float(r * 17 + k * 3)) * 0.5f;
        }
      }
      bias[1] = -std::numeric_limits<float>::infinity();
      std::fill_n(emb.data() + dim, dim, 50.f);
      FullVocabQ4 full(emb.data(), bias.data(), vocab, dim);
      const std::size_t seq = full.argmax(x.data(), workspace, false);
      const std::size_t par = full.argmax(x.data(), workspace, true);
      assert(seq == 7 && par == 7);
      if (vocab == 13) {
        for (std::size_t k = 0; k < dim; ++k) emb[12 * dim + k] = 9.0f * x[k];
        FullVocabQ4 tail(emb.data(), bias.data(), vocab, dim);
        assert(tail.argmax(x.data(), workspace, false) == 12);
      }
    }
    std::vector<float> ok_emb(9 * 8, 1.f), ok_bias(9, 0.f), bad_x(8, 1.f);
    rejects([&] { FullVocabQ4 bad(ok_emb.data(), ok_bias.data(), 8, 8); });
    FullVocabQ4 small(ok_emb.data(), ok_bias.data(), 9, 8);
    rejects([&] { small.argmax(nullptr, workspace, false); });
    bad_x[0] = std::numeric_limits<float>::infinity();
    rejects([&] { small.argmax(bad_x.data(), workspace, false); });
  }
}

static void batchAndInt8Golden() {
  using mtran::ssru::packed::FullVocabQ4;
  using mtran::ssru::packed::quantizeLinearU8Batch;
  using mtran::ssru::packed::quantizeLinearU8FromMinMax;
  using mtran::ssru::packed::quantizeLinearU8WithNorm;

  // 1. quantizeLinearU8Batch, quantizeLinearU8FromMinMax, quantizeLinearU8WithNorm
  {
    constexpr std::size_t rows = 5, cols = 97, in_stride = 104, out_stride = 128;
    std::vector<float> in(rows * in_stride, 0.0f), mins(rows), maxs(rows);
    std::vector<std::uint8_t> ref_q(rows * out_stride, 77), got_batch_q(rows * out_stride, 77), got_mm_q(rows * out_stride, 77);
    std::vector<float> ref_sc(rows), got_batch_sc(rows), got_mm_sc(rows);
    std::vector<std::uint8_t> ref_zp(rows), got_batch_zp(rows), got_mm_zp(rows);

    for (std::size_t m = 0; m < rows; ++m) {
      float rmin = std::numeric_limits<float>::infinity();
      float rmax = -std::numeric_limits<float>::infinity();
      for (std::size_t c = 0; c < cols; ++c) {
        const float v = (m == 3) ? 0.0f : std::sin(float(m * 131 + c * 17)) * 3.5f + float(int(m) - 2);
        in[m * in_stride + c] = v;
        rmin = std::min(rmin, v);
        rmax = std::max(rmax, v);
      }
      mins[m] = rmin;
      maxs[m] = rmax;
      quantizeLinearU8(in.data() + m * in_stride, 1, cols, ref_q.data() + m * out_stride, ref_sc[m], ref_zp[m]);

      std::vector<std::uint8_t> norm_q(cols);
      float norm_sc = 0.0f, dx_sq = -1.0f, qx_sq = -1.0f;
      std::uint8_t norm_zp = 0;
      quantizeLinearU8WithNorm(in.data() + m * in_stride, cols, norm_q.data(), norm_sc, norm_zp, dx_sq, &qx_sq);
      assert(std::bit_cast<std::uint32_t>(norm_sc) == std::bit_cast<std::uint32_t>(ref_sc[m]));
      assert(norm_zp == ref_zp[m]);
      for (std::size_t c = 0; c < cols; ++c) assert(norm_q[c] == ref_q[m * out_stride + c]);
      double ref_dx = 0.0, ref_qx = 0.0;
      for (std::size_t c = 0; c < cols; ++c) {
        const float qf = static_cast<float>(static_cast<int>(norm_q[c]) - static_cast<int>(norm_zp)) * norm_sc;
        const double d = static_cast<double>(in[m * in_stride + c]) - static_cast<double>(qf);
        ref_dx += d * d;
        ref_qx += static_cast<double>(qf) * qf;
      }
      assert(std::abs(double(dx_sq) - ref_dx) <= 1e-4 * (1.0 + ref_dx));
      assert(std::abs(double(qx_sq) - ref_qx) <= 1e-4 * (1.0 + ref_qx));
    }

    quantizeLinearU8Batch(in.data(), rows, cols, in_stride, got_batch_q.data(), out_stride, got_batch_sc.data(), got_batch_zp.data());
    quantizeLinearU8FromMinMax(in.data(), rows, cols, in_stride, mins.data(), maxs.data(), got_mm_q.data(), out_stride, got_mm_sc.data(), got_mm_zp.data());
    for (std::size_t m = 0; m < rows; ++m) {
      assert(std::bit_cast<std::uint32_t>(got_batch_sc[m]) == std::bit_cast<std::uint32_t>(ref_sc[m]));
      assert(std::bit_cast<std::uint32_t>(got_mm_sc[m]) == std::bit_cast<std::uint32_t>(ref_sc[m]));
      assert(got_batch_zp[m] == ref_zp[m] && got_mm_zp[m] == ref_zp[m]);
      for (std::size_t c = 0; c < cols; ++c) {
        assert(got_batch_q[m * out_stride + c] == ref_q[m * out_stride + c]);
        assert(got_mm_q[m * out_stride + c] == ref_q[m * out_stride + c]);
      }
      for (std::size_t c = cols; c < out_stride; ++c) {
        assert(got_batch_q[m * out_stride + c] == 77 && got_mm_q[m * out_stride + c] == 77);
      }
    }
    in[2] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { quantizeLinearU8Batch(in.data(), rows, cols, in_stride, got_batch_q.data(), out_stride, got_batch_sc.data(), got_batch_zp.data()); });
    rejects([&] { quantizeLinearU8FromMinMax(in.data(), rows, cols, in_stride, mins.data(), maxs.data(), got_mm_q.data(), out_stride, got_mm_sc.data(), got_mm_zp.data()); });
  }

  // 2. Weight::packInt8, multiplyBatch (bit-exact vs M=1), slicing, force_separate_bias, multiplyBatchReluMinMax, move
  {
    const bool ortSat = std::string_view(mtran::ssru::packed::backend()) == "packed-avx2-ort";
    for (std::size_t k : {32, 384}) {
      for (std::size_t n : {24, 64}) {
        std::vector<std::int8_t> wq8(k * n);
        std::vector<float> w_fp32(k * n), bias(n);
        const float w_scale = 0.015625f;
        for (std::size_t i = 0; i < k * n; ++i) {
          wq8[i] = static_cast<std::int8_t>(int((i * 53) % 251) - 125);
          w_fp32[i] = static_cast<float>(wq8[i]) * w_scale;
        }
        wq8[0] = 127;
        w_fp32[0] = 127.0f * w_scale;
        for (std::size_t c = 0; c < n; ++c) bias[c] = static_cast<float>(int(c % 13) - 6) * 0.25f;

        Weight w_fused = Weight::packInt8(wq8.data(), w_scale, k, n, bias.data(), nullptr, true);
        Weight w_sep = Weight::packInt8(wq8.data(), w_scale, k, n, nullptr, bias.data(), false);
        Weight w_moved = std::move(w_fused);
        Weight w_assigned(w_fp32.data(), k, n);
        w_assigned = std::move(w_moved);

        for (std::size_t rows : {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(4), std::size_t(6), std::size_t(7), std::size_t(8)}) {
          const std::size_t out_stride = n + 16;
          std::vector<std::uint8_t> a_q(rows * k);
          std::vector<float> a_sc(rows);
          std::vector<std::uint8_t> a_zp(rows);
          for (std::size_t m = 0; m < rows; ++m) {
            a_sc[m] = 0.01f * float(m + 1);
            a_zp[m] = static_cast<std::uint8_t>((m * 47 + 19) % 256);
            for (std::size_t c = 0; c < k; ++c) {
              a_q[m * k + c] = static_cast<std::uint8_t>((m * 71 + c * 29) % 256);
            }
          }

          std::vector<float> out_fused(rows * out_stride, 999.0f), out_sep(rows * out_stride, 999.0f),
              out_force_sep(rows * out_stride, 999.0f), out_relu(rows * out_stride, 999.0f);
          std::vector<float> maxs(rows, -1.0f);
          w_assigned.multiplyBatch(a_q.data(), a_sc.data(), a_zp.data(), rows, out_fused.data(), out_stride);
          w_assigned.multiplyBatch(a_q.data(), a_sc.data(), a_zp.data(), rows, out_force_sep.data(), out_stride,
                                   0, ~std::size_t(0), true);
          // Sliced calls including empty [0, 0).
          w_sep.multiplyBatch(a_q.data(), a_sc.data(), a_zp.data(), rows, out_sep.data(), out_stride, 0, 0);
          w_sep.multiplyBatch(a_q.data(), a_sc.data(), a_zp.data(), rows, out_sep.data(), out_stride, 0, 8);
          w_sep.multiplyBatch(a_q.data(), a_sc.data(), a_zp.data(), rows, out_sep.data(), out_stride, 8, n);
          w_assigned.multiplyBatchReluMinMax(a_q.data(), a_sc.data(), a_zp.data(), rows, out_relu.data(), out_stride,
                                             maxs.data());

          Workspace ws;
          std::uint8_t* a1_aligned = ws.resize(k);
          std::vector<float> ref_m1(n);
          for (std::size_t m = 0; m < rows; ++m) {
            std::copy_n(a_q.data() + m * k, k, a1_aligned);
            float expected_max = 0.0f;
            for (std::size_t c = 0; c < n; ++c) {
              std::int64_t acc = 0;
              if (ortSat) {
                for (std::size_t j = 0; j < k; j += 2) {
                  const int pair = int(a_q[m * k + j]) * wq8[j * n + c] + int(a_q[m * k + j + 1]) * wq8[(j + 1) * n + c];
                  acc += std::clamp(pair, -32768, 32767) - int(a_zp[m]) * (int(wq8[j * n + c]) + int(wq8[(j + 1) * n + c]));
                }
              } else {
                for (std::size_t j = 0; j < k; ++j)
                  acc += (std::int64_t(a_q[m * k + j]) - a_zp[m]) * wq8[j * n + c];
              }
              const float comb = a_sc[m] * w_scale;
              const float ef = std::fma(static_cast<float>(acc), comb, bias[c]);
              const float es = static_cast<float>(acc) * comb + bias[c];
              const float er = ef > 0.0f ? ef : 0.0f;
              expected_max = std::max(expected_max, er);
              assert(std::bit_cast<std::uint32_t>(out_fused[m * out_stride + c]) == std::bit_cast<std::uint32_t>(ef));
              assert(std::bit_cast<std::uint32_t>(out_sep[m * out_stride + c]) == std::bit_cast<std::uint32_t>(es));
              assert(std::bit_cast<std::uint32_t>(out_force_sep[m * out_stride + c]) == std::bit_cast<std::uint32_t>(es));
              assert(std::bit_cast<std::uint32_t>(out_relu[m * out_stride + c]) == std::bit_cast<std::uint32_t>(er));
            }
            assert(std::bit_cast<std::uint32_t>(maxs[m]) == std::bit_cast<std::uint32_t>(expected_max));
            for (std::size_t c = n; c < out_stride; ++c) {
              assert(out_fused[m * out_stride + c] == 999.0f && out_sep[m * out_stride + c] == 999.0f);
            }
          }
        }
      }
    }
  }

  // 3. ShortlistTable::createInt8 & argMaxBatch
  {
    Workspace ws;
    for (std::size_t dim : {std::size_t(20), std::size_t(64)}) {
      const std::size_t vocab = 512;
      const float min_pos = 0.015625f;
      std::vector<std::int8_t> emb_q8(vocab * dim);
      std::vector<float> emb_fp32(vocab * dim), bias(vocab);
      for (std::size_t r = 0; r < vocab; ++r) {
        bias[r] = static_cast<float>(int(r % 19) - 9) * 0.125f;
        for (std::size_t k = 0; k < dim; ++k) {
          emb_q8[r * dim + k] = static_cast<std::int8_t>(int((r * 29 + k * 41) % 241) - 120);
          emb_fp32[r * dim + k] = static_cast<float>(emb_q8[r * dim + k]) * min_pos;
        }
      }
      bias[1] = -std::numeric_limits<float>::infinity();
      std::fill_n(emb_q8.data() + dim, dim, 127);
      std::fill_n(emb_fp32.data() + dim, dim, 127.0f * min_pos);

      ShortlistTable tbl_fp32(emb_fp32.data(), bias.data(), vocab, dim, false);
      ShortlistTable tbl_i8 = ShortlistTable::createInt8(emb_q8.data(), min_pos, bias.data(), vocab, dim, false);
      ShortlistTable tbl_moved = std::move(tbl_i8);

      // Case A: contiguous hot 0..255 + tail with heterogeneous masks across rows=12 (plus empty slot m=11).
      const std::size_t rows = 12, x_stride = dim + 16;
      std::vector<float> x_rows(rows * x_stride, 0.0f);
      std::vector<std::int64_t> union_hot;
      std::vector<std::uint32_t> masks_hot;
      for (std::int64_t id = 0; id < 256; ++id) {
        union_hot.push_back(id);
        std::uint32_t mask = 0;
        for (std::size_t m = 0; m < 11; ++m) {
          if (((id + static_cast<std::int64_t>(m)) % 3) != 0 || id < 16) mask |= (1u << m);
        }
        masks_hot.push_back(mask);
      }
      for (std::int64_t id = 260; id < 320; id += 2) {
        union_hot.push_back(id);
        std::uint32_t mask = 0;
        for (std::size_t m = 0; m < 11; ++m) {
          if (((id / 2 + static_cast<std::int64_t>(m)) & 1) == 0) mask |= (1u << m);
        }
        masks_hot.push_back(mask);
      }
      for (std::size_t m = 0; m < rows; ++m) {
        for (std::size_t k = 0; k < dim; ++k) {
          x_rows[m * x_stride + k] = std::cos(float(m * 31 + k * 7)) * 1.5f;
        }
        if (m < 11) {
          const std::size_t win = (m == 0) ? 7 : (m < 6 ? 12 + m * 20 : 260 + (m - 6) * 10);
          for (std::size_t k = 0; k < dim; ++k) {
            x_rows[m * x_stride + k] += 3.0f * static_cast<float>(emb_q8[win * dim + k]) * min_pos;
          }
        }
      }
      std::vector<std::int64_t> got_tokens(rows, -1);
      tbl_moved.argMaxBatch(x_rows.data(), rows, x_stride, union_hot.data(), masks_hot.data(), union_hot.size(), got_tokens.data());
      for (std::size_t m = 0; m < 11; ++m) {
        std::vector<std::int64_t> cids_m;
        for (std::size_t j = 0; j < union_hot.size(); ++j) {
          if (masks_hot[j] & (1u << m)) cids_m.push_back(union_hot[j]);
        }
        const std::size_t j_fp32 = tbl_fp32.argmax(x_rows.data() + m * x_stride, cids_m.data(), cids_m.size(), ws);
        float full_logit = 0.0f;
        const std::size_t j_i8 = tbl_moved.argmax(x_rows.data() + m * x_stride, cids_m.data(), cids_m.size(), ws, false, &full_logit);
        assert(cids_m[j_fp32] == cids_m[j_i8]);
        assert(got_tokens[m] == cids_m[j_i8]);
        if (cids_m.size() >= 4) {
          const std::size_t mid = cids_m.size() / 2;
          float l0 = 0.0f, l1 = 0.0f;
          const std::size_t s0 = tbl_moved.argmax(x_rows.data() + m * x_stride, cids_m.data(), mid, ws, true, &l0);
          const std::size_t s1 = tbl_moved.argmax(x_rows.data() + m * x_stride, cids_m.data() + mid, cids_m.size() - mid, ws, true, &l1);
          const std::size_t merged = (l1 > l0 || (l1 == l0 && mid + s1 < s0)) ? (mid + s1) : s0;
          assert(cids_m[merged] == cids_m[j_i8]);
          assert(std::max(l0, l1) == full_logit);
        }
      }
      assert(got_tokens[11] == 0);

      // Case B: unsorted union IDs + ties.
      std::vector<std::int64_t> union_unsorted{400, 1, 250, 7, 300, 12, 9, 450, 500, 33};
      std::vector<std::uint32_t> masks_all(union_unsorted.size(), 0xFu);
      std::vector<std::int64_t> got_unsorted(4, -1);
      tbl_moved.argMaxBatch(x_rows.data(), 4, x_stride, union_unsorted.data(), masks_all.data(), union_unsorted.size(), got_unsorted.data());
      for (std::size_t m = 0; m < 4; ++m) {
        const std::size_t j_ref = tbl_moved.argmax(x_rows.data() + m * x_stride, union_unsorted.data(), union_unsorted.size(), ws);
        assert(got_unsorted[m] == union_unsorted[j_ref]);
      }
    }
  }

  // 4. FullVocabQ4::createInt8 & argMaxBatch (M=1..8, max_threads=1,4,6)
  {
    Workspace ws;
    for (auto [vocab, dim] : {std::pair<std::size_t, std::size_t>{13, 64}, {384, 384}, {4100, 64}}) {
      const float min_pos = 0.015625f;
      std::vector<std::int8_t> emb_q8(vocab * dim);
      std::vector<float> emb_fp32(vocab * dim), bias(vocab);
      for (std::size_t r = 0; r < vocab; ++r) {
        bias[r] = static_cast<float>(int(r % 17) - 8) * 0.125f;
        for (std::size_t k = 0; k < dim; ++k) {
          emb_q8[r * dim + k] = static_cast<std::int8_t>(int((r * 19 + k * 37) % 241) - 120);
          emb_fp32[r * dim + k] = static_cast<float>(emb_q8[r * dim + k]) * min_pos;
        }
      }
      bias[1] = -std::numeric_limits<float>::infinity();
      FullVocabQ4 q4_fp32(emb_fp32.data(), bias.data(), vocab, dim);
      FullVocabQ4 q4_i8 = FullVocabQ4::createInt8(emb_q8.data(), min_pos, bias.data(), vocab, dim);
      FullVocabQ4 q4_moved = std::move(q4_i8);

      const std::size_t x_stride = dim + 16;
      for (std::size_t rows : {std::size_t(1), std::size_t(2), std::size_t(4), std::size_t(8)}) {
        std::vector<float> x_rows(rows * x_stride, 0.0f);
        for (std::size_t m = 0; m < rows; ++m) {
          const std::size_t target = (vocab == 13) ? ((m * 3 + 2) % 12 + (m % 2)) : (10 + m * (vocab / rows - 2));
          const std::size_t safe_target = (target == 1) ? 2 : std::min(target, vocab - 1);
          for (std::size_t k = 0; k < dim; ++k) {
            x_rows[m * x_stride + k] = static_cast<float>(emb_q8[safe_target * dim + k]) * min_pos * 4.0f +
                                       std::sin(float(m * 13 + k)) * 0.1f;
          }
        }
        for (std::size_t mt : {std::size_t(1), std::size_t(4), std::size_t(6), std::size_t(12)}) {
          std::vector<std::int64_t> got(rows, -1);
          q4_moved.argMaxBatch(x_rows.data(), rows, x_stride, got.data(), mt);
          for (std::size_t m = 0; m < rows; ++m) {
            const std::size_t ref_fp = q4_fp32.argmax(x_rows.data() + m * x_stride, ws, mt > 1);
            const std::size_t ref_i8 = q4_moved.argmax(x_rows.data() + m * x_stride, ws, mt > 1);
            assert(ref_fp == ref_i8);
            assert(got[m] == static_cast<std::int64_t>(ref_i8));
          }
        }
      }
    }
  }
}
int main() {
  cpuOsGolden();
  if (!mtran::ssru::packed::backend()) {
    const float unused = 0;
    rejects([&] { Weight unavailable(&unused, 32, 8); });
    std::cout << "SKIP: packed CPU/OS unavailable; baseline entry point ran safely\n";
    return 0;
  }
  std::cout << "Backend: " << mtran::ssru::packed::backend() << "\n";
  std::fesetround(FE_TONEAREST);
  kleidiGolden();
  dqlAndShortlistGolden();
  batchAndInt8Golden();
  for (std::size_t k : {32, 64, 512, 2048}) for (std::size_t n : {8, 24, 32, 64}) {
    std::vector<float> original(k * n, 127.f);
    const Weight positive(original.data(), k, n);
    golden(positive, original, k, n, 3, .03125f, 0, true); // Distinguishes exact from ORT pair saturation.
    golden(positive, original, k, n, 1, .03125f, 255, true);
    std::fill(original.begin(), original.end(), -127.f);
    const Weight negative(original.data(), k, n);
    golden(negative, original, k, n, 3, .03125f, 0, true);
    for (std::size_t i = 0; i < original.size(); ++i) original[i] = static_cast<int>((i * 71) % 255) - 127;
    original[0] = 127;
    // Ties to even on both signs; retain the +127 anchor so scale == 1.
    const float ties[] = {.5f, 1.5f, 2.5f, -.5f, -1.5f, -2.5f};
    std::copy(std::begin(ties), std::end(ties), original.begin() + 1);
    const Weight mixed(original.data(), k, n);
    auto run = [&] {
      for (std::uint8_t zp : {0, 1, 127, 128, 255}) golden(mixed, original, k, n, 3, .03125f, zp, false);
    };
    auto concurrent = std::async(std::launch::async, run);
    run(); concurrent.get();
    for (auto& x : original) x /= 7.f;
    const Weight scaled(original.data(), k, n);
    golden(scaled, original, k, n, 1, .007f, 128, false);
    golden(scaled, original, k, n, 3, .007f, 128, false, true);
    golden(scaled, original, k, n, 3, .007f, 128, false, false, true);
  }
  {
    constexpr std::size_t k = (std::size_t(INT32_MAX) / (255 * 128)) & ~std::size_t(31);
    const std::vector<float> large(k * 8, -127.f);
    const Weight weight(large.data(), k, 8);
    golden(weight, large, k, 8, 1, .03125f, 0, true);
    golden(weight, large, k, 8, 1, .03125f, 255, false);
  }
  std::vector<float> original(32 * 8, 0.f);
  const Weight zeros(original.data(), 32, 8);
  golden(zeros, original, 32, 8, 1, .1f, 255, true);
  rejects([&] { Weight bad(nullptr, 32, 8); });
  for (auto [k, n] : {std::pair<std::size_t,std::size_t>{0,8}, {32,0}, {31,8}, {32,7}, {131072,8}, {32,SIZE_MAX/32+1}})
    rejects([&] { Weight bad(original.data(), k, n); });
  original[0] = std::numeric_limits<float>::quiet_NaN();
  rejects([&] { Weight bad(original.data(), 32, 8); });
  original[0] = std::numeric_limits<float>::max();
  rejects([&] { Weight bad(original.data(), 32, 8); });
  Workspace workspace;
  rejects([&] { workspace.resize(SIZE_MAX); });
  auto* activation = workspace.resize(64);
  std::fill(activation, activation + 64, 255);
  std::vector<float> output(8, 123456.f);
  rejects([&] { zeros.multiply(nullptr, 1, .1f, 0, output.data()); });
  rejects([&] { zeros.multiply(activation, 1, .1f, 0, nullptr); });
  rejects([&] { zeros.multiply(activation + 1, 1, .1f, 0, output.data()); });
  rejects([&] { zeros.multiply(activation, 0, .1f, 0, output.data()); });
  rejects([&] { zeros.multiply(activation, SIZE_MAX/32+1, .1f, 0, output.data()); });
  for (float scale : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    rejects([&] { zeros.multiply(activation, 1, scale, 0, output.data()); });
  assert(std::all_of(output.begin(), output.end(), [](float x) { return x == 123456.f; }));
  // An invalid call does not poison existing weights or reusable request storage.
  activation = workspace.resize(32);
  std::fill(activation, activation + 32, 255);
  zeros.multiply(activation, 1, .1f, 255, output.data());
  assert(std::all_of(output.begin(), output.end(), [](float x) { return x == 0; }));
  std::cout << "PASS: scalar golden (" << (std::string_view(mtran::ssru::packed::backend()) == "packed-avx2-ort" ? "ORT int16 saturation" : "mathematically exact")
            << "), CPUID/OS state, scale/zero/full-u8 extremes, shape/overflow, recovery and concurrency\n";
}
