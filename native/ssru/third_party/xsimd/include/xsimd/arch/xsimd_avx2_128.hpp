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

#ifndef XSIMD_AVX2_128_HPP
#define XSIMD_AVX2_128_HPP

#include "../types/xsimd_avx2_register.hpp"
#include "../types/xsimd_batch_constant.hpp"

#include <type_traits>

namespace xsimd
{
    namespace kernel
    {
        using namespace types;

        // Defined in xsimd_avx512vl_128.hpp (included after this header). The
        // masked load/store half-fold below forwards to the 128-bit sized-batch
        // arch, which is avx512vl_128 when the 256-bit one stays in the AVX2
        // lineage (e.g. avxvnni). That unqualified dependent call resolves by
        // ordinary lookup here, so the avx512vl_128 overloads must be visible at
        // this point; declarations only, never instantiated unless AVX512VL is on.
        template <class A, class T, bool... V, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, V...> mask, convert<T>, Mode, requires_arch<avx512vl_128>) noexcept;
        template <class A, class T, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx512vl_128>) noexcept;
        template <class A, class T, bool... V, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, V...> mask, Mode, requires_arch<avx512vl_128>) noexcept;
        template <class A, class T, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx512vl_128>) noexcept;

        // select
        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx2_128>) noexcept
        {
            constexpr int mask = batch_bool_constant<T, A, Values...>::mask();
            if constexpr (sizeof(T) == 4)
            {
                return _mm_blend_epi32(false_br, true_br, mask);
            }
            else
            {
                return select(batch_bool_constant<T, A, Values...>(), true_br, false_br, avx_128 {});
            }
        }

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2_128>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm_sllv_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm_sllv_epi64(self, other);
            }
            else
            {
                return bitwise_lshift(self, other, avx {});
            }
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx2_128>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm_srav_epi32(self, other);
                }
                else
                {
                    return bitwise_rshift(self, other, avx_128 {});
                }
            }
            else
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm_srlv_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm_srlv_epi64(self, other);
                }
                else
                {
                    return bitwise_rshift(self, other, avx_128 {});
                }
            }
        }

        // load_masked / store_masked: native 128-bit integer masked memory.
        // Tagged on avx2_128 because vpmaskmov* needs AVX2; an AVX1-only build
        // routes integer masked memory through the float path in
        // xsimd_common_memory.hpp. 8/16-bit fall back to the common scalar path.
        namespace detail
        {
            template <class T>
            XSIMD_INLINE __m128i maskload_avx2_128(T const* mem, __m128i mask) noexcept
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm_maskload_epi32(reinterpret_cast<int const*>(mem), mask);
                }
                else
                {
                    return _mm_maskload_epi64(reinterpret_cast<long long const*>(mem), mask);
                }
            }

            template <class T>
            XSIMD_INLINE void maskstore_avx2_128(T* mem, __m128i mask, __m128i src) noexcept
            {
                if constexpr (sizeof(T) == 4)
                {
                    _mm_maskstore_epi32(reinterpret_cast<int*>(mem), mask, src);
                }
                else
                {
                    _mm_maskstore_epi64(reinterpret_cast<long long*>(mem), mask, src);
                }
            }
        }

        // Masks that lower to plain moves go to the sse2 float/double kernels
        // (int bitcast to same-width float); the rest keep the native vpmaskmov.
        template <class A, class T, bool... Values, class Mode,
                  typename = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, Values...> mask, convert<T>, Mode, requires_arch<avx2_128>) noexcept
        {
            if constexpr (detail::lowers_to_plain_moves(mask))
            {
                return detail::plain_move_load<sse2>(mem, mask, convert<T> {}, Mode {});
            }
            else
            {
                return load_masked(mem, mask.as_batch_bool(), convert<T> {}, Mode {}, avx2_128 {});
            }
        }

        template <class A, class T, bool... Values, class Mode,
                  typename = std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, Values...> mask, Mode, requires_arch<avx2_128>) noexcept
        {
            if constexpr (detail::lowers_to_plain_moves(mask))
            {
                detail::plain_move_store<sse2>(mem, src, mask, Mode {});
            }
            else
            {
                store_masked(mem, src, mask.as_batch_bool(), Mode {}, avx2_128 {});
            }
        }

        template <class A, class T, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), batch<T, A>>
        load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx2_128>) noexcept
        {
            return detail::maskload_avx2_128(mem, __m128i(mask));
        }

        template <class A, class T, class Mode>
        XSIMD_INLINE std::enable_if_t<std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8), void>
        store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx2_128>) noexcept
        {
            detail::maskstore_avx2_128(mem, __m128i(mask), __m128i(src));
        }

        // gather
        template <class T, class A, class U, detail::enable_sized_integral_t<T, 4> = 0, detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx2_128>) noexcept
        {
            return _mm_i32gather_epi32(reinterpret_cast<const int*>(src), index, sizeof(T));
        }

        template <class T, class A, class U, detail::enable_sized_integral_t<T, 8> = 0, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx2_128>) noexcept
        {
            return _mm_i64gather_epi64(reinterpret_cast<const long long int*>(src), index, sizeof(T));
        }

        template <class A, class U,
                  detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<float, A> gather(batch<float, A> const&, float const* src,
                                            batch<U, A> const& index,
                                            kernel::requires_arch<avx2_128>) noexcept
        {
            return _mm_i32gather_ps(src, index, sizeof(float));
        }

        template <class A, class U, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<double, A> gather(batch<double, A> const&, double const* src,
                                             batch<U, A> const& index,
                                             requires_arch<avx2_128>) noexcept
        {
            return _mm_i64gather_pd(src, index, sizeof(double));
        }
    }
}

#endif
