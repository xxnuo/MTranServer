/***************************************************************
 *                                       _                     *
 *                                      | |                    *
 *   __ _  ___ _ __ ___  _ __ ___   ___ | | ___   __ _ _   _   *
 *  / _` |/ _ \ '_ ` _ \| '_ ` _ \ / _ \| |/ _ \ / _` | | | |  *
 * | (_| |  __/ | | | | | | | | | | (_) | | (_) | (_| | |_| |  *
 *  \__, |\___|_| |_| |_|_| |_| |_|\___/|_|\___/ \__, |\__, |  *
 *   __/ |                                        __/ | __/ |  *
 *  |___/                                        |___/ |___/   *
 *                                                             *
 *                                                 version 0.1 *
 ***************************************************************/

#ifndef GEMMOLOGY_FWD_H
#define GEMMOLOGY_FWD_H

#include <cstdint>
#include <cstring>
#include <tuple>
#include <xsimd/xsimd.hpp>

#ifdef GEMMOLOGY_WITH_STD_THREAD
#include <thread>
#include <vector>
#endif

namespace gemmology {

struct SequentialExecutionEngine {

  template<class F>
  inline void operator()(size_t Start, size_t End, size_t Stride, F&& f) {
    for(size_t i = Start; i < End; i += Stride) {
      f(i);
    }
  }

};


#ifdef GEMMOLOGY_WITH_STD_THREAD
struct StdThreadExecutionEngine {

  StdThreadExecutionEngine(size_t PoolSize) : MaxPoolSize(PoolSize) {
    Pool.reserve(PoolSize - 1);
  }

  template<class F>
  inline void operator()(size_t Start, size_t End, size_t Stride, F&& f) {
    const size_t NbIter = (End - Start) / Stride;
    const size_t NbThread = std::min(NbIter, MaxPoolSize);
    const size_t Chunk = (NbIter / NbThread) * Stride;

    size_t Curr = Start, Next = Start;

    for(size_t threadID = 0; threadID < NbThread - 1; ++threadID) {
      Next += Chunk;
      Pool.emplace_back([=]() {
        for(size_t i = Curr; i < Next; i += Stride) {
          f(i);
        };
      });
      Curr = Next;
    }

    for(size_t i = Next; i < End; i += Stride) {
      f(i);
    };
    for(size_t threadID = 0; threadID < Pool.size(); ++threadID) {
      Pool[threadID].join();
    }
    Pool.clear();
  }

  private:
    const size_t MaxPoolSize;
    std::vector<std::thread> Pool;

};

#endif

#ifdef _OPENMP
struct OpenMPExecutionEngine {

  template<class F>
  inline void operator()(size_t Start, size_t End, size_t Stride, F&& f) {
#pragma omp parallel for
    for(size_t i = Start; i < End; i += Stride) {
      f(i);
    }
  }

};
#endif

namespace callbacks {

struct Unquantize {
  float unquant_mult;
  template <class Arch>
  xsimd::batch<float, Arch> operator()(xsimd::batch<int32_t, Arch> total, size_t, size_t, size_t);
  template <class Arch>
  std::tuple<xsimd::batch<float, Arch>, xsimd::batch<float, Arch>> operator()(
      std::tuple<xsimd::batch<int32_t, Arch>, xsimd::batch<int32_t, Arch>>
          total,
      size_t, size_t, size_t);
};

struct AddBias {
  const float *bias_addr;
  template <class Arch>
  xsimd::batch<float, Arch> operator()(xsimd::batch<float, Arch> total, size_t, size_t col_idx,
                  size_t);
  template <class Arch>
  std::tuple<xsimd::batch<float, Arch>, xsimd::batch<float, Arch>>
  operator()(
      std::tuple<xsimd::batch<float, Arch>, xsimd::batch<float, Arch>> total,
      size_t, size_t col_idx, size_t);
};

struct Write {
  float *output_addr;

  Write(float *o) : output_addr(o) {}
  Write(int32_t *o) : output_addr(reinterpret_cast<float*>(o)) {}

  template <class Arch>
  void operator()(xsimd::batch<float, Arch> result, size_t row_idx,
                  size_t col_idx, size_t col_size);
  template <class Arch>
  void operator()(xsimd::batch<int32_t, Arch> result, size_t row_idx,
                  size_t col_idx, size_t col_size);

  template <class Arch>
  void operator()(
      std::tuple<xsimd::batch<float, Arch>, xsimd::batch<float, Arch>> result,
      size_t row_idx, size_t col_idx, size_t col_size);

  template <class Arch>
  void operator()(
      std::tuple<xsimd::batch<int32_t, Arch>, xsimd::batch<int32_t, Arch>>
          result,
      size_t row_idx, size_t col_idx, size_t col_size);
};

struct UnquantizeAndWrite {

  Unquantize unquantize;
  Write write;

  UnquantizeAndWrite(float factor, float *output)
      : unquantize{factor}, write{output} {}

  template <class T>
  void operator()(T const &total, size_t row_idx, size_t col_idx,
                  size_t col_size);
};

struct UnquantizeAndAddBiasAndWrite {

  Unquantize unquantize;
  AddBias add_bias;
  Write write;

  UnquantizeAndAddBiasAndWrite(float factor, const float *bias, float *output)
      : unquantize{factor}, add_bias{bias}, write{output} {}

  template <class T>
  void operator()(T const &total, size_t row_idx, size_t col_idx,
                  size_t col_size);
};

} // namespace callbacks

//
// Arch-specific implementation of each routine
//
template <class Arch> struct Engine {

  static void QuantizeU(const float *input, uint8_t *output, float quant_mult,
                        size_t size);

  static void Quantize(const float *const input, int8_t *const output,
                       float quant_mult, size_t size);

  template <typename IntegerTy>
  static void SelectColumnsB(const int8_t *input, int8_t *output, size_t rows,
                             const IntegerTy *cols_begin,
                             const IntegerTy *cols_end);

  static void PrepareBTransposed(const float *input, int8_t *output,
                                 float quant_mult, size_t cols, size_t rows);

  static void PrepareBQuantizedTransposed(const int8_t *input, int8_t *output,
                                          size_t cols, size_t rows);

  static void PrepareBQuantized(const int8_t *input, int8_t *output,
                                size_t cols, size_t rows);

  static void PrepareB(const float *input, int8_t *output_shadow,
                       float quant_mult, size_t rows, size_t cols);

  static void PrepareA(const float *input, int8_t *output, float quant_mult,
                       size_t rows, size_t cols);

  struct Shift {

    static void PrepareA(const float *input, uint8_t *output, float quant_mult,
                         size_t rows, size_t cols);

    template <class Callback, class ExecutionEngine>
    static void Multiply(const uint8_t *A, const int8_t *B, size_t A_rows,
                         size_t width, size_t B_cols, Callback callback,
                         ExecutionEngine& engine);

    template <class Callback>
    static void PrepareBias(const int8_t *B, size_t width, size_t B_cols,
                            Callback C);
  };
};

//
// Top-level wrappers that mostly match intgemm API
//

template <class Arch = xsimd::default_arch>
inline void QuantizeU(const float *input, uint8_t *output, float quant_mult,
                      size_t size) {
  return Engine<Arch>::QuantizeU(input, output, quant_mult, size);
}

template <class Arch = xsimd::default_arch>
inline void Quantize(const float *const input, int8_t *const output,
                     float quant_mult, size_t size) {
  return Engine<Arch>::Quantize(input, output, quant_mult, size);
}

template <class Arch = xsimd::default_arch, typename IntegerTy>
inline void SelectColumnsB(const int8_t *input, int8_t *output, size_t rows,
                           const IntegerTy *cols_begin,
                           const IntegerTy *cols_end) {
  return Engine<Arch>::SelectColumnsB(input, output, rows, cols_begin,
                                      cols_end);
}

template <class Arch = xsimd::default_arch>
inline void PrepareBTransposed(const float *input, int8_t *output,
                               float quant_mult, size_t cols, size_t rows) {
  return Engine<Arch>::PrepareBTransposed(input, output, quant_mult, cols,
                                          rows);
}

template <class Arch = xsimd::default_arch>
inline void PrepareBQuantized(const int8_t *input, int8_t *output,
                                        size_t cols, size_t rows) {
  return Engine<Arch>::PrepareBQuantized(input, output, cols, rows);
}

template <class Arch = xsimd::default_arch>
inline void PrepareBQuantizedTransposed(const int8_t *input, int8_t *output,
                                        size_t cols, size_t rows) {
  return Engine<Arch>::PrepareBQuantizedTransposed(input, output, cols, rows);
}

template <class Arch = xsimd::default_arch>
inline void PrepareB(const float *input, int8_t *output_shadow,
                     float quant_mult, size_t rows, size_t cols) {
  return Engine<Arch>::PrepareB(input, output_shadow, quant_mult, rows, cols);
}

template <class Arch = xsimd::default_arch>
inline void PrepareA(const float *input, int8_t *output, float quant_mult,
                     size_t rows, size_t cols) {
  return Engine<Arch>::PrepareA(input, output, quant_mult, rows, cols);
}

namespace Shift {

template <class Arch = xsimd::default_arch>
inline void PrepareA(const float *input, uint8_t *output, float quant_mult,
                     size_t rows, size_t cols) {
  return Engine<Arch>::Shift::PrepareA(input, output, quant_mult, rows, cols);
}

template <class Arch = xsimd::default_arch, class Callback, class ExecutionEngine=SequentialExecutionEngine>
inline void Multiply(const uint8_t *A, const int8_t *B, size_t A_rows,
                     size_t width, size_t B_cols, Callback C, ExecutionEngine&& engine={}) {
  return Engine<Arch>::Shift::Multiply(A, B, A_rows, width, B_cols, C, engine);
}

template <class Arch = xsimd::default_arch, class Callback>
inline void PrepareBias(const int8_t *B, size_t width, size_t B_cols,
                        Callback C) {
  return Engine<Arch>::Shift::PrepareBias(B, width, B_cols, C);
}

} // namespace Shift

} // namespace gemmology

#endif
