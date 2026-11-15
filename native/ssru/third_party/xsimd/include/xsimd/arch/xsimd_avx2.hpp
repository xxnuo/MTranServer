/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_AVX2_HPP
#define XSIMD_AVX2_HPP

#include "../types/xsimd_avx2_register.hpp"
#include "../types/xsimd_batch_constant.hpp"
#include "./utils/shifts.hpp"

#include <complex>
#include <limits>
#include <type_traits>

namespace xsimd
{

    namespace kernel
    {
        using namespace types;

        // abs
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& self, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_abs_epi8(self);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_abs_epi16(self);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_abs_epi32(self);
                }
                else
                {
                    return abs(self, avx {});
                }
            }
            return self;
        }

        // add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_add_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_add_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_add_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_add_epi64(self, other);
            }
            else
            {
                return add(self, other, avx {});
            }
        }

        // avgr
        template <class A, class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_avg_epu8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_avg_epu16(self, other);
            }
            else
            {
                return avgr(self, other, common {});
            }
        }

        // avg
        template <class A, class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                auto adj = ((self ^ other) << 7) >> 7;
                return avgr(self, other, A {}) - adj;
            }
            else if constexpr (sizeof(T) == 2)
            {
                auto adj = ((self ^ other) << 15) >> 15;
                return avgr(self, other, A {}) - adj;
            }
            else
            {
                return avg(self, other, common {});
            }
        }

        // load_masked / store_masked: AVX2 has _mm256_maskload/maskstore_epi{32,64};
        // 8/16-bit integers fall back to the common scalar path.
        namespace detail
        {
            template <class T>
            XSIMD_INLINE __m256i maskload(T const* mem, __m256i mask) noexcept
            {
                if constexpr (sizeof(T) == 4)
                {
                    static_assert(sizeof(int) == 4, "_mm256_maskload_epi32 requires a 4-byte int");
                    return _mm256_maskload_epi32(reinterpret_cast<int const*>(mem), mask);
                }
                else
                {
                    static_assert(sizeof(long long) == 8, "_mm256_maskload_epi64 requires an 8-byte long long");
                    return _mm256_maskload_epi64(reinterpret_cast<long long const*>(mem), mask);
                }
            }

            template <class T>
            XSIMD_INLINE void maskstore(T* mem, __m256i mask, __m256i src) noexcept
            {
                if constexpr (sizeof(T) == 4)
                {
                    static_assert(sizeof(int) == 4, "_mm256_maskstore_epi32 requires a 4-byte int");
                    _mm256_maskstore_epi32(reinterpret_cast<int*>(mem), mask, src);
                }
                else
                {
                    static_assert(sizeof(long long) == 8, "_mm256_maskstore_epi64 requires an 8-byte long long");
                    _mm256_maskstore_epi64(reinterpret_cast<long long*>(mem), mask, src);
                }
            }
        }

        // Constant masks: prefix/suffix shapes lower to plain moves.
        template <class A, class T, bool... Values, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), batch<T, A>>
        load_masked(T const* mem, batch_bool_constant<T, A, Values...> mask, convert<T>, Mode, requires_arch<avx2>) noexcept
        {
            return detail::plain_move_load<avx>(mem, mask, convert<T> {}, Mode {});
        }

        template <class A, class T, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), batch<T, A>>
        load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx2>) noexcept
        {
            return detail::maskload(mem, __m256i(mask));
        }

        template <class A, class T, bool... Values, class Mode,
                  typename = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, Values...> mask, Mode, requires_arch<avx2>) noexcept
        {
            detail::plain_move_store<avx>(mem, src, mask, Mode {});
        }

        template <class A, class T, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), void>
        store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx2>) noexcept
        {
            detail::maskstore(mem, __m256i(mask), __m256i(src));
        }

        // load_stream
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE batch<T, A> load_stream(T const* mem, convert<T>, requires_arch<avx2>) noexcept
        {
            return _mm256_stream_load_si256((__m256i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_stream(float const* mem, convert<float>, requires_arch<avx2>) noexcept
        {
            return _mm256_castsi256_ps(_mm256_stream_load_si256((__m256i const*)mem));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_stream(double const* mem, convert<double>, requires_arch<avx2>) noexcept
        {
            return _mm256_castsi256_pd(_mm256_stream_load_si256((__m256i const*)mem));
        }

        // bitwise_and
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_and_si256(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_and_si256(self, other);
        }

        // bitwise_andnot
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_andnot_si256(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_andnot_si256(other, self);
        }

        // bitwise_not
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& self, requires_arch<avx2>) noexcept
        {
            return _mm256_xor_si256(self, _mm256_set1_epi32(-1));
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<avx2>) noexcept
        {
            return _mm256_xor_si256(self, _mm256_set1_epi32(-1));
        }

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 2)
            {
                return _mm256_slli_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_slli_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_slli_epi64(self, other);
            }
            else
            {
                return bitwise_lshift(self, other, avx {});
            }
        }

        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, requires_arch<avx2>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits, "Shift must be less than the number of bits in T");
            if constexpr (sizeof(T) == 1)
            {
                // 8-bit left shift via 16-bit shift + mask
                __m256i shifted = _mm256_slli_epi16(self, shift);
                // TODO(C++17): without `if constexpr ` we must ensure the compile-time shift does not overflow
                constexpr uint8_t mask8 = static_cast<uint8_t>(sizeof(T) == 1 ? (~0u << shift) : 0);
                const __m256i mask = _mm256_set1_epi8(mask8);
                return _mm256_and_si256(shifted, mask);
            }
            if constexpr (sizeof(T) == 2)
            {
                return _mm256_slli_epi16(self, shift);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_slli_epi32(self, shift);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_slli_epi64(self, shift);
            }
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm256_sllv_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_sllv_epi64(self, other);
            }
            else
            {
                return bitwise_lshift(self, other, avx {});
            }
        }

        // bitwise_lshift multiple (constant) specific implementations.
        // Missing implementations are dispatched to the `batch` overload in xsimd_api.
        // The 1 byte constant implementation calls the 2 bytes constant version, the 2 bytes
        // constant version calls into the 4 bytes version which resolves to the dynamic one above.
        template <class A, class T, T... Vs,
                  std::enable_if_t<std::is_integral_v<T> && (sizeof(T) <= 2), int> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(
            batch<T, A> const& self, batch_constant<T, A, Vs...> shifts, requires_arch<avx2> req) noexcept
        {
            using uint_t = std::make_unsigned_t<T>;

            // AVX2 only supports 16-bit shifts with a uniform bitshift value,
            // otherwise emulate using 32-bit shifts.
            if constexpr (utils::all_equals(shifts))
            {
                return bitwise_lshift<shifts.get(0), A>(self, req);
            }
            return bitwise_cast<T>(
                utils::bitwise_lshift_as_twice_larger<uint_t>(
                    bitwise_cast<uint_t>(self),
                    batch_constant<uint_t, A, static_cast<uint_t>(Vs)...> {}));
        }

        // bitwise_or
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_or_si256(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_or_si256(self, other);
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    __m256i sign_mask = _mm256_set1_epi16((0xFF00 >> other) & 0x00FF);
                    __m256i cmp_is_negative = _mm256_cmpgt_epi8(_mm256_setzero_si256(), self);
                    __m256i res = _mm256_srai_epi16(self, other);
                    return _mm256_or_si256(
                        detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                           { return bitwise_and(batch<T, sse4_2>(s), batch<T, sse4_2>(o), sse4_2 {}); },
                                           sign_mask, cmp_is_negative),
                        _mm256_andnot_si256(sign_mask, res));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_srai_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_srai_epi32(self, other);
                }
                else
                {
                    return bitwise_rshift(self, other, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 2)
                {
                    return _mm256_srli_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_srli_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm256_srli_epi64(self, other);
                }
                else
                {
                    return bitwise_rshift(self, other, avx {});
                }
            }
        }

        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, requires_arch<avx2>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits, "Shift amount must be less than the number of bits in T");
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    __m256i sign_mask = _mm256_set1_epi16((0xFF00 >> shift) & 0x00FF);
                    __m256i cmp_is_negative = _mm256_cmpgt_epi8(_mm256_setzero_si256(), self);
                    __m256i res = _mm256_srai_epi16(self, shift);
                    return _mm256_or_si256(
                        detail::fwd_to_sse([](__m128i s, __m128i o) noexcept
                                           { return bitwise_and(batch<T, sse4_2>(s), batch<T, sse4_2>(o), sse4_2 {}); },
                                           sign_mask, cmp_is_negative),
                        _mm256_andnot_si256(sign_mask, res));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_srai_epi16(self, shift);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_srai_epi32(self, shift);
                }
                else
                {
                    return bitwise_rshift<shift>(self, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    // 8-bit left shift via 16-bit shift + mask
                    const __m256i shifted = _mm256_srli_epi16(self, shift);
                    // TODO(C++17): without `if constexpr ` we must ensure the compile-time shift does not overflow
                    constexpr uint8_t mask8 = static_cast<uint8_t>(sizeof(T) == 1 ? ((1u << shift) - 1u) : 0);
                    const __m256i mask = _mm256_set1_epi8(mask8);
                    return _mm256_and_si256(shifted, mask);
                }
                if constexpr (sizeof(T) == 2)
                {
                    return _mm256_srli_epi16(self, shift);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_srli_epi32(self, shift);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm256_srli_epi64(self, shift);
                }
            }
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm256_srav_epi32(self, other);
                }
                else
                {
                    return bitwise_rshift(self, other, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm256_srlv_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm256_srlv_epi64(self, other);
                }
                else
                {
                    return bitwise_rshift(self, other, avx {});
                }
            }
        }

        // bitwise_xor
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_xor_si256(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_xor_si256(self, other);
        }

        // complex_low
        template <class A>
        XSIMD_INLINE batch<double, A> complex_low(batch<std::complex<double>, A> const& self, requires_arch<avx2>) noexcept
        {
            __m256d tmp0 = _mm256_permute4x64_pd(self.real(), _MM_SHUFFLE(3, 1, 1, 0));
            __m256d tmp1 = _mm256_permute4x64_pd(self.imag(), _MM_SHUFFLE(1, 2, 0, 0));
            return _mm256_blend_pd(tmp0, tmp1, 10);
        }

        // complex_high
        template <class A>
        XSIMD_INLINE batch<double, A> complex_high(batch<std::complex<double>, A> const& self, requires_arch<avx2>) noexcept
        {
            __m256d tmp0 = _mm256_permute4x64_pd(self.real(), _MM_SHUFFLE(3, 3, 1, 2));
            __m256d tmp1 = _mm256_permute4x64_pd(self.imag(), _MM_SHUFFLE(3, 2, 2, 0));
            return _mm256_blend_pd(tmp0, tmp1, 10);
        }

        // fast_cast
        namespace detail
        {

            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<uint64_t, A> const& x, batch<double, A> const&, requires_arch<avx2>) noexcept
            {
                // from https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
                // adapted to avx
                __m256i xH = _mm256_srli_epi64(x, 32);
                xH = _mm256_or_si256(xH, _mm256_castpd_si256(_mm256_set1_pd(19342813113834066795298816.))); //  2^84
                __m256i mask = _mm256_setr_epi16(0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000,
                                                 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000);
                __m256i xL = _mm256_or_si256(_mm256_and_si256(mask, x), _mm256_andnot_si256(mask, _mm256_castpd_si256(_mm256_set1_pd(0x0010000000000000)))); //  2^52
                __m256d f = _mm256_sub_pd(_mm256_castsi256_pd(xH), _mm256_set1_pd(19342813118337666422669312.)); //  2^84 + 2^52
                detail::reassociation_barrier(f, "prevent (xH-C)+xL -> xH+(xL-C)");
                return _mm256_add_pd(f, _mm256_castsi256_pd(xL));
            }

            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<int64_t, A> const& x, batch<double, A> const&, requires_arch<avx2>) noexcept
            {
                // from https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
                // adapted to avx
                __m256i xH = _mm256_srai_epi32(x, 16);
                xH = _mm256_and_si256(xH, _mm256_setr_epi16(0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF));
                xH = _mm256_add_epi64(xH, _mm256_castpd_si256(_mm256_set1_pd(442721857769029238784.))); //  3*2^67
                __m256i mask = _mm256_setr_epi16(0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
                                                 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000);
                __m256i xL = _mm256_or_si256(_mm256_and_si256(mask, x), _mm256_andnot_si256(mask, _mm256_castpd_si256(_mm256_set1_pd(0x0010000000000000)))); //  2^52
                __m256d f = _mm256_sub_pd(_mm256_castsi256_pd(xH), _mm256_set1_pd(442726361368656609280.)); //  3*2^67 + 2^52
                detail::reassociation_barrier(f, "prevent (xH-C)+xL -> xH+(xL-C)");
                return _mm256_add_pd(f, _mm256_castsi256_pd(xL));
            }
        }

        // eq
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_cmpeq_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_cmpeq_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_cmpeq_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_cmpeq_epi64(self, other);
            }
            else
            {
                return eq(self, other, avx {});
            }
        }

        // gather
        template <class T, class A, class U, detail::enable_sized_integral_t<T, 4> = 0, detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx2>) noexcept
        {
            // scatter for this one is AVX512F+AVX512VL
            return _mm256_i32gather_epi32(reinterpret_cast<const int*>(src), index, sizeof(T));
        }

        template <class T, class A, class U, detail::enable_sized_integral_t<T, 8> = 0, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx2>) noexcept
        {
            // scatter for this one is AVX512F+AVX512VL
            return _mm256_i64gather_epi64(reinterpret_cast<const long long int*>(src), index, sizeof(T));
        }

        template <class A, class U,
                  detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<float, A> gather(batch<float, A> const&, float const* src,
                                            batch<U, A> const& index,
                                            kernel::requires_arch<avx2>) noexcept
        {
            // scatter for this one is AVX512F+AVX512VL
            return _mm256_i32gather_ps(src, index, sizeof(float));
        }

        template <class A, class U, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<double, A> gather(batch<double, A> const&, double const* src,
                                             batch<U, A> const& index,
                                             requires_arch<avx2>) noexcept
        {
            // scatter for this one is AVX512F+AVX512VL
            return _mm256_i64gather_pd(src, index, sizeof(double));
        }

        // gather: handmade conversions
        template <class A, class V, detail::enable_sized_integral_t<V, 4> = 0>
        XSIMD_INLINE batch<float, A> gather(batch<float, A> const&, double const* src,
                                            batch<V, A> const& index,
                                            requires_arch<avx2>) noexcept
        {
            const batch<double, A> low(_mm256_i32gather_pd(src, _mm256_castsi256_si128(index.data), sizeof(double)));
            const batch<double, A> high(_mm256_i32gather_pd(src, _mm256_extractf128_si256(index.data, 1), sizeof(double)));
            return detail::merge_sse(_mm256_cvtpd_ps(low.data), _mm256_cvtpd_ps(high.data));
        }

        template <class A, class V, detail::enable_sized_integral_t<V, 4> = 0>
        XSIMD_INLINE batch<int32_t, A> gather(batch<int32_t, A> const&, double const* src,
                                              batch<V, A> const& index,
                                              requires_arch<avx2>) noexcept
        {
            const batch<double, A> low(_mm256_i32gather_pd(src, _mm256_castsi256_si128(index.data), sizeof(double)));
            const batch<double, A> high(_mm256_i32gather_pd(src, _mm256_extractf128_si256(index.data, 1), sizeof(double)));
            return detail::merge_sse(_mm256_cvtpd_epi32(low.data), _mm256_cvtpd_epi32(high.data));
        }

        // lt
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_cmpgt_epi8(other, self);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_cmpgt_epi16(other, self);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_cmpgt_epi32(other, self);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm256_cmpgt_epi64(other, self);
                }
                else
                {
                    return lt(self, other, avx {});
                }
            }
            else
            {
                return lt(self, other, avx {});
            }
        }

        // load_complex
        template <class A>
        XSIMD_INLINE batch<std::complex<float>, A> load_complex(batch<float, A> const& hi, batch<float, A> const& lo, requires_arch<avx2>) noexcept
        {
            using batch_type = batch<float, A>;
            batch_type real = _mm256_castpd_ps(
                _mm256_permute4x64_pd(
                    _mm256_castps_pd(_mm256_shuffle_ps(hi, lo, _MM_SHUFFLE(2, 0, 2, 0))),
                    _MM_SHUFFLE(3, 1, 2, 0)));
            batch_type imag = _mm256_castpd_ps(
                _mm256_permute4x64_pd(
                    _mm256_castps_pd(_mm256_shuffle_ps(hi, lo, _MM_SHUFFLE(3, 1, 3, 1))),
                    _MM_SHUFFLE(3, 1, 2, 0)));
            return { real, imag };
        }
        template <class A>
        XSIMD_INLINE batch<std::complex<double>, A> load_complex(batch<double, A> const& hi, batch<double, A> const& lo, requires_arch<avx2>) noexcept
        {
            using batch_type = batch<double, A>;
            batch_type real = _mm256_permute4x64_pd(_mm256_unpacklo_pd(hi, lo), _MM_SHUFFLE(3, 1, 2, 0));
            batch_type imag = _mm256_permute4x64_pd(_mm256_unpackhi_pd(hi, lo), _MM_SHUFFLE(3, 1, 2, 0));
            return { real, imag };
        }

        // load_unaligned<batch_bool>

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return { _mm256_sub_epi8(_mm256_set1_epi8(0), _mm256_loadu_si256((__m256i const*)mem)) };
            }
            else if constexpr (sizeof(T) == 2)
            {
                auto bpack = _mm_loadu_si128((__m128i const*)mem);
                return { _mm256_sub_epi16(_mm256_set1_epi8(0), _mm256_cvtepu8_epi16(bpack)) };
            }
            // GCC <12 have missing or buggy unaligned load intrinsics; use memcpy to work around this.
            // GCC/Clang/MSVC will turn it into the correct load.
            else if constexpr (sizeof(T) == 4)
            {
#if defined(__x86_64__)
                uint64_t tmp;
                memcpy(&tmp, mem, sizeof(tmp));
                auto val = _mm_cvtsi64_si128(tmp);
#else
                __m128i val;
                memcpy(&val, mem, sizeof(uint64_t));
#endif
                return { _mm256_sub_epi32(_mm256_set1_epi8(0), _mm256_cvtepu8_epi32(val)) };
            }
            else if constexpr (sizeof(T) == 8)
            {
                uint32_t tmp;
                memcpy(&tmp, mem, sizeof(tmp));
                return { _mm256_sub_epi64(_mm256_set1_epi8(0), _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(tmp))) };
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        template <class A>
        XSIMD_INLINE batch_bool<float, A> load_unaligned(bool const* mem, batch_bool<float, A>, requires_arch<avx2> r) noexcept
        {
            return { _mm256_castsi256_ps(load_unaligned(mem, batch_bool<uint32_t, A> {}, r).data) };
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> load_unaligned(bool const* mem, batch_bool<double, A>, requires_arch<avx2> r) noexcept
        {
            return { _mm256_castsi256_pd(load_unaligned(mem, batch_bool<uint64_t, A> {}, r).data) };
        }

        // mask
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return 0xFFFFFFFF & (uint64_t)_mm256_movemask_epi8(self);
            }
            else if constexpr (sizeof(T) == 2)
            {
                uint64_t mask8 = 0xFFFFFFFF & (uint64_t)_mm256_movemask_epi8(self);
                return detail::mask_lut(mask8) | (detail::mask_lut(mask8 >> 8) << 4) | (detail::mask_lut(mask8 >> 16) << 8) | (detail::mask_lut(mask8 >> 24) << 12);
            }
            else
            {
                return mask(self, avx {});
            }
        }

        // max
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_max_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_max_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_max_epi32(self, other);
                }
                else
                {
                    return max(self, other, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_max_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_max_epu16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_max_epu32(self, other);
                }
                else
                {
                    return max(self, other, avx {});
                }
            }
        }

        // min
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_min_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_min_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_min_epi32(self, other);
                }
                else
                {
                    return min(self, other, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_min_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_min_epu16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm256_min_epu32(self, other);
                }
                else
                {
                    return min(self, other, avx {});
                }
            }
        }

        // mul
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                __m256i mask_hi = _mm256_set1_epi32(0xFF00FF00);
                __m256i res_lo = _mm256_mullo_epi16(self, other);
                __m256i other_hi = _mm256_srli_epi16(other, 8);
                __m256i self_hi = _mm256_and_si256(self, mask_hi);
                __m256i res_hi = _mm256_mullo_epi16(self_hi, other_hi);
                __m256i res = _mm256_blendv_epi8(res_lo, res_hi, mask_hi);
                return res;
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_mullo_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_mullo_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_add_epi64(
                    _mm256_mul_epu32(self, other),
                    _mm256_slli_epi64(
                        _mm256_add_epi64(
                            _mm256_mul_epu32(other, _mm256_shuffle_epi32(self, _MM_SHUFFLE(2, 3, 0, 1))),
                            _mm256_mul_epu32(self, _mm256_shuffle_epi32(other, _MM_SHUFFLE(2, 3, 0, 1)))),
                        32));
            }
            else
            {
                return mul(self, other, avx {});
            }
        }

        // mul_hi
        template <class A>
        XSIMD_INLINE batch<int8_t, A> mul_hi(batch<int8_t, A> const& self, batch<int8_t, A> const& other, requires_arch<avx2>) noexcept
        {
            // Sign-extend bytes to 16-bit (unpack-with-self followed by srai 8
            // duplicates the byte then arithmetic-shifts the sign in), do the
            // 16x16->16 multiply, then take the high byte of each product.
            // unpacklo/unpackhi and packs are all per-128-bit-lane, so the
            // round trip preserves byte ordering and no vpermq is needed.
            __m256i a_lo = _mm256_srai_epi16(_mm256_unpacklo_epi8(self, self), 8);
            __m256i a_hi = _mm256_srai_epi16(_mm256_unpackhi_epi8(self, self), 8);
            __m256i b_lo = _mm256_srai_epi16(_mm256_unpacklo_epi8(other, other), 8);
            __m256i b_hi = _mm256_srai_epi16(_mm256_unpackhi_epi8(other, other), 8);
            __m256i p_lo = _mm256_srai_epi16(_mm256_mullo_epi16(a_lo, b_lo), 8);
            __m256i p_hi = _mm256_srai_epi16(_mm256_mullo_epi16(a_hi, b_hi), 8);
            // results already lie in [-128, 127], so packs is exact (no saturation kicks in).
            return _mm256_packs_epi16(p_lo, p_hi);
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> mul_hi(batch<uint8_t, A> const& self, batch<uint8_t, A> const& other, requires_arch<avx2>) noexcept
        {
            __m256i zero = _mm256_setzero_si256();
            __m256i a_lo = _mm256_unpacklo_epi8(self, zero);
            __m256i a_hi = _mm256_unpackhi_epi8(self, zero);
            __m256i b_lo = _mm256_unpacklo_epi8(other, zero);
            __m256i b_hi = _mm256_unpackhi_epi8(other, zero);
            __m256i p_lo = _mm256_srli_epi16(_mm256_mullo_epi16(a_lo, b_lo), 8);
            __m256i p_hi = _mm256_srli_epi16(_mm256_mullo_epi16(a_hi, b_hi), 8);
            return _mm256_packus_epi16(p_lo, p_hi);
        }
        template <class A>
        XSIMD_INLINE batch<int16_t, A> mul_hi(batch<int16_t, A> const& self, batch<int16_t, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_mulhi_epi16(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> mul_hi(batch<uint16_t, A> const& self, batch<uint16_t, A> const& other, requires_arch<avx2>) noexcept
        {
            return _mm256_mulhi_epu16(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<int32_t, A> mul_hi(batch<int32_t, A> const& self, batch<int32_t, A> const& other, requires_arch<avx2>) noexcept
        {
            __m256i even = _mm256_mul_epi32(self, other);
            __m256i odd = _mm256_mul_epi32(_mm256_shuffle_epi32(self, _MM_SHUFFLE(3, 3, 1, 1)),
                                           _mm256_shuffle_epi32(other, _MM_SHUFFLE(3, 3, 1, 1)));
            __m256i even_hi = _mm256_srli_epi64(even, 32);
            return _mm256_blend_epi16(even_hi, odd, 0xCC);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> mul_hi(batch<uint32_t, A> const& self, batch<uint32_t, A> const& other, requires_arch<avx2>) noexcept
        {
            __m256i even = _mm256_mul_epu32(self, other);
            __m256i odd = _mm256_mul_epu32(_mm256_srli_epi64(self, 32), _mm256_srli_epi64(other, 32));
            __m256i even_hi = _mm256_srli_epi64(even, 32);
            return _mm256_blend_epi16(even_hi, odd, 0xCC);
        }

        template <class A>
        XSIMD_INLINE batch<uint64_t, A> mul_hi(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx2>) noexcept
        {
            return detail::mulhi_u64_core<A>(self, other,
                                             [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                             { return batch<uint64_t, A>(_mm256_mul_epu32(a, b)); });
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> mul_hi(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<avx2>) noexcept
        {
            return detail::mulhi_i64_core<A>(self, other,
                                             [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                             { return batch<uint64_t, A>(_mm256_mul_epu32(a, b)); });
        }

        // mul_hilo
        template <class A>
        XSIMD_INLINE std::pair<batch<uint64_t, A>, batch<uint64_t, A>>
        mul_hilo(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx2>) noexcept
        {
            return detail::mulhilo_u64_core<A>(self, other,
                                               [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                               { return batch<uint64_t, A>(_mm256_mul_epu32(a, b)); });
        }

        // reduce_add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                __m256i tmp1 = _mm256_hadd_epi32(self, self);
                __m256i tmp2 = _mm256_hadd_epi32(tmp1, tmp1);
                __m128i tmp3 = _mm256_extracti128_si256(tmp2, 1);
                __m128i tmp4 = _mm_add_epi32(_mm256_castsi256_si128(tmp2), tmp3);
                return _mm_cvtsi128_si32(tmp4);
            }
            else if constexpr (sizeof(T) == 8)
            {
                __m256i tmp1 = _mm256_shuffle_epi32(self, 0x0E);
                __m256i tmp2 = _mm256_add_epi64(self, tmp1);
                __m128i tmp3 = _mm256_extracti128_si256(tmp2, 1);
                __m128i res = _mm_add_epi64(_mm256_castsi256_si128(tmp2), tmp3);
#if defined(__x86_64__)
                return _mm_cvtsi128_si64(res);
#else
                __m128i m;
                _mm_storel_epi64(&m, res);
                int64_t i;
                std::memcpy(&i, &m, sizeof(i));
                return i;
#endif
            }
            else
            {
                return reduce_add(self, avx {});
            }
        }

        // rotate_left
        template <size_t N, class A>
        XSIMD_INLINE batch<uint8_t, A> rotate_left(batch<uint8_t, A> const& self, requires_arch<avx2>) noexcept
        {
            auto other = _mm256_permute2x128_si256(self, self, 0x1);
            if (N < 16)
            {
                return _mm256_alignr_epi8(other, self, N);
            }
            else
            {
                return _mm256_alignr_epi8(self, other, N - 16);
            }
        }
        template <size_t N, class A>
        XSIMD_INLINE batch<int8_t, A> rotate_left(batch<int8_t, A> const& self, requires_arch<avx2>) noexcept
        {
            return bitwise_cast<int8_t>(rotate_left<N, A>(bitwise_cast<uint8_t>(self), avx2 {}));
        }
        template <size_t N, class A>
        XSIMD_INLINE batch<uint16_t, A> rotate_left(batch<uint16_t, A> const& self, requires_arch<avx2>) noexcept
        {
            auto other = _mm256_permute2x128_si256(self, self, 0x1);
            if (N < 8)
            {
                return _mm256_alignr_epi8(other, self, 2 * N);
            }
            else
            {
                return _mm256_alignr_epi8(self, other, 2 * (N - 8));
            }
        }
        template <size_t N, class A>
        XSIMD_INLINE batch<int16_t, A> rotate_left(batch<int16_t, A> const& self, requires_arch<avx2>) noexcept
        {
            return bitwise_cast<int16_t>(rotate_left<N, A>(bitwise_cast<uint16_t>(self), avx2 {}));
        }

        // sadd
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_adds_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_adds_epi16(self, other);
                }
                else
                {
                    return sadd(self, other, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_adds_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_adds_epu16(self, other);
                }
                else
                {
                    return sadd(self, other, avx {});
                }
            }
        }

        // select
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_blendv_epi8(false_br, true_br, cond);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_blendv_epi8(false_br, true_br, cond);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_blendv_epi8(false_br, true_br, cond);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_blendv_epi8(false_br, true_br, cond);
            }
            else
            {
                return select(cond, true_br, false_br, avx {});
            }
        }
        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx2>) noexcept
        {
            // FIXME: for some reason mask here is not considered as an immediate,
            // but it's okay for _mm256_blend_epi32
            // case 2: return _mm256_blend_epi16(false_br, true_br, mask);
            if constexpr (sizeof(T) == 4)
            {
                constexpr int mask = batch_bool_constant<T, A, Values...>::mask();
                return _mm256_blend_epi32(false_br, true_br, mask);
            }
            else if constexpr (sizeof(T) == 8)
            {
                constexpr int mask = batch_bool_constant<T, A, Values...>::mask();
                constexpr int imask = detail::interleave(mask);
                return _mm256_blend_epi32(false_br, true_br, imask);
            }
            else
            {
                return select(batch_bool<T, A> { Values... }, true_br, false_br, avx2 {});
            }
        }

        // slide_left
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<avx2>) noexcept
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
                auto y = _mm256_bslli_epi128(x, M);
                return _mm256_permute2x128_si256(y, y, 0x28);
            }
            if (BitCount == 128)
            {
                return _mm256_permute2x128_si256(x, x, 0x28);
            }
            // shifting by [0, 128[ bits
            constexpr unsigned M = BitCount / 8;
            auto y = _mm256_bslli_epi128(x, M);
            auto z = _mm256_bsrli_epi128(x, 16 - M);
            auto w = _mm256_permute2x128_si256(z, z, 0x28);
            return _mm256_or_si256(y, w);
        }

        // slide_right
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<avx2>) noexcept
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
                auto y = _mm256_bsrli_epi128(x, M);
                return _mm256_permute2x128_si256(y, y, 0x81);
            }
            if (BitCount == 128)
            {
                return _mm256_permute2x128_si256(x, x, 0x81);
            }
            // shifting by [0, 128[ bits
            constexpr unsigned M = BitCount / 8;
            auto y = _mm256_bsrli_epi128(x, M);
            auto z = _mm256_bslli_epi128(x, 16 - M);
            auto w = _mm256_permute2x128_si256(z, z, 0x81);
            return _mm256_or_si256(y, w);
        }

        // store<batch_bool>
        namespace detail
        {
            template <class T>
            XSIMD_INLINE void store_bool_avx2(__m256i b, bool* mem, T) noexcept
            {
                // GCC <12 have missing or buggy unaligned store intrinsics; use memcpy to work around this.
                // GCC/Clang/MSVC will turn it into the correct store.
                if constexpr (sizeof(T) == 1)
                {
                    // negate mask to convert to 0 or 1
                    auto val = _mm256_sub_epi8(_mm256_set1_epi8(0), b);
                    memcpy(mem, &val, sizeof(val));
                    return;
                }

                auto b_hi = _mm256_extractf128_si256(b, 1);
                auto b_lo = _mm256_castsi256_si128(b);
                if constexpr (sizeof(T) == 2)
                {
                    auto val = _mm_sub_epi8(_mm_set1_epi8(0), _mm_packs_epi16(b_lo, b_hi));
                    memcpy(mem, &val, sizeof(val));
                }
                else if constexpr (sizeof(T) == 4)
                {
                    auto pack_16 = _mm_packs_epi32(b_lo, b_hi);
                    auto val = _mm_sub_epi8(_mm_set1_epi8(0), _mm_packs_epi16(pack_16, pack_16));
#if defined(__x86_64__)
                    auto val_lo = _mm_cvtsi128_si64(val);
                    memcpy(mem, &val_lo, sizeof(val_lo));
#else
                    memcpy(mem, &val, sizeof(uint64_t));
#endif
                }
                else if constexpr (sizeof(T) == 8)
                {
                    uint32_t mask = _mm256_movemask_epi8(_mm256_srli_epi64(b, 56));
                    memcpy(mem, &mask, sizeof(mask));
                }
                else
                {
                    assert(false && "unsupported arch/op combination");
                }
            }

            XSIMD_INLINE __m256i avx_to_i(__m256 x) { return _mm256_castps_si256(x); }
            XSIMD_INLINE __m256i avx_to_i(__m256d x) { return _mm256_castpd_si256(x); }
            XSIMD_INLINE __m256i avx_to_i(__m256i x) { return x; }
        }

        template <class T, class A>
        XSIMD_INLINE void store(batch_bool<T, A> b, bool* mem, requires_arch<avx2>) noexcept
        {
            detail::store_bool_avx2(detail::avx_to_i(b), mem, T {});
        }

        // ssub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_subs_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_subs_epi16(self, other);
                }
                else
                {
                    return ssub(self, other, avx {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm256_subs_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm256_subs_epu16(self, other);
                }
                else
                {
                    return ssub(self, other, avx {});
                }
            }
        }

        // sub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm256_sub_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm256_sub_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_sub_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_sub_epi64(self, other);
            }
            else
            {
                return sub(self, other, avx {});
            }
        }

        // swizzle (dynamic mask) on 8 and 16 bits; see avx for 32 and 64 bits versions
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch<uint8_t, A> mask, requires_arch<avx2>) noexcept
        {
            // swap lanes
            __m256i swapped = _mm256_permute2x128_si256(self, self, 0x01); // [high | low]

            // normalize mask taking modulo 16
            batch<uint8_t, A> half_mask = mask & 0b1111u;

            // permute bytes within each lane (AVX2 only)
            __m256i r0 = _mm256_shuffle_epi8(self, half_mask);
            __m256i r1 = _mm256_shuffle_epi8(swapped, half_mask);

            // select lane by the mask index divided by 16, first lane is 0, second is 16.
            constexpr auto lane_size = make_batch_constant<uint8_t, 16, A>();
            constexpr auto lane = (make_iota_batch_constant<uint8_t, A>() / lane_size) * lane_size;
            batch_bool<uint8_t, A> blend_mask = (mask & 0b10000u) != lane;
            return _mm256_blendv_epi8(r0, r1, blend_mask);
        }

        template <class A, typename T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch<uint8_t, A> const& mask, requires_arch<avx2> req) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<uint8_t>(self), mask, req));
        }

        template <class A>
        XSIMD_INLINE batch<uint16_t, A> swizzle(
            batch<uint16_t, A> const& self, batch<uint16_t, A> mask, requires_arch<avx2> req) noexcept
        {
            // No blend/shuffle for 16 bits, we need to use the 8 bits version
            const auto self_bytes = bitwise_cast<uint8_t>(self);
            // If a mask entry is k, we want 2k in low byte and 2k+1 in high byte
            const auto mask_2k_2kp1 = bitwise_cast<uint8_t>((mask << 1) | (mask << 9) | 0x100);
            return bitwise_cast<uint16_t>(swizzle(self_bytes, mask_2k_2kp1, req));
        }

        template <class A, typename T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch<uint16_t, A> const& mask, requires_arch<avx2> req) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<uint16_t>(self), mask, req));
        }

        namespace detail
        {
            template <bool cross_batch, typename T, T... Vals>
            struct swizzle_mask
            {
                static constexpr auto values = std::array<T, sizeof...(Vals)> { Vals... };

                static constexpr T get(std::size_t idx_, std::size_t size_) noexcept
                {
                    const T size = static_cast<T>(size_);
                    const T idx = static_cast<T>(idx_);
                    const T val = values[idx_];

                    // Check if value in bounds
                    if ((T(0) <= val) && (val < size))
                    {
                        // Whether we need to access the value from the other lane
                        const bool val_is_cross_lane = (idx < (size / 2)) != (val < (size / 2));
                        if (val_is_cross_lane == cross_batch)
                        {
                            return val % (size / 2);
                        }
                    }

                    // Out of bounds with most significant bit set to 1 will set the swizzle target to 0
                    return ~T {};
                }
            };
        }

        // swizzle (constant mask)
        template <class A, uint8_t... Vals>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch_constant<uint8_t, A, Vals...> mask, requires_arch<avx2>) noexcept
        {
            static_assert(sizeof...(Vals) == 32, "Must contain as many uint8_t as can fit in avx register");

            if constexpr (detail::is_identity(mask))
            {
                return self;
            }

            constexpr auto lane_mask = mask % std::integral_constant<uint8_t, (mask.size / 2)>();

            if constexpr (!detail::is_cross_lane(mask))
            {
                return _mm256_shuffle_epi8(self, lane_mask.as_batch());
            }
            if constexpr (detail::is_only_from_lo(mask))
            {
                __m256i broadcast = _mm256_permute2x128_si256(self, self, 0x00); // [low | low]
                return _mm256_shuffle_epi8(broadcast, lane_mask.as_batch());
            }
            if constexpr (detail::is_only_from_hi(mask))
            {
                __m256i broadcast = _mm256_permute2x128_si256(self, self, 0x11); // [high | high]
                return _mm256_shuffle_epi8(broadcast, lane_mask.as_batch());
            }

            // swap lanes
            __m256i swapped = _mm256_permute2x128_si256(self, self, 0x01); // [high | low]

            // We can outsmart the dynamic version by creating a compile-time mask that leaves zeros
            // where it does not need to select data, resulting in a simple OR merge of the two batches.
            constexpr auto self_mask = make_batch_constant<uint8_t, detail::swizzle_mask<false, uint8_t, Vals...>, A>();
            constexpr auto cross_mask = make_batch_constant<uint8_t, detail::swizzle_mask<true, uint8_t, Vals...>, A>();

            // permute bytes within each lane (AVX2 only)
            __m256i r0 = _mm256_shuffle_epi8(self, self_mask.as_batch());
            __m256i r1 = _mm256_shuffle_epi8(swapped, cross_mask.as_batch());

            return _mm256_or_si256(r0, r1);
        }

        template <class A, typename T, uint8_t... Vals, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint8_t, A, Vals...> const& mask, requires_arch<avx2> req) noexcept
        {
            static_assert(sizeof...(Vals) == 32, "Must contain as many uint8_t as can fit in avx register");
            return bitwise_cast<T>(swizzle(bitwise_cast<uint8_t>(self), mask, req));
        }

        template <
            class A,
            uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3,
            uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7,
            uint16_t V8, uint16_t V9, uint16_t V10, uint16_t V11,
            uint16_t V12, uint16_t V13, uint16_t V14, uint16_t V15>
        XSIMD_INLINE batch<uint16_t, A> swizzle(
            batch<uint16_t, A> const& self,
            batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15>,
            requires_arch<avx2> req) noexcept
        {
            const auto self_bytes = bitwise_cast<uint8_t>(self);
            // If a mask entry is k, we want 2k in low byte and 2k+1 in high byte
            auto constexpr mask_2k_2kp1 = batch_constant<
                uint8_t, A,
                2 * V0, 2 * V0 + 1, 2 * V1, 2 * V1 + 1, 2 * V2, 2 * V2 + 1, 2 * V3, 2 * V3 + 1,
                2 * V4, 2 * V4 + 1, 2 * V5, 2 * V5 + 1, 2 * V6, 2 * V6 + 1, 2 * V7, 2 * V7 + 1,
                2 * V8, 2 * V8 + 1, 2 * V9, 2 * V9 + 1, 2 * V10, 2 * V10 + 1, 2 * V11, 2 * V11 + 1,
                2 * V12, 2 * V12 + 1, 2 * V13, 2 * V13 + 1, 2 * V14, 2 * V14 + 1, 2 * V15, 2 * V15 + 1> {};
            return bitwise_cast<uint16_t>(swizzle(self_bytes, mask_2k_2kp1, req));
        }

        template <class A, typename T, uint16_t... Vals, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint16_t, A, Vals...> const& mask, requires_arch<avx2> req) noexcept
        {
            static_assert(sizeof...(Vals) == 16, "Must contain as many uint16_t as can fit in avx register");
            return bitwise_cast<T>(swizzle(bitwise_cast<uint16_t>(self), mask, req));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3, uint32_t V4, uint32_t V5, uint32_t V6, uint32_t V7>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<avx2>) noexcept
        {
            if constexpr (detail::is_identity(mask))
            {
                return self;
            }
            if constexpr (!detail::is_cross_lane(mask))
            {
                constexpr auto lane_mask = mask % std::integral_constant<uint32_t, (mask.size / 2)>();
                // Cheaper intrinsics when not crossing lanes
                // Contrary to the uint64_t version, the limits of 8 bits for the immediate constant
                // cannot make different permutations across lanes
                batch<float, A> permuted = _mm256_permutevar_ps(bitwise_cast<float>(self), lane_mask.as_batch());
                return bitwise_cast<uint32_t>(permuted);
            }
            return _mm256_permutevar8x32_epi32(self, mask.as_batch());
        }

        template <class A, typename T, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3, uint32_t V4, uint32_t V5, uint32_t V6, uint32_t V7, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<avx2> req) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<uint32_t>(self), mask, req));
        }

        template <class A, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch_constant<uint64_t, A, V0, V1, V2, V3> mask, requires_arch<avx2>) noexcept
        {
            if constexpr (detail::is_identity(mask))
            {
                return self;
            }
            if constexpr (!detail::is_cross_lane(mask))
            {
                constexpr uint8_t lane_mask = (V0 % 2) | ((V1 % 2) << 1) | ((V2 % 2) << 2) | ((V3 % 2) << 3);
                // Cheaper intrinsics when not crossing lanes
                batch<double, A> permuted = _mm256_permute_pd(bitwise_cast<double>(self), lane_mask);
                return bitwise_cast<uint64_t>(permuted);
            }
            constexpr auto mask_int = detail::mod_shuffle(V0, V1, V2, V3);
            return _mm256_permute4x64_epi64(self, mask_int);
        }

        template <class A, typename T, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint64_t, A, V0, V1, V2, V3> mask, requires_arch<avx2> req) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<uint64_t>(self), mask, req));
        }

        // zip_hi
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                auto lo = _mm256_unpacklo_epi8(self, other);
                auto hi = _mm256_unpackhi_epi8(self, other);
                return _mm256_permute2f128_si256(lo, hi, 0x31);
            }
            else if constexpr (sizeof(T) == 2)
            {
                auto lo = _mm256_unpacklo_epi16(self, other);
                auto hi = _mm256_unpackhi_epi16(self, other);
                return _mm256_permute2f128_si256(lo, hi, 0x31);
            }
            else if constexpr (sizeof(T) == 4)
            {
                auto lo = _mm256_unpacklo_epi32(self, other);
                auto hi = _mm256_unpackhi_epi32(self, other);
                return _mm256_permute2f128_si256(lo, hi, 0x31);
            }
            else if constexpr (sizeof(T) == 8)
            {
                auto lo = _mm256_unpacklo_epi64(self, other);
                auto hi = _mm256_unpackhi_epi64(self, other);
                return _mm256_permute2f128_si256(lo, hi, 0x31);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // zip_lo
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                auto lo = _mm256_unpacklo_epi8(self, other);
                auto hi = _mm256_unpackhi_epi8(self, other);
                return _mm256_inserti128_si256(lo, _mm256_castsi256_si128(hi), 1);
            }
            else if constexpr (sizeof(T) == 2)
            {
                auto lo = _mm256_unpacklo_epi16(self, other);
                auto hi = _mm256_unpackhi_epi16(self, other);
                return _mm256_inserti128_si256(lo, _mm256_castsi256_si128(hi), 1);
            }
            else if constexpr (sizeof(T) == 4)
            {
                auto lo = _mm256_unpacklo_epi32(self, other);
                auto hi = _mm256_unpackhi_epi32(self, other);
                return _mm256_inserti128_si256(lo, _mm256_castsi256_si128(hi), 1);
            }
            else if constexpr (sizeof(T) == 8)
            {
                auto lo = _mm256_unpacklo_epi64(self, other);
                auto hi = _mm256_unpackhi_epi64(self, other);
                return _mm256_inserti128_si256(lo, _mm256_castsi256_si128(hi), 1);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // widen
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<avx2>) noexcept
        {
            __m128i x_lo = detail::lower_half(x);
            __m128i x_hi = detail::upper_half(x);
            __m256i lo, hi;
            if constexpr (sizeof(T) == 4)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm256_cvtepi32_epi64(x_lo);
                    hi = _mm256_cvtepi32_epi64(x_hi);
                }
                else
                {
                    lo = _mm256_cvtepu32_epi64(x_lo);
                    hi = _mm256_cvtepu32_epi64(x_hi);
                }
            }
            else if constexpr (sizeof(T) == 2)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm256_cvtepi16_epi32(x_lo);
                    hi = _mm256_cvtepi16_epi32(x_hi);
                }
                else
                {
                    lo = _mm256_cvtepu16_epi32(x_lo);
                    hi = _mm256_cvtepu16_epi32(x_hi);
                }
            }
            else if constexpr (sizeof(T) == 1)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm256_cvtepi8_epi16(x_lo);
                    hi = _mm256_cvtepi8_epi16(x_hi);
                }
                else
                {
                    lo = _mm256_cvtepu8_epi16(x_lo);
                    hi = _mm256_cvtepu8_epi16(x_hi);
                }
            }
            return { lo, hi };
        }

    }
}

#endif
