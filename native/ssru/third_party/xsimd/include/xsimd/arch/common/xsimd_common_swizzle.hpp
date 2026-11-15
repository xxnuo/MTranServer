/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 * Copyright (c) Marco Barbone                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software.*
 ****************************************************************************/
#ifndef XSIMD_COMMON_SWIZZLE_HPP
#define XSIMD_COMMON_SWIZZLE_HPP

#include "../../config/xsimd_macros.hpp"

#include <cstddef>
#include <type_traits>

namespace xsimd
{
    template <typename T, class A, T... Values>
    struct batch_constant;

    namespace kernel
    {
        namespace detail
        {
            // v[i] == i for every i
            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_identity() noexcept
            {
                std::size_t i = 0;
                return ((Vs == static_cast<T>(i++)) && ...);
            }

            // every index points into the low / high half
            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_only_from_lo() noexcept
            {
                return ((Vs < static_cast<T>(sizeof...(Vs) / 2)) && ...);
            }

            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_only_from_hi() noexcept
            {
                return ((Vs >= static_cast<T>(sizeof...(Vs) / 2)) && ...);
            }

            // 0 <= v[i] < N for every i (negative values wrap to a huge size_t)
            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_in_range() noexcept
            {
                return ((static_cast<std::size_t>(Vs) < sizeof...(Vs)) && ...);
            }

            // v[i] == v[i + N / 2] for every i in the low half
            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool has_equal_halves() noexcept
            {
                constexpr std::size_t half = sizeof...(Vs) / 2;
                constexpr T v[] = { Vs... };
                for (std::size_t i = 0; i < half; ++i)
                    if (v[i] != v[i + half])
                        return false;
                return true;
            }

            // both halves read the same indices, all taken from the low / high half
            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_dup_lo() noexcept
            {
                return is_in_range<T, Vs...>() && is_only_from_lo<T, Vs...>() && has_equal_halves<T, Vs...>();
            }
            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_dup_hi() noexcept
            {
                return is_in_range<T, Vs...>() && is_only_from_hi<T, Vs...>() && has_equal_halves<T, Vs...>();
            }

            /**
             * @brief Internal: Check if a swizzle pattern crosses lane boundaries
             *
             * @tparam LaneSizeBytes Size of a lane in bytes (must be > 0)
             * @tparam ElemT Element type to determine element size
             * @tparam U Type of the index values
             * @tparam Vs... Index values for the swizzle pattern
             *
             * @return true if any element accesses data from a different lane
             *
             * This is an internal helper. Architecture-specific code can call this directly
             * with explicit lane sizes (e.g., detail::is_cross_lane_with_lane_size<16, float, ...>()
             * for 128-bit lanes).
             */
            template <std::size_t LaneSizeBytes, typename ElemT, typename U, U... Vs>
            XSIMD_INLINE constexpr bool is_cross_lane_with_lane_size() noexcept
            {
                static_assert(std::is_integral_v<U>, "swizzle mask values must be integral");
                static_assert(sizeof...(Vs) >= 1, "need at least one value");
                static_assert(LaneSizeBytes > 0, "lane size must be positive");

                constexpr std::size_t lane_elems = LaneSizeBytes / sizeof(ElemT);
                constexpr U values[] = { Vs... };
                constexpr std::size_t N = sizeof...(Vs);

                for (std::size_t i = 0; i < N; ++i)
                {
                    std::size_t elem_lane = i / lane_elems;
                    std::size_t target_lane = static_cast<std::size_t>(values[i]) / lane_elems;
                    if (elem_lane != target_lane)
                        return true;
                }
                return false;
            }

            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_identity(batch_constant<T, A, Vs...>) noexcept { return is_identity<T, Vs...>(); }
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_dup_lo(batch_constant<T, A, Vs...>) noexcept { return is_dup_lo<T, Vs...>(); }
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_dup_hi(batch_constant<T, A, Vs...>) noexcept { return is_dup_hi<T, Vs...>(); }
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_only_from_lo(batch_constant<T, A, Vs...>) noexcept { return detail::is_only_from_lo<T, Vs...>(); }
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_only_from_hi(batch_constant<T, A, Vs...>) noexcept { return detail::is_only_from_hi<T, Vs...>(); }
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_cross_lane(batch_constant<T, A, Vs...>) noexcept
            {
                return detail::is_cross_lane_with_lane_size<16, T, T, Vs...>();
            }

            /**
             * @brief Public: Check if a swizzle pattern crosses 128-bit lane boundaries
             *
             * Checks if indices cross 128-bit (16-byte) lane boundaries, which is the
             * standard lane size for SSE/AVX/AVX512 shuffle operations.
             *
             * @tparam ElemT Element type to determine element size
             * @tparam U Type of the index values
             * @tparam Vs... Index values for the swizzle pattern
             *
             * @return true if any element accesses data from a different 128-bit lane
             *
             * Examples:
             * - is_cross_lane<float, 0, 1, 2, 3, 4, 5, 6, 7>() // no crossing (within 128-bit)
             * - is_cross_lane<float, 4, 5, 6, 7, 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15>() // crosses
             */
            template <typename ElemT, typename U, U... Vs>
            XSIMD_INLINE constexpr bool is_cross_lane() noexcept
            {
                return is_cross_lane_with_lane_size<16, ElemT, U, Vs...>();
            }

            // Overload with std::size_t indices
            template <typename ElemT, std::size_t... Vs>
            XSIMD_INLINE constexpr bool is_cross_lane() noexcept
            {
                return is_cross_lane<ElemT, std::size_t, Vs...>();
            }

        } // namespace detail
    } // namespace kernel
} // namespace xsimd

#endif // XSIMD_COMMON_SWIZZLE_HPP
