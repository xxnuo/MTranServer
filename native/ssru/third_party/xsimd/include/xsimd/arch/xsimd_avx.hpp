/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 * Copyright (c) Marco Barbone                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_AVX_HPP
#define XSIMD_AVX_HPP

#include "../types/xsimd_avx_register.hpp"
#include "../types/xsimd_batch_constant.hpp"

#include <cassert>
#include <complex>
#include <limits>
#include <type_traits>

namespace xsimd
{
    namespace kernel
    {
        using namespace types;

        // fwd
        template <class A, class T, size_t I>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<common>) noexcept;

        template <class A>
        XSIMD_INLINE void transpose(batch<uint16_t, A>* matrix_begin, batch<uint16_t, A>* matrix_end, requires_arch<common>) noexcept;
        template <class A>
        XSIMD_INLINE void transpose(batch<uint8_t, A>* matrix_begin, batch<uint8_t, A>* matrix_end, requires_arch<common>) noexcept;

        namespace detail
        {
            XSIMD_INLINE __m128i lower_half(__m256i self) noexcept
            {
                return _mm256_castsi256_si128(self);
            }
            XSIMD_INLINE __m128 lower_half(__m256 self) noexcept
            {
                return _mm256_castps256_ps128(self);
            }
            XSIMD_INLINE __m128d lower_half(__m256d self) noexcept
            {
                return _mm256_castpd256_pd128(self);
            }
            XSIMD_INLINE __m128i upper_half(__m256i self) noexcept
            {
                return _mm256_extractf128_si256(self, 1);
            }
            XSIMD_INLINE __m128 upper_half(__m256 self) noexcept
            {
                return _mm256_extractf128_ps(self, 1);
            }
            XSIMD_INLINE __m128d upper_half(__m256d self) noexcept
            {
                return _mm256_extractf128_pd(self, 1);
            }
            XSIMD_INLINE __m256i merge_sse(__m128i low, __m128i high) noexcept
            {
                return _mm256_insertf128_si256(_mm256_castsi128_si256(low), high, 1);
            }
            XSIMD_INLINE __m256 merge_sse(__m128 low, __m128 high) noexcept
            {
                return _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1);
            }
            XSIMD_INLINE __m256d merge_sse(__m128d low, __m128d high) noexcept
            {
                return _mm256_insertf128_pd(_mm256_castpd128_pd256(low), high, 1);
            }
            template <class F>
            XSIMD_INLINE __m256i fwd_to_sse(F f, __m256i self) noexcept
            {
                __m128i self_low = lower_half(self), self_high = upper_half(self);
                __m128i res_low = f(self_low);
                __m128i res_high = f(self_high);
                return merge_sse(res_low, res_high);
            }
            template <class F>
            XSIMD_INLINE __m256i fwd_to_sse(F f, __m256i self, __m256i other) noexcept
            {
                __m128i self_low = lower_half(self), self_high = upper_half(self),
                        other_low = lower_half(other), other_high = upper_half(other);
                __m128i res_low = f(self_low, other_low);
                __m128i res_high = f(self_high, other_high);
                return merge_sse(res_low, res_high);
            }
            template <class F>
            XSIMD_INLINE __m256i fwd_to_sse(F f, __m256i self, int32_t other) noexcept
            {
                __m128i self_low = lower_half(self), self_high = upper_half(self);
                __m128i res_low = f(self_low, other);
                __m128i res_high = f(self_high, other);
                return merge_sse(res_low, res_high);
            }
        }

        // abs
        template <class A>
        XSIMD_INLINE batch<float, A> abs(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            __m256 sign_mask = _mm256_set1_ps(-0.f); // -0.f = 1 << 31
            return _mm256_andnot_ps(sign_mask, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> abs(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            __m256d sign_mask = _mm256_set1_pd(-0.f); // -0.f = 1 << 31
            return _mm256_andnot_pd(sign_mask, self);
        }

        // add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return add(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> add(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_add_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> add(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_add_pd(self, other);
        }

        // all
        template <class A>
        XSIMD_INLINE bool all(batch_bool<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_testc_ps(self, batch_bool<float, A>(true)) != 0;
        }
        template <class A>
        XSIMD_INLINE bool all(batch_bool<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_testc_pd(self, batch_bool<double, A>(true)) != 0;
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE bool all(batch_bool<T, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_testc_si256(self, batch_bool<T, A>(true)) != 0;
        }

        // any
        template <class A>
        XSIMD_INLINE bool any(batch_bool<float, A> const& self, requires_arch<avx>) noexcept
        {
            return !_mm256_testz_ps(self, self);
        }
        template <class A>
        XSIMD_INLINE bool any(batch_bool<double, A> const& self, requires_arch<avx>) noexcept
        {
            return !_mm256_testz_pd(self, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE bool any(batch_bool<T, A> const& self, requires_arch<avx>) noexcept
        {
            return !_mm256_testz_si256(self, self);
        }

        // batch_bool_cast
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<avx>) noexcept
        {
            return { bitwise_cast<T_out>(batch<T_in, A>(self.data)).data };
        }

        // bitwise_and
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_and(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_and_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_and(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_and_pd(self, other);
        }

        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_and(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_and_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_and(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_and_pd(self, other);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_and(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_and(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }

        // bitwise_andnot
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_andnot(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_andnot_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_andnot(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_andnot_pd(other, self);
        }

        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_andnot(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_andnot_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_andnot(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_andnot_pd(other, self);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_andnot(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_andnot(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, int32_t o) noexcept
                                      { return bitwise_lshift(batch<T, sse4_2>(s), o, sse4_2 {}); },
                                      self, other);
        }

        // bitwise_not
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s) noexcept
                                      { return bitwise_not(batch<T, sse4_2>(s), sse4_2 {}); },
                                      self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s) noexcept
                                      { return bitwise_not(batch_bool<T, sse4_2>(s), sse4_2 {}); },
                                      self);
        }

        // bitwise_or
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_or(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_or_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_or(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_or_pd(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_or(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_or_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_or(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_or_pd(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_or(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_or(batch_bool<T, sse4_2>(s), batch_bool<T, sse4_2>(o)); },
                                      self, other);
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, int32_t o) noexcept
                                      { return bitwise_rshift(batch<T, sse4_2>(s), o, sse4_2 {}); },
                                      self, other);
        }

        // bitwise_xor
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_xor(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_xor_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_xor(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_xor_pd(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_xor(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_xor_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_xor(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_xor_pd(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_xor(batch<T, sse4_2>(s), batch<T, sse4_2>(o), sse4_2 {}); },
                                      self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return bitwise_xor(batch_bool<T, sse4_2>(s), batch_bool<T, sse4_2>(o), sse4_2 {}); },
                                      self, other);
        }

        // bitwise_cast
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<T, A> const& self, batch<float, A> const&, requires_arch<avx>) noexcept
        {
            return _mm256_castsi256_ps(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<T, A> const& self, batch<double, A> const&, requires_arch<avx>) noexcept
        {
            return _mm256_castsi256_pd(self);
        }
        template <class A, class T, class Tp, class = std::enable_if_t<std::is_integral_v<std::common_type_t<T, Tp>>>>
        XSIMD_INLINE batch<Tp, A> bitwise_cast(batch<T, A> const& self, batch<Tp, A> const&, requires_arch<avx>) noexcept
        {
            return batch<Tp, A>(self.data);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<float, A> const& self, batch<double, A> const&, requires_arch<avx>) noexcept
        {
            return _mm256_castps_pd(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_cast(batch<float, A> const& self, batch<T, A> const&, requires_arch<avx>) noexcept
        {
            return _mm256_castps_si256(self);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<double, A> const& self, batch<float, A> const&, requires_arch<avx>) noexcept
        {
            return _mm256_castpd_ps(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_cast(batch<double, A> const& self, batch<T, A> const&, requires_arch<avx>) noexcept
        {
            return _mm256_castpd_si256(self);
        }

        // bitwise_not
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_not(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_xor_ps(self, _mm256_castsi256_ps(_mm256_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_not(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_xor_pd(self, _mm256_castsi256_pd(_mm256_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_not(batch_bool<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_xor_ps(self, _mm256_castsi256_ps(_mm256_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_not(batch_bool<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_xor_pd(self, _mm256_castsi256_pd(_mm256_set1_epi32(-1)));
        }

        // broadcast
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_set1_epi8(val);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_set1_epi16(val);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_set1_epi32(val);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_set1_epi64x(val);
            }
            else
            {
                assert(false && "unsupported");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A> broadcast(float val, requires_arch<avx>) noexcept
        {
            return _mm256_set1_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> broadcast(double val, requires_arch<avx>) noexcept
        {
            return _mm256_set1_pd(val);
        }

        // ceil
        template <class A>
        XSIMD_INLINE batch<float, A> ceil(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_ceil_ps(self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> ceil(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_ceil_pd(self);
        }

        namespace detail
        {
            // On clang, _mm256_extractf128_ps is built upon build_shufflevector
            // which require index parameter to be a constant
            template <int index, class B>
            XSIMD_INLINE B get_half_complex_f(const B& real, const B& imag) noexcept
            {
                __m128 tmp0 = _mm256_extractf128_ps(real, index);
                __m128 tmp1 = _mm256_extractf128_ps(imag, index);
                __m128 tmp2 = _mm_unpackhi_ps(tmp0, tmp1);
                tmp0 = _mm_unpacklo_ps(tmp0, tmp1);
                __m256 res = real;
                res = _mm256_insertf128_ps(res, tmp0, 0);
                res = _mm256_insertf128_ps(res, tmp2, 1);
                return res;
            }
            template <int index, class B>
            XSIMD_INLINE B get_half_complex_d(const B& real, const B& imag) noexcept
            {
                __m128d tmp0 = _mm256_extractf128_pd(real, index);
                __m128d tmp1 = _mm256_extractf128_pd(imag, index);
                __m128d tmp2 = _mm_unpackhi_pd(tmp0, tmp1);
                tmp0 = _mm_unpacklo_pd(tmp0, tmp1);
                __m256d res = real;
                res = _mm256_insertf128_pd(res, tmp0, 0);
                res = _mm256_insertf128_pd(res, tmp2, 1);
                return res;
            }

            // complex_low
            template <class A>
            XSIMD_INLINE batch<float, A> complex_low(batch<std::complex<float>, A> const& self, requires_arch<avx>) noexcept
            {
                return get_half_complex_f<0>(self.real(), self.imag());
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_low(batch<std::complex<double>, A> const& self, requires_arch<avx>) noexcept
            {
                return get_half_complex_d<0>(self.real(), self.imag());
            }

            // complex_high
            template <class A>
            XSIMD_INLINE batch<float, A> complex_high(batch<std::complex<float>, A> const& self, requires_arch<avx>) noexcept
            {
                return get_half_complex_f<1>(self.real(), self.imag());
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_high(batch<std::complex<double>, A> const& self, requires_arch<avx>) noexcept
            {
                return get_half_complex_d<1>(self.real(), self.imag());
            }
        }

        // fast_cast
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<int32_t, A> const& self, batch<float, A> const&, requires_arch<avx>) noexcept
            {
                return _mm256_cvtepi32_ps(self);
            }

            template <class A>
            XSIMD_INLINE batch<int32_t, A> fast_cast(batch<float, A> const& self, batch<int32_t, A> const&, requires_arch<avx>) noexcept
            {
                return _mm256_cvttps_epi32(self);
            }
        }

        // decr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx>) noexcept
        {
            return self + batch<T, A>(mask.data);
        }

        // div
        template <class A>
        XSIMD_INLINE batch<float, A> div(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_div_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> div(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_div_pd(self, other);
        }

        // eq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> eq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_ps(self, other, _CMP_EQ_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_pd(self, other, _CMP_EQ_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> eq(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<avx>) noexcept
        {
            return ~(self != other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<avx>) noexcept
        {
            return ~(self != other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return eq(batch<T, sse4_2>(s), batch<T, sse4_2>(o), sse4_2 {}); },
                                      self, other);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx>) noexcept
        {
            return ~(self != other);
        }

        // floor
        template <class A>
        XSIMD_INLINE batch<float, A> floor(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_floor_ps(self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> floor(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_floor_pd(self);
        }

        // from_mask
        template <class A>
        XSIMD_INLINE batch_bool<float, A> from_mask(batch_bool<float, A> const&, uint64_t mask, requires_arch<avx>) noexcept
        {
            alignas(A::alignment()) static const uint64_t lut32[] = {
                0x0000000000000000ul,
                0x00000000FFFFFFFFul,
                0xFFFFFFFF00000000ul,
                0xFFFFFFFFFFFFFFFFul,
            };
            assert(!(mask & ~0xFFul) && "inbound mask");
            return _mm256_castsi256_ps(_mm256_setr_epi64x(lut32[mask & 0x3], lut32[(mask >> 2) & 0x3], lut32[(mask >> 4) & 0x3], lut32[mask >> 6]));
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> from_mask(batch_bool<double, A> const&, uint64_t mask, requires_arch<avx>) noexcept
        {
            alignas(A::alignment()) static const uint64_t lut64[][4] = {
                { 0x0000000000000000ul, 0x0000000000000000ul, 0x0000000000000000ul, 0x0000000000000000ul },
                { 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0x0000000000000000ul, 0x0000000000000000ul },
                { 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0x0000000000000000ul },
                { 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0x0000000000000000ul },
                { 0x0000000000000000ul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul },
                { 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul },
                { 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul },
                { 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul },
                { 0x0000000000000000ul, 0x0000000000000000ul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul },
                { 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul },
                { 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul },
                { 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul },
                { 0x0000000000000000ul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul },
                { 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul },
                { 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul },
                { 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul },
            };
            assert(!(mask & ~0xFul) && "inbound mask");
            return _mm256_castsi256_pd(_mm256_load_si256((const __m256i*)lut64[mask]));
        }
        template <class T, class A, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> from_mask(batch_bool<T, A> const&, uint64_t mask, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                alignas(A::alignment()) static const uint32_t lut32[] = {
                    0x00000000,
                    0x000000FF,
                    0x0000FF00,
                    0x0000FFFF,
                    0x00FF0000,
                    0x00FF00FF,
                    0x00FFFF00,
                    0x00FFFFFF,
                    0xFF000000,
                    0xFF0000FF,
                    0xFF00FF00,
                    0xFF00FFFF,
                    0xFFFF0000,
                    0xFFFF00FF,
                    0xFFFFFF00,
                    0xFFFFFFFF,
                };
                assert(!(mask & ~0xFFFFFFFFul) && "inbound mask");
                return _mm256_setr_epi32(lut32[mask & 0xF], lut32[(mask >> 4) & 0xF],
                                         lut32[(mask >> 8) & 0xF], lut32[(mask >> 12) & 0xF],
                                         lut32[(mask >> 16) & 0xF], lut32[(mask >> 20) & 0xF],
                                         lut32[(mask >> 24) & 0xF], lut32[mask >> 28]);
            }
            else if constexpr (sizeof(T) == 2)
            {
                alignas(A::alignment()) static const uint64_t lut64[] = {
                    0x0000000000000000ul,
                    0x000000000000FFFFul,
                    0x00000000FFFF0000ul,
                    0x00000000FFFFFFFFul,
                    0x0000FFFF00000000ul,
                    0x0000FFFF0000FFFFul,
                    0x0000FFFFFFFF0000ul,
                    0x0000FFFFFFFFFFFFul,
                    0xFFFF000000000000ul,
                    0xFFFF00000000FFFFul,
                    0xFFFF0000FFFF0000ul,
                    0xFFFF0000FFFFFFFFul,
                    0xFFFFFFFF00000000ul,
                    0xFFFFFFFF0000FFFFul,
                    0xFFFFFFFFFFFF0000ul,
                    0xFFFFFFFFFFFFFFFFul,
                };
                assert(!(mask & ~0xFFFFul) && "inbound mask");
                return _mm256_setr_epi64x(lut64[mask & 0xF], lut64[(mask >> 4) & 0xF], lut64[(mask >> 8) & 0xF], lut64[(mask >> 12) & 0xF]);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_castps_si256(from_mask(batch_bool<float, A> {}, mask, avx {}));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_castpd_si256(from_mask(batch_bool<double, A> {}, mask, avx {}));
            }
        }

        // haddp
        template <class A>
        XSIMD_INLINE batch<float, A> haddp(batch<float, A> const* row, requires_arch<avx>) noexcept
        {
            // row = (a,b,c,d,e,f,g,h)
            // tmp0 = (a0+a1, a2+a3, b0+b1, b2+b3, a4+a5, a6+a7, b4+b5, b6+b7)
            __m256 tmp0 = _mm256_hadd_ps(row[0], row[1]);
            // tmp1 = (c0+c1, c2+c3, d1+d2, d2+d3, c4+c5, c6+c7, d4+d5, d6+d7)
            __m256 tmp1 = _mm256_hadd_ps(row[2], row[3]);
            // tmp1 = (a0+a1+a2+a3, b0+b1+b2+b3, c0+c1+c2+c3, d0+d1+d2+d3,
            // a4+a5+a6+a7, b4+b5+b6+b7, c4+c5+c6+c7, d4+d5+d6+d7)
            tmp1 = _mm256_hadd_ps(tmp0, tmp1);
            // tmp0 = (e0+e1, e2+e3, f0+f1, f2+f3, e4+e5, e6+e7, f4+f5, f6+f7)
            tmp0 = _mm256_hadd_ps(row[4], row[5]);
            // tmp2 = (g0+g1, g2+g3, h0+h1, h2+h3, g4+g5, g6+g7, h4+h5, h6+h7)
            __m256 tmp2 = _mm256_hadd_ps(row[6], row[7]);
            // tmp2 = (e0+e1+e2+e3, f0+f1+f2+f3, g0+g1+g2+g3, h0+h1+h2+h3,
            // e4+e5+e6+e7, f4+f5+f6+f7, g4+g5+g6+g7, h4+h5+h6+h7)
            tmp2 = _mm256_hadd_ps(tmp0, tmp2);
            // tmp0 = (a0+a1+a2+a3, b0+b1+b2+b3, c0+c1+c2+c3, d0+d1+d2+d3,
            // e4+e5+e6+e7, f4+f5+f6+f7, g4+g5+g6+g7, h4+h5+h6+h7)
            tmp0 = _mm256_blend_ps(tmp1, tmp2, 0b11110000);
            // tmp1 = (a4+a5+a6+a7, b4+b5+b6+b7, c4+c5+c6+c7, d4+d5+d6+d7,
            // e0+e1+e2+e3, f0+f1+f2+f3, g0+g1+g2+g3, h0+h1+h2+h3)
            tmp1 = _mm256_permute2f128_ps(tmp1, tmp2, 0x21);
            return _mm256_add_ps(tmp0, tmp1);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> haddp(batch<double, A> const* row, requires_arch<avx>) noexcept
        {
            // row = (a,b,c,d)
            // tmp0 = (a0+a1, b0+b1, a2+a3, b2+b3)
            __m256d tmp0 = _mm256_hadd_pd(row[0], row[1]);
            // tmp1 = (c0+c1, d0+d1, c2+c3, d2+d3)
            __m256d tmp1 = _mm256_hadd_pd(row[2], row[3]);
            // tmp2 = (a0+a1, b0+b1, c2+c3, d2+d3)
            __m256d tmp2 = _mm256_blend_pd(tmp0, tmp1, 0b1100);
            // tmp1 = (a2+a3, b2+b3, c2+c3, d2+d3)
            tmp1 = _mm256_permute2f128_pd(tmp0, tmp1, 0x21);
            return _mm256_add_pd(tmp1, tmp2);
        }

        // incr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx>) noexcept
        {
            return self - batch<T, A>(mask.data);
        }

        // first (must precede get for two-phase lookup)
        template <class A>
        XSIMD_INLINE float first(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_cvtss_f32(self);
        }

        template <class A>
        XSIMD_INLINE double first(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_cvtsd_f64(self);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return static_cast<T>(_mm_cvtsi128_si32(_mm256_castsi256_si128(self)) & 0xFF);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return static_cast<T>(_mm_cvtsi128_si32(_mm256_castsi256_si128(self)) & 0xFFFF);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_mm_cvtsi128_si32(_mm256_castsi256_si128(self)));
            }
            else if constexpr (sizeof(T) == 8)
            {
                batch<T, sse4_2> low = _mm256_castsi256_si128(self);
                return first(low, sse4_2 {});
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // get
        template <class A, size_t I>
        XSIMD_INLINE float get(batch<float, A> const& self, ::xsimd::index<I>, requires_arch<avx>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, avx {});
            }
            constexpr size_t elements_per_lane = batch<float, sse4_1>::size;
            constexpr size_t lane = I / elements_per_lane;
            constexpr size_t sub_index = I % elements_per_lane;
            const auto half = (lane == 0) ? detail::lower_half(self) : detail::upper_half(self);
            return kernel::get(batch<float, sse4_1>(half), ::xsimd::index<sub_index> {}, sse4_1 {});
        }

        template <class A, size_t I>
        XSIMD_INLINE double get(batch<double, A> const& self, ::xsimd::index<I>, requires_arch<avx>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, avx {});
            }
            constexpr size_t elements_per_lane = batch<double, sse4_1>::size;
            constexpr size_t lane = I / elements_per_lane;
            constexpr size_t sub_index = I % elements_per_lane;
            const auto half = (lane == 0) ? detail::lower_half(self) : detail::upper_half(self);
            return kernel::get(batch<double, sse4_1>(half), ::xsimd::index<sub_index> {}, sse4_1 {});
        }

        template <class A, size_t I, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<avx>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, avx {});
            }
            constexpr size_t elements_per_lane = batch<T, sse4_1>::size;
            constexpr size_t lane = I / elements_per_lane;
            constexpr size_t sub_index = I % elements_per_lane;
            const auto half = (lane == 0) ? detail::lower_half(self) : detail::upper_half(self);
            return kernel::get(batch<T, sse4_1>(half), ::xsimd::index<sub_index> {}, sse4_1 {});
        }

        // insert
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I> pos, requires_arch<avx>) noexcept
        {
#if !defined(_MSC_VER) || _MSC_VER > 1900
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_insert_epi8(self, val, I);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_insert_epi16(self, val, I);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_insert_epi32(self, val, I);
            }
            else
            {
                return insert(self, val, pos, common {});
            }
#endif
            return insert(self, val, pos, common {});
        }

        // isnan
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isnan(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_ps(self, self, _CMP_UNORD_Q);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isnan(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_pd(self, self, _CMP_UNORD_Q);
        }

        // le
        template <class A>
        XSIMD_INLINE batch_bool<float, A> le(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_ps(self, other, _CMP_LE_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> le(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_pd(self, other, _CMP_LE_OQ);
        }

        // load_aligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_aligned(T const* mem, convert<T>, requires_arch<avx>) noexcept
        {
            return _mm256_load_si256((__m256i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_aligned(float const* mem, convert<float>, requires_arch<avx>) noexcept
        {
            return _mm256_load_ps(mem);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_aligned(double const* mem, convert<double>, requires_arch<avx>) noexcept
        {
            return _mm256_load_pd(mem);
        }

        namespace detail
        {
            // load_complex
            template <class A>
            XSIMD_INLINE batch<std::complex<float>, A> load_complex(batch<float, A> const& hi, batch<float, A> const& lo, requires_arch<avx>) noexcept
            {
                using batch_type = batch<float, A>;
                __m128 tmp0 = _mm256_extractf128_ps(hi, 0);
                __m128 tmp1 = _mm256_extractf128_ps(hi, 1);
                __m128 tmp_real = _mm_shuffle_ps(tmp0, tmp1, _MM_SHUFFLE(2, 0, 2, 0));
                __m128 tmp_imag = _mm_shuffle_ps(tmp0, tmp1, _MM_SHUFFLE(3, 1, 3, 1));
                batch_type real = _mm256_castps128_ps256(tmp_real);
                batch_type imag = _mm256_castps128_ps256(tmp_imag);

                tmp0 = _mm256_extractf128_ps(lo, 0);
                tmp1 = _mm256_extractf128_ps(lo, 1);
                tmp_real = _mm_shuffle_ps(tmp0, tmp1, _MM_SHUFFLE(2, 0, 2, 0));
                tmp_imag = _mm_shuffle_ps(tmp0, tmp1, _MM_SHUFFLE(3, 1, 3, 1));
                real = _mm256_insertf128_ps(real, tmp_real, 1);
                imag = _mm256_insertf128_ps(imag, tmp_imag, 1);
                return { real, imag };
            }
            template <class A>
            XSIMD_INLINE batch<std::complex<double>, A> load_complex(batch<double, A> const& hi, batch<double, A> const& lo, requires_arch<avx>) noexcept
            {
                using batch_type = batch<double, A>;
                __m128d tmp0 = _mm256_extractf128_pd(hi, 0);
                __m128d tmp1 = _mm256_extractf128_pd(hi, 1);
                batch_type real = _mm256_castpd128_pd256(_mm_unpacklo_pd(tmp0, tmp1));
                batch_type imag = _mm256_castpd128_pd256(_mm_unpackhi_pd(tmp0, tmp1));

                tmp0 = _mm256_extractf128_pd(lo, 0);
                tmp1 = _mm256_extractf128_pd(lo, 1);
                __m256d re_tmp1 = _mm256_insertf128_pd(real, _mm_unpacklo_pd(tmp0, tmp1), 1);
                __m256d im_tmp1 = _mm256_insertf128_pd(imag, _mm_unpackhi_pd(tmp0, tmp1), 1);
                real = _mm256_blend_pd(real, re_tmp1, 12);
                imag = _mm256_blend_pd(imag, im_tmp1, 12);
                return { real, imag };
            }
        }

        // load_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* mem, convert<T>, requires_arch<avx>) noexcept
        {
            return _mm256_loadu_si256((__m256i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_unaligned(float const* mem, convert<float>, requires_arch<avx>) noexcept
        {
            return _mm256_loadu_ps(mem);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_unaligned(double const* mem, convert<double>, requires_arch<avx>) noexcept
        {
            return _mm256_loadu_pd(mem);
        }

        // AVX helpers to avoid type-based branching in the generic load_masked
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<float, A> maskload(float const* mem, batch<as_integer_t<float>, A> const& mask) noexcept
            {
                return _mm256_maskload_ps(mem, mask);
            }

            template <class A>
            XSIMD_INLINE batch<double, A> maskload(double const* mem, batch<as_integer_t<double>, A> const& mask) noexcept
            {
                return _mm256_maskload_pd(mem, mask);
            }

            template <class A>
            XSIMD_INLINE batch<float, A> zero_extend(batch<float, A> const& hi) noexcept
            {
                return _mm256_insertf128_ps(_mm256_setzero_ps(), hi, 1);
            }

            template <class A>
            XSIMD_INLINE batch<double, A> zero_extend(batch<double, A> const& hi) noexcept
            {
                return _mm256_insertf128_pd(_mm256_setzero_pd(), hi, 1);
            }

            // allow inserting a 128-bit SSE batch into the upper half of an AVX batch
            template <class A, class SrcA>
            XSIMD_INLINE batch<float, A> zero_extend(batch<float, SrcA> const& hi) noexcept
            {
                return _mm256_insertf128_ps(_mm256_setzero_ps(), hi, 1);
            }

            template <class A, class SrcA>
            XSIMD_INLINE batch<double, A> zero_extend(batch<double, SrcA> const& hi) noexcept
            {
                return _mm256_insertf128_pd(_mm256_setzero_pd(), hi, 1);
            }

            // 128-bit batch into the lower half, upper half zero (no instruction)
            template <class A, class SrcA>
            XSIMD_INLINE batch<float, A> zero_extend_lo(batch<float, SrcA> const& lo) noexcept
            {
                return _mm256_zextps128_ps256(lo);
            }

            template <class A, class SrcA>
            XSIMD_INLINE batch<double, A> zero_extend_lo(batch<double, SrcA> const& lo) noexcept
            {
                return _mm256_zextpd128_pd256(lo);
            }
        }

        // Runtime-mask load (float/double).
        template <class A, class Mode>
        XSIMD_INLINE batch<float, A>
        load_masked(float const* mem, batch_bool<float, A> mask, convert<float>, Mode, requires_arch<avx>) noexcept
        {
            return _mm256_maskload_ps(mem, _mm256_castps_si256(mask));
        }

        template <class A, class Mode>
        XSIMD_INLINE batch<double, A>
        load_masked(double const* mem, batch_bool<double, A> mask, convert<double>, Mode, requires_arch<avx>) noexcept
        {
            return _mm256_maskload_pd(mem, _mm256_castpd_si256(mask));
        }

        // 4/8-byte ints: bitcast to same-width float, reuse the vmaskmov path.
        template <class A, class T, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), batch<T, A>>
        load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return bitwise_cast<T>(batch<float, A>(_mm256_maskload_ps(reinterpret_cast<float const*>(mem), __m256i(mask))));
            }
            else
            {
                return bitwise_cast<T>(batch<double, A>(_mm256_maskload_pd(reinterpret_cast<double const*>(mem), __m256i(mask))));
            }
        }

        // load_masked (single overload for float/double)
        template <class A, class T, bool... Values, class Mode, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, Values...> mask, convert<T>, Mode, requires_arch<avx>) noexcept
        {
            constexpr size_t half_size = batch<T, A>::size / 2;
            using half_batch = make_sized_batch_t<T, half_size>;
            using half_arch = typename half_batch::arch_type;

            // exactly the lower 128-bit half: one plain load, upper lanes zero
            if constexpr (mask.prefix() == half_size)
            {
                // cross-check the plain move via countr_one/countl_zero (independent of prefix())
                assert(mask.countr_one() >= half_size && mask.countl_zero() >= half_size && "lower half fully active, upper empty");
                return detail::zero_extend_lo<A>(half_batch::load(mem, Mode {}));
            }
            // lower 128-bit half: stay in the value domain so the half kernel can
            // lower pure-prefix shapes to plain narrow moves (movss/movlps/movsd)
            else if constexpr (mask.countl_zero() >= half_size)
            {
                constexpr auto mlo = ::xsimd::detail::lower_half<half_arch>(mask);
                const auto lo = load_masked(mem, mlo, convert<T> {}, Mode {}, half_arch {});
                return detail::zero_extend_lo<A>(lo);
            }
            // prefix crossing the 128-bit boundary: plain lower half +
            // prefix-masked upper half (mirrors the store side)
            else if constexpr (mask.prefix() > half_size && mask.prefix() < batch<T, A>::size)
            {
                // the plain lower-half load reads every lower lane, so they must all be active
                assert(mask.countr_one() >= half_size && "plain lower-half load needs the lower half fully active");
                const half_batch lo = half_batch::load(mem, Mode {});
                constexpr auto mhi = ::xsimd::detail::upper_half<half_arch>(mask);
                const half_batch hi = load_masked(mem + half_size, mhi, convert<T> {}, Mode {}, half_arch {});
                return detail::merge_sse(lo.data, hi.data);
            }
            // exactly the upper 128-bit half: one plain load into the upper lanes
            else if constexpr (mask.suffix() == half_size)
            {
                assert(mask.countl_one() >= half_size && mask.countr_zero() >= half_size && "upper half fully active, lower empty");
                return detail::zero_extend<A>(half_batch::load(mem + half_size, Mode {}));
            }
            // upper 128-bit half
            else if constexpr (mask.countr_zero() >= half_size)
            {
                constexpr auto mhi = ::xsimd::detail::upper_half<half_arch>(mask);
                const auto hi = load_masked(mem + half_size, mhi, convert<T> {}, Mode {}, half_arch {});
                return detail::zero_extend<A>(hi);
            }
            else
            {
                // crossing 128-bit boundary → use 256-bit masked load
                return detail::maskload<A>(mem, mask.as_batch());
            }
        }

        // store_masked
        namespace detail
        {
            // True when batch_bool<T, A> shares the data register (__m256/__m256d) rather
            // than an EVEX k-register; the _mm256_cast*_si256 path below needs the former.
            template <class T, class A>
            inline constexpr bool uses_vector_mask_v = std::is_same_v<typename batch_bool<T, A>::register_type,
                                                                      typename batch<T, A>::register_type>;

            template <class A, class = std::enable_if_t<uses_vector_mask_v<float, A>>>
            XSIMD_INLINE void maskstore(float* mem, batch_bool<float, A> const& mask, batch<float, A> const& src) noexcept
            {
                _mm256_maskstore_ps(mem, _mm256_castps_si256(mask), src);
            }

            template <class A, class = std::enable_if_t<uses_vector_mask_v<double, A>>>
            XSIMD_INLINE void maskstore(double* mem, batch_bool<double, A> const& mask, batch<double, A> const& src) noexcept
            {
                _mm256_maskstore_pd(mem, _mm256_castpd_si256(mask), src);
            }
        }

        template <class A, class T, bool... Values, class Mode,
                  typename = std::enable_if_t<std::is_floating_point_v<T> && detail::uses_vector_mask_v<T, A>>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, Values...> mask, Mode, requires_arch<avx>) noexcept
        {
            constexpr size_t half_size = batch<T, A>::size / 2;
            using half_batch = ::xsimd::make_sized_batch_t<T, half_size>;
            using half_arch = typename half_batch::arch_type;

            // exactly the lower 128-bit half: one plain store
            if constexpr (mask.prefix() == half_size)
            {
                // a plain store writes every lower lane and no upper lane, so the mask
                // must have the lower half fully active and the upper half empty
                assert(mask.countr_one() >= half_size && mask.countl_zero() >= half_size && "lower half fully active, upper empty");
                const half_batch lo = detail::lower_half(src);
                lo.store(mem, Mode {});
            }
            // prefix crossing the 128-bit boundary: plain lower half + prefix-masked
            // upper half. Never emits vmaskmov, which does not store-forward.
            else if constexpr (mask.prefix() > half_size && mask.prefix() < batch<T, A>::size)
            {
                assert(mask.countr_one() >= half_size && "plain lower-half store needs the lower half fully active");
                const half_batch lo = detail::lower_half(src);
                lo.store(mem, Mode {});
                constexpr auto mhi = ::xsimd::detail::upper_half<half_arch>(mask);
                const half_batch hi = detail::upper_half(src);
                store_masked<half_arch>(mem + half_size, hi, mhi, Mode {}, half_arch {});
            }
            // exactly the upper 128-bit half: one plain store
            else if constexpr (mask.suffix() == half_size)
            {
                assert(mask.countl_one() >= half_size && mask.countr_zero() >= half_size && "upper half fully active, lower empty");
                const half_batch hi = detail::upper_half(src);
                hi.store(mem + half_size, Mode {});
            }
            // lower 128-bit half
            else if constexpr (mask.countl_zero() >= half_size)
            {
                constexpr auto mlo = ::xsimd::detail::lower_half<half_arch>(mask);
                const half_batch lo = detail::lower_half(src);
                store_masked<half_arch>(mem, lo, mlo, Mode {}, half_arch {});
            }
            // upper 128-bit half
            else if constexpr (mask.countr_zero() >= half_size)
            {
                constexpr auto mhi = ::xsimd::detail::upper_half<half_arch>(mask);
                const half_batch hi = detail::upper_half(src);
                store_masked<half_arch>(mem + half_size, hi, mhi, Mode {}, half_arch {});
            }
            else
            {
                detail::maskstore(mem, mask.as_batch_bool(), src);
            }
        }

        // Runtime-mask store (float/double).
        template <class A, class Mode>
        XSIMD_INLINE void
        store_masked(float* mem, batch<float, A> const& src, batch_bool<float, A> mask, Mode, requires_arch<avx>) noexcept
        {
            detail::maskstore(mem, mask, src);
        }

        template <class A, class Mode>
        XSIMD_INLINE void
        store_masked(double* mem, batch<double, A> const& src, batch_bool<double, A> mask, Mode, requires_arch<avx>) noexcept
        {
            detail::maskstore(mem, mask, src);
        }

        // 4/8-byte ints: bitcast to same-width float, reuse the vmaskmov path.
        template <class A, class T, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), void>
        store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                _mm256_maskstore_ps(reinterpret_cast<float*>(mem), __m256i(mask), bitwise_cast<float>(src));
            }
            else
            {
                _mm256_maskstore_pd(reinterpret_cast<double*>(mem), __m256i(mask), bitwise_cast<double>(src));
            }
        }

        namespace detail
        {
            // Reinterpret a constant-mask 4/8-byte load/store as same-width float
            // and run DstArch's kernel, which lowers prefix/suffix shapes to plain
            // moves. Shared by the int/EVEX 128- and 256-bit archs.
            template <class DstArch, class A, class T, bool... V, class Mode>
            XSIMD_INLINE batch<T, A> plain_move_load(T const* mem, batch_bool_constant<T, A, V...>, convert<T>, Mode) noexcept
            {
                static_assert(sizeof(T) == 4 || sizeof(T) == 8, "plain-move delegation only supports 4/8-byte lanes");
                using F = std::conditional_t<sizeof(T) == 4, float, double>;
                // same-width float has the same lane count, so the V... mask pack and the
                // memory reinterpret line up one-to-one; wrong here would load the wrong lanes
                static_assert(batch<F, A>::size == batch<T, A>::size, "same-width float must preserve lane count");
                static_assert(sizeof...(V) == batch<T, A>::size, "mask pack width must match the batch");
                // the plain-move path emits aligned moves in aligned/stream mode, which fault
                // on a misaligned pointer (the old vmaskmov tolerated it)
                assert((std::is_same_v<Mode, unaligned_mode> || ::xsimd::is_aligned<A>(mem)) && "aligned/stream masked load needs an aligned pointer");
                // qualify: an unqualified call resolves to detail::load_masked (a different
                // helper) under MSVC's two-phase lookup; we want the kernel-level overload
                return bitwise_cast<T>(batch<F, A>(::xsimd::kernel::load_masked(reinterpret_cast<F const*>(mem), batch_bool_constant<F, A, V...> {}, convert<F> {}, Mode {}, DstArch {})));
            }

            // Re-tag to DstArch so the vector-mask store kernel is used (the AVX
            // store is gated off EVEX k-register archs).
            template <class DstArch, class A, class T, bool... V, class Mode>
            XSIMD_INLINE void plain_move_store(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, V...>, Mode) noexcept
            {
                static_assert(sizeof(T) == 4 || sizeof(T) == 8, "plain-move delegation only supports 4/8-byte lanes");
                using F = std::conditional_t<sizeof(T) == 4, float, double>;
                static_assert(batch<F, A>::size == batch<T, A>::size, "same-width float must preserve lane count");
                static_assert(sizeof...(V) == batch<T, A>::size, "mask pack width must match the batch");
                assert((std::is_same_v<Mode, unaligned_mode> || ::xsimd::is_aligned<A>(mem)) && "aligned/stream masked store needs an aligned pointer");
                const auto fsrc = bitwise_cast<F>(src);
                // qualify: an unqualified call resolves to detail::store_masked (a different
                // helper) under MSVC's two-phase lookup; we want the kernel-level overload
                ::xsimd::kernel::store_masked(reinterpret_cast<F*>(mem), batch<F, DstArch>(fsrc.data), batch_bool_constant<F, DstArch, V...> {}, Mode {}, DstArch {});
            }
        }

        // lt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> lt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_ps(self, other, _CMP_LT_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> lt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_pd(self, other, _CMP_LT_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return lt(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }

        // mask
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 1 || sizeof(T) == 2)
            {
                __m128i self_low = detail::lower_half(self), self_high = detail::upper_half(self);
                return mask(batch_bool<T, sse4_2>(self_low), sse4_2 {}) | (mask(batch_bool<T, sse4_2>(self_high), sse4_2 {}) << (128 / (8 * sizeof(T))));
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_movemask_ps(_mm256_castsi256_ps(self));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_movemask_pd(_mm256_castsi256_pd(self));
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE uint64_t mask(batch_bool<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_movemask_ps(self);
        }

        template <class A>
        XSIMD_INLINE uint64_t mask(batch_bool<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_movemask_pd(self);
        }

        // max
        template <class A>
        XSIMD_INLINE batch<float, A> max(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_max_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> max(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_max_pd(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return select(self > other, self, other);
        }

        // min
        template <class A>
        XSIMD_INLINE batch<float, A> min(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_min_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> min(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_min_pd(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return select(self <= other, self, other);
        }

        // mul
        template <class A>
        XSIMD_INLINE batch<float, A> mul(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_mul_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> mul(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_mul_pd(self, other);
        }

        // nearbyint
        template <class A>
        XSIMD_INLINE batch<float, A> nearbyint(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_round_ps(self, _MM_FROUND_TO_NEAREST_INT);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> nearbyint(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_round_pd(self, _MM_FROUND_TO_NEAREST_INT);
        }

        // nearbyint_as_int
        template <class A>
        XSIMD_INLINE batch<int32_t, A> nearbyint_as_int(batch<float, A> const& self,
                                                        requires_arch<avx>) noexcept
        {
            return _mm256_cvtps_epi32(self);
        }

        // neg
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            return 0 - self;
        }
        template <class A>
        batch<float, A> neg(batch<float, A> const& self, requires_arch<avx>)
        {
            return _mm256_xor_ps(self, _mm256_castsi256_ps(_mm256_set1_epi32(0x80000000)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> neg(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_xor_pd(self, _mm256_castsi256_pd(_mm256_set1_epi64x(0x8000000000000000)));
        }

        // neq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> neq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_ps(self, other, _CMP_NEQ_UQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_cmp_pd(self, other, _CMP_NEQ_UQ);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return ~(self == other);
        }

        template <class A>
        XSIMD_INLINE batch_bool<float, A> neq(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_xor_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_xor_pd(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_castps_si256(_mm256_xor_ps(_mm256_castsi256_ps(self.data), _mm256_castsi256_ps(other.data)));
        }

        // reciprocal
        template <class A>
        XSIMD_INLINE batch<float, A> reciprocal(batch<float, A> const& self,
                                                kernel::requires_arch<avx>) noexcept
        {
            return _mm256_rcp_ps(self);
        }

        // reduce_add
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            typename batch<T, sse4_2>::register_type low = detail::lower_half(self), high = detail::upper_half(self);
            batch<T, sse4_2> blow(low), bhigh(high);
            return reduce_add(blow + bhigh);
        }

        // reduce_max
        template <class A, class T, class = std::enable_if_t<(sizeof(T) <= 2)>>
        XSIMD_INLINE T reduce_max(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            constexpr auto mask = detail::shuffle(1, 0);
            batch<T, A> step = _mm256_permute2f128_si256(self, self, mask);
            batch<T, A> acc = max(self, step);
            __m128i low = _mm256_castsi256_si128(acc);
            return reduce_max(batch<T, sse4_2>(low));
        }

        // reduce_min
        template <class A, class T, class = std::enable_if_t<(sizeof(T) <= 2)>>
        XSIMD_INLINE T reduce_min(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            constexpr auto mask = detail::shuffle(1, 0);
            batch<T, A> step = _mm256_permute2f128_si256(self, self, mask);
            batch<T, A> acc = min(self, step);
            __m128i low = _mm256_castsi256_si128(acc);
            return reduce_min(batch<T, sse4_2>(low));
        }

        // reduce_mul
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T reduce_mul(batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            typename batch<T, sse4_2>::register_type low = detail::lower_half(self), high = detail::upper_half(self);
            batch<T, sse4_2> blow(low), bhigh(high);
            return reduce_mul(blow * bhigh);
        }

        // rsqrt
        template <class A>
        XSIMD_INLINE batch<float, A> rsqrt(batch<float, A> const& val, requires_arch<avx>) noexcept
        {
            return _mm256_rsqrt_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> rsqrt(batch<double, A> const& val, requires_arch<avx>) noexcept
        {
            return _mm256_cvtps_pd(_mm_rsqrt_ps(_mm256_cvtpd_ps(val)));
        }

        // sadd
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                auto mask = (other >> (8 * sizeof(T) - 1));
                auto self_pos_branch = min(std::numeric_limits<T>::max() - other, self);
                auto self_neg_branch = max(std::numeric_limits<T>::min() - other, self);
                return other + select(batch_bool<T, A>(mask.data), self_neg_branch, self_pos_branch);
            }
            else
            {
                const auto diffmax = std::numeric_limits<T>::max() - self;
                const auto mindiff = min(diffmax, other);
                return self + mindiff;
            }
        }

        // select
        template <class A>
        XSIMD_INLINE batch<float, A> select(batch_bool<float, A> const& cond, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<avx>) noexcept
        {
            return _mm256_blendv_ps(false_br, true_br, cond);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> select(batch_bool<double, A> const& cond, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<avx>) noexcept
        {
            return _mm256_blendv_pd(false_br, true_br, cond);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx>) noexcept
        {
            __m128i cond_low = detail::lower_half(cond), cond_hi = detail::upper_half(cond);

            __m128i true_low = detail::lower_half(true_br), true_hi = detail::upper_half(true_br);

            __m128i false_low = detail::lower_half(false_br), false_hi = detail::upper_half(false_br);

            __m128i res_low = select(batch_bool<T, sse4_2>(cond_low), batch<T, sse4_2>(true_low), batch<T, sse4_2>(false_low), sse4_2 {});
            __m128i res_hi = select(batch_bool<T, sse4_2>(cond_hi), batch<T, sse4_2>(true_hi), batch<T, sse4_2>(false_hi), sse4_2 {});
            return detail::merge_sse(res_low, res_hi);
        }
        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx>) noexcept
        {
            return select(batch_bool<T, A> { Values... }, true_br, false_br, avx2 {});
        }

        template <class A, bool... Values>
        XSIMD_INLINE batch<float, A> select(batch_bool_constant<float, A, Values...> const&, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<avx>) noexcept
        {
            constexpr auto mask = batch_bool_constant<float, A, Values...>::mask();
            return _mm256_blend_ps(false_br, true_br, mask);
        }

        template <class A, bool... Values>
        XSIMD_INLINE batch<double, A> select(batch_bool_constant<double, A, Values...> const&, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<avx>) noexcept
        {
            constexpr auto mask = batch_bool_constant<double, A, Values...>::mask();
            return _mm256_blend_pd(false_br, true_br, mask);
        }

        // set
        template <class A, class... Values>
        XSIMD_INLINE batch<float, A> set(batch<float, A> const&, requires_arch<avx>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch<float, A>::size, "consistent init");
            return _mm256_setr_ps(values...);
        }

        template <class A, class... Values>
        XSIMD_INLINE batch<double, A> set(batch<double, A> const&, requires_arch<avx>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch<double, A>::size, "consistent init");
            return _mm256_setr_pd(values...);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx>, T v0, T v1, T v2, T v3) noexcept
        {
            return _mm256_set_epi64x(v3, v2, v1, v0);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7) noexcept
        {
            return _mm256_setr_epi32(v0, v1, v2, v3, v4, v5, v6, v7);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7, T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15) noexcept
        {
            return _mm256_setr_epi16(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7, T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15,
                                     T v16, T v17, T v18, T v19, T v20, T v21, T v22, T v23, T v24, T v25, T v26, T v27, T v28, T v29, T v30, T v31) noexcept
        {
            return _mm256_setr_epi8(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31);
        }

        template <class A, class T, class... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<avx>, Values... values) noexcept
        {
            return set(batch<T, A>(), A {}, static_cast<T>(values ? -1LL : 0LL)...).data;
        }

        template <class A, class... Values>
        XSIMD_INLINE batch_bool<float, A> set(batch_bool<float, A> const&, requires_arch<avx>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<float, A>::size, "consistent init");
            return _mm256_castsi256_ps(set(batch<int32_t, A>(), A {}, static_cast<int32_t>(values ? -1LL : 0LL)...).data);
        }

        template <class A, class... Values>
        XSIMD_INLINE batch_bool<double, A> set(batch_bool<double, A> const&, requires_arch<avx>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<double, A>::size, "consistent init");
            return _mm256_castsi256_pd(set(batch<int64_t, A>(), A {}, static_cast<int64_t>(values ? -1LL : 0LL)...).data);
        }

        // shuffle
        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3, ITy I4, ITy I5, ITy I6, ITy I7>
        XSIMD_INLINE batch<float, A> shuffle(batch<float, A> const& x, batch<float, A> const& y, batch_constant<ITy, A, I0, I1, I2, I3, I4, I5, I6, I7> mask, requires_arch<avx>) noexcept
        {
            constexpr uint32_t smask = detail::mod_shuffle(I0, I1, I2, I3);
            // shuffle within lane
            if (I4 == (I0 + 4) && I5 == (I1 + 4) && I6 == (I2 + 4) && I7 == (I3 + 4) && I0 < 4 && I1 < 4 && I2 >= 8 && I2 < 12 && I3 >= 8 && I3 < 12)
                return _mm256_shuffle_ps(x, y, smask);

            // shuffle within opposite lane
            if (I4 == (I0 + 4) && I5 == (I1 + 4) && I6 == (I2 + 4) && I7 == (I3 + 4) && I2 < 4 && I3 < 4 && I0 >= 8 && I0 < 12 && I1 >= 8 && I1 < 12)
                return _mm256_shuffle_ps(y, x, smask);

            return shuffle(x, y, mask, common {});
        }

        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3>
        XSIMD_INLINE batch<double, A> shuffle(batch<double, A> const& x, batch<double, A> const& y, batch_constant<ITy, A, I0, I1, I2, I3> mask, requires_arch<avx>) noexcept
        {
            constexpr uint32_t smask = (I0 & 0x1) | ((I1 & 0x1) << 1) | ((I2 & 0x1) << 2) | ((I3 & 0x1) << 3);
            // shuffle within lane
            if (I0 < 2 && I1 >= 4 && I1 < 6 && I2 >= 2 && I2 < 4 && I3 >= 6)
                return _mm256_shuffle_pd(x, y, smask);

            // shuffle within opposite lane
            if (I1 < 2 && I0 >= 4 && I0 < 6 && I3 >= 2 && I3 < 4 && I2 >= 6)
                return _mm256_shuffle_pd(y, x, smask);

            return shuffle(x, y, mask, common {});
        }

        // slide_left
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<avx>) noexcept
        {
            constexpr unsigned BitCount = N * 8;
            if (BitCount == 0)
            {
                return x;
            }
            if (BitCount >= 256)
            {
                return batch<T, A>(T(0));
            }
            if (BitCount > 128)
            {
                constexpr unsigned M = (BitCount - 128) / 8;
                __m128i low = _mm256_castsi256_si128(x);
                auto y = _mm_slli_si128(low, M);
                __m256i zero = _mm256_setzero_si256();
                return _mm256_insertf128_si256(zero, y, 1);
            }
            if (BitCount == 128)
            {
                __m128i low = _mm256_castsi256_si128(x);
                __m256i zero = _mm256_setzero_si256();
                return _mm256_insertf128_si256(zero, low, 1);
            }
            // shifting by [0, 128[ bits
            constexpr unsigned M = BitCount / 8;

            __m128i low = _mm256_castsi256_si128(x);
            auto ylow = _mm_slli_si128(low, M);
            auto zlow = _mm_srli_si128(low, 16 - M);

            __m128i high = _mm256_extractf128_si256(x, 1);
            auto yhigh = _mm_slli_si128(high, M);

            __m256i res = _mm256_castsi128_si256(ylow);
            return _mm256_insertf128_si256(res, _mm_or_si128(yhigh, zlow), 1);
        }

        // slide_right
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<avx>) noexcept
        {
            constexpr unsigned BitCount = N * 8;
            if (BitCount == 0)
            {
                return x;
            }
            if (BitCount >= 256)
            {
                return batch<T, A>(T(0));
            }
            if (BitCount > 128)
            {
                constexpr unsigned M = (BitCount - 128) / 8;
                __m128i high = _mm256_extractf128_si256(x, 1);
                __m128i y = _mm_srli_si128(high, M);
                __m256i zero = _mm256_setzero_si256();
                return _mm256_insertf128_si256(zero, y, 0);
            }
            if (BitCount == 128)
            {
                __m128i high = _mm256_extractf128_si256(x, 1);
                return _mm256_castsi128_si256(high);
            }
            // shifting by [0, 128[ bits
            constexpr unsigned M = BitCount / 8;

            __m128i low = _mm256_castsi256_si128(x);
            auto ylow = _mm_srli_si128(low, M);

            __m128i high = _mm256_extractf128_si256(x, 1);
            auto yhigh = _mm_srli_si128(high, M);
            auto zhigh = _mm_slli_si128(high, 16 - M);

            __m256i res = _mm256_castsi128_si256(_mm_or_si128(ylow, zhigh));
            return _mm256_insertf128_si256(res, yhigh, 1);
        }

        // sqrt
        template <class A>
        XSIMD_INLINE batch<float, A> sqrt(batch<float, A> const& val, requires_arch<avx>) noexcept
        {
            return _mm256_sqrt_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sqrt(batch<double, A> const& val, requires_arch<avx>) noexcept
        {
            return _mm256_sqrt_pd(val);
        }

        // ssub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            // 8 and 16 bit saturating subtraction has dedicated SSE2 instructions,
            // wider types have none, so only those take the generic sequence below.
            if constexpr (sizeof(T) <= 2)
            {
                return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                          { return ssub(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                          self, other);
            }
            else if (std::is_signed_v<T>)
            {
                auto mask = (other >> (8 * sizeof(T) - 1));
                auto self_overflow_branch = min(std::numeric_limits<T>::max() + other, self);
                auto self_underflow_branch = max(std::numeric_limits<T>::min() + other, self);
                return select(batch_bool<T, A>(mask.data), self_overflow_branch, self_underflow_branch) - other;
            }
            else
            {
                const auto diff = min(self, other);
                return self - diff;
            }
        }

        // store_aligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_store_si256((__m256i*)mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch_bool<T, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_store_si256((__m256i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_aligned(float* mem, batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_store_ps(mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_aligned(double* mem, batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_store_pd(mem, self);
        }

        // store_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_storeu_si256((__m256i*)mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch_bool<T, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_storeu_si256((__m256i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_unaligned(float* mem, batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_storeu_ps(mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_unaligned(double* mem, batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_storeu_pd(mem, self);
        }

        // store_stream
        template <class A>
        XSIMD_INLINE void store_stream(float* mem, batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            _mm256_stream_ps(mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_stream(double* mem, batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            _mm256_stream_pd(mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE void store_stream(T* mem, batch<T, A> const& self, requires_arch<avx>) noexcept
        {
            _mm256_stream_si256((__m256i*)mem, self);
        }

        // sub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            return detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                      { return sub(batch<T, sse4_2>(s), batch<T, sse4_2>(o)); },
                                      self, other);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> sub(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_sub_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sub(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            return _mm256_sub_pd(self, other);
        }

        // swizzle (dynamic mask)
        template <class A>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch<uint32_t, A> mask, requires_arch<avx>) noexcept
        {
            // swap lanes
            __m256 swapped = _mm256_permute2f128_ps(self, self, 0x01); // [high | low]

            // normalize mask taking modulo 4
            batch<uint32_t, A> half_mask = mask & 0b11u;

            // permute within each lane
            __m256 r0 = _mm256_permutevar_ps(self, half_mask);
            __m256 r1 = _mm256_permutevar_ps(swapped, half_mask);

            // select lane by the mask index divided by 4
            constexpr auto lane = batch_constant<uint32_t, A, 0, 0, 0, 0, 4, 4, 4, 4> {};
            batch_bool<uint32_t, A> blend_mask = (mask & 0b100u) != lane;
            return _mm256_blendv_ps(r0, r1, batch_bool_cast<float>(blend_mask));
        }

        template <class A>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch<uint64_t, A> mask, requires_arch<avx>) noexcept
        {
            // swap lanes
            __m256d swapped = _mm256_permute2f128_pd(self, self, 0x01); // [high | low]

            // The half mask value is found in mask modulo 2, but the intrinsic expect it in the
            // second least significant bit. We use negative as a cheap alternative to lshift.
            batch<uint64_t, A> half_mask = -(mask & 0b1u);

            // permute within each lane
            __m256d r0 = _mm256_permutevar_pd(self, half_mask);
            __m256d r1 = _mm256_permutevar_pd(swapped, half_mask);

            // select lane by the mask index divided by 2
            constexpr auto lane = batch_constant<uint64_t, A, 0, 0, 2, 2> {};
            batch_bool<uint64_t, A> blend_mask = (mask & 0b10u) != lane;
            return _mm256_blendv_pd(r0, r1, batch_bool_cast<double>(blend_mask));
        }

        template <class A, typename T, detail::enable_sized_integral_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch<uint32_t, A> const& mask, requires_arch<avx>) noexcept
        {
            return bitwise_cast<T>(
                swizzle(bitwise_cast<float>(self), mask));
        }

        template <class A, typename T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A>
        swizzle(batch<T, A> const& self, batch<uint64_t, A> const& mask, requires_arch<avx>) noexcept
        {
            return bitwise_cast<T>(
                swizzle(bitwise_cast<double>(self), mask));
        }

        // swizzle (constant mask)
        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3, uint32_t V4, uint32_t V5, uint32_t V6, uint32_t V7>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<avx>) noexcept
        {
            constexpr bool is_identity = detail::is_identity(mask);
            constexpr bool is_dup_low = detail::is_dup_lo(mask);
            constexpr bool is_dup_hi = detail::is_dup_hi(mask);
            constexpr bool is_dup = is_dup_low || is_dup_hi;
            if constexpr (is_identity)
            {
                return self;
            }
            if constexpr (is_dup)
            {
                constexpr auto control = is_dup_low ? 0x00 : 0x11;
                constexpr auto is_dup_identity = is_dup_low ? detail::is_identity<uint32_t, V0, V1, V2, V3>() : detail::is_identity<int64_t, V4 - 4, V5 - 4, V6 - 4, V7 - 4>();
                auto split = _mm256_permute2f128_ps(self, self, control);
                if constexpr (!is_dup_identity)
                {
                    constexpr auto shuffle_mask = is_dup_low ? detail::mod_shuffle(V0, V1, V2, V3) : detail::mod_shuffle(V4 - 4, V5 - 4, V6 - 4, V7 - 4);
                    split = _mm256_permute_ps(split, shuffle_mask);
                }
                return split;
            }
            constexpr auto lane_mask = mask % std::integral_constant<uint32_t, (mask.size / 2)>();
            if constexpr (detail::is_only_from_lo(mask))
            {
                __m256 broadcast = _mm256_permute2f128_ps(self, self, 0x00); // [low | low]
                return _mm256_permutevar_ps(broadcast, lane_mask.as_batch());
            }
            if constexpr (detail::is_only_from_hi(mask))
            {
                __m256 broadcast = _mm256_permute2f128_ps(self, self, 0x11); // [high | high]
                return _mm256_permutevar_ps(broadcast, lane_mask.as_batch());
            }

            // Fallback to general algorithm. This is the same as the dynamic version with the exception
            // that possible operations are done at compile time.

            // swap lanes
            __m256 swapped = _mm256_permute2f128_ps(self, self, 0x01); // [high | low]

            // normalize mask taking modulo 4
            constexpr auto half_mask = mask % std::integral_constant<uint32_t, 4>();

            // permute within each lane
            __m256 r0 = _mm256_permutevar_ps(self, half_mask.as_batch());
            __m256 r1 = _mm256_permutevar_ps(swapped, half_mask.as_batch());

            // select lane by the mask index divided by 4
            constexpr auto lane = batch_constant<uint32_t, A, 0, 0, 0, 0, 1, 1, 1, 1> {};
            constexpr int lane_idx = ((mask / std::integral_constant<uint32_t, 4>()) != lane).mask();

            return _mm256_blend_ps(r0, r1, lane_idx);
        }

        template <class A, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch_constant<uint64_t, A, V0, V1, V2, V3> mask, requires_arch<avx>) noexcept
        {
            // cannot use detail::mod_shuffle as the mod and shift are different in this case
            constexpr auto imm = ((V0 % 2) << 0) | ((V1 % 2) << 1) | ((V2 % 2) << 2) | ((V3 % 2) << 3);
            if constexpr (detail::is_identity(mask))
            {
                return self;
            }
            if constexpr (!detail::is_cross_lane(mask))
            {
                return _mm256_permute_pd(self, imm);
            }
            if constexpr (detail::is_only_from_lo(mask))
            {
                __m256d broadcast = _mm256_permute2f128_pd(self, self, 0x00); // [low | low]
                return _mm256_permute_pd(broadcast, imm);
            }
            if constexpr (detail::is_only_from_hi(mask))
            {
                __m256d broadcast = _mm256_permute2f128_pd(self, self, 0x11); // [high | high]
                return _mm256_permute_pd(broadcast, imm);
            }

            // Fallback to general algorithm. This is the same as the dynamic version with the exception
            // that possible operations are done at compile time.

            // swap lanes
            __m256d swapped = _mm256_permute2f128_pd(self, self, 0x01); // [high | low]

            // permute within each lane
            __m256d r0 = _mm256_permute_pd(self, imm);
            __m256d r1 = _mm256_permute_pd(swapped, imm);

            // select lane by the mask index divided by 2
            constexpr auto lane = batch_constant<uint64_t, A, 0, 0, 1, 1> {};
            constexpr int lane_idx = ((mask / std::integral_constant<uint64_t, 2>()) != lane).mask();

            // blend the two permutes
            return _mm256_blend_pd(r0, r1, lane_idx);
        }

        template <
            class A, typename T,
            uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3, uint32_t V4, uint32_t V5, uint32_t V6, uint32_t V7,
            detail::enable_sized_integral_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> swizzle(
            batch<T, A> const& self,
            batch_constant<uint32_t, A, V0, V1, V2, V3, V4, V5, V6, V7> const& mask,
            requires_arch<avx>) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<float>(self), mask));
        }

        template <class A, typename T, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint64_t, A, V0, V1, V2, V3> const& mask, requires_arch<avx>) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<double>(self), mask));
        }

        // transpose
        template <class A>
        XSIMD_INLINE void transpose(batch<float, A>* matrix_begin, batch<float, A>* matrix_end, requires_arch<avx>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<float, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            // See
            // https://stackoverflow.com/questions/25622745/transpose-an-8x8-float-using-avx-avx2
            auto r0 = matrix_begin[0], r1 = matrix_begin[1],
                 r2 = matrix_begin[2], r3 = matrix_begin[3],
                 r4 = matrix_begin[4], r5 = matrix_begin[5],
                 r6 = matrix_begin[6], r7 = matrix_begin[7];

            auto t0 = _mm256_unpacklo_ps(r0, r1);
            auto t1 = _mm256_unpackhi_ps(r0, r1);
            auto t2 = _mm256_unpacklo_ps(r2, r3);
            auto t3 = _mm256_unpackhi_ps(r2, r3);
            auto t4 = _mm256_unpacklo_ps(r4, r5);
            auto t5 = _mm256_unpackhi_ps(r4, r5);
            auto t6 = _mm256_unpacklo_ps(r6, r7);
            auto t7 = _mm256_unpackhi_ps(r6, r7);

            r0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
            r1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
            r2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
            r3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
            r4 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
            r5 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
            r6 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
            r7 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

            matrix_begin[0] = _mm256_permute2f128_ps(r0, r4, 0x20);
            matrix_begin[1] = _mm256_permute2f128_ps(r1, r5, 0x20);
            matrix_begin[2] = _mm256_permute2f128_ps(r2, r6, 0x20);
            matrix_begin[3] = _mm256_permute2f128_ps(r3, r7, 0x20);
            matrix_begin[4] = _mm256_permute2f128_ps(r0, r4, 0x31);
            matrix_begin[5] = _mm256_permute2f128_ps(r1, r5, 0x31);
            matrix_begin[6] = _mm256_permute2f128_ps(r2, r6, 0x31);
            matrix_begin[7] = _mm256_permute2f128_ps(r3, r7, 0x31);
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint32_t, A>* matrix_begin, batch<uint32_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            return transpose(reinterpret_cast<batch<float, A>*>(matrix_begin), reinterpret_cast<batch<float, A>*>(matrix_end), A {});
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int32_t, A>* matrix_begin, batch<int32_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            return transpose(reinterpret_cast<batch<float, A>*>(matrix_begin), reinterpret_cast<batch<float, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<double, A>* matrix_begin, batch<double, A>* matrix_end, requires_arch<avx>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<double, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1],
                 r2 = matrix_begin[2], r3 = matrix_begin[3];

            auto t0 = _mm256_unpacklo_pd(r0, r1); // r00 r10 r01 r11
            auto t1 = _mm256_unpackhi_pd(r0, r1); // r02 r12 r03 r13
            auto t2 = _mm256_unpacklo_pd(r2, r3); // r20 r30 r21 r31
            auto t3 = _mm256_unpackhi_pd(r2, r3); // r22 r32 r23 r33

            matrix_begin[0] = _mm256_permute2f128_pd(t0, t2, 0x20);
            matrix_begin[1] = _mm256_permute2f128_pd(t1, t3, 0x20);
            matrix_begin[2] = _mm256_permute2f128_pd(t0, t2, 0x31);
            matrix_begin[3] = _mm256_permute2f128_pd(t1, t3, 0x31);
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint64_t, A>* matrix_begin, batch<uint64_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            return transpose(reinterpret_cast<batch<double, A>*>(matrix_begin), reinterpret_cast<batch<double, A>*>(matrix_end), A {});
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int64_t, A>* matrix_begin, batch<int64_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            return transpose(reinterpret_cast<batch<double, A>*>(matrix_begin), reinterpret_cast<batch<double, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint16_t, A>* matrix_begin, batch<uint16_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<uint16_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            batch<uint16_t, sse4_2> tmp_lo0[8];
            for (int i = 0; i < 8; ++i)
                tmp_lo0[i] = _mm256_castsi256_si128(matrix_begin[i]);
            transpose(tmp_lo0 + 0, tmp_lo0 + 8, sse4_2 {});

            batch<uint16_t, sse4_2> tmp_hi0[8];
            for (int i = 0; i < 8; ++i)
                tmp_hi0[i] = _mm256_castsi256_si128(matrix_begin[8 + i]);
            transpose(tmp_hi0 + 0, tmp_hi0 + 8, sse4_2 {});

            batch<uint16_t, sse4_2> tmp_lo1[8];
            for (int i = 0; i < 8; ++i)
                tmp_lo1[i] = _mm256_extractf128_si256(matrix_begin[i], 1);
            transpose(tmp_lo1 + 0, tmp_lo1 + 8, sse4_2 {});

            batch<uint16_t, sse4_2> tmp_hi1[8];
            for (int i = 0; i < 8; ++i)
                tmp_hi1[i] = _mm256_extractf128_si256(matrix_begin[8 + i], 1);
            transpose(tmp_hi1 + 0, tmp_hi1 + 8, sse4_2 {});

            for (int i = 0; i < 8; ++i)
                matrix_begin[i] = detail::merge_sse(tmp_lo0[i], tmp_hi0[i]);
            for (int i = 0; i < 8; ++i)
                matrix_begin[i + 8] = detail::merge_sse(tmp_lo1[i], tmp_hi1[i]);
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int16_t, A>* matrix_begin, batch<int16_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            return transpose(reinterpret_cast<batch<uint16_t, A>*>(matrix_begin), reinterpret_cast<batch<uint16_t, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint8_t, A>* matrix_begin, batch<uint8_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<uint8_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            batch<uint8_t, sse4_2> tmp_lo0[16];
            for (int i = 0; i < 16; ++i)
                tmp_lo0[i] = _mm256_castsi256_si128(matrix_begin[i]);
            transpose(tmp_lo0 + 0, tmp_lo0 + 16, sse4_2 {});

            batch<uint8_t, sse4_2> tmp_hi0[16];
            for (int i = 0; i < 16; ++i)
                tmp_hi0[i] = _mm256_castsi256_si128(matrix_begin[16 + i]);
            transpose(tmp_hi0 + 0, tmp_hi0 + 16, sse4_2 {});

            batch<uint8_t, sse4_2> tmp_lo1[16];
            for (int i = 0; i < 16; ++i)
                tmp_lo1[i] = _mm256_extractf128_si256(matrix_begin[i], 1);
            transpose(tmp_lo1 + 0, tmp_lo1 + 16, sse4_2 {});

            batch<uint8_t, sse4_2> tmp_hi1[16];
            for (int i = 0; i < 16; ++i)
                tmp_hi1[i] = _mm256_extractf128_si256(matrix_begin[16 + i], 1);
            transpose(tmp_hi1 + 0, tmp_hi1 + 16, sse4_2 {});

            for (int i = 0; i < 16; ++i)
                matrix_begin[i] = detail::merge_sse(tmp_lo0[i], tmp_hi0[i]);
            for (int i = 0; i < 16; ++i)
                matrix_begin[i + 16] = detail::merge_sse(tmp_lo1[i], tmp_hi1[i]);
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int8_t, A>* matrix_begin, batch<int8_t, A>* matrix_end, requires_arch<avx>) noexcept
        {
            return transpose(reinterpret_cast<batch<uint8_t, A>*>(matrix_begin), reinterpret_cast<batch<uint8_t, A>*>(matrix_end), A {});
        }

        // trunc
        template <class A>
        XSIMD_INLINE batch<float, A> trunc(batch<float, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_round_ps(self, _MM_FROUND_TO_ZERO);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> trunc(batch<double, A> const& self, requires_arch<avx>) noexcept
        {
            return _mm256_round_pd(self, _MM_FROUND_TO_ZERO);
        }

        // zip_hi
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 1 || sizeof(T) == 2)
            {
                // extract high word
                __m128i self_hi = _mm256_extractf128_si256(self, 1);
                __m128i other_hi = _mm256_extractf128_si256(other, 1);

                // interleave
                __m128i res_lo, res_hi;
                if constexpr (sizeof(T) == 1)
                {
                    res_lo = _mm_unpacklo_epi8(self_hi, other_hi);
                    res_hi = _mm_unpackhi_epi8(self_hi, other_hi);
                }
                else
                {
                    res_lo = _mm_unpacklo_epi16(self_hi, other_hi);
                    res_hi = _mm_unpackhi_epi16(self_hi, other_hi);
                }

                // fuse
                return _mm256_castps_si256(
                    _mm256_insertf128_ps(
                        _mm256_castsi256_ps(_mm256_castsi128_si256(res_lo)),
                        _mm_castsi128_ps(res_hi),
                        1));
            }
            else if constexpr (sizeof(T) == 4)
            {
                auto lo = _mm256_unpacklo_ps(_mm256_castsi256_ps(self), _mm256_castsi256_ps(other));
                auto hi = _mm256_unpackhi_ps(_mm256_castsi256_ps(self), _mm256_castsi256_ps(other));
                return _mm256_castps_si256(_mm256_permute2f128_ps(lo, hi, 0x31));
            }
            else if constexpr (sizeof(T) == 8)
            {
                auto lo = _mm256_unpacklo_pd(_mm256_castsi256_pd(self), _mm256_castsi256_pd(other));
                auto hi = _mm256_unpackhi_pd(_mm256_castsi256_pd(self), _mm256_castsi256_pd(other));
                return _mm256_castpd_si256(_mm256_permute2f128_pd(lo, hi, 0x31));
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A> zip_hi(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            auto lo = _mm256_unpacklo_ps(self, other);
            auto hi = _mm256_unpackhi_ps(self, other);
            return _mm256_permute2f128_ps(lo, hi, 0x31);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> zip_hi(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            auto lo = _mm256_unpacklo_pd(self, other);
            auto hi = _mm256_unpackhi_pd(self, other);
            return _mm256_permute2f128_pd(lo, hi, 0x31);
        }

        // zip_lo
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx>) noexcept
        {
            if constexpr (sizeof(T) == 1 || sizeof(T) == 2)
            {
                // extract low word
                __m128i self_lo = _mm256_extractf128_si256(self, 0);
                __m128i other_lo = _mm256_extractf128_si256(other, 0);

                // interleave
                __m128i res_lo, res_hi;
                if constexpr (sizeof(T) == 1)
                {
                    res_lo = _mm_unpacklo_epi8(self_lo, other_lo);
                    res_hi = _mm_unpackhi_epi8(self_lo, other_lo);
                }
                else
                {
                    res_lo = _mm_unpacklo_epi16(self_lo, other_lo);
                    res_hi = _mm_unpackhi_epi16(self_lo, other_lo);
                }

                // fuse
                return _mm256_castps_si256(
                    _mm256_insertf128_ps(
                        _mm256_castsi256_ps(_mm256_castsi128_si256(res_lo)),
                        _mm_castsi128_ps(res_hi),
                        1));
            }
            else if constexpr (sizeof(T) == 4)
            {
                auto lo = _mm256_unpacklo_ps(_mm256_castsi256_ps(self), _mm256_castsi256_ps(other));
                auto hi = _mm256_unpackhi_ps(_mm256_castsi256_ps(self), _mm256_castsi256_ps(other));
                return _mm256_castps_si256(_mm256_insertf128_ps(lo, _mm256_castps256_ps128(hi), 1));
            }
            else if constexpr (sizeof(T) == 8)
            {
                auto lo = _mm256_unpacklo_pd(_mm256_castsi256_pd(self), _mm256_castsi256_pd(other));
                auto hi = _mm256_unpackhi_pd(_mm256_castsi256_pd(self), _mm256_castsi256_pd(other));
                return _mm256_castpd_si256(_mm256_insertf128_pd(lo, _mm256_castpd256_pd128(hi), 1));
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        template <class A>
        XSIMD_INLINE batch<float, A> zip_lo(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx>) noexcept
        {
            auto lo = _mm256_unpacklo_ps(self, other);
            auto hi = _mm256_unpackhi_ps(self, other);
            return _mm256_insertf128_ps(lo, _mm256_castps256_ps128(hi), 1);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> zip_lo(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx>) noexcept
        {
            auto lo = _mm256_unpacklo_pd(self, other);
            auto hi = _mm256_unpackhi_pd(self, other);
            return _mm256_insertf128_pd(lo, _mm256_castpd256_pd128(hi), 1);
        }

        // widen
        template <class A, class T>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<avx>) noexcept
        {
            auto pair_lo = widen(batch<T, sse4_2>(detail::lower_half(x)), sse4_2 {});
            auto pair_hi = widen(batch<T, sse4_2>(detail::upper_half(x)), sse4_2 {});
            return { detail::merge_sse(pair_lo[0], pair_lo[1]), detail::merge_sse(pair_hi[0], pair_hi[1]) };
        }
        template <class A>
        XSIMD_INLINE std::array<batch<double, A>, 2> widen(batch<float, A> const& x, requires_arch<avx>) noexcept
        {
            __m256d lo = _mm256_cvtps_pd(detail::lower_half(x));
            __m256d hi = _mm256_cvtps_pd(detail::upper_half(x));
            return { lo, hi };
        }

    }
}

#endif
