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

#ifndef XSIMD_SSSE3_HPP
#define XSIMD_SSSE3_HPP

#include "../types/xsimd_ssse3_register.hpp"
#include "../types/xsimd_utils.hpp"

#include <cstddef>
#include <type_traits>

namespace xsimd
{

    namespace kernel
    {
        using namespace types;

        // abs
        template <class A, class T, std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& self, requires_arch<ssse3>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_abs_epi8(self);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_abs_epi16(self);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_abs_epi32(self);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_abs_epi64(self);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // extract_pair
        namespace detail
        {

            template <class T, class A>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const&, batch<T, A> const& other, std::size_t, std::index_sequence<>) noexcept
            {
                return other;
            }

            template <class T, class A, std::size_t I, std::size_t... Is>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& self, batch<T, A> const& other, std::size_t i, std::index_sequence<I, Is...>) noexcept
            {
                if (i == I)
                {
                    return _mm_alignr_epi8(self, other, sizeof(T) * I);
                }
                else
                    return extract_pair(self, other, i, std::index_sequence<Is...>());
            }
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& self, batch<T, A> const& other, std::size_t i, requires_arch<ssse3>) noexcept
        {
            constexpr std::size_t size = batch<T, A>::size;
            assert(i < size && "index in bounds");
            return detail::extract_pair(self, other, i, std::make_index_sequence<size>());
        }

        // reduce_add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<ssse3>) noexcept
        {
            if constexpr (sizeof(T) == 2)
            {
                __m128i tmp1 = _mm_hadd_epi16(self, self);
                __m128i tmp2 = _mm_hadd_epi16(tmp1, tmp1);
                __m128i tmp3 = _mm_hadd_epi16(tmp2, tmp2);
                return _mm_cvtsi128_si32(tmp3) & 0xFFFF;
            }
            else if constexpr (sizeof(T) == 4)
            {
                __m128i tmp1 = _mm_hadd_epi32(self, self);
                __m128i tmp2 = _mm_hadd_epi32(tmp1, tmp1);
                return _mm_cvtsi128_si32(tmp2);
            }
            else
            {
                return reduce_add(self, sse3 {});
            }
        }

        // rotate_left
        template <size_t N, class A>
        XSIMD_INLINE batch<uint8_t, A> rotate_left(batch<uint8_t, A> const& self, requires_arch<ssse3>) noexcept
        {
            return _mm_alignr_epi8(self, self, N);
        }
        template <size_t N, class A>
        XSIMD_INLINE batch<int8_t, A> rotate_left(batch<int8_t, A> const& self, requires_arch<ssse3>) noexcept
        {
            return bitwise_cast<int8_t>(rotate_left<N, A>(bitwise_cast<uint8_t>(self), ssse3 {}));
        }

        template <size_t N, class A>
        XSIMD_INLINE batch<uint16_t, A> rotate_left(batch<uint16_t, A> const& self, requires_arch<ssse3>) noexcept
        {
            return _mm_alignr_epi8(self, self, 2 * N);
        }
        template <size_t N, class A>
        XSIMD_INLINE batch<int16_t, A> rotate_left(batch<int16_t, A> const& self, requires_arch<ssse3>) noexcept
        {
            return bitwise_cast<int16_t>(rotate_left<N, A>(bitwise_cast<uint16_t>(self), ssse3 {}));
        }

        // swizzle (dynamic mask)
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch<uint8_t, A> mask, requires_arch<ssse3>) noexcept
        {
            return _mm_shuffle_epi8(self, mask);
        }
        template <class A>
        XSIMD_INLINE batch<int8_t, A> swizzle(batch<int8_t, A> const& self, batch<uint8_t, A> mask, requires_arch<ssse3>) noexcept
        {
            return _mm_shuffle_epi8(self, mask);
        }

        template <class A, class T, class IT>
        XSIMD_INLINE std::enable_if_t<std::is_arithmetic_v<T>, batch<T, A>>
        swizzle(batch<T, A> const& self, batch<IT, A> mask, requires_arch<ssse3>) noexcept
        {
            constexpr auto pikes = static_cast<as_unsigned_integer_t<T>>(0x0706050403020100ul);
            constexpr auto comb = static_cast<as_unsigned_integer_t<T>>(0x0101010101010101ul * sizeof(T));
            return bitwise_cast<T>(swizzle(bitwise_cast<uint8_t>(self), bitwise_cast<uint8_t>(mask * comb + pikes), ssse3 {}));
        }

        // swizzle (constant mask)
        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7>, requires_arch<ssse3>) noexcept
        {
            constexpr batch_constant<uint8_t, A, 2 * V0, 2 * V0 + 1, 2 * V1, 2 * V1 + 1, 2 * V2, 2 * V2 + 1, 2 * V3, 2 * V3 + 1,
                                     2 * V4, 2 * V4 + 1, 2 * V5, 2 * V5 + 1, 2 * V6, 2 * V6 + 1, 2 * V7, 2 * V7 + 1>
                mask8;
            return _mm_shuffle_epi8(self, mask8.as_batch());
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<ssse3>) noexcept
        {
            return bitwise_cast<int16_t>(swizzle(bitwise_cast<uint16_t>(self), mask, ssse3 {}));
        }

        template <class A, uint8_t V0, uint8_t V1, uint8_t V2, uint8_t V3, uint8_t V4, uint8_t V5, uint8_t V6, uint8_t V7,
                  uint8_t V8, uint8_t V9, uint8_t V10, uint8_t V11, uint8_t V12, uint8_t V13, uint8_t V14, uint8_t V15>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch_constant<uint8_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15> mask, requires_arch<ssse3>) noexcept
        {
            return swizzle(self, mask.as_batch(), ssse3 {});
        }

        template <class A, uint8_t V0, uint8_t V1, uint8_t V2, uint8_t V3, uint8_t V4, uint8_t V5, uint8_t V6, uint8_t V7,
                  uint8_t V8, uint8_t V9, uint8_t V10, uint8_t V11, uint8_t V12, uint8_t V13, uint8_t V14, uint8_t V15>
        XSIMD_INLINE batch<int8_t, A> swizzle(batch<int8_t, A> const& self, batch_constant<uint8_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15> mask, requires_arch<ssse3>) noexcept
        {
            return swizzle(self, mask.as_batch(), ssse3 {});
        }

    }

}

#endif
