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

#ifndef XSIMD_SSE4_1_HPP
#define XSIMD_SSE4_1_HPP

#include "../types/xsimd_sse4_1_register.hpp"
#include "./common/xsimd_common_cast.hpp"

#include <type_traits>

namespace xsimd
{

    namespace kernel
    {
        using namespace types;
        // any
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE bool any(batch<T, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return !_mm_testz_si128(self, self);
        }
        // ceil
        template <class A>
        XSIMD_INLINE batch<float, A> ceil(batch<float, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_ceil_ps(self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> ceil(batch<double, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_ceil_pd(self);
        }

        // bitwise_lshift multiple (constant)
        template <class A, uint32_t... Vs>
        XSIMD_INLINE batch<uint32_t, A> bitwise_lshift(
            batch<uint32_t, A> const& self, batch_constant<uint32_t, A, Vs...>, requires_arch<sse4_1>) noexcept
        {
            constexpr auto mults = batch_constant<uint32_t, A, static_cast<uint32_t>(1u << Vs)...>();
            return _mm_mullo_epi32(self, mults.as_batch());
        }

        // fast_cast
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<int64_t, A> const& x, batch<double, A> const&, requires_arch<sse4_1>) noexcept
            {
                // from https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
                __m128i xH = _mm_srai_epi32(x, 16);
                xH = _mm_blend_epi16(xH, _mm_setzero_si128(), 0x33);
                xH = _mm_add_epi64(xH, _mm_castpd_si128(_mm_set1_pd(442721857769029238784.))); //  3*2^67
                __m128i xL = _mm_blend_epi16(x, _mm_castpd_si128(_mm_set1_pd(0x0010000000000000)), 0x88); //  2^52
                __m128d f = _mm_sub_pd(_mm_castsi128_pd(xH), _mm_set1_pd(442726361368656609280.)); //  3*2^67 + 2^52
                detail::reassociation_barrier(f, "prevent (xH-C)+xL -> xH+(xL-C)");
                return _mm_add_pd(f, _mm_castsi128_pd(xL));
            }

            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<uint64_t, A> const& x, batch<double, A> const&, requires_arch<sse4_1>) noexcept
            {
                // from https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
                __m128i xH = _mm_srli_epi64(x, 32);
                xH = _mm_or_si128(xH, _mm_castpd_si128(_mm_set1_pd(19342813113834066795298816.))); //  2^84
                __m128i xL = _mm_blend_epi16(x, _mm_castpd_si128(_mm_set1_pd(0x0010000000000000)), 0xcc); //  2^52
                __m128d f = _mm_sub_pd(_mm_castsi128_pd(xH), _mm_set1_pd(19342813118337666422669312.)); //  2^84 + 2^52
                detail::reassociation_barrier(f, "prevent (xH-C)+xL -> xH+(xL-C)");
                return _mm_add_pd(f, _mm_castsi128_pd(xL));
            }
        }

        // eq
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse4_1>) noexcept
        {
            if constexpr (sizeof(T) == 8)
            {
                return _mm_cmpeq_epi64(self, other);
            }
            else
            {
                return eq(self, other, ssse3 {});
            }
        }

        // floor
        template <class A>
        XSIMD_INLINE batch<float, A> floor(batch<float, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_floor_ps(self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> floor(batch<double, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_floor_pd(self);
        }

        // get
        template <class A, size_t I, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<sse4_1>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, sse2 {});
            }
            else if constexpr (sizeof(T) == 1)
            {
                return static_cast<T>(_mm_extract_epi8(self, I));
            }
            else if constexpr (sizeof(T) == 2)
            {
                return static_cast<T>(_mm_extract_epi16(self, I));
            }
            else if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_mm_extract_epi32(self, I));
            }
            else if constexpr (sizeof(T) == 8)
            {
#if defined(__x86_64__)
                return static_cast<T>(_mm_extract_epi64(self, I));
#else
                return get(self, ::xsimd::index<I> {}, sse2 {});
#endif
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // insert
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I> pos, requires_arch<sse4_1>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_insert_epi8(self, val, I);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_insert_epi32(self, val, I);
            }
            else if constexpr (sizeof(T) == 8)
            {
#if (!defined(_MSC_VER) && __x86_64__) || (_MSC_VER > 1900 && defined(_M_X64))
                return _mm_insert_epi64(self, val, I);
#else
                uint32_t lo, hi;
                memcpy(&lo, (reinterpret_cast<uint32_t*>(&val)), sizeof(lo));
                memcpy(&hi, (reinterpret_cast<uint32_t*>(&val)) + 1, sizeof(hi));
                return _mm_insert_epi32(_mm_insert_epi32(self, lo, 2 * I), hi, 2 * I + 1);
#endif
            }
            else
            {
                return insert(self, val, pos, ssse3 {});
            }
        }

        // load_unaligned<batch_bool>

        template <class A, class T, class = std::enable_if_t<(std::is_integral_v<T> && sizeof(T) > 1)>>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<sse4_1>) noexcept
        {
            // GCC <12 have missing or buggy unaligned load intrinsics; use memcpy to work around this.
            // GCC/Clang/MSVC will turn it into the correct load.
            if constexpr (sizeof(T) == 2)
            {
#if defined(__x86_64__)
                uint64_t tmp;
                memcpy(&tmp, mem, sizeof(tmp));
                auto val = _mm_cvtsi64_si128(tmp);
#else
                __m128i val;
                memcpy(&val, mem, sizeof(uint64_t));
#endif
                return { _mm_sub_epi16(_mm_set1_epi8(0), _mm_cvtepu8_epi16(val)) };
            }
            else if constexpr (sizeof(T) == 4)
            {
                uint32_t tmp;
                memcpy(&tmp, mem, sizeof(tmp));
                return { _mm_sub_epi32(_mm_set1_epi8(0), _mm_cvtepu8_epi32(_mm_cvtsi32_si128(tmp))) };
            }
            else if constexpr (sizeof(T) == 8)
            {
                uint16_t tmp;
                memcpy(&tmp, mem, sizeof(tmp));
                return { _mm_sub_epi64(_mm_set1_epi8(0), _mm_cvtepu8_epi64(_mm_cvtsi32_si128((uint32_t)tmp))) };
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return __m128i {};
            }
        }

        template <class A>
        XSIMD_INLINE batch_bool<float, A> load_unaligned(bool const* mem, batch_bool<float, A>, requires_arch<sse4_1> r) noexcept
        {
            return { _mm_castsi128_ps(load_unaligned(mem, batch_bool<uint32_t, A> {}, r)) };
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> load_unaligned(bool const* mem, batch_bool<double, A>, requires_arch<sse4_1> r) noexcept
        {
            return { _mm_castsi128_pd(load_unaligned(mem, batch_bool<uint64_t, A> {}, r)) };
        }

        // max
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse4_1>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_max_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_max_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_max_epi32(self, other);
                }
                else
                {
                    return max(self, other, ssse3 {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_max_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_max_epu16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_max_epu32(self, other);
                }
                else
                {
                    return max(self, other, ssse3 {});
                }
            }
        }

        // load_stream
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE batch<T, A> load_stream(T const* mem, convert<T>, requires_arch<sse4_1>) noexcept
        {
            return _mm_stream_load_si128((__m128i*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_stream(float const* mem, convert<float>, requires_arch<sse4_1>) noexcept
        {
            return _mm_castsi128_ps(_mm_stream_load_si128((__m128i*)mem));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_stream(double const* mem, convert<double>, requires_arch<sse4_1>) noexcept
        {
            return _mm_castsi128_pd(_mm_stream_load_si128((__m128i*)mem));
        }

        // min
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse4_1>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_min_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_min_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_min_epi32(self, other);
                }
                else
                {
                    return min(self, other, ssse3 {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_min_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_min_epu16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_min_epu32(self, other);
                }
                else
                {
                    return min(self, other, ssse3 {});
                }
            }
        }

        // mul
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse4_1>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_or_si128(
                    _mm_and_si128(_mm_mullo_epi16(self, other), _mm_srli_epi16(_mm_cmpeq_epi8(self, self), 8)),
                    _mm_slli_epi16(_mm_mullo_epi16(_mm_srli_epi16(self, 8), _mm_srli_epi16(other, 8)), 8));
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_mullo_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_mullo_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_add_epi64(
                    _mm_mul_epu32(self, other),
                    _mm_slli_epi64(
                        _mm_add_epi64(
                            _mm_mul_epu32(other, _mm_shuffle_epi32(self, _MM_SHUFFLE(2, 3, 0, 1))),
                            _mm_mul_epu32(self, _mm_shuffle_epi32(other, _MM_SHUFFLE(2, 3, 0, 1)))),
                        32));
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // mul_hi
        template <class A>
        XSIMD_INLINE batch<int32_t, A> mul_hi(batch<int32_t, A> const& self, batch<int32_t, A> const& other, requires_arch<sse4_1>) noexcept
        {
            __m128i even = _mm_mul_epi32(self, other); // 64-bit products in lanes 0,2
            __m128i odd = _mm_mul_epi32(_mm_srli_epi64(self, 32), _mm_srli_epi64(other, 32));
            // hi halves in the low 32 of each 64 lane of (even>>32), and in the high 32 of odd
            __m128i even_hi = _mm_srli_epi64(even, 32);
            // blend: 32-bit lanes {even_hi[0], odd_hi[1], even_hi[2], odd_hi[3]}
            return _mm_blend_epi16(even_hi, odd, 0xCC);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> mul_hi(batch<uint32_t, A> const& self, batch<uint32_t, A> const& other, requires_arch<sse4_1>) noexcept
        {
            __m128i even = _mm_mul_epu32(self, other);
            __m128i odd = _mm_mul_epu32(_mm_srli_epi64(self, 32), _mm_srli_epi64(other, 32));
            __m128i even_hi = _mm_srli_epi64(even, 32);
            return _mm_blend_epi16(even_hi, odd, 0xCC);
        }

        template <class A>
        XSIMD_INLINE batch<uint64_t, A> mul_hi(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<sse4_1>) noexcept
        {
            return detail::mulhi_u64_core<A>(self, other,
                                             [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                             { return batch<uint64_t, A>(_mm_mul_epu32(a, b)); });
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> mul_hi(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<sse4_1>) noexcept
        {
            return detail::mulhi_i64_core<A>(self, other,
                                             [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                             { return batch<uint64_t, A>(_mm_mul_epu32(a, b)); });
        }

        // mul_hilo
        template <class A>
        XSIMD_INLINE std::pair<batch<uint64_t, A>, batch<uint64_t, A>>
        mul_hilo(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<sse4_1>) noexcept
        {
            return detail::mulhilo_u64_core<A>(self, other,
                                               [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                               { return batch<uint64_t, A>(_mm_mul_epu32(a, b)); });
        }

        // nearbyint
        template <class A>
        XSIMD_INLINE batch<float, A> nearbyint(batch<float, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_round_ps(self, _MM_FROUND_TO_NEAREST_INT);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> nearbyint(batch<double, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_round_pd(self, _MM_FROUND_TO_NEAREST_INT);
        }

        // select
        namespace detail
        {
            template <class T>
            XSIMD_INLINE constexpr T interleave(T const& cond) noexcept
            {
                return (((cond * 0x0101010101010101ULL & 0x8040201008040201ULL) * 0x0102040810204081ULL >> 49) & 0x5555) | (((cond * 0x0101010101010101ULL & 0x8040201008040201ULL) * 0x0102040810204081ULL >> 48) & 0xAAAA);
            }
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<sse4_1>) noexcept
        {
            return _mm_blendv_epi8(false_br, true_br, cond);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> select(batch_bool<float, A> const& cond, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<sse4_1>) noexcept
        {
            return _mm_blendv_ps(false_br, true_br, cond);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> select(batch_bool<double, A> const& cond, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<sse4_1>) noexcept
        {
            return _mm_blendv_pd(false_br, true_br, cond);
        }

        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<sse4_1>) noexcept
        {
            constexpr int mask = batch_bool_constant<T, A, Values...>::mask();
            if constexpr (sizeof(T) == 2)
            {
                return _mm_blend_epi16(false_br, true_br, mask);
            }
            else if constexpr (sizeof(T) == 4)
            {
                constexpr int imask = detail::interleave(mask);
                return _mm_blend_epi16(false_br, true_br, imask);
            }
            else if constexpr (sizeof(T) == 8)
            {
                constexpr int imask = detail::interleave(mask);
                constexpr int imask2 = detail::interleave(imask);
                return _mm_blend_epi16(false_br, true_br, imask2);
            }
            else
            {
                return select(batch_bool_constant<T, A, Values...>(), true_br, false_br, ssse3 {});
            }
        }
        template <class A, bool... Values>
        XSIMD_INLINE batch<float, A> select(batch_bool_constant<float, A, Values...> const&, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<sse4_1>) noexcept
        {
            constexpr int mask = batch_bool_constant<float, A, Values...>::mask();
            return _mm_blend_ps(false_br, true_br, mask);
        }
        template <class A, bool... Values>
        XSIMD_INLINE batch<double, A> select(batch_bool_constant<double, A, Values...> const&, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<sse4_1>) noexcept
        {
            constexpr int mask = batch_bool_constant<double, A, Values...>::mask();
            return _mm_blend_pd(false_br, true_br, mask);
        }

        // trunc
        template <class A>
        XSIMD_INLINE batch<float, A> trunc(batch<float, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_round_ps(self, _MM_FROUND_TO_ZERO);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> trunc(batch<double, A> const& self, requires_arch<sse4_1>) noexcept
        {
            return _mm_round_pd(self, _MM_FROUND_TO_ZERO);
        }

        // widen
        template <class A, class T>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<sse4_1>) noexcept
        {
            __m128i x_lo = x;
            __m128i x_hi = _mm_unpackhi_epi64(x, x);
            __m128i lo, hi;
            if constexpr (sizeof(T) == 4)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm_cvtepi32_epi64(x_lo);
                    hi = _mm_cvtepi32_epi64(x_hi);
                }
                else
                {
                    lo = _mm_cvtepu32_epi64(x_lo);
                    hi = _mm_cvtepu32_epi64(x_hi);
                }
            }
            else if constexpr (sizeof(T) == 2)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm_cvtepi16_epi32(x_lo);
                    hi = _mm_cvtepi16_epi32(x_hi);
                }
                else
                {
                    lo = _mm_cvtepu16_epi32(x_lo);
                    hi = _mm_cvtepu16_epi32(x_hi);
                }
            }
            else if constexpr (sizeof(T) == 1)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm_cvtepi8_epi16(x_lo);
                    hi = _mm_cvtepi8_epi16(x_hi);
                }
                else
                {
                    lo = _mm_cvtepu8_epi16(x_lo);
                    hi = _mm_cvtepu8_epi16(x_hi);
                }
            }
            return { lo, hi };
        }
        template <class A>
        XSIMD_INLINE std::array<batch<double, A>, 2> widen(batch<float, A> const& x, requires_arch<sse4_1>) noexcept
        {
            __m128 x_shuf = _mm_unpackhi_ps(x, x);
            __m128d lo = _mm_cvtps_pd(x);
            __m128d hi = _mm_cvtps_pd(x_shuf);
            return { lo, hi };
        }

    }

}

#endif
