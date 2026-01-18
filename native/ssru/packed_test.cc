#include "packed.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cpuid.h>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

// This entry point is compiled without AVX flags, including on unsupported hosts.
static bool available() {
  unsigned eax, ebx, ecx, edx;
  return __builtin_cpu_supports("avx2") &&
         __get_cpuid_count(7, 1, &eax, &ebx, &ecx, &edx) && (eax & (1u << 4));
}
using mtran::ssru::packed::Weight;
using mtran::ssru::packed::Workspace;

template<class F> static void rejects(F f) {
  bool threw = false;
  try { f(); } catch (const std::exception&) { threw = true; }
  assert(threw);
}
static void golden(const Weight& weight, const std::vector<float>& original,
                   std::size_t k, std::size_t n, std::size_t rows,
                   float activation_scale, std::uint8_t zero_point, bool extreme) {
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
  weight.multiply(activation, rows, activation_scale, zero_point, output.data() + 1);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < n; ++col) {
      std::int64_t reference = 0;
      for (std::size_t j = 0; j < k; ++j)
        reference += (std::int64_t(activation[row * k + j]) - zero_point) * quantized[j * n + col];
      const float expected = static_cast<float>(reference) * (activation_scale * weight_scale);
      assert(std::bit_cast<std::uint32_t>(output[1 + row * n + col]) == std::bit_cast<std::uint32_t>(expected));
    }
  }
  assert(output.front() == 123456.f && output.back() == 123456.f);
}
int main() {
  if (!available()) { std::cout << "SKIP: AVXVNNI unavailable; baseline entry point ran safely\n"; return 0; }
  std::fesetround(FE_TONEAREST);
  for (std::size_t k : {32, 64}) for (std::size_t n : {8, 24}) {
    std::vector<float> original(k * n, 127.f);
    const Weight positive(original.data(), k, n);
    golden(positive, original, k, n, 3, .03125f, 0, true); // 255*127 must not saturate at int16.
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
  auto* activation = workspace.resize(64);
  std::fill(activation, activation + 64, 255);
  std::vector<float> output(8, 123456.f);
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
  std::cout << "PASS: scalar golden, scale/zero/extremes, shape/overflow, recovery and concurrency\n";
}
