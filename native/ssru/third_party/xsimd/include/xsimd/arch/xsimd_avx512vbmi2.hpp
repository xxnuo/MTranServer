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

#ifndef XSIMD_AVX512VBMI2_HPP
#define XSIMD_AVX512VBMI2_HPP

#include "../types/xsimd_avx512vbmi2_register.hpp"

#include <type_traits>

namespace xsimd
{

    namespace kernel
    {
        using namespace types;

        // compress
        template <class A>
        XSIMD_INLINE batch<int16_t, A> compress(batch<int16_t, A> const& self, batch_bool<int16_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_compress_epi16((__mmask32)mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> compress(batch<uint16_t, A> const& self, batch_bool<uint16_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_compress_epi16((__mmask32)mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int8_t, A> compress(batch<int8_t, A> const& self, batch_bool<int8_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_compress_epi8(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> compress(batch<uint8_t, A> const& self, batch_bool<uint8_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_compress_epi8(mask.mask(), self);
        }

        // expand
        template <class A>
        XSIMD_INLINE batch<int16_t, A> expand(batch<int16_t, A> const& self, batch_bool<int16_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_expand_epi16((__mmask32)mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> expand(batch<uint16_t, A> const& self, batch_bool<uint16_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_expand_epi16((__mmask32)mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int8_t, A> expand(batch<int8_t, A> const& self, batch_bool<int8_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_expand_epi8(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> expand(batch<uint8_t, A> const& self, batch_bool<uint8_t, A> const& mask, requires_arch<avx512vbmi2>) noexcept
        {
            return _mm512_maskz_expand_epi8(mask.mask(), self);
        }

        // rotl
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, int32_t other, requires_arch<avx512vbmi2>) noexcept
        {
            if constexpr (sizeof(T) == 2)
            {
                return _mm512_shldv_epi16(self, self, _mm512_set1_epi16(static_cast<uint16_t>(other)));
            }
            else
            {
                return rotl(self, other, avx512bw {});
            }
        }

        template <size_t count, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, requires_arch<avx512vbmi2>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count must be less than the number of bits in T");
            if constexpr (sizeof(T) == 2)
            {
                return _mm512_shldi_epi16(self, self, count);
            }
            else
            {
                return rotl<count>(self, avx512bw {});
            }
        }

        // rotr
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, int32_t other, requires_arch<avx512vbmi2>) noexcept
        {
            if constexpr (sizeof(T) == 2)
            {
                return _mm512_shrdv_epi16(self, self, _mm512_set1_epi16(static_cast<uint16_t>(other)));
            }
            else
            {
                return rotr(self, other, avx512bw {});
            }
        }

        template <size_t count, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, requires_arch<avx512vbmi2>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "count must be less than the number of bits in T");
            if constexpr (sizeof(T) == 2)
            {
                return _mm512_shrdi_epi16(self, self, count);
            }
            else
            {
                return rotr<count>(self, avx512bw {});
            }
        }
    }
}

#endif
