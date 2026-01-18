/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 * Copyright (c) Yibo Cai                                                   *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_SVE_REGISTER_HPP
#define XSIMD_SVE_REGISTER_HPP

#include "./xsimd_common_arch.hpp"
#include "./xsimd_register.hpp"

#if XSIMD_WITH_SVE
#include <arm_sve.h>
#endif

namespace xsimd
{
    namespace detail
    {
        /**
         * @ingroup architectures
         *
         * SVE instructions (fixed vector size) for arm64
         */
        template <size_t Width>
        struct sve : xsimd::common
        {
            static constexpr bool supported() noexcept { return Width == XSIMD_SVE_BITS; }
            static constexpr bool available() noexcept { return true; }
            static constexpr bool requires_alignment() noexcept { return true; }
            static constexpr std::size_t alignment() noexcept { return 16; }
            static constexpr char const* name() noexcept { return "arm64+sve"; }
        };
    }

#if XSIMD_WITH_SVE

    using sve = detail::sve<__ARM_FEATURE_SVE_BITS>;

    namespace types
    {
        namespace detail
        {
// define fixed size alias per SVE sizeless type
#define SVE_TO_FIXED_SIZE(ty) ty __attribute__((arm_sve_vector_bits(__ARM_FEATURE_SVE_BITS)))
            using sve_int8_t = SVE_TO_FIXED_SIZE(svint8_t);
            using sve_uint8_t = SVE_TO_FIXED_SIZE(svuint8_t);
            using sve_int16_t = SVE_TO_FIXED_SIZE(svint16_t);
            using sve_uint16_t = SVE_TO_FIXED_SIZE(svuint16_t);
            using sve_int32_t = SVE_TO_FIXED_SIZE(svint32_t);
            using sve_uint32_t = SVE_TO_FIXED_SIZE(svuint32_t);
            using sve_int64_t = SVE_TO_FIXED_SIZE(svint64_t);
            using sve_uint64_t = SVE_TO_FIXED_SIZE(svuint64_t);
            using sve_float32_t = SVE_TO_FIXED_SIZE(svfloat32_t);
            using sve_float64_t = SVE_TO_FIXED_SIZE(svfloat64_t);
            using sve_bool_t = SVE_TO_FIXED_SIZE(svbool_t);
#undef SVE_TO_FIXED_SIZE

            template <size_t S>
            struct sve_vector_type_impl;

            template <>
            struct sve_vector_type_impl<1>
            {
                using signed_type = sve_int8_t;
                using unsigned_type = sve_uint8_t;
                using floating_point_type = void;
                using sizeless_unsigned_type = svuint8_t;
                using sizeless_signed_type = svint8_t;
                using sizeless_floating_point_type = void;
            };

            template <>
            struct sve_vector_type_impl<2>
            {
                using signed_type = sve_int16_t;
                using unsigned_type = sve_uint16_t;
                using floating_point_type = void;
                using sizeless_unsigned_type = svuint16_t;
                using sizeless_signed_type = svint16_t;
                using sizeless_floating_point_type = void;
            };

            template <>
            struct sve_vector_type_impl<4>
            {
                using signed_type = sve_int32_t;
                using unsigned_type = sve_uint32_t;
                using floating_point_type = sve_float32_t;
                using sizeless_unsigned_type = svuint32_t;
                using sizeless_signed_type = svint32_t;
                using sizeless_floating_point_type = svfloat32_t;
            };

            template <>
            struct sve_vector_type_impl<8>
            {
                using signed_type = sve_int64_t;
                using unsigned_type = sve_uint64_t;
                using floating_point_type = sve_float64_t;
                using sizeless_unsigned_type = svuint64_t;
                using sizeless_signed_type = svint64_t;
                using sizeless_floating_point_type = svfloat64_t;
            };

            template <class T>
            using signed_int_sve_vector_type = typename sve_vector_type_impl<sizeof(T)>::signed_type;

            template <class T>
            using unsigned_int_sve_vector_type = typename sve_vector_type_impl<sizeof(T)>::unsigned_type;

            template <class T>
            using floating_point_sve_vector_type = typename sve_vector_type_impl<sizeof(T)>::floating_point_type;

            template <class T>
            using sizeless_signed_int_sve_vector_type = typename sve_vector_type_impl<sizeof(T)>::sizeless_signed_type;

            template <class T>
            using sizeless_unsigned_int_sve_vector_type = typename sve_vector_type_impl<sizeof(T)>::sizeless_unsigned_type;

            template <class T>
            using sizeless_floating_point_sve_vector_type = typename sve_vector_type_impl<sizeof(T)>::sizeless_floating_point_type;

            template <typename T, typename = void>
            struct sve_vector_impl;

            template <typename T>
            struct sve_vector_impl<T, std::enable_if_t<std::is_floating_point_v<T>>>
            {
                using type = floating_point_sve_vector_type<T>;
            };

            template <typename T>
            struct sve_vector_impl<T, std::enable_if_t<!std::is_floating_point_v<T> && std::is_signed_v<T>>>
            {
                using type = signed_int_sve_vector_type<T>;
            };

            template <typename T>
            struct sve_vector_impl<T, std::enable_if_t<!std::is_floating_point_v<T> && std::is_unsigned_v<T>>>
            {
                using type = unsigned_int_sve_vector_type<T>;
            };

            template <typename T, typename = void>
            struct sizeless_sve_vector_impl;

            template <typename T>
            struct sizeless_sve_vector_impl<T, std::enable_if_t<std::is_floating_point_v<T>>>
            {
                using type = sizeless_floating_point_sve_vector_type<T>;
            };

            template <typename T>
            struct sizeless_sve_vector_impl<T, std::enable_if_t<!std::is_floating_point_v<T> && std::is_signed_v<T>>>
            {
                using type = sizeless_signed_int_sve_vector_type<T>;
            };

            template <typename T>
            struct sizeless_sve_vector_impl<T, std::enable_if_t<!std::is_floating_point_v<T> && std::is_unsigned_v<T>>>
            {
                using type = sizeless_unsigned_int_sve_vector_type<T>;
            };

            template <class T>
            using sve_vector_type = typename detail::sve_vector_impl<T>::type;

            template <class T>
            using sizeless_sve_vector_type = typename detail::sizeless_sve_vector_impl<T>::type;

        } // namespace detail

        XSIMD_DECLARE_SIMD_REGISTER(signed char, sve, detail::sve_vector_type<signed char>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned char, sve, detail::sve_vector_type<unsigned char>);
        XSIMD_DECLARE_SIMD_REGISTER(char, sve, detail::sve_vector_type<char>);
        XSIMD_DECLARE_SIMD_REGISTER(short, sve, detail::sve_vector_type<short>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned short, sve, detail::sve_vector_type<unsigned short>);
        XSIMD_DECLARE_SIMD_REGISTER(int, sve, detail::sve_vector_type<int>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned int, sve, detail::sve_vector_type<unsigned int>);
        XSIMD_DECLARE_SIMD_REGISTER(long int, sve, detail::sve_vector_type<long int>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned long int, sve, detail::sve_vector_type<unsigned long int>);
        XSIMD_DECLARE_SIMD_REGISTER(long long int, sve, detail::sve_vector_type<long long int>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned long long int, sve, detail::sve_vector_type<unsigned long long int>);
        XSIMD_DECLARE_SIMD_REGISTER(float, sve, detail::sve_vector_type<float>);
        XSIMD_DECLARE_SIMD_REGISTER(double, sve, detail::sve_vector_type<double>);

        namespace detail
        {
            struct sve_bool_simd_register
            {
                using register_type = sve_bool_t;
                register_type data;
                operator register_type() const noexcept { return data; }
            };
        } // namespace detail

        template <class T>
        struct get_bool_simd_register<T, sve>
        {
            using type = detail::sve_bool_simd_register;
        };
    } // namespace types
#else
    using sve = detail::sve<0xFFFFFFFF>;
#endif
} // namespace xsimd

#endif
