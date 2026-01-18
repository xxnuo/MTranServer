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

#ifndef XSIMD_AVX512BW_HPP
#define XSIMD_AVX512BW_HPP

#include "../types/xsimd_avx512bw_register.hpp"

#include <array>
#include <type_traits>

namespace xsimd
{

    namespace kernel
    {
        using namespace types;

        namespace detail
        {
            template <class A, class T, int Cmp>
            XSIMD_INLINE batch_bool<T, A> compare_int_avx512bw(batch<T, A> const& self, batch<T, A> const& other) noexcept
            {
                using register_type = typename batch_bool<T, A>::register_type;
                if (std::is_signed_v<T>)
                {
                    if constexpr (sizeof(T) == 1)
                    {
                        return (register_type)_mm512_cmp_epi8_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 2)
                    {
                        return (register_type)_mm512_cmp_epi16_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 4)
                    {
                        return (register_type)_mm512_cmp_epi32_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 8)
                    {
                        return (register_type)_mm512_cmp_epi64_mask(self, other, Cmp);
                    }
                }
                else
                {
                    if constexpr (sizeof(T) == 1)
                    {
                        return (register_type)_mm512_cmp_epu8_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 2)
                    {
                        return (register_type)_mm512_cmp_epu16_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 4)
                    {
                        return (register_type)_mm512_cmp_epu32_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 8)
                    {
                        return (register_type)_mm512_cmp_epu64_mask(self, other, Cmp);
                    }
                }
            }
        }

        // abs
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& self, requires_arch<avx512bw>) noexcept
        {
            if (std::is_unsigned_v<T>)
            {
                return self;
            }

            if constexpr (sizeof(T) == 1)
            {
                return _mm512_abs_epi8(self);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_abs_epi16(self);
            }
            else
            {
                return abs(self, avx512dq {});
            }
        }

        // add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_add_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_add_epi16(self, other);
            }
            else
            {
                return add(self, other, avx512dq {});
            }
        }

        // avgr
        template <class A, class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_avg_epu8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_avg_epu16(self, other);
            }
            else
            {
                return avgr(self, other, common {});
            }
        }

        // avg
        template <class A, class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
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

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<avx512bw>) noexcept
        {
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
            if constexpr (sizeof(T) == 2)
            {
                return _mm512_sllv_epi16(self, _mm512_set1_epi16(other));
#else
            if constexpr (sizeof(T) == 2)
            {
                return _mm512_slli_epi16(self, other);
#endif
            }
            else
            {
                return bitwise_lshift(self, other, avx512dq {});
            }
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<avx512bw>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    __m512i sign_mask = _mm512_set1_epi16((0xFF00 >> other) & 0x00FF);
                    __m512i zeros = _mm512_setzero_si512();
                    __mmask64 cmp_is_negative_mask = _mm512_cmpgt_epi8_mask(zeros, self);
                    __m512i cmp_sign_mask = _mm512_mask_blend_epi8(cmp_is_negative_mask, zeros, sign_mask);
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                    __m512i res = _mm512_srav_epi16(self, _mm512_set1_epi16(other));
#else
                    __m512i res = _mm512_srai_epi16(self, other);
#endif
                    return _mm512_or_si512(cmp_sign_mask, _mm512_andnot_si512(sign_mask, res));
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_srav_epi16(self, _mm512_set1_epi16(other));
#else
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_srai_epi16(self, other);
#endif
                }
                else
                {
                    return bitwise_rshift(self, other, avx512dq {});
                }
            }
            else
            {
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                if constexpr (sizeof(T) == 2)
                {
                    return _mm512_srlv_epi16(self, _mm512_set1_epi16(other));
#else
                if constexpr (sizeof(T) == 2)
                {
                    return _mm512_srli_epi16(self, other);
#endif
                }
                else
                {
                    return bitwise_rshift(self, other, avx512dq {});
                }
            }
        }

        // decr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx512bw>) noexcept
        {

            if constexpr (sizeof(T) == 1)
            {
                return _mm512_mask_sub_epi8(self, mask.data, self, _mm512_set1_epi8(1));
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_mask_sub_epi16(self, mask.data, self, _mm512_set1_epi16(1));
            }
            else
            {
                return decr_if(self, mask, avx512dq {});
            }
        }

        // eq
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return detail::compare_int_avx512bw<A, T, _MM_CMPINT_EQ>(self, other);
        }

        // ge
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return detail::compare_int_avx512bw<A, T, _MM_CMPINT_GE>(self, other);
        }

        // gt
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return detail::compare_int_avx512bw<A, T, _MM_CMPINT_GT>(self, other);
        }

        // incr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx512bw>) noexcept
        {

            if constexpr (sizeof(T) == 1)
            {
                return _mm512_mask_add_epi8(self, mask.data, self, _mm512_set1_epi8(1));
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_mask_add_epi16(self, mask.data, self, _mm512_set1_epi16(1));
            }
            else
            {
                return incr_if(self, mask, avx512dq {});
            }
        }

        // insert
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I> pos, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_mask_set1_epi8(self, __mmask64(1ULL << (I & 63)), val);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_mask_set1_epi16(self, __mmask32(1 << (I & 31)), val);
            }
            else
            {
                return insert(self, val, pos, avx512dq {});
            }
        }

        // le
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return detail::compare_int_avx512bw<A, T, _MM_CMPINT_LE>(self, other);
        }

        // lt
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return detail::compare_int_avx512bw<A, T, _MM_CMPINT_LT>(self, other);
        }

        // load
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<avx512bw>) noexcept
        {
            using mask_type = typename batch_bool<T, A>::register_type;
            if constexpr (batch_bool<T, A>::size == 64)
            {
                __m512i bool_val = _mm512_loadu_si512((__m512i const*)mem);
                return (mask_type)_mm512_cmpgt_epu8_mask(bool_val, _mm512_setzero_si512());
            }
            else if constexpr (batch_bool<T, A>::size == 32)
            {
                __m256i bpack = _mm256_loadu_si256((__m256i const*)mem);
                return (mask_type)_mm512_cmpgt_epu16_mask(_mm512_cvtepu8_epi16(bpack), _mm512_setzero_si512());
            }
            else if constexpr (batch_bool<T, A>::size == 16)
            {
                __m128i bpack = _mm_loadu_si128((__m128i const*)mem);
                return (mask_type)_mm512_cmpgt_epu32_mask(_mm512_cvtepu8_epi32(bpack), _mm512_setzero_si512());
            }
            else if constexpr (batch_bool<T, A>::size == 8)
            {
                __m128i bpack = _mm_loadl_epi64((__m128i const*)mem);
                return (mask_type)_mm512_cmpgt_epu64_mask(_mm512_cvtepu8_epi64(bpack), _mm512_setzero_si512());
            }
            else
            {
                assert(false && "unexpected batch size");
                return {};
            }
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> load_aligned(bool const* mem, batch_bool<T, A>, requires_arch<avx512bw>) noexcept
        {
            using mask_type = typename batch_bool<T, A>::register_type;
            if constexpr (batch_bool<T, A>::size == 64)
            {
                __m512i bool_val = _mm512_load_si512((__m512i const*)mem);
                return (mask_type)_mm512_cmpgt_epu8_mask(bool_val, _mm512_setzero_si512());
            }
            else if constexpr (batch_bool<T, A>::size == 32)
            {
                __m256i bpack = _mm256_load_si256((__m256i const*)mem);
                return (mask_type)_mm512_cmpgt_epu16_mask(_mm512_cvtepu8_epi16(bpack), _mm512_setzero_si512());
            }
            else if constexpr (batch_bool<T, A>::size == 16)
            {
                __m128i bpack = _mm_load_si128((__m128i const*)mem);
                return (mask_type)_mm512_cmpgt_epu32_mask(_mm512_cvtepu8_epi32(bpack), _mm512_setzero_si512());
            }
            else if constexpr (batch_bool<T, A>::size == 8)
            {
                __m128i bpack = _mm_loadl_epi64((__m128i const*)mem);
                return (mask_type)_mm512_cmpgt_epu64_mask(_mm512_cvtepu8_epi64(bpack), _mm512_setzero_si512());
            }
            else
            {
                assert(false && "unexpected batch size");
                return {};
            }
        }

        // load_masked / store_masked: native vmovdqu8 / vmovdqu16 predication for
        // 8/16-bit, replacing the common scalar fallback. No aligned masked 8/16
        // intrinsic exists and masked moves never fault, so loadu fits both modes.
        template <class A, class T, class Mode,
                  class = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_maskz_loadu_epi8((__mmask64)mask.mask(), mem);
            }
            else
            {
                return _mm512_maskz_loadu_epi16((__mmask32)mask.mask(), mem);
            }
        }

        template <class A, class T, class Mode,
                  class = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                _mm512_mask_storeu_epi8((void*)mem, (__mmask64)mask.mask(), src);
            }
            else
            {
                _mm512_mask_storeu_epi16((void*)mem, (__mmask32)mask.mask(), src);
            }
        }

        // Constant masks reuse the runtime overloads; as_batch_bool() also avoids
        // batch_bool_constant::mask() truncating a 64-lane int8 mask to int.
        template <class A, class T, bool... Values, class Mode,
                  class = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, Values...> mask, convert<T>, Mode, requires_arch<avx512bw>) noexcept
        {
            return load_masked(mem, mask.as_batch_bool(), convert<T> {}, Mode {}, avx512bw {});
        }

        template <class A, class T, bool... Values, class Mode,
                  class = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, Values...> mask, Mode, requires_arch<avx512bw>) noexcept
        {
            store_masked(mem, src, mask.as_batch_bool(), Mode {}, avx512bw {});
        }

        // max
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_max_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_max_epi16(self, other);
                }
                else
                {
                    return max(self, other, avx512dq {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_max_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_max_epu16(self, other);
                }
                else
                {
                    return max(self, other, avx512dq {});
                }
            }
        }

        // min
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_min_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_min_epi16(self, other);
                }
                else
                {
                    return min(self, other, avx512dq {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_min_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_min_epu16(self, other);
                }
                else
                {
                    return min(self, other, avx512dq {});
                }
            }
        }

        // mul
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                __m512i upper = _mm512_and_si512(_mm512_mullo_epi16(self, other), _mm512_srli_epi16(_mm512_set1_epi16(-1), 8));
                __m512i lower = _mm512_slli_epi16(_mm512_mullo_epi16(_mm512_srli_epi16(self, 8), _mm512_srli_epi16(other, 8)), 8);
                return _mm512_or_si512(upper, lower);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_mullo_epi16(self, other);
            }
            else
            {
                return mul(self, other, avx512dq {});
            }
        }

        // mul_hi
        template <class A>
        XSIMD_INLINE batch<int8_t, A> mul_hi(batch<int8_t, A> const& self, batch<int8_t, A> const& other, requires_arch<avx512bw>) noexcept
        {
            // Per-128-bit-lane unpack/pack pair preserves byte ordering across
            // the four 128-bit lanes of a ZMM, so no inter-lane permute needed.
            __m512i a_lo = _mm512_srai_epi16(_mm512_unpacklo_epi8(self, self), 8);
            __m512i a_hi = _mm512_srai_epi16(_mm512_unpackhi_epi8(self, self), 8);
            __m512i b_lo = _mm512_srai_epi16(_mm512_unpacklo_epi8(other, other), 8);
            __m512i b_hi = _mm512_srai_epi16(_mm512_unpackhi_epi8(other, other), 8);
            __m512i p_lo = _mm512_srai_epi16(_mm512_mullo_epi16(a_lo, b_lo), 8);
            __m512i p_hi = _mm512_srai_epi16(_mm512_mullo_epi16(a_hi, b_hi), 8);
            return _mm512_packs_epi16(p_lo, p_hi);
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> mul_hi(batch<uint8_t, A> const& self, batch<uint8_t, A> const& other, requires_arch<avx512bw>) noexcept
        {
            __m512i zero = _mm512_setzero_si512();
            __m512i a_lo = _mm512_unpacklo_epi8(self, zero);
            __m512i a_hi = _mm512_unpackhi_epi8(self, zero);
            __m512i b_lo = _mm512_unpacklo_epi8(other, zero);
            __m512i b_hi = _mm512_unpackhi_epi8(other, zero);
            __m512i p_lo = _mm512_srli_epi16(_mm512_mullo_epi16(a_lo, b_lo), 8);
            __m512i p_hi = _mm512_srli_epi16(_mm512_mullo_epi16(a_hi, b_hi), 8);
            return _mm512_packus_epi16(p_lo, p_hi);
        }
        template <class A>
        XSIMD_INLINE batch<int16_t, A> mul_hi(batch<int16_t, A> const& self, batch<int16_t, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return _mm512_mulhi_epi16(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> mul_hi(batch<uint16_t, A> const& self, batch<uint16_t, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return _mm512_mulhi_epu16(self, other);
        }

        // neq
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            return detail::compare_int_avx512bw<A, T, _MM_CMPINT_NE>(self, other);
        }

        // sadd
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_adds_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_adds_epi16(self, other);
                }
                else
                {
                    return sadd(self, other, avx512dq {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_adds_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_adds_epu16(self, other);
                }
                else
                {
                    return sadd(self, other, avx512dq {});
                }
            }
        }

        // select
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_mask_blend_epi8(cond, false_br.data, true_br.data);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_mask_blend_epi16(cond, false_br.data, true_br.data);
            }
            else
            {
                return select(cond, true_br, false_br, avx512dq {});
            }
        }

        // slide_left
        template <size_t N, class A, class T, class = std::enable_if_t<(N & 3) == 2 && (N < 64)>>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<avx512bw>) noexcept
        {
            static_assert((N & 3) == 2 && N < 64, "The AVX512F implementation may have a lower latency.");

            __mmask32 mask = 0xFFFFFFFFu << ((N / 2) & 31);
            auto slide_pattern = make_batch_constant<uint16_t, detail::make_slide_left_pattern<N / 2>, A>();
            return _mm512_maskz_permutexvar_epi16(mask, slide_pattern.as_batch(), x);
        }

        // slide_right
        template <size_t N, class A, class T, class = std::enable_if_t<(N & 3) == 2 && (N < 64)>>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<avx512bw>) noexcept
        {
            static_assert((N & 3) == 2 && N < 64, "The AVX512F implementation may have a lower latency.");

            __mmask32 mask = 0xFFFFFFFFu >> ((N / 2) & 31);
            auto slide_pattern = make_batch_constant<uint16_t, detail::make_slide_right_pattern<N / 2>, A>();
            return _mm512_maskz_permutexvar_epi16(mask, slide_pattern.as_batch(), x);
        }

        // ssub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_subs_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_subs_epi16(self, other);
                }
                else
                {
                    return ssub(self, other, avx512dq {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm512_subs_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm512_subs_epu16(self, other);
                }
                else
                {
                    return ssub(self, other, avx512dq {});
                }
            }
        }

        // store
        template <class T, class A>
        XSIMD_INLINE void store(batch_bool<T, A> const& self, bool* mem, requires_arch<avx512bw>) noexcept
        {
            constexpr auto size = batch_bool<T, A>::size;
            __m512i bool_val = _mm512_maskz_set1_epi8(self.data, 0x01);
            __mmask64 mask = size >= 64 ? ~(__mmask64)0 : (1ULL << size) - 1;
            _mm512_mask_storeu_epi8((void*)mem, mask, bool_val);
        }

        // sub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_sub_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_sub_epi16(self, other);
            }
            else
            {
                return sub(self, other, avx512dq {});
            }
        }

        // swizzle (dynamic version)
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch<uint16_t, A> mask, requires_arch<avx512bw>) noexcept
        {
            return _mm512_permutexvar_epi16(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self, batch<uint16_t, A> mask, requires_arch<avx512bw>) noexcept
        {
            return bitwise_cast<int16_t>(swizzle(bitwise_cast<uint16_t>(self), mask, avx512bw {}));
        }

        // swizzle (static version)
        template <class A, uint16_t... Vs>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch_constant<uint16_t, A, Vs...> mask, requires_arch<avx512bw>) noexcept
        {
            return swizzle(self, mask.as_batch(), avx512bw {});
        }

        template <class A, uint16_t... Vs>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self, batch_constant<uint16_t, A, Vs...> mask, requires_arch<avx512bw>) noexcept
        {
            return swizzle(self, mask.as_batch(), avx512bw {});
        }

        // widen
        template <class A>
        XSIMD_INLINE std::array<batch<widen_t<uint8_t>, A>, 2> widen(batch<uint8_t, A> const& x, requires_arch<avx512bw>) noexcept
        {
            __m256i x_lo = _mm512_extracti64x4_epi64(x, 0);
            __m256i x_hi = _mm512_extracti64x4_epi64(x, 1);
            __m512i lo = _mm512_cvtepu8_epi16(x_lo);
            __m512i hi = _mm512_cvtepu8_epi16(x_hi);
            return { lo, hi };
        }
        template <class A>
        XSIMD_INLINE std::array<batch<widen_t<int8_t>, A>, 2> widen(batch<int8_t, A> const& x, requires_arch<avx512bw>) noexcept
        {
            __m256i x_lo = _mm512_extracti64x4_epi64(x, 0);
            __m256i x_hi = _mm512_extracti64x4_epi64(x, 1);
            __m512i lo = _mm512_cvtepi8_epi16(x_lo);
            __m512i hi = _mm512_cvtepi8_epi16(x_hi);
            return { lo, hi };
        }

        // zip_hi
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            __m512i lo, hi;
            if constexpr (sizeof(T) == 1)
            {
                lo = _mm512_unpacklo_epi8(self, other);
                hi = _mm512_unpackhi_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                lo = _mm512_unpacklo_epi16(self, other);
                hi = _mm512_unpackhi_epi16(self, other);
            }
            else
            {
                return zip_hi(self, other, avx512f {});
            }
            return _mm512_inserti32x4(
                _mm512_inserti32x4(
                    _mm512_inserti32x4(hi, _mm512_extracti32x4_epi32(lo, 2), 0),
                    _mm512_extracti32x4_epi32(lo, 3),
                    2),
                _mm512_extracti32x4_epi32(hi, 2),
                1);
        }

        // zip_lo
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512bw>) noexcept
        {
            __m512i lo, hi;
            if constexpr (sizeof(T) == 1)
            {
                lo = _mm512_unpacklo_epi8(self, other);
                hi = _mm512_unpackhi_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                lo = _mm512_unpacklo_epi16(self, other);
                hi = _mm512_unpackhi_epi16(self, other);
            }
            else
            {
                return zip_lo(self, other, avx512f {});
            }
            return _mm512_inserti32x4(
                _mm512_inserti32x4(
                    _mm512_inserti32x4(lo, _mm512_extracti32x4_epi32(hi, 0), 1),
                    _mm512_extracti32x4_epi32(hi, 1),
                    3),
                _mm512_extracti32x4_epi32(lo, 1),
                2);
        }
    }
}

#endif
