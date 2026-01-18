// Compile this translation unit only with -mavx2 -mavxvnni; callers stay baseline.
#include "third_party/gemmology/gemmology.h"
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace mtran::ssru::packed {
void multiplyVnni(const std::uint8_t* a, const std::int8_t* b, const std::int32_t* sums,
                  std::size_t rows, std::size_t k, std::size_t n,
                  std::uint8_t zp, float scale, float* output, const float* bias) {
  gemmology::SequentialExecutionEngine engine;
  gemmology::Engine<xsimd::avxvnni>::Shift::Multiply(a, b, rows, k, n,
    [&](xsimd::batch<std::int32_t, xsimd::avxvnni> acc, std::size_t row, std::size_t col, std::size_t) {
      using I = xsimd::batch<std::int32_t, xsimd::avxvnni>;
      acc = acc - I(zp) * I::load_unaligned(sums + col);
      if (bias) {
        alignas(32) float values[8];
        xsimd::batch_cast<float>(acc).store_aligned(values);
        for (std::size_t lane = 0; lane < 8; ++lane) output[row * n + col + lane] = std::fma(values[lane], scale, bias[col + lane]);
      } else (xsimd::batch_cast<float>(acc) * scale).store_unaligned(output + row * n + col);
    }, engine);
}
}
