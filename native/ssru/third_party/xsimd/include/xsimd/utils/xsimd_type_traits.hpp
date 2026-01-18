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

#ifndef XSIMD_TYPE_TRAITS_HPP
#define XSIMD_TYPE_TRAITS_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace xsimd
{
    namespace detail
    {
        template <std::size_t S>
        struct sized_num_types;

        template <>
        struct sized_num_types<1>
        {
            using signed_type = std::int8_t;
            using unsigned_type = std::uint8_t;
            using floating_point_type = void;
        };

        template <>
        struct sized_num_types<2>
        {
            using signed_type = std::int16_t;
            using unsigned_type = std::uint16_t;
            using floating_point_type = void;
        };

        template <>
        struct sized_num_types<4>
        {
            using signed_type = std::int32_t;
            using unsigned_type = std::uint32_t;
            using floating_point_type = float;
        };

        template <>
        struct sized_num_types<8>
        {
            using signed_type = std::int64_t;
            using unsigned_type = std::uint64_t;
            using floating_point_type = double;
        };
    }

    /**
     * @ingroup type_traits
     *
     * Signed integer type with exactly @c S bytes (1, 2, 4, or 8).
     *
     * @tparam S size in bytes.
     */
    template <std::size_t S>
    using sized_int_t = typename detail::sized_num_types<S>::signed_type;

    /**
     * @ingroup type_traits
     *
     * Unsigned integer type with exactly @c S bytes (1, 2, 4, or 8).
     *
     * @tparam S size in bytes.
     */
    template <std::size_t S>
    using sized_uint_t = typename detail::sized_num_types<S>::unsigned_type;

    /**
     * @ingroup type_traits
     *
     * Floating-point type with exactly @c S bytes (4 for @c float, 8 for @c double).
     * Yields @c void for sizes without a standard floating-point type (1, 2).
     *
     * @tparam S size in bytes.
     */
    template <std::size_t S>
    using sized_fp_t = typename detail::sized_num_types<S>::floating_point_type;

    namespace detail
    {
        template <typename T, std::size_t factor, typename = void>
        struct remap_num
        {
            using type = T;
        };

        template <typename T, std::size_t factor>
        struct remap_num<T, factor, std::enable_if_t<std::is_floating_point_v<T>>>
        {
            using type = xsimd::sized_fp_t<sizeof(T) * factor>;
        };

        template <typename T, std::size_t factor>
        struct remap_num<T, factor, std::enable_if_t<!std::is_floating_point_v<T> && std::is_signed_v<T>>>
        {
            using type = xsimd::sized_int_t<sizeof(T) * factor>;
        };

        template <typename T, std::size_t factor>
        struct remap_num<T, factor, std::enable_if_t<!std::is_floating_point_v<T> && std::is_unsigned_v<T>>>
        {
            using type = xsimd::sized_uint_t<sizeof(T) * factor>;
        };
    }

    /**
     * @ingroup type_traits
     *
     * Remap numeral types to their fixed sized variant (``[u]int{8,16,32}_t``
     * and pass through other types).
     * Certain platforms have different types (*i.e.* not aliases) between
     * ``char`` and ``int8_t``, or ``long long`` and ``int{32,64}_t``, with SIMD
     * intrinsicts only defined for some of them.
     * Handling them requires to cast to a known predictable type.
     *
     * @tparam T arithmetic type to project from.
     */
    template <typename T>
    using map_to_sized_type_t = typename detail::remap_num<T, /* factor= */ 1>::type;

    /**
     * @ingroup type_traits
     *
     * The next-wider arithmetic type for @c T: doubles the size while preserving
     * signedness for integers and yielding @c double for @c float.
     * Supported input types: @c [u]int{8,16,32}_t and @c float.
     *
     * @tparam T arithmetic type to widen.
     */
    template <typename T>
    using widen_t = typename detail::remap_num<T, /* factor= */ 2>::type;
}

#endif
