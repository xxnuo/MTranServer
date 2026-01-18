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

#ifndef XSIMD_UTILS_HPP
#define XSIMD_UTILS_HPP

#include <array>
#include <complex>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#ifdef XSIMD_ENABLE_XTL_COMPLEX
#include <xtl/xcomplex.hpp>
#endif

#include "./xsimd_batch_fwd.hpp"

namespace xsimd
{
    /**************
     * index      *
     **************/

    template <size_t I>
    using index = std::integral_constant<size_t, I>;

    /**************
     * as_integer *
     **************/

    template <class T>
    struct as_integer : std::make_signed<T>
    {
    };

    template <>
    struct as_integer<float>
    {
        using type = int32_t;
    };

    template <>
    struct as_integer<double>
    {
        using type = int64_t;
    };

    template <class T, class A>
    struct as_integer<batch<T, A>>
    {
        using type = batch<typename as_integer<T>::type, A>;
    };

    template <class B>
    using as_integer_t = typename as_integer<B>::type;

    /***********************
     * as_unsigned_integer *
     ***********************/

    template <class T>
    struct as_unsigned_integer : std::make_unsigned<T>
    {
    };

    template <>
    struct as_unsigned_integer<bool>
    {
        using type = uint8_t;
    };

    template <>
    struct as_unsigned_integer<float>
    {
        using type = uint32_t;
    };

    template <>
    struct as_unsigned_integer<double>
    {
        using type = uint64_t;
    };

    template <class T, class A>
    struct as_unsigned_integer<batch<T, A>>
    {
        using type = batch<typename as_unsigned_integer<T>::type, A>;
    };

    template <class T>
    using as_unsigned_integer_t = typename as_unsigned_integer<T>::type;

    /*********************
     * as_signed_integer *
     *********************/

    template <class T>
    struct as_signed_integer : std::make_signed<T>
    {
    };

    template <class T>
    using as_signed_integer_t = typename as_signed_integer<T>::type;

    /******************
     * flip_sign_type *
     ******************/

    namespace detail
    {
        template <class T, bool is_signed>
        struct flipped_sign_type_impl : std::make_signed<T>
        {
        };

        template <class T>
        struct flipped_sign_type_impl<T, true> : std::make_unsigned<T>
        {
        };
    }

    template <class T>
    struct flipped_sign_type
        : detail::flipped_sign_type_impl<T, std::is_signed_v<T>>
    {
    };

    template <class T>
    using flipped_sign_type_t = typename flipped_sign_type<T>::type;

    /***********
     * as_float *
     ************/

    template <class T>
    struct as_float;

    template <>
    struct as_float<int32_t>
    {
        using type = float;
    };

    template <>
    struct as_float<int64_t>
    {
        using type = double;
    };

    template <class T, class A>
    struct as_float<batch<T, A>>
    {
        using type = batch<typename as_float<T>::type, A>;
    };

    template <class T>
    using as_float_t = typename as_float<T>::type;

    /**************
     * as_logical *
     **************/

    template <class T>
    struct as_logical;

    template <class T, class A>
    struct as_logical<batch<T, A>>
    {
        using type = batch_bool<T, A>;
    };

    template <class T>
    using as_logical_t = typename as_logical<T>::type;

    /********************
     * bit_cast *
     ********************/

    template <class To, class From>
    inline To bit_cast(From val) noexcept
    {
        static_assert(sizeof(From) == sizeof(To), "casting between compatible layout");
        static_assert(std::is_trivially_copyable_v<From>, "input type is trivially copyable");
        static_assert(std::is_trivially_copyable_v<To>, "output type is trivially copyable");
        To res;
        std::memcpy(&res, &val, sizeof(val));
        return res;
    }

    namespace kernel
    {
        namespace detail
        {
            /**************************************
             * enabling / disabling metafunctions *
             **************************************/

            template <class T>
            using enable_arithmetic_t = std::enable_if_t<std::is_arithmetic_v<T>, int>;

            /// Enable signed integral or floating point
            template <class T>
            using enable_signed_numeral_t = std::enable_if_t<std::is_signed_v<T>, int>;

            template <class T>
            using enable_floating_point_t = std::enable_if_t<std::is_floating_point_v<T>, int>;

            template <class T>
            using enable_integral_t = std::enable_if_t<std::is_integral_v<T>, int>;

            template <class T>
            using enable_signed_integral_t = std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, int>;

            template <class T>
            using enable_unsigned_integral_t = std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, int>;

            template <class T, size_t S>
            using enable_sized_signed_t = std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T> && sizeof(T) == S, int>;

            template <class T, size_t S>
            using enable_sized_unsigned_t = std::enable_if_t<std::is_integral_v<T> && !std::is_signed_v<T> && sizeof(T) == S, int>;

            template <class T, size_t S>
            using enable_sized_integral_t = std::enable_if_t<std::is_integral_v<T> && sizeof(T) == S, int>;

            template <class T, size_t S>
            using enable_sized_t = std::enable_if_t<sizeof(T) == S, int>;

            template <class T, size_t S>
            using enable_max_sized_integral_t = std::enable_if_t<std::is_integral_v<T> && sizeof(T) <= S, int>;

            /********************************
             * Matching & mismatching sizes *
             ********************************/

            template <class T, class U, class B = int>
            using sizes_match_t = std::enable_if_t<sizeof(T) == sizeof(U), B>;

            template <class T, class U, class B = int>
            using sizes_mismatch_t = std::enable_if_t<sizeof(T) != sizeof(U), B>;

            template <class T, class U, class B = int>
            using stride_match_t = std::enable_if_t<!std::is_same_v<T, U> && sizeof(T) == sizeof(U), B>;
        } // namespace detail
    } // namespace kernel

    /*******************************************
     * int_sequence and make_sequence_as_batch *
     *******************************************/

    // TODO: Remove this once we drop C++11 support
    namespace detail
    {
        template <int... Is>
        using int_sequence = std::integer_sequence<int, Is...>;

        template <int N>
        using make_int_sequence = std::make_integer_sequence<int, N>;

        template <typename... Ts>
        using int_sequence_for = make_int_sequence<(int)sizeof...(Ts)>;

        // Type-casted index sequence.
        template <class P, size_t... Is>
        inline P indexes_from(std::index_sequence<Is...>) noexcept
        {
            return { static_cast<typename P::value_type>(Is)... };
        }

        template <class P>
        inline P make_sequence_as_batch() noexcept
        {
            return indexes_from<P>(std::make_index_sequence<P::size>());
        }
    }

    /*****************************************
     * Supplementary std::array constructors *
     *****************************************/

    namespace detail
    {
        // std::array constructor from scalar value ("broadcast")
        template <typename T, std::size_t... Is>
        inline constexpr std::array<T, sizeof...(Is)>
        array_from_scalar_impl(const T& scalar, std::index_sequence<Is...>) noexcept
        {
            // You can safely ignore this silly ternary, the "scalar" is all
            // that matters. The rest is just a dirty workaround...
            return std::array<T, sizeof...(Is)> { (Is + 1) ? scalar : T()... };
        }

        template <typename T, std::size_t N>
        inline constexpr std::array<T, N>
        array_from_scalar(const T& scalar) noexcept
        {
            return array_from_scalar_impl(scalar, std::make_index_sequence<N>());
        }

        // std::array constructor from C-style pointer (handled as an array)
        template <typename T, std::size_t... Is>
        inline constexpr std::array<T, sizeof...(Is)>
        array_from_pointer_impl(const T* c_array, std::index_sequence<Is...>) noexcept
        {
            return std::array<T, sizeof...(Is)> { c_array[Is]... };
        }

        template <typename T, std::size_t N>
        inline constexpr std::array<T, N>
        array_from_pointer(const T* c_array) noexcept
        {
            return array_from_pointer_impl(c_array, std::make_index_sequence<N>());
        }
    }

    /************************
     * is_array_initializer *
     ************************/

    namespace detail
    {
        template <typename T, typename... Args>
        inline constexpr bool is_all_convertible_v = std::conjunction_v<std::is_convertible<Args, T>...>;

        // Check that a variadic argument pack is a list of N values of type T,
        // as usable for instantiating a value of type std::array<T, N>.
        template <typename T, std::size_t N, typename... Args>
        using is_array_initializer_t = std::enable_if_t<
            (sizeof...(Args) == N) && is_all_convertible_v<T, Args...>>;
    }

    /**************
     * is_complex *
     **************/

    // This is used in both xsimd_complex_base.hpp and xsimd_traits.hpp
    // However xsimd_traits.hpp indirectly includes xsimd_complex_base.hpp
    // so we cannot define is_complex in xsimd_traits.hpp. Besides, if
    // no file defining batches is included, we still need this definition
    // in xsimd_traits.hpp, so let's define it here.

    namespace detail
    {
        template <class T>
        struct is_complex : std::false_type
        {
        };

        template <class T>
        struct is_complex<std::complex<T>> : std::true_type
        {
        };

#ifdef XSIMD_ENABLE_XTL_COMPLEX
        template <class T, bool i3ec>
        struct is_complex<xtl::xcomplex<T, T, i3ec>> : std::true_type
        {
        };
#endif

        template <class T>
        inline constexpr bool is_complex_v = is_complex<T>::value;
    }

    /*******************
     * real_batch_type *
     *******************/

    template <class B>
    struct real_batch_type
    {
        using type = B;
    };

    template <class T, class A>
    struct real_batch_type<batch<std::complex<T>, A>>
    {
        using type = batch<T, A>;
    };

    template <class B>
    using real_batch_type_t = typename real_batch_type<B>::type;

    /**********************
     * complex_batch_type *
     **********************/

    template <class B>
    struct complex_batch_type
    {
        using real_value_type = typename B::value_type;
        using arch_type = typename B::arch_type;
        using type = batch<std::complex<real_value_type>, arch_type>;
    };

    template <class T, class A>
    struct complex_batch_type<batch<std::complex<T>, A>>
    {
        using type = batch<std::complex<T>, A>;
    };

    template <class B>
    using complex_batch_type_t = typename complex_batch_type<B>::type;
}

#endif
