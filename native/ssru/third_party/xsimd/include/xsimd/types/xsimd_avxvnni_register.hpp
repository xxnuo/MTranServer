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

#ifndef XSIMD_AVXVNNI_REGISTER_HPP
#define XSIMD_AVXVNNI_REGISTER_HPP

#include "./xsimd_avx2_register.hpp"
#include "./xsimd_fma3_avx2_register.hpp"

namespace xsimd
{
    /**
     * @ingroup architectures
     *
     * AVXVNNI instructions
     */
    // Derive from fma3<avx2> rather than avx2 so the FMA3 kernels (fnma/fnms ->
    // vfnmadd) are in avxvnni's dispatch chain instead of the generic neg(x*y)+z
    // fallback. fma3<avx2> always derives from avx2 and its kernels are only
    // registered when XSIMD_WITH_FMA3_AVX2, so when FMA is disabled this base is
    // transparent (dispatch falls straight through to avx2).
    struct avxvnni : fma3<avx2>
    {
        static constexpr bool supported() noexcept { return XSIMD_WITH_AVXVNNI; }
        static constexpr bool available() noexcept { return true; }
        static constexpr char const* name() noexcept { return "avxvnni"; }
    };

#if XSIMD_WITH_AVXVNNI

#if !XSIMD_WITH_AVX2
#error "architecture inconsistency: avxvnni requires avx2"
#endif

    namespace types
    {
        XSIMD_DECLARE_SIMD_REGISTER_ALIAS(avxvnni, avx2);
    }
#endif
}

#endif
