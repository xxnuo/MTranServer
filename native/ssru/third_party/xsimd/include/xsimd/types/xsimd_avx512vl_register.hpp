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

#ifndef XSIMD_AVX512VL_REGISTER_HPP
#define XSIMD_AVX512VL_REGISTER_HPP

#include "./xsimd_avx512cd_register.hpp"
#include "./xsimd_fma3_avx2_128_register.hpp"

namespace xsimd
{

    /**
     * @ingroup architectures
     *
     * AVX512VL instructions
     */
    struct avx512vl : avx512cd
    {
        static constexpr bool supported() noexcept { return XSIMD_WITH_AVX512VL; }
        static constexpr bool available() noexcept { return true; }
        static constexpr char const* name() noexcept { return "avx512vl"; }
    };

    /**
     * @ingroup architectures
     *
     * AVX512VL instructions extension for 128 bits registers
     */
    struct avx512vl_128 : fma3<avx2_128>
    {
        static constexpr bool supported() noexcept { return XSIMD_WITH_AVX512VL; }
        static constexpr bool available() noexcept { return true; }
        static constexpr char const* name() noexcept { return "avx512vl/128"; }
    };

    /**
     * @ingroup architectures
     *
     * AVX512VL instructions extension for 256 bits registers
     */
    struct avx512vl_256 : fma3<avx2>
    {
        static constexpr bool supported() noexcept { return XSIMD_WITH_AVX512VL; }
        static constexpr bool available() noexcept { return true; }
        static constexpr char const* name() noexcept { return "avx512vl/256"; }
    };

#if XSIMD_WITH_AVX512VL

#if !XSIMD_WITH_AVX512CD
#error "architecture inconsistency: avx512vl requires avx512cd"
#endif

    namespace types
    {
        template <class T>
        struct get_bool_simd_register<T, avx512vl>
        {
            using type = simd_avx512_bool_register<T>;
        };

        XSIMD_DECLARE_SIMD_REGISTER_ALIAS(avx512vl, avx512cd);

        template <class T>
        struct get_bool_simd_register<T, avx512vl_128>
        {
            using type = simd_avx512_bool_register<T>;
        };
        XSIMD_DECLARE_SIMD_REGISTER_ALIAS(avx512vl_128, avx2_128);

        template <class T>
        struct get_bool_simd_register<T, avx512vl_256>
        {
            using type = simd_avx512_bool_register<T>;
        };
        XSIMD_DECLARE_SIMD_REGISTER_ALIAS(avx512vl_256, avx2);
    }
#endif
}
#endif
