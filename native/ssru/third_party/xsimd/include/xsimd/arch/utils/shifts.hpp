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

#ifndef XSIMD_UTILS_SHIFTS_HPP
#define XSIMD_UTILS_SHIFTS_HPP

#include "../../config/xsimd_macros.hpp"
#include "../../types/xsimd_batch.hpp"
#include "../../types/xsimd_batch_constant.hpp"
#include "../../types/xsimd_traits.hpp"

namespace xsimd
{
    namespace kernel
    {
        namespace utils
        {
            template <typename I, I offset, I length, I... Vs>
            struct select_stride
            {
                template <typename K>
                static constexpr K get(K i, K)
                {
                    constexpr I values_array[] = { Vs... };
                    return static_cast<K>(values_array[length * i + offset]);
                }
            };

            template <typename I>
            constexpr I lsb_mask(I bit_index)
            {
                if (bit_index == 8 * sizeof(I))
                {
                    return ~I { 0 };
                }
                return static_cast<I>((I { 1 } << bit_index) - I { 1 });
            }

            template <class T, class A, T V0, T... Vs>
            constexpr bool all_equals(batch_constant<T, A, V0, Vs...> c)
            {
                return (c == std::integral_constant<T, V0> {}).all();
            }

            template <class T, class A, T... Vs>
            XSIMD_INLINE batch<T, A> bitwise_lshift_as_twice_larger(
                batch<T, A> const& self, batch_constant<T, A, Vs...>) noexcept
            {
                using T2 = widen_t<T>;

                const auto self2 = bitwise_cast<T2>(self);

                // Lower byte: shift as twice the size and mask bits flowing to higher byte.
                constexpr auto shifts_lo = make_batch_constant<T2, select_stride<T, 0, 2, Vs...>, A>();
                constexpr auto mask_lo = lsb_mask<T2>(8 * sizeof(T));
                const auto shifted_lo = bitwise_lshift(self2, shifts_lo);
                constexpr auto batch_mask_lo = make_batch_constant<T2, mask_lo, A>();
                const auto masked_lo = bitwise_and(shifted_lo, batch_mask_lo.as_batch());

                // Higher byte: mask bits that would flow from lower byte and shift as twice the size.
                constexpr auto shifts_hi = make_batch_constant<T2, select_stride<T, 1, 2, Vs...>, A>();
                constexpr auto mask_hi = mask_lo << (8 * sizeof(T));
                constexpr auto batch_mask_hi = make_batch_constant<T2, mask_hi, A>();
                const auto masked_hi = bitwise_and(self2, batch_mask_hi.as_batch());
                const auto shifted_hi = bitwise_lshift(masked_hi, shifts_hi);

                return bitwise_cast<T>(bitwise_or(masked_lo, shifted_hi));
            }
        }
    }
}

#endif
