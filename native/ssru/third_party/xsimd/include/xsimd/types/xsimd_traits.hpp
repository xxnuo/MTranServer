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

#ifndef XSIMD_TRAITS_HPP
#define XSIMD_TRAITS_HPP

#include <complex>
#include <cstdint>
#include <type_traits>

#ifdef XSIMD_ENABLE_XTL_COMPLEX
#include <xtl/xcomplex.hpp>
#endif

#include "../config/xsimd_config.hpp"
#include "./xsimd_batch_fwd.hpp"
#include "./xsimd_utils.hpp"

/**
 * high level type traits
 *
 * @defgroup batch_traits Type traits
 *
 **/

namespace xsimd
{

    /**************************************
     * simd_traits and revert_simd_traits *
     **************************************/

    template <class T, class A = default_arch>
    struct has_simd_register : types::has_simd_register<T, A>
    {
    };

    template <class T, class A>
    struct has_simd_register<std::complex<T>, A> : has_simd_register<T, A>
    {
    };

    template <typename T, class A = default_arch>
    inline constexpr bool has_simd_register_v = has_simd_register<T, A>::value;

    namespace detail
    {
        template <class T, bool>
        struct simd_traits_impl;

        template <class T>
        struct simd_traits_impl<T, false>
        {
            using type = T;
            using bool_type = bool;
            static constexpr size_t size = 1;
        };

#if XSIMD_CPP_VERSION < 201703L
        template <class T>
        constexpr size_t simd_traits_impl<T, false>::size;
#endif

        template <class T>
        struct simd_traits_impl<T, true>
        {
            using type = batch<T>;
            using bool_type = typename type::batch_bool_type;
            static constexpr size_t size = type::size;
        };

#if XSIMD_CPP_VERSION < 201703L
        template <class T>
        constexpr size_t simd_traits_impl<T, true>::size;
#endif

        template <class T, class A>
        struct static_check_supported_config_emitter
        {

            static_assert(A::supported(),
                          "usage of batch type with unsupported architecture");
            static_assert(!A::supported() || xsimd::has_simd_register_v<T, A>,
                          "usage of batch type with unsupported type");
        };

        template <class A>
        struct static_check_supported_config_emitter<bool, A> : static_check_supported_config_emitter<xsimd::as_unsigned_integer_t<bool>, A>
        {
        };

        template <class T, class A>
        struct static_check_supported_config_emitter<std::complex<T>, A> : static_check_supported_config_emitter<T, A>
        {
        };

#ifdef XSIMD_ENABLE_XTL_COMPLEX
        template <class T, class A, bool i3ec>
        struct static_check_supported_config_emitter<xtl::xcomplex<T, T, i3ec>, A> : static_check_supported_config_emitter<T, A>
        {
        };
#endif

        // consistency checker
        template <class T, class A>
        XSIMD_INLINE void static_check_supported_config()
        {
            (void)static_check_supported_config_emitter<T, A>();
        }
    }

    template <class T>
    struct simd_traits : detail::simd_traits_impl<T, xsimd::has_simd_register_v<T>>
    {
    };

    template <class T>
    struct simd_traits<std::complex<T>>
        : detail::simd_traits_impl<std::complex<T>, xsimd::has_simd_register_v<T>>
    {
    };

#ifdef XSIMD_ENABLE_XTL_COMPLEX
    template <class T, bool i3ec>
    struct simd_traits<xtl::xcomplex<T, T, i3ec>>
        : detail::simd_traits_impl<std::complex<T>, xsimd::has_simd_register_v<T>>
    {
    };
#endif

    template <class T>
    struct revert_simd_traits
    {
        using type = T;
        static constexpr size_t size = simd_traits<type>::size;
    };

#if XSIMD_CPP_VERSION < 201703L
    template <class T>
    constexpr size_t revert_simd_traits<T>::size;
#endif

    template <class T>
    struct revert_simd_traits<batch<T>>
    {
        using type = T;
        static constexpr size_t size = batch<T>::size;
    };

#if XSIMD_CPP_VERSION < 201703L
    template <class T>
    constexpr size_t revert_simd_traits<batch<T>>::size;
#endif

    template <class T>
    using simd_type = typename simd_traits<T>::type;

    template <class T>
    using simd_bool_type = typename simd_traits<T>::bool_type;

    template <class T>
    using revert_simd_type = typename revert_simd_traits<T>::type;

    /********************
     * simd_return_type *
     ********************/

    namespace detail
    {
        template <class T1, class T2>
        struct simd_condition
        {
            static constexpr bool value = (std::is_same_v<T1, T2> && !std::is_same_v<T1, bool>) || (std::is_same_v<T1, bool> && !std::is_same_v<T2, bool>) || std::is_same_v<T1, float> || std::is_same_v<T1, double> || std::is_same_v<T1, int8_t> || std::is_same_v<T1, uint8_t> || std::is_same_v<T1, int16_t> || std::is_same_v<T1, uint16_t> || std::is_same_v<T1, int32_t> || std::is_same_v<T1, uint32_t> || std::is_same_v<T1, int64_t> || std::is_same_v<T1, uint64_t> || std::is_same_v<T1, char> || detail::is_complex_v<T1>;
        };

        template <class T1, class T2>
        inline constexpr bool simd_condition_v = simd_condition<T1, T2>::value;

        template <class T1, class T2, class A>
        struct simd_return_type_impl
            : std::enable_if<simd_condition_v<T1, T2>, batch<T2, A>>
        {
        };

        template <class T2, class A>
        struct simd_return_type_impl<bool, T2, A>
            : std::enable_if<simd_condition_v<bool, T2>, batch_bool<T2, A>>
        {
        };

        template <class T2, class A>
        struct simd_return_type_impl<bool, std::complex<T2>, A>
            : std::enable_if<simd_condition_v<bool, T2>, batch_bool<T2, A>>
        {
        };

        template <class T1, class T2, class A>
        struct simd_return_type_impl<std::complex<T1>, T2, A>
            : std::enable_if<simd_condition_v<T1, T2>, batch<std::complex<T2>, A>>
        {
        };

        template <class T1, class T2, class A>
        struct simd_return_type_impl<std::complex<T1>, std::complex<T2>, A>
            : std::enable_if<simd_condition_v<T1, T2>, batch<std::complex<T2>, A>>
        {
        };

#ifdef XSIMD_ENABLE_XTL_COMPLEX
        template <class T1, class T2, bool I3EC, class A>
        struct simd_return_type_impl<xtl::xcomplex<T1, T1, I3EC>, T2, A>
            : std::enable_if<simd_condition_v<T1, T2>, batch<std::complex<T2>, A>>
        {
        };

        template <class T1, class T2, bool I3EC, class A>
        struct simd_return_type_impl<xtl::xcomplex<T1, T1, I3EC>, std::complex<T2>, A>
            : std::enable_if<simd_condition_v<T1, T2>, batch<std::complex<T2>, A>>
        {
        };

        template <class T1, class T2, bool I3EC, class A>
        struct simd_return_type_impl<xtl::xcomplex<T1, T1, I3EC>, xtl::xcomplex<T2, T2, I3EC>, A>
            : std::enable_if<simd_condition_v<T1, T2>, batch<std::complex<T2>, A>>
        {
        };

        template <class T1, class T2, bool I3EC, class A>
        struct simd_return_type_impl<std::complex<T1>, xtl::xcomplex<T2, T2, I3EC>, A>
            : std::enable_if<simd_condition_v<T1, T2>, batch<std::complex<T2>, A>>
        {
        };
#endif
    }

    template <class T1, class T2, class A = default_arch>
    using simd_return_type = typename detail::simd_return_type_impl<T1, T2, A>::type;

    /**
     * @ingroup batch_traits
     *
     * type traits that provide information about a batch or scalar type.
     *
     * @tparam T type to analyze.
     */

    template <class T>
    struct batch_traits
    {
        using scalar_type = T; ///< T if scalar, or type of the scalar element for the batch T.
        using mask_type = bool; ///< Mask type for T: bool for scalars, or batch_bool for batch types.

        static constexpr bool is_batch = false; ///< True if T is @c batch<...>.
        static constexpr bool is_batch_bool = false; ///< True if T is @c batch_bool<...>.
        static constexpr bool is_any_batch = false; ///< True if T is @c batch<...> or @c batch_bool<...>.
        static constexpr bool is_complex = detail::is_complex_v<T>; ///< True if T is complex or a batch of complex values.
    };

#if XSIMD_CPP_VERSION < 201703L
    template <class T>
    constexpr bool batch_traits<T>::is_batch;
    template <class T>
    constexpr bool batch_traits<T>::is_batch_bool;
    template <class T>
    constexpr bool batch_traits<T>::is_any_batch;
    template <class T>
    constexpr bool batch_traits<T>::is_complex;
#endif

    template <class T, class A>
    struct batch_traits<batch<T, A>>
    {
        using scalar_type = T;
        using mask_type = typename batch<T, A>::batch_bool_type;

        static constexpr bool is_batch = true;
        static constexpr bool is_batch_bool = false;
        static constexpr bool is_any_batch = true;
        static constexpr bool is_complex = detail::is_complex_v<T>;
    };

#if XSIMD_CPP_VERSION < 201703L
    template <class T, class A>
    constexpr bool batch_traits<batch<T, A>>::is_batch;
    template <class T, class A>
    constexpr bool batch_traits<batch<T, A>>::is_batch_bool;
    template <class T, class A>
    constexpr bool batch_traits<batch<T, A>>::is_any_batch;
    template <class T, class A>
    constexpr bool batch_traits<batch<T, A>>::is_complex;
#endif

    template <class T, class A>
    struct batch_traits<batch_bool<T, A>>
    {
        using scalar_type = bool;
        using mask_type = batch_bool<T, A>;

        static constexpr bool is_batch = false;
        static constexpr bool is_batch_bool = true;
        static constexpr bool is_any_batch = true;
        static constexpr bool is_complex = false;
    };

#if XSIMD_CPP_VERSION < 201703L
    template <class T, class A>
    constexpr bool batch_traits<batch_bool<T, A>>::is_batch;
    template <class T, class A>
    constexpr bool batch_traits<batch_bool<T, A>>::is_batch_bool;
    template <class T, class A>
    constexpr bool batch_traits<batch_bool<T, A>>::is_any_batch;
    template <class T, class A>
    constexpr bool batch_traits<batch_bool<T, A>>::is_complex;
#endif

    /**
     * @ingroup batch_traits
     *
     * type traits that inherits from @c std::true_type for @c batch<...> types and from
     * @c std::false_type otherwise.
     *
     * @tparam T type to analyze.
     */

    template <class T>
    struct is_batch : std::integral_constant<bool, batch_traits<T>::is_batch>
    {
    };

    /**
     * @ingroup batch_traits
     *
     * type traits that inherits from @c std::true_type for @c batch_bool<...> types and from
     * @c std::false_type otherwise.
     *
     * @tparam T type to analyze.
     */

    template <class T>
    struct is_batch_bool : std::integral_constant<bool, batch_traits<T>::is_batch_bool>
    {
    };

    /**
     * @ingroup batch_traits
     *
     * type traits that inherits from @c std::true_type for @c batch<...> or batch_bool<...>
     * types and from @c std::false_type otherwise.
     *
     * @tparam T type to analyze.
     */

    template <class T>
    struct is_any_batch : std::integral_constant<bool, batch_traits<T>::is_any_batch>
    {
    };

    /**
     * @ingroup batch_traits
     *
     * type traits that inherits from @c std::true_type for @c batch<std::complex<...>>
     * types and from @c std::false_type otherwise.
     *
     * @tparam T type to analyze.
     */

    template <class T>
    struct is_batch_complex : std::integral_constant<bool, batch_traits<T>::is_batch && batch_traits<T>::is_complex>
    {
    };

    /**
     * @ingroup batch_traits
     *
     * type traits whose @c type field is set to @c T::value_type if @c
     * is_batch_v<T> and to @c T otherwise.
     *
     * @tparam T type to analyze.
     */
    template <class T>
    struct scalar_type
    {
        using type = typename batch_traits<T>::scalar_type;
    };

    template <class T>
    using scalar_type_t = typename scalar_type<T>::type;

    /**
     * @ingroup batch_traits
     *
     * type traits whose @c type field is set to @c T::value_type if @c
     * is_batch_bool_v<T> and to @c bool otherwise.
     *
     * @tparam T type to analyze.
     */
    template <class T>
    struct mask_type
    {
        using type = typename batch_traits<T>::mask_type;
    };

    template <class T>
    using mask_type_t = typename mask_type<T>::type;
}

#endif
