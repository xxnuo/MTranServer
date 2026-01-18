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
  constexpr unsigned avx = 0x18000000u, avx2 = 1u << 5, vnni = 1u << 4;
  constexpr unsigned avx512 = 0xc0030000u, vnni512 = 1u << 11;
  // i7-8550U / EPYC 7763: no VNNI, including CPUs with no CPUID 7.1.
  assert(x86Features(avx, avx2, 0, 0, 6) == Avx2);
  assert(x86Features(avx, avx2 | avx512, 0, 0, 0xe6) == Avx2);
  // AVXVNNI needs only XMM/YMM state; AVX512VNNI needs all ZMM/opmask state.
  assert(x86Features(avx, avx2, 0, vnni, 6) == (Avx2 | AvxVnni));
  assert(x86Features(avx, avx2 | avx512, vnni512, 0, 0xe6) == (Avx2 | Avx512Vnni));
  assert(x86Features(avx, avx2 | avx512, vnni512, vnni, 0xe6) == (Avx2 | AvxVnni | Avx512Vnni));
  for (unsigned bit : {16, 17, 30, 31})
    assert(x86Features(avx, avx2 | (avx512 & ~(1u << bit)), vnni512, 0, 0xe6) == Avx2);
  for (std::uint64_t state : {0x6, 0x26, 0x46, 0x66, 0x86, 0xa6, 0xc6}) {
    assert(x86Features(avx, avx2 | avx512, vnni512, 0, state) == Avx2);
    assert(x86Features(avx, avx2 | avx512, vnni512, vnni, state) == (Avx2 | AvxVnni));
  }
  for (unsigned leaf1 : {0u, 1u << 27, 1u << 28})
    assert(x86Features(leaf1, avx2 | avx512, vnni512, vnni, 0xe6) == 0);
  for (std::uint64_t state : {0, 2, 4, 0xe0, 0xe2, 0xe4})
    assert(x86Features(avx, avx2 | avx512, vnni512, vnni, state) == 0);
  assert(x86Features(avx, avx512, vnni512, vnni, 0xe6) == 0);
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
