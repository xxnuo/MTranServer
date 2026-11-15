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

#ifndef XSIMD_AVX512VBMI2_REGISTER_HPP
#define XSIMD_AVX512VBMI2_REGISTER_HPP

#include "./xsimd_avx512vbmi_register.hpp"

namespace xsimd
{

    /**
     * @ingroup architectures
     *
     * AVX512VBMI instructions
     */
    struct avx512vbmi2 : avx512vbmi
    {
        static constexpr bool supported() noexcept { return XSIMD_WITH_AVX512VBMI2; }
        static constexpr bool available() noexcept { return true; }
        static constexpr char const* name() noexcept { return "avx512vbmi2"; }
    };

#if XSIMD_WITH_AVX512VBMI2

#if !XSIMD_WITH_AVX512VBMI
#error "architecture inconsistency: avx512vbmi2 requires avx512vbmi"
#endif

    namespace types
    {
        template <class T>
        struct get_bool_simd_register<T, avx512vbmi2>
        {
            using type = simd_avx512_bool_register<T>;
        };

        XSIMD_DECLARE_SIMD_REGISTER_ALIAS(avx512vbmi2, avx512vbmi);

    }
#endif
}
#endif
