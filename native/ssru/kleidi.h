#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace mtran::ssru::kleidi {
// ORT 1.30 KleidiAI SME (kr=4): signed per-row quantization, stored as u8 for
// the existing packed kernels. Preserve reciprocal scale and separate bias.
inline float quantize(const float* input, std::size_t size, std::uint8_t* output, std::uint8_t& zero) {
  if (!input || !output || !size) throw std::invalid_argument("Invalid SSRU KleidiAI activation");
  float low = 0, high = 0;
  for (std::size_t i = 0; i < size; ++i) {
    if (!std::isfinite(input[i])) throw std::runtime_error("Nonfinite SSRU KleidiAI activation");
    low = std::min(low, input[i]); high = std::max(high, input[i]);
  }
  const float inverse = low == high ? 1.f : 255.f / (high - low);
  const float scale = inverse ? 1.f / inverse : 0.f;
  if (!std::isfinite(inverse) || !std::isfinite(scale) || scale <= 0)
    throw std::runtime_error("Invalid SSRU KleidiAI scale");
  const float lo = low * inverse, hi = high * inverse;
  const float z = std::clamp((-128.f + lo) + (127.f + hi) > 0 ? -128.f - lo : 127.f - hi, -128.f, 127.f);
  const float floor = std::floor(z), fraction = z - floor;
  const float rounded = fraction < .5f ? floor : fraction > .5f ? floor + 1.f : std::fmod(floor, 2.f) == 0 ? floor : floor + 1.f;
  zero = static_cast<std::uint8_t>(rounded + 128.f);
  for (std::size_t i = 0; i < size; ++i)
    output[i] = static_cast<std::uint8_t>(std::clamp(std::round(input[i] * inverse) + rounded, -128.f, 127.f) + 128.f);
  return scale;
}
}
