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

#ifndef XSIMD_SSE2_HPP
#define XSIMD_SSE2_HPP

#include "../types/xsimd_batch_constant.hpp"
#include "../types/xsimd_sse2_register.hpp"
#include "./utils/shifts.hpp"

#include <complex>
#include <limits>
#include <type_traits>

namespace xsimd
{
    template <typename T, class A, bool... Values>
    struct batch_bool_constant;

    template <class T_out, class T_in, class A>
    XSIMD_INLINE batch<T_out, A> bitwise_cast(batch<T_in, A> const& x) noexcept;

    template <typename T, class A, T... Values>
    struct batch_constant;

    namespace kernel
    {
        using namespace types;

        namespace detail
        {
            constexpr uint32_t shuffle(uint32_t w, uint32_t x, uint32_t y, uint32_t z)
            {
                return (z << 6) | (y << 4) | (x << 2) | w;
            }
            constexpr uint32_t shuffle(uint32_t x, uint32_t y)
            {
                return (y << 1) | x;
            }

            constexpr uint32_t mod_shuffle(uint32_t w, uint32_t x, uint32_t y, uint32_t z)
            {
                return shuffle(w % 4, x % 4, y % 4, z % 4);
            }

            constexpr uint32_t mod_shuffle(uint32_t w, uint32_t x)
            {
                return shuffle(w % 2, x % 2);
            }
        }

        // fwd
        template <class A, class T, size_t I>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<common>) noexcept;
        template <class A, typename T, typename ITy, ITy... Indices>
        XSIMD_INLINE batch<T, A> shuffle(batch<T, A> const& x, batch<T, A> const& y, batch_constant<ITy, A, Indices...>, requires_arch<common>) noexcept;
        template <class A, class T>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const&, batch<T, A> const&, requires_arch<common>) noexcept;
        template <class A, class T>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const&, batch<T, A> const&, requires_arch<common>) noexcept;

        // abs
        template <class A>
        XSIMD_INLINE batch<double, A> abs(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            __m128d sign_mask = _mm_set1_pd(-0.f); // -0.f = 1 << 31
            return _mm_andnot_pd(sign_mask, self);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> abs(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            __m128 sign_mask = _mm_set1_ps(-0.f); // -0.f = 1 << 31
            return _mm_andnot_ps(sign_mask, self);
        }

        // add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_add_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_add_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_add_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_add_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        template <class A>
        XSIMD_INLINE batch<float, A> add(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_add_ps(self, other);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> add(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_add_pd(self, other);
        }

        // all
        template <class A>
        XSIMD_INLINE bool all(batch_bool<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_ps(self) == 0x0F;
        }
        template <class A>
        XSIMD_INLINE bool all(batch_bool<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_pd(self) == 0x03;
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE bool all(batch_bool<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_epi8(self) == 0xFFFF;
        }

        // any
        template <class A>
        XSIMD_INLINE bool any(batch_bool<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_ps(self) != 0;
        }
        template <class A>
        XSIMD_INLINE bool any(batch_bool<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_pd(self) != 0;
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE bool any(batch_bool<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_epi8(self) != 0;
        }

        // avgr
        template <class A, class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_avg_epu8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_avg_epu16(self, other);
            }
            else
            {
                return avgr(self, other, common {});
            }
        }

        // avg
        template <class A, class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
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

        // batch_bool_cast
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<sse2>) noexcept
        {
            return { bitwise_cast<T_out>(batch<T_in, A>(self.data)).data };
        }

        // bitwise_and
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_and(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_and_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_and(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_and_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_and_si128(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_and_si128(self, other);
        }

        template <class A>
        batch<double, A> XSIMD_INLINE bitwise_and(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_and_pd(self, other);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_and(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_and_pd(self, other);
        }

        // bitwise_andnot
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_andnot(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_andnot_ps(other, self);
        }

        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_andnot(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_andnot_ps(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_andnot_si128(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_andnot_si128(other, self);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_andnot(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_andnot_pd(other, self);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_andnot(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_andnot_pd(other, self);
        }

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_and_si128(_mm_set1_epi8(0xFF << other), _mm_slli_epi32(self, other));
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_slli_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_slli_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_slli_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits, "Count must be less than the number of bits in T");
            if constexpr (shift == 0)
            {
                return self;
            }
            else if constexpr (sizeof(T) == 1)
            {
                // 8-bit left shift via 16-bit shift + mask
                __m128i shifted = _mm_slli_epi16(self, static_cast<int>(shift));
                // TODO(C++17): without `if constexpr ` we must ensure the compile-time shift does not overflow
                constexpr uint8_t mask8 = static_cast<uint8_t>(sizeof(T) == 1 ? (~0u << shift) : 0);
                const __m128i mask = _mm_set1_epi8(mask8);
                return _mm_and_si128(shifted, mask);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_slli_epi16(self, static_cast<int>(shift));
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_slli_epi32(self, static_cast<int>(shift));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_slli_epi64(self, static_cast<int>(shift));
            }
            return bitwise_lshift<shift>(self, common {});
        }

        // bitwise_lshift multiple (constant)
        // Missing implementations are dispacthed to the `batch` overload in xsimd_api.
        template <class A, class T, T... Vs, detail::enable_sized_integral_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(
            batch<T, A> const& self, batch_constant<T, A, Vs...> shifts, requires_arch<sse2> req) noexcept
        {
            if constexpr (utils::all_equals(shifts))
            {
                return bitwise_lshift<shifts.get(0), A>(self, req);
            }
            constexpr auto mults = batch_constant<T, A, static_cast<T>(1u << Vs)...>();
            return _mm_mullo_epi16(self, mults.as_batch());
        }

        template <class A, class T, T... Vs, detail::enable_sized_integral_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(
            batch<T, A> const& self, batch_constant<T, A, Vs...> shifts, requires_arch<sse2> req) noexcept
        {
            using uint_t = std::make_unsigned_t<T>;

            if constexpr (utils::all_equals(shifts))
            {
                return bitwise_lshift<shifts.get(0), A>(self, req);
            }
            return bitwise_cast<T>(
                utils::bitwise_lshift_as_twice_larger<uint_t>(
                    bitwise_cast<uint_t>(self),
                    batch_constant<uint_t, A, static_cast<uint_t>(Vs)...> {}));
        }

        // bitwise_not
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_not(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_ps(self, _mm_castsi128_ps(_mm_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_not(batch_bool<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_ps(self, _mm_castsi128_ps(_mm_set1_epi32(-1)));
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_si128(self, _mm_set1_epi32(-1));
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_si128(self, _mm_set1_epi32(-1));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_not(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_pd(self, _mm_castsi128_pd(_mm_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_not(batch_bool<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_pd(self, _mm_castsi128_pd(_mm_set1_epi32(-1)));
        }

        // bitwise_or
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_or(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_or_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_or(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_or_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_or_si128(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_or_si128(self, other);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_or(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_or_pd(self, other);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_or(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_or_pd(self, other);
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<sse2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    __m128i sign_mask = _mm_set1_epi16((0xFF00 >> other) & 0x00FF);
                    __m128i cmp_is_negative = _mm_cmpgt_epi8(_mm_setzero_si128(), self);
                    __m128i res = _mm_srai_epi16(self, other);
                    return _mm_or_si128(_mm_and_si128(sign_mask, cmp_is_negative), _mm_andnot_si128(sign_mask, res));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_srai_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_srai_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    // from https://github.com/samyvilar/vect/blob/master/vect_128.h
                    return _mm_or_si128(
                        _mm_srli_epi64(self, other),
                        _mm_slli_epi64(
                            _mm_srai_epi32(_mm_shuffle_epi32(self, _MM_SHUFFLE(3, 3, 1, 1)), 32),
                            64 - other));
                }
                else
                {
                    assert(false && "unsupported arch/op combination");
                    return {};
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_and_si128(_mm_set1_epi8(0xFF >> other), _mm_srli_epi32(self, other));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_srli_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_srli_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm_srli_epi64(self, other);
                }
                else
                {
                    assert(false && "unsupported arch/op combination");
                    return {};
                }
            }
        }
        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits,
                          "Shift must be less than the number of value bits in the type");

            if constexpr (shift == 0)
            {
                return self;
            }

            if constexpr (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    // 8-bit arithmetic right shift via 16-bit shift + sign-extension handling.
                    __m128i shifted = _mm_srai_epi16(self, static_cast<int>(shift));
                    __m128i sign_mask = _mm_set1_epi16(static_cast<short>(0xFF00 >> shift));
                    __m128i cmp_negative = _mm_cmpgt_epi8(_mm_setzero_si128(), self);
                    return _mm_or_si128(_mm_and_si128(sign_mask, cmp_negative),
                                        _mm_andnot_si128(sign_mask, shifted));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_srai_epi16(self, static_cast<int>(shift));
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_srai_epi32(self, static_cast<int>(shift));
                }
                // No 64-bit arithmetic right shift in SSE2; fall back
                return bitwise_rshift<shift>(self, common {});
            }
            else // unsigned / logical right shift
            {
                if constexpr (sizeof(T) == 1)
                {
                    // 8-bit left shift via 16-bit shift + mask
                    __m128i shifted = _mm_srli_epi16(self, static_cast<int>(shift));
                    // TODO(C++17): without `if constexpr ` we must ensure the compile-time shift does not overflow
                    constexpr uint8_t mask8 = static_cast<uint8_t>(sizeof(T) == 1 ? ((1u << shift) - 1u) : 0);
                    const __m128i mask = _mm_set1_epi8(mask8);
                    return _mm_and_si128(shifted, mask);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_srli_epi16(self, static_cast<int>(shift));
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_srli_epi32(self, static_cast<int>(shift));
                }
                else // sizeof(T) == 8
                {
                    return _mm_srli_epi64(self, static_cast<int>(shift));
                }
            }
        }

        // bitwise_xor
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_xor(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> bitwise_xor(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_si128(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_xor(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_pd(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_xor(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_pd(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_si128(self, other);
        }

        // bitwise_cast
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<T, A> const& self, batch<float, A> const&, requires_arch<sse2>) noexcept
        {
            return _mm_castsi128_ps(self);
        }
        template <class A, class T, class Tp, class = std::enable_if_t<std::is_integral_v<std::common_type_t<T, Tp>>>>
        XSIMD_INLINE batch<Tp, A> bitwise_cast(batch<T, A> const& self, batch<Tp, A> const&, requires_arch<sse2>) noexcept
        {
            return batch<Tp, A>(self.data);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_cast(batch<float, A> const& self, batch<T, A> const&, requires_arch<sse2>) noexcept
        {
            return _mm_castps_si128(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<T, A> const& self, batch<double, A> const&, requires_arch<sse2>) noexcept
        {
            return _mm_castsi128_pd(self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<float, A> const& self, batch<double, A> const&, requires_arch<sse2>) noexcept
        {
            return _mm_castps_pd(self);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<double, A> const& self, batch<float, A> const&, requires_arch<sse2>) noexcept
        {
            return _mm_castpd_ps(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_cast(batch<double, A> const& self, batch<T, A> const&, requires_arch<sse2>) noexcept
        {
            return _mm_castpd_si128(self);
        }

        // broadcast
        template <class A>
        batch<float, A> XSIMD_INLINE broadcast(float val, requires_arch<sse2>) noexcept
        {
            return _mm_set1_ps(val);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_set1_epi8(val);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_set1_epi16(val);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_set1_epi32(val);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_set1_epi64x(val);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<double, A> broadcast(double val, requires_arch<sse2>) noexcept
        {
            return _mm_set1_pd(val);
        }

        // store_complex
        namespace detail
        {
            // Override these methods in SSE-based archs, no need to override store_aligned / store_unaligned
            // complex_low
            template <class A>
            XSIMD_INLINE batch<float, A> complex_low(batch<std::complex<float>, A> const& self, requires_arch<sse2>) noexcept
            {
                return _mm_unpacklo_ps(self.real(), self.imag());
            }
            // complex_high
            template <class A>
            XSIMD_INLINE batch<float, A> complex_high(batch<std::complex<float>, A> const& self, requires_arch<sse2>) noexcept
            {
                return _mm_unpackhi_ps(self.real(), self.imag());
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_low(batch<std::complex<double>, A> const& self, requires_arch<sse2>) noexcept
            {
                return _mm_unpacklo_pd(self.real(), self.imag());
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_high(batch<std::complex<double>, A> const& self, requires_arch<sse2>) noexcept
            {
                return _mm_unpackhi_pd(self.real(), self.imag());
            }
        }

        // decr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<sse2>) noexcept
        {
            return self + batch<T, A>(mask.data);
        }

        // div
        template <class A>
        XSIMD_INLINE batch<float, A> div(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_div_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> div(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_div_pd(self, other);
        }

        // fast_cast
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<int32_t, A> const& self, batch<float, A> const&, requires_arch<sse2>) noexcept
            {
                return _mm_cvtepi32_ps(self);
            }

            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<uint64_t, A> const& x, batch<double, A> const&, requires_arch<sse2>) noexcept
            {
                // from https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
                // adapted to sse2
                __m128i xH = _mm_srli_epi64(x, 32);
                xH = _mm_or_si128(xH, _mm_castpd_si128(_mm_set1_pd(19342813113834066795298816.))); //  2^84
                __m128i mask = _mm_setr_epi16(0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000);
                __m128i xL = _mm_or_si128(_mm_and_si128(mask, x), _mm_andnot_si128(mask, _mm_castpd_si128(_mm_set1_pd(0x0010000000000000)))); //  2^52
                __m128d f = _mm_sub_pd(_mm_castsi128_pd(xH), _mm_set1_pd(19342813118337666422669312.)); //  2^84 + 2^52
                detail::reassociation_barrier(f, "prevent (xH-C)+xL -> xH+(xL-C)");
                return _mm_add_pd(f, _mm_castsi128_pd(xL));
            }

            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<int64_t, A> const& x, batch<double, A> const&, requires_arch<sse2>) noexcept
            {
                // from https://stackoverflow.com/questions/41144668/how-to-efficiently-perform-double-int64-conversions-with-sse-avx
                // adapted to sse2
                __m128i xH = _mm_srai_epi32(x, 16);
                xH = _mm_and_si128(xH, _mm_setr_epi16(0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF));
                xH = _mm_add_epi64(xH, _mm_castpd_si128(_mm_set1_pd(442721857769029238784.))); //  3*2^67
                __m128i mask = _mm_setr_epi16(0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000);
                __m128i xL = _mm_or_si128(_mm_and_si128(mask, x), _mm_andnot_si128(mask, _mm_castpd_si128(_mm_set1_pd(0x0010000000000000)))); //  2^52
                __m128d f = _mm_sub_pd(_mm_castsi128_pd(xH), _mm_set1_pd(442726361368656609280.)); //  3*2^67 + 2^52
                detail::reassociation_barrier(f, "prevent (xH-C)+xL -> xH+(xL-C)");
                return _mm_add_pd(f, _mm_castsi128_pd(xL));
            }

            template <class A>
            XSIMD_INLINE batch<int32_t, A> fast_cast(batch<float, A> const& self, batch<int32_t, A> const&, requires_arch<sse2>) noexcept
            {
                return _mm_cvttps_epi32(self);
            }
        }

        // eq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> eq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpeq_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> eq(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(self), _mm_castps_si128(other)));
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_cmpeq_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_cmpeq_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_cmpeq_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                __m128i tmp1 = _mm_cmpeq_epi32(self, other);
                __m128i tmp2 = _mm_shuffle_epi32(tmp1, 0xB1);
                __m128i tmp3 = _mm_and_si128(tmp1, tmp2);
                __m128i tmp4 = _mm_srai_epi32(tmp3, 31);
                return _mm_shuffle_epi32(tmp4, 0xF5);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return ~(self != other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpeq_pd(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_castsi128_pd(_mm_cmpeq_epi32(_mm_castpd_si128(self), _mm_castpd_si128(other)));
        }

        // first
        template <class A>
        XSIMD_INLINE float first(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_cvtss_f32(self);
        }

        template <class A>
        XSIMD_INLINE double first(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_cvtsd_f64(self);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return static_cast<T>(_mm_cvtsi128_si32(self) & 0xFF);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return static_cast<T>(_mm_cvtsi128_si32(self) & 0xFFFF);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_mm_cvtsi128_si32(self));
            }
            else if constexpr (sizeof(T) == 8)
            {
#if defined(__x86_64__)
                return static_cast<T>(_mm_cvtsi128_si64(self));
#else
                __m128i m;
                _mm_storel_epi64(&m, self);
                int64_t i;
                std::memcpy(&i, &m, sizeof(i));
                return i;
#endif
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // from_mask
        template <class A>
        XSIMD_INLINE batch_bool<float, A> from_mask(batch_bool<float, A> const&, uint64_t mask, requires_arch<sse2>) noexcept
        {
            alignas(A::alignment()) static const uint32_t lut[][4] = {
                { 0x00000000, 0x00000000, 0x00000000, 0x00000000 },
                { 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 },
                { 0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000 },
                { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 },
                { 0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000 },
                { 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000 },
                { 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 },
                { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 },
                { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF },
                { 0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF },
                { 0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF },
                { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF },
                { 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF },
                { 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF },
                { 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
                { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
            };
            assert(!(mask & ~0xFul) && "inbound mask");
            return _mm_castsi128_ps(_mm_load_si128((const __m128i*)lut[mask]));
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> from_mask(batch_bool<double, A> const&, uint64_t mask, requires_arch<sse2>) noexcept
        {
            alignas(A::alignment()) static const uint64_t lut[][4] = {
                { 0x0000000000000000ul, 0x0000000000000000ul },
                { 0xFFFFFFFFFFFFFFFFul, 0x0000000000000000ul },
                { 0x0000000000000000ul, 0xFFFFFFFFFFFFFFFFul },
                { 0xFFFFFFFFFFFFFFFFul, 0xFFFFFFFFFFFFFFFFul },
            };
            assert(!(mask & ~0x3ul) && "inbound mask");
            return _mm_castsi128_pd(_mm_load_si128((const __m128i*)lut[mask]));
        }
        template <class T, class A, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> from_mask(batch_bool<T, A> const&, uint64_t mask, requires_arch<sse2>) noexcept
        {
            alignas(A::alignment()) static const uint64_t lut64[] = {
                0x0000000000000000,
                0x000000000000FFFF,
                0x00000000FFFF0000,
                0x00000000FFFFFFFF,
                0x0000FFFF00000000,
                0x0000FFFF0000FFFF,
                0x0000FFFFFFFF0000,
                0x0000FFFFFFFFFFFF,
                0xFFFF000000000000,
                0xFFFF00000000FFFF,
                0xFFFF0000FFFF0000,
                0xFFFF0000FFFFFFFF,
                0xFFFFFFFF00000000,
                0xFFFFFFFF0000FFFF,
                0xFFFFFFFFFFFF0000,
                0xFFFFFFFFFFFFFFFF,
            };
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
            if constexpr (sizeof(T) == 1)
            {
                assert(!(mask & ~0xFFFF) && "inbound mask");
                return _mm_setr_epi32(lut32[mask & 0xF], lut32[(mask >> 4) & 0xF], lut32[(mask >> 8) & 0xF], lut32[mask >> 12]);
            }
            else if constexpr (sizeof(T) == 2)
            {
                assert(!(mask & ~0xFF) && "inbound mask");
                return _mm_set_epi64x(lut64[mask >> 4], lut64[mask & 0xF]);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_castps_si128(from_mask(batch_bool<float, A> {}, mask, sse2 {}));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_castpd_si128(from_mask(batch_bool<double, A> {}, mask, sse2 {}));
            }
        }

        // ge
        template <class A>
        XSIMD_INLINE batch_bool<float, A> ge(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpge_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> ge(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpge_pd(self, other);
        }

        // gt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> gt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpgt_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_cmpgt_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_cmpgt_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_cmpgt_epi32(self, other);
                }
                else
                {
                    return gt(self, other, common {});
                }
            }
            else
            {
                return gt(self, other, common {});
            }
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> gt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpgt_pd(self, other);
        }

        // haddp
        template <class A>
        XSIMD_INLINE batch<float, A> haddp(batch<float, A> const* row, requires_arch<sse2>) noexcept
        {
            __m128 tmp0 = _mm_unpacklo_ps(row[0], row[1]);
            __m128 tmp1 = _mm_unpackhi_ps(row[0], row[1]);
            __m128 tmp2 = _mm_unpackhi_ps(row[2], row[3]);
            tmp0 = _mm_add_ps(tmp0, tmp1);
            tmp1 = _mm_unpacklo_ps(row[2], row[3]);
            tmp1 = _mm_add_ps(tmp1, tmp2);
            tmp2 = _mm_movehl_ps(tmp1, tmp0);
            tmp0 = _mm_movelh_ps(tmp0, tmp1);
            return _mm_add_ps(tmp0, tmp2);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> haddp(batch<double, A> const* row, requires_arch<sse2>) noexcept
        {
            return _mm_add_pd(_mm_unpacklo_pd(row[0], row[1]),
                              _mm_unpackhi_pd(row[0], row[1]));
        }

        // incr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<sse2>) noexcept
        {
            return self - batch<T, A>(mask.data);
        }

        // insert
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I> pos, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 2)
            {
                return _mm_insert_epi16(self, val, I);
            }
            else
            {
                return insert(self, val, pos, common {});
            }
        }

        // isnan
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isnan(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_cmpunord_ps(self, self);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isnan(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_cmpunord_pd(self, self);
        }

        // load_aligned
        template <class A>
        XSIMD_INLINE batch<float, A> load_aligned(float const* mem, convert<float>, requires_arch<sse2>) noexcept
        {
            return _mm_load_ps(mem);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_aligned(T const* mem, convert<T>, requires_arch<sse2>) noexcept
        {
            return _mm_load_si128((__m128i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_aligned(double const* mem, convert<double>, requires_arch<sse2>) noexcept
        {
            return _mm_load_pd(mem);
        }

        // load_unaligned
        template <class A>
        XSIMD_INLINE batch<float, A> load_unaligned(float const* mem, convert<float>, requires_arch<sse2>) noexcept
        {
            return _mm_loadu_ps(mem);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* mem, convert<T>, requires_arch<sse2>) noexcept
        {
            return _mm_loadu_si128((__m128i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_unaligned(double const* mem, convert<double>, requires_arch<sse2>) noexcept
        {
            return _mm_loadu_pd(mem);
        }
        // load batch_bool

        template <class A>
        XSIMD_INLINE batch_bool<char, A> load_unaligned(bool const* mem, batch_bool<char, A>, requires_arch<sse2>) noexcept
        {
            return _mm_sub_epi8(_mm_set1_epi8(0), _mm_loadu_si128((__m128i const*)mem));
        }

        template <class A>
        XSIMD_INLINE batch_bool<unsigned char, A> load_unaligned(bool const* mem, batch_bool<unsigned char, A>, requires_arch<sse2> r) noexcept
        {
            return { load_unaligned(mem, batch_bool<char, A> {}, r).data };
        }

        template <class A>
        XSIMD_INLINE batch_bool<signed char, A> load_unaligned(bool const* mem, batch_bool<signed char, A>, requires_arch<sse2> r) noexcept
        {
            return { load_unaligned(mem, batch_bool<char, A> {}, r).data };
        }

        namespace detail
        {
            // Plain moves store-forward; vmaskmov never does on Intel and its
            // stores are microcoded on AMD. So wider archs delegate the masks
            // that lower to plain moves here rather than taking their runtime path.
            template <class T, class A, bool... Values>
            constexpr bool lowers_to_plain_moves(batch_bool_constant<T, A, Values...> mask) noexcept
            {
                return (mask.is_prefix() && mask.any() && !mask.all())
                    || mask.suffix() == mask.size / 2;
            }
        }

        // load_masked
        template <class A, class T, bool... Values, class Mode, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, Values...> mask, convert<T>, Mode, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 2 && mask.prefix() == 1)
            {
                return _mm_loadu_si16(mem);
            }
            else if constexpr (sizeof(T) == 4 && mask.prefix() == 1)
            {
                return _mm_loadu_si32(mem);
            }
            else if constexpr (sizeof(T) == 8 && mask.prefix() == 1)
            {
                return _mm_loadu_si64(mem);
            }
            else if constexpr (sizeof(T) == 2 && mask.prefix() == 2)
            {
                return _mm_loadu_si32(mem);
            }
            else if constexpr (sizeof(T) == 4 && mask.prefix() == 2)
            {
                return _mm_loadu_si64(mem);
            }
            else
            {
                return load_masked<A>(mem, mask, convert<T> {}, Mode {}, common {});
            }
        }
        template <class A, bool... Values, class Mode>
        XSIMD_INLINE batch<float, A> load_masked(float const* mem, batch_bool_constant<float, A, Values...> mask, convert<float>, Mode, requires_arch<sse2>) noexcept
        {
            if constexpr (mask.prefix() == 1)
            {
                return _mm_load_ss(mem);
            }
            else if constexpr (mask.prefix() == 2)
            {
                return _mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<__m64 const*>(mem));
            }
            else if constexpr (mask.suffix() == 2)
            {
                return _mm_loadh_pi(_mm_setzero_ps(), reinterpret_cast<__m64 const*>(mem + 2));
            }
            else if constexpr (mask.prefix() == 3)
            {
                __m128 const lo2 = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<__m128i const*>(mem)));
                return _mm_shuffle_ps(lo2, _mm_load_ss(mem + 2), _MM_SHUFFLE(3, 0, 1, 0));
            }
            else
            {
                return load_masked<A>(mem, mask, convert<float> {}, Mode {}, common {});
            }
        }
        template <class A, bool... Values, class Mode>
        XSIMD_INLINE batch<double, A> load_masked(double const* mem, batch_bool_constant<double, A, Values...> mask, convert<double>, Mode, requires_arch<sse2>) noexcept
        {
            if constexpr (mask.prefix() == 1)
            {
                return _mm_load_sd(mem);
            }
            else if constexpr (mask.suffix() == 1)
            {
                return _mm_loadh_pd(_mm_setzero_pd(), mem + 1);
            }
            else
            {
                return load_masked<A>(mem, mask, convert<double> {}, Mode {}, common {});
            }
        }

        // store_masked
        template <class A, bool... Values, class Mode>
        XSIMD_INLINE void store_masked(float* mem, batch<float, A> const& src, batch_bool_constant<float, A, Values...> mask, Mode, requires_arch<sse2>) noexcept
        {
            if constexpr (mask.prefix() == 1)
            {
                _mm_store_ss(mem, src);
            }
            else if constexpr (mask.prefix() == 2)
            {
                _mm_storel_pi(reinterpret_cast<__m64*>(mem), src);
            }
            else if constexpr (mask.suffix() == 2)
            {
                _mm_storeh_pi(reinterpret_cast<__m64*>(mem + 2), src);
            }
            else if constexpr (mask.prefix() == 3)
            {
                _mm_storel_pi(reinterpret_cast<__m64*>(mem), src);
                _mm_store_ss(mem + 2, _mm_movehl_ps(src, src));
            }
            else
            {
                store_masked<A>(mem, src, mask, Mode {}, common {});
            }
        }

        template <class A, bool... Values, class Mode>
        XSIMD_INLINE void store_masked(double* mem, batch<double, A> const& src, batch_bool_constant<double, A, Values...> mask, Mode, requires_arch<sse2>) noexcept
        {
            if constexpr (mask.prefix() == 1)
            {
                _mm_store_sd(mem, src);
            }
            else if constexpr (mask.suffix() == 1)
            {
                _mm_storeh_pd(mem + 1, src);
            }
            else
            {
                store_masked<A>(mem, src, mask, Mode {}, common {});
            }
        }

        // load_complex
        namespace detail
        {
            // Redefine these methods in the SSE-based archs if required
            template <class A>
            XSIMD_INLINE batch<std::complex<float>, A> load_complex(batch<float, A> const& hi, batch<float, A> const& lo, requires_arch<sse2>) noexcept
            {
                return { _mm_shuffle_ps(hi, lo, _MM_SHUFFLE(2, 0, 2, 0)), _mm_shuffle_ps(hi, lo, _MM_SHUFFLE(3, 1, 3, 1)) };
            }
            template <class A>
            XSIMD_INLINE batch<std::complex<double>, A> load_complex(batch<double, A> const& hi, batch<double, A> const& lo, requires_arch<sse2>) noexcept
            {
                return { _mm_shuffle_pd(hi, lo, _MM_SHUFFLE2(0, 0)), _mm_shuffle_pd(hi, lo, _MM_SHUFFLE2(1, 1)) };
            }
        }

        // le
        template <class A>
        XSIMD_INLINE batch_bool<float, A> le(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmple_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> le(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmple_pd(self, other);
        }

        // lt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> lt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmplt_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_cmplt_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_cmplt_epi16(self, other);
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_cmplt_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    __m128i tmp1 = _mm_sub_epi64(self, other);
                    __m128i tmp2 = _mm_xor_si128(self, other);
                    __m128i tmp3 = _mm_andnot_si128(other, self);
                    __m128i tmp4 = _mm_andnot_si128(tmp2, tmp1);
                    __m128i tmp5 = _mm_or_si128(tmp3, tmp4);
                    __m128i tmp6 = _mm_srai_epi32(tmp5, 31);
                    return _mm_shuffle_epi32(tmp6, 0xF5);
                }
                else
                {
                    assert(false && "unsupported arch/op combination");
                    return {};
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_cmplt_epi8(_mm_xor_si128(self, _mm_set1_epi8(std::numeric_limits<int8_t>::lowest())), _mm_xor_si128(other, _mm_set1_epi8(std::numeric_limits<int8_t>::lowest())));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_cmplt_epi16(_mm_xor_si128(self, _mm_set1_epi16(std::numeric_limits<int16_t>::lowest())), _mm_xor_si128(other, _mm_set1_epi16(std::numeric_limits<int16_t>::lowest())));
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm_cmplt_epi32(_mm_xor_si128(self, _mm_set1_epi32(std::numeric_limits<int32_t>::lowest())), _mm_xor_si128(other, _mm_set1_epi32(std::numeric_limits<int32_t>::lowest())));
                }
                else if constexpr (sizeof(T) == 8)
                {
                    auto xself = _mm_xor_si128(self, _mm_set1_epi64x(std::numeric_limits<int64_t>::lowest()));
                    auto xother = _mm_xor_si128(other, _mm_set1_epi64x(std::numeric_limits<int64_t>::lowest()));
                    __m128i tmp1 = _mm_sub_epi64(xself, xother);
                    __m128i tmp2 = _mm_xor_si128(xself, xother);
                    __m128i tmp3 = _mm_andnot_si128(xother, xself);
                    __m128i tmp4 = _mm_andnot_si128(tmp2, tmp1);
                    __m128i tmp5 = _mm_or_si128(tmp3, tmp4);
                    __m128i tmp6 = _mm_srai_epi32(tmp5, 31);
                    return _mm_shuffle_epi32(tmp6, 0xF5);
                }
                else
                {
                    assert(false && "unsupported arch/op combination");
                    return {};
                }
            }
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> lt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmplt_pd(self, other);
        }

        /* compression table to turn 0b10 into 0b1,
         * 0b100010 into 0b101 etc
         */
        namespace detail
        {
            XSIMD_INLINE int mask_lut(uint64_t mask)
            {
                // clang-format off
                static const int mask_lut[256] = {
                  0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x0, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x4, 0x0, 0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x6, 0x0, 0x7, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x8, 0x0, 0x9, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA, 0x0, 0xB, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0xC, 0x0, 0xD, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE, 0x0, 0xF, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                };
                // clang-format on
                return mask_lut[mask & 0xAA];
            }
        }

        // mask
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_movemask_epi8(self);
            }
            else if constexpr (sizeof(T) == 2)
            {
                uint64_t mask8 = _mm_movemask_epi8(self);
                return detail::mask_lut(mask8) | (detail::mask_lut(mask8 >> 8) << 4);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_movemask_ps(_mm_castsi128_ps(self));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_movemask_pd(_mm_castsi128_pd(self));
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE uint64_t mask(batch_bool<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_ps(self);
        }

        template <class A>
        XSIMD_INLINE uint64_t mask(batch_bool<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_movemask_pd(self);
        }

        // max
        template <class A>
        XSIMD_INLINE batch<float, A> max(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_max_ps(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return select(self > other, self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> max(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_max_pd(other, self);
        }

        // min
        template <class A>
        XSIMD_INLINE batch<float, A> min(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_min_ps(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return select(self <= other, self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> min(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_min_pd(other, self);
        }

        // mul
        template <class A>
        XSIMD_INLINE batch<float, A> mul(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_mul_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> mul(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_mul_pd(self, other);
        }

        // mul
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> mul(batch<uint8_t, A> const& self, batch<uint8_t, A> const& other, requires_arch<sse2>) noexcept
        {
            // Low byte of an N-bit*N-bit product is bitwise identical for
            // signed and unsigned operands. Split into even/odd bytes inside
            // each 16-bit lane, do two 16-bit mullos, then re-interleave.
            __m128i mask = _mm_set1_epi16(0x00FF);
            __m128i a_even = _mm_and_si128(self, mask);
            __m128i b_even = _mm_and_si128(other, mask);
            __m128i a_odd = _mm_srli_epi16(self, 8);
            __m128i b_odd = _mm_srli_epi16(other, 8);
            __m128i p_even = _mm_and_si128(_mm_mullo_epi16(a_even, b_even), mask);
            __m128i p_odd = _mm_slli_epi16(_mm_mullo_epi16(a_odd, b_odd), 8);
            return _mm_or_si128(p_even, p_odd);
        }
        template <class A>
        XSIMD_INLINE batch<int8_t, A> mul(batch<int8_t, A> const& self, batch<int8_t, A> const& other, requires_arch<sse2>) noexcept
        {
            return bitwise_cast<int8_t>(mul(bitwise_cast<uint8_t>(self), bitwise_cast<uint8_t>(other), sse2 {}));
        }
        template <class A>
        XSIMD_INLINE batch<int16_t, A> mul(batch<int16_t, A> const& self, batch<int16_t, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_mullo_epi16(self, other);
        }

        // mul_hi
        template <class A>
        XSIMD_INLINE batch<int8_t, A> mul_hi(batch<int8_t, A> const& self, batch<int8_t, A> const& other, requires_arch<sse2>) noexcept
        {
            // Sign-extend bytes to 16-bit (unpack-with-self followed by srai 8
            // duplicates the byte then arithmetic-shifts the sign in), do the
            // 16x16->16 multiply, then take the high byte of each product.
            __m128i a_lo = _mm_srai_epi16(_mm_unpacklo_epi8(self, self), 8);
            __m128i a_hi = _mm_srai_epi16(_mm_unpackhi_epi8(self, self), 8);
            __m128i b_lo = _mm_srai_epi16(_mm_unpacklo_epi8(other, other), 8);
            __m128i b_hi = _mm_srai_epi16(_mm_unpackhi_epi8(other, other), 8);
            __m128i p_lo = _mm_srai_epi16(_mm_mullo_epi16(a_lo, b_lo), 8);
            __m128i p_hi = _mm_srai_epi16(_mm_mullo_epi16(a_hi, b_hi), 8);
            // results already lie in [-128, 127], so packs is exact (no saturation kicks in).
            return _mm_packs_epi16(p_lo, p_hi);
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> mul_hi(batch<uint8_t, A> const& self, batch<uint8_t, A> const& other, requires_arch<sse2>) noexcept
        {
            __m128i zero = _mm_setzero_si128();
            __m128i a_lo = _mm_unpacklo_epi8(self, zero);
            __m128i a_hi = _mm_unpackhi_epi8(self, zero);
            __m128i b_lo = _mm_unpacklo_epi8(other, zero);
            __m128i b_hi = _mm_unpackhi_epi8(other, zero);
            __m128i p_lo = _mm_srli_epi16(_mm_mullo_epi16(a_lo, b_lo), 8);
            __m128i p_hi = _mm_srli_epi16(_mm_mullo_epi16(a_hi, b_hi), 8);
            return _mm_packus_epi16(p_lo, p_hi);
        }
        template <class A>
        XSIMD_INLINE batch<int16_t, A> mul_hi(batch<int16_t, A> const& self, batch<int16_t, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_mulhi_epi16(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> mul_hi(batch<uint16_t, A> const& self, batch<uint16_t, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_mulhi_epu16(self, other);
        }

        // nearbyint_as_int
        template <class A>
        XSIMD_INLINE batch<int32_t, A> nearbyint_as_int(batch<float, A> const& self,
                                                        requires_arch<sse2>) noexcept
        {
            return _mm_cvtps_epi32(self);
        }

        // neg
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return 0 - self;
        }
        template <class A>
        XSIMD_INLINE batch<float, A> neg(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_ps(self, _mm_castsi128_ps(_mm_set1_epi32(0x80000000)));
        }

        template <class A>
        XSIMD_INLINE batch<double, A> neg(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_xor_pd(
                self, _mm_castsi128_pd(_mm_setr_epi32(0, 0x80000000, 0, 0x80000000)));
        }

        // neq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> neq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpneq_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return ~(self == other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> neq(batch_bool<float, A> const& self, batch_bool<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_castps_si128(_mm_xor_ps(_mm_castsi128_ps(self.data), _mm_castsi128_ps(other.data)));
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_cmpneq_pd(self, other);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch_bool<double, A> const& self, batch_bool<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_xor_pd(self, other);
        }

        // reciprocal
        template <class A>
        XSIMD_INLINE batch<float, A> reciprocal(batch<float, A> const& self,
                                                kernel::requires_arch<sse2>)
        {
            return _mm_rcp_ps(self);
        }

        // reduce_add
        template <class A>
        XSIMD_INLINE float reduce_add(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            __m128 tmp0 = _mm_add_ps(self, _mm_movehl_ps(self, self));
            __m128 tmp1 = _mm_add_ss(tmp0, _mm_shuffle_ps(tmp0, tmp0, 1));
            return _mm_cvtss_f32(tmp1);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                __m128i tmp1 = _mm_shuffle_epi32(self, 0x0E);
                __m128i tmp2 = _mm_add_epi32(self, tmp1);
                __m128i tmp3 = _mm_shuffle_epi32(tmp2, 0x01);
                __m128i tmp4 = _mm_add_epi32(tmp2, tmp3);
                return _mm_cvtsi128_si32(tmp4);
            }
            else if constexpr (sizeof(T) == 8)
            {
                __m128i tmp1 = _mm_shuffle_epi32(self, 0x0E);
                __m128i tmp2 = _mm_add_epi64(self, tmp1);
#if defined(__x86_64__)
                return _mm_cvtsi128_si64(tmp2);
#else
                __m128i m;
                _mm_storel_epi64(&m, tmp2);
                int64_t i;
                std::memcpy(&i, &m, sizeof(i));
                return i;
#endif
            }
            else
            {
                return reduce_add(self, common {});
            }
        }

        template <class A>
        XSIMD_INLINE double reduce_add(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_cvtsd_f64(_mm_add_sd(self, _mm_unpackhi_pd(self, self)));
        }

        // reduce_max
        template <class A, class T, class = std::enable_if_t<(sizeof(T) <= 2)>>
        XSIMD_INLINE T reduce_max(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            constexpr auto mask0 = detail::shuffle(2, 3, 0, 0);
            batch<T, A> step0 = _mm_shuffle_epi32(self, mask0);
            batch<T, A> acc0 = max(self, step0);

            constexpr auto mask1 = detail::shuffle(1, 0, 0, 0);
            batch<T, A> step1 = _mm_shuffle_epi32(acc0, mask1);
            batch<T, A> acc1 = max(acc0, step1);

            constexpr auto mask2 = detail::shuffle(1, 0, 0, 0);
            batch<T, A> step2 = _mm_shufflelo_epi16(acc1, mask2);
            batch<T, A> acc2 = max(acc1, step2);
            if (sizeof(T) == 2)
                return first(acc2, A {});
            batch<T, A> step3 = bitwise_cast<T>(bitwise_cast<uint16_t>(acc2) >> 8);
            batch<T, A> acc3 = max(acc2, step3);
            return first(acc3, A {});
        }

        // reduce_min
        template <class A, class T, class = std::enable_if_t<(sizeof(T) <= 2)>>
        XSIMD_INLINE T reduce_min(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            constexpr auto mask0 = detail::shuffle(2, 3, 0, 0);
            batch<T, A> step0 = _mm_shuffle_epi32(self, mask0);
            batch<T, A> acc0 = min(self, step0);

            constexpr auto mask1 = detail::shuffle(1, 0, 0, 0);
            batch<T, A> step1 = _mm_shuffle_epi32(acc0, mask1);
            batch<T, A> acc1 = min(acc0, step1);

            constexpr auto mask2 = detail::shuffle(1, 0, 0, 0);
            batch<T, A> step2 = _mm_shufflelo_epi16(acc1, mask2);
            batch<T, A> acc2 = min(acc1, step2);
            if (sizeof(T) == 2)
                return first(acc2, A {});
            batch<T, A> step3 = bitwise_cast<T>(bitwise_cast<uint16_t>(acc2) >> 8);
            batch<T, A> acc3 = min(acc2, step3);
            return first(acc3, A {});
        }

        // reduce_mul
        template <class A>
        XSIMD_INLINE float reduce_mul(batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            __m128 tmp0 = _mm_mul_ps(self, _mm_movehl_ps(self, self));
            __m128 tmp1 = _mm_mul_ss(tmp0, _mm_shuffle_ps(tmp0, tmp0, 1));
            return _mm_cvtss_f32(tmp1);
        }

        template <class A>
        XSIMD_INLINE double reduce_mul(batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_cvtsd_f64(_mm_mul_sd(self, _mm_unpackhi_pd(self, self)));
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T reduce_mul(batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                batch<T, A> tmp1 = _mm_shuffle_epi32(self, _MM_SHUFFLE(0, 1, 2, 3));
                tmp1 = tmp1 * self;
                batch<T, A> tmp2 = _mm_unpackhi_epi32(tmp1, tmp1);
                tmp2 = tmp2 * tmp1;
                return _mm_cvtsi128_si32(tmp2);
            }
            else if constexpr (sizeof(T) == 8)
            {
                batch<T, A> tmp1 = _mm_unpackhi_epi64(self, self);
                auto tmp2 = tmp1 * self;
#if defined(__x86_64__)
                return _mm_cvtsi128_si64(tmp2);
#else
                __m128i m;
                _mm_storel_epi64(&m, tmp2);
                int64_t i;
                std::memcpy(&i, &m, sizeof(i));
                return i;
#endif
            }
            else
            {
                return reduce_mul(self, common {});
            }
        }

        // rsqrt
        template <class A>
        XSIMD_INLINE batch<float, A> rsqrt(batch<float, A> const& val, requires_arch<sse2>) noexcept
        {
            return _mm_rsqrt_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> rsqrt(batch<double, A> const& val, requires_arch<sse2>) noexcept
        {
            return _mm_cvtps_pd(_mm_rsqrt_ps(_mm_cvtpd_ps(val)));
        }

        // select
        template <class A>
        XSIMD_INLINE batch<float, A> select(batch_bool<float, A> const& cond, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<sse2>) noexcept
        {
            return _mm_or_ps(_mm_and_ps(cond, true_br), _mm_andnot_ps(cond, false_br));
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<sse2>) noexcept
        {
            return _mm_or_si128(_mm_and_si128(cond, true_br), _mm_andnot_si128(cond, false_br));
        }
        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<sse2>) noexcept
        {
            return select(batch_bool<T, A> { Values... }, true_br, false_br, sse2 {});
        }
        template <class A>
        XSIMD_INLINE batch<double, A> select(batch_bool<double, A> const& cond, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<sse2>) noexcept
        {
            return _mm_or_pd(_mm_and_pd(cond, true_br), _mm_andnot_pd(cond, false_br));
        }

        // shuffle
        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3>
        XSIMD_INLINE batch<float, A> shuffle(batch<float, A> const& x, batch<float, A> const& y, batch_constant<ITy, A, I0, I1, I2, I3> mask, requires_arch<sse2>) noexcept
        {
            constexpr uint32_t smask = detail::mod_shuffle(I0, I1, I2, I3);
            // shuffle within lane
            if (I0 < 4 && I1 < 4 && I2 >= 4 && I3 >= 4)
                return _mm_shuffle_ps(x, y, smask);

            // shuffle within opposite lane
            if (I0 >= 4 && I1 >= 4 && I2 < 4 && I3 < 4)
                return _mm_shuffle_ps(y, x, smask);
            return shuffle(x, y, mask, common {});
        }

        template <class A, class ITy, ITy I0, ITy I1>
        XSIMD_INLINE batch<double, A> shuffle(batch<double, A> const& x, batch<double, A> const& y, batch_constant<ITy, A, I0, I1> mask, requires_arch<sse2>) noexcept
        {
            constexpr uint32_t smask = detail::mod_shuffle(I0, I1);
            // shuffle within lane
            if (I0 < 2 && I1 >= 2)
                return _mm_shuffle_pd(x, y, smask);

            // shuffle within opposite lane
            if (I0 >= 2 && I1 < 2)
                return _mm_shuffle_pd(y, x, smask);
            return shuffle(x, y, mask, common {});
        }

        // sqrt
        template <class A>
        XSIMD_INLINE batch<float, A> sqrt(batch<float, A> const& val, requires_arch<sse2>) noexcept
        {
            return _mm_sqrt_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sqrt(batch<double, A> const& val, requires_arch<sse2>) noexcept
        {
            return _mm_sqrt_pd(val);
        }

        // slide_left
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<sse2>) noexcept
        {
            return _mm_slli_si128(x, N);
        }

        // slide_right
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<sse2>) noexcept
        {
            return _mm_srli_si128(x, N);
        }

        // sadd

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_adds_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_adds_epi16(self, other);
                }
                else
                {
                    return sadd(self, other, common {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_adds_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_adds_epu16(self, other);
                }
                else
                {
                    return sadd(self, other, common {});
                }
            }
        }

        // set
        template <class A, class... Values>
        XSIMD_INLINE batch<float, A> set(batch<float, A> const&, requires_arch<sse2>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch<float, A>::size, "consistent init");
            return _mm_setr_ps(values...);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<sse2>, T v0, T v1) noexcept
        {
            return _mm_set_epi64x(v1, v0);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<sse2>, T v0, T v1, T v2, T v3) noexcept
        {
            return _mm_setr_epi32(v0, v1, v2, v3);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<sse2>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7) noexcept
        {
            return _mm_setr_epi16(v0, v1, v2, v3, v4, v5, v6, v7);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<sse2>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7, T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15) noexcept
        {
            return _mm_setr_epi8(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
        }

        template <class A, class... Values>
        XSIMD_INLINE batch<double, A> set(batch<double, A> const&, requires_arch<sse2>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch<double, A>::size, "consistent init");
            return _mm_setr_pd(values...);
        }

        template <class A, class T, class... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<sse2>, Values... values) noexcept
        {
            return set(batch<T, A>(), A {}, static_cast<T>(values ? -1LL : 0LL)...).data;
        }

        template <class A, class... Values>
        XSIMD_INLINE batch_bool<float, A> set(batch_bool<float, A> const&, requires_arch<sse2>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<float, A>::size, "consistent init");
            return _mm_castsi128_ps(set(batch<int32_t, A>(), A {}, static_cast<int32_t>(values ? -1LL : 0LL)...).data);
        }

        template <class A, class... Values>
        XSIMD_INLINE batch_bool<double, A> set(batch_bool<double, A> const&, requires_arch<sse2>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<double, A>::size, "consistent init");
            return _mm_castsi128_pd(set(batch<int64_t, A>(), A {}, static_cast<int64_t>(values ? -1LL : 0LL)...).data);
        }

        // ssub

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_subs_epi8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_subs_epi16(self, other);
                }
                else
                {
                    return ssub(self, other, common {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
                    return _mm_subs_epu8(self, other);
                }
                else if constexpr (sizeof(T) == 2)
                {
                    return _mm_subs_epu16(self, other);
                }
                else
                {
                    return ssub(self, other, common {});
                }
            }
        }

        // store<batch_bool>
        namespace detail
        {
            template <class T>
            XSIMD_INLINE void store_bool_sse2(__m128i b, bool* mem, T) noexcept
            {
                // GCC <12 have missing or buggy unaligned store intrinsics; use memcpy to work around this.
                // GCC/Clang/MSVC will turn it into the correct store.
                if constexpr (sizeof(T) == 1)
                {
                    // negate mask to convert to 0 or 1
                    auto val = _mm_sub_epi8(_mm_set1_epi8(0), b);
                    memcpy(mem, &val, sizeof(val));
                }
                else if constexpr (sizeof(T) == 2)
                {
                    auto val = _mm_sub_epi8(_mm_set1_epi8(0), _mm_packs_epi16(b, b));
#if defined(__x86_64__)
                    auto val_lo = _mm_cvtsi128_si64(val);
                    memcpy(mem, &val_lo, sizeof(val_lo));
#else
                    memcpy(mem, &val, sizeof(uint64_t));
#endif
                }
                else if constexpr (sizeof(T) == 4)
                {
                    auto pack_16 = _mm_packs_epi32(b, b);
                    uint32_t val = _mm_cvtsi128_si32(_mm_sub_epi8(_mm_set1_epi8(0), _mm_packs_epi16(pack_16, pack_16)));
                    memcpy(mem, &val, sizeof(val));
                }
                else if constexpr (sizeof(T) == 8)
                {
                    auto pack_32 = _mm_packs_epi32(b, b);
                    auto pack_16 = _mm_packs_epi32(pack_32, pack_32);
                    uint16_t val = _mm_cvtsi128_si32(_mm_sub_epi8(_mm_set1_epi8(0), _mm_packs_epi16(pack_16, pack_16)));
                    memcpy(mem, &val, sizeof(val));
                }
                else
                {
                    assert(false && "unsupported arch/op combination");
                }
            }

            XSIMD_INLINE __m128i sse_to_i(__m128 x) { return _mm_castps_si128(x); }
            XSIMD_INLINE __m128i sse_to_i(__m128d x) { return _mm_castpd_si128(x); }
            XSIMD_INLINE __m128i sse_to_i(__m128i x) { return x; }
        }

        template <class T, class A>
        XSIMD_INLINE void store(batch_bool<T, A> b, bool* mem, requires_arch<sse2>) noexcept
        {
            detail::store_bool_sse2(detail::sse_to_i(b), mem, T {});
        }

        // store_aligned
        template <class A>
        XSIMD_INLINE void store_aligned(float* mem, batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_store_ps(mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_store_si128((__m128i*)mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch_bool<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_store_si128((__m128i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_aligned(double* mem, batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_store_pd(mem, self);
        }

        // store_unaligned
        template <class A>
        XSIMD_INLINE void store_unaligned(float* mem, batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_storeu_ps(mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_storeu_si128((__m128i*)mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch_bool<T, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_storeu_si128((__m128i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_unaligned(double* mem, batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            return _mm_storeu_pd(mem, self);
        }

        // store_stream
        template <class A>
        XSIMD_INLINE void store_stream(float* mem, batch<float, A> const& self, requires_arch<sse2>) noexcept
        {
            _mm_stream_ps(mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE void store_stream(T* mem, batch<T, A> const& self, requires_arch<sse2>) noexcept
        {
            _mm_stream_si128((__m128i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_stream(double* mem, batch<double, A> const& self, requires_arch<sse2>) noexcept
        {
            _mm_stream_pd(mem, self);
        }

        // sub
        template <class A>
        XSIMD_INLINE batch<float, A> sub(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_sub_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_sub_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_sub_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_sub_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_sub_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sub(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_sub_pd(self, other);
        }

        // swizzle (constant mask)
        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<sse2>) noexcept
        {
            constexpr uint32_t index = detail::shuffle(V0, V1, V2, V3);
            return _mm_shuffle_ps(self, self, index);
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<sse2>) noexcept
        {
            constexpr uint32_t index = detail::shuffle(V0, V1);
            return _mm_shuffle_pd(self, self, index);
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<sse2>) noexcept
        {
            constexpr uint32_t index = detail::shuffle(2 * V0, 2 * V0 + 1, 2 * V1, 2 * V1 + 1);
            return _mm_shuffle_epi32(self, index);
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self, batch_constant<uint64_t, A, V0, V1> mask, requires_arch<sse2>) noexcept
        {
            return bitwise_cast<int64_t>(swizzle(bitwise_cast<uint64_t>(self), mask, sse2 {}));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<sse2>) noexcept
        {
            constexpr uint32_t index = detail::shuffle(V0, V1, V2, V3);
            return _mm_shuffle_epi32(self, index);
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3> mask, requires_arch<sse2>) noexcept
        {
            return bitwise_cast<int32_t>(swizzle(bitwise_cast<uint32_t>(self), mask, sse2 {}));
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<sse2>) noexcept
        {
            constexpr bool is_identity = detail::is_identity(mask);
            constexpr bool is_dup_lo = detail::is_dup_lo(mask);
            constexpr bool is_dup_hi = detail::is_dup_hi(mask);

            if constexpr (is_identity)
            {
                return self;
            }
            if constexpr (is_dup_lo)
            {
                // permute the low half
                constexpr int imm = detail::mod_shuffle(V0, V1, V2, V3);
                const auto lo = _mm_shufflelo_epi16(self, imm);
                // broadcast that 64-bit low half into both halves
                const auto lo_all = _mm_unpacklo_epi64(lo, lo);
                return lo_all;
            }
            if constexpr (is_dup_hi)
            {
                // permute the high half
                constexpr int imm = detail::mod_shuffle(V4, V5, V6, V7);
                const auto hi = _mm_shufflehi_epi16(self, imm);
                // broadcast that 64-bit high half into both halves
                const auto hi_all = _mm_unpackhi_epi64(hi, hi);
                return hi_all;
            }
            // Only pick elements from the low lane
            if constexpr (detail::is_only_from_lo(mask))
            {
                // permute within each sub lane
                constexpr auto mask_lo = detail::mod_shuffle(V0, V1, V2, V3);
                constexpr auto mask_hi = detail::mod_shuffle(V4, V5, V6, V7);
                __m128i lol = _mm_shufflelo_epi16(self, mask_lo);
                __m128i loh = _mm_shufflelo_epi16(self, mask_hi);

                // generate temporary lanes
                return _mm_unpacklo_epi64(lol, loh);
            }
            // Only pick elements from the high lane
            if constexpr (detail::is_only_from_hi(mask))
            {
                // permute within each sub lane
                constexpr auto mask_lo = detail::mod_shuffle(V0, V1, V2, V3);
                constexpr auto mask_hi = detail::mod_shuffle(V4, V5, V6, V7);
                __m128i hil = _mm_shufflehi_epi16(self, mask_lo);
                __m128i hih = _mm_shufflehi_epi16(self, mask_hi);

                // generate temporary lanes
                return _mm_unpackhi_epi64(hil, hih);
            }

            // Generic case

            // permute within each sub lane
            constexpr auto mask_lo = detail::mod_shuffle(V0, V1, V2, V3);
            constexpr auto mask_hi = detail::mod_shuffle(V4, V5, V6, V7);
            __m128i lol = _mm_shufflelo_epi16(self, mask_lo);
            __m128i loh = _mm_shufflelo_epi16(self, mask_hi);
            __m128i hil = _mm_shufflehi_epi16(self, mask_lo);
            __m128i hih = _mm_shufflehi_epi16(self, mask_hi);

            // generate temporary lanes
            __m128i lo = _mm_unpacklo_epi64(lol, loh);
            __m128i hi = _mm_unpackhi_epi64(hil, hih);

            // mask to choose the right lane
            constexpr auto blend_mask = mask < std::integral_constant<uint16_t, 4>();

            // blend the two permutes
            return select(blend_mask, batch<uint16_t, A>(lo), batch<uint16_t, A>(hi));
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<sse2>) noexcept
        {
            return bitwise_cast<int16_t>(swizzle(bitwise_cast<uint16_t>(self), mask, sse2 {}));
        }

        // transpose
        template <class A>
        XSIMD_INLINE void transpose(batch<float, A>* matrix_begin, batch<float, A>* matrix_end, requires_arch<sse2>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<float, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1], r2 = matrix_begin[2], r3 = matrix_begin[3];
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            matrix_begin[0] = r0;
            matrix_begin[1] = r1;
            matrix_begin[2] = r2;
            matrix_begin[3] = r3;
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<uint32_t, A>* matrix_begin, batch<uint32_t, A>* matrix_end, requires_arch<sse2>) noexcept
        {
            transpose(reinterpret_cast<batch<float, A>*>(matrix_begin), reinterpret_cast<batch<float, A>*>(matrix_end), A {});
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int32_t, A>* matrix_begin, batch<int32_t, A>* matrix_end, requires_arch<sse2>) noexcept
        {
            transpose(reinterpret_cast<batch<float, A>*>(matrix_begin), reinterpret_cast<batch<float, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<double, A>* matrix_begin, batch<double, A>* matrix_end, requires_arch<sse2>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<double, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1];
            matrix_begin[0] = _mm_unpacklo_pd(r0, r1);
            matrix_begin[1] = _mm_unpackhi_pd(r0, r1);
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<uint64_t, A>* matrix_begin, batch<uint64_t, A>* matrix_end, requires_arch<sse2>) noexcept
        {
            transpose(reinterpret_cast<batch<double, A>*>(matrix_begin), reinterpret_cast<batch<double, A>*>(matrix_end), A {});
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int64_t, A>* matrix_begin, batch<int64_t, A>* matrix_end, requires_arch<sse2>) noexcept
        {
            transpose(reinterpret_cast<batch<double, A>*>(matrix_begin), reinterpret_cast<batch<double, A>*>(matrix_end), A {});
        }

        // zip_hi
        template <class A>
        XSIMD_INLINE batch<float, A> zip_hi(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_unpackhi_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_unpackhi_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_unpackhi_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_unpackhi_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_unpackhi_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<double, A> zip_hi(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_unpackhi_pd(self, other);
        }

        // zip_lo
        template <class A>
        XSIMD_INLINE batch<float, A> zip_lo(batch<float, A> const& self, batch<float, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_unpacklo_ps(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<sse2>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm_unpacklo_epi8(self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm_unpacklo_epi16(self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm_unpacklo_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_unpacklo_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<double, A> zip_lo(batch<double, A> const& self, batch<double, A> const& other, requires_arch<sse2>) noexcept
        {
            return _mm_unpacklo_pd(self, other);
        }

        // store_masked
        template <class A, bool... Values>
        XSIMD_INLINE void store_masked(float* mem,
                                       batch<float, A> const& src,
                                       batch_bool_constant<float, A, Values...> mask,
                                       aligned_mode,
                                       requires_arch<sse2>) noexcept
        {
            if constexpr (mask.prefix() == 2)
            {
                _mm_storel_pi(reinterpret_cast<__m64*>(mem), src);
            }
            else if constexpr (mask.suffix() == 2)
            {
                _mm_storeh_pi(reinterpret_cast<__m64*>(mem + 2), src);
            }
            else
            {
                store_masked<A>(mem, src, mask, aligned_mode {}, common {});
            }
        }

        // get (must appear after first and swizzle so it can delegate through the xsimd API)
        namespace detail
        {
            // broadcast lane index I across a batch_constant<IdxT, A, I, I, ..., I> matching batch<T, A>::size
            template <class T, class A, size_t I, size_t... Is>
            XSIMD_INLINE auto broadcast_lane_index(std::index_sequence<Is...>) noexcept
                -> batch_constant<as_unsigned_integer_t<T>, A, static_cast<as_unsigned_integer_t<T>>(Is * 0 + I)...>
            {
                return {};
            }

            template <class T, class A, size_t I>
            XSIMD_INLINE auto broadcast_lane_index() noexcept
                -> decltype(broadcast_lane_index<T, A, I>(std::make_index_sequence<batch<T, A>::size> {}))
            {
                return {};
            }
        }

        template <class A, size_t I, class T>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && sizeof(T) <= 2, T>
        get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<sse2>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, A {});
            }
            else if constexpr (sizeof(T) == 2)
            {
                return static_cast<T>(_mm_extract_epi16(self, I));
            }
            else
            {
                // SSE2 has no pextrb; byte-lane shift + movd is the shortest path for I>0.
                return static_cast<T>(_mm_cvtsi128_si32(_mm_srli_si128(self, I)) & 0xFF);
            }
        }

        template <class A, size_t I, class T>
        XSIMD_INLINE std::enable_if_t<(std::is_integral_v<T> && sizeof(T) >= 4) || std::is_floating_point_v<T>, T>
        get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<sse2>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, A {});
            }
            else
            {
                return first(swizzle(self, detail::broadcast_lane_index<T, A, I>(), A {}), A {});
            }
        }
    }
}

#endif
