/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ***************************************************************************/

#ifndef XSIMD_CPU_FEATURES_PPC_HPP
#define XSIMD_CPU_FEATURES_PPC_HPP

#include "./xsimd_config.hpp"
#include "./xsimd_getauxval.hpp"

namespace xsimd
{
    /**
     * An opinionated CPU feature detection utility for PowerPC.
     *
     * On Linux, runtime detection uses getauxval to query the auxiliary vector.
     * On other platforms, only compile-time information is used.
     *
     * This is well defined on all architectures.
     * It will always return false on non-PowerPC architectures.
     */
    class ppc_cpu_features : private linux_hwcap_backend_default
    {
    public:
        inline bool vsx() const noexcept;
    };

    /********************
     *  Implementation  *
     ********************/

    inline bool ppc_cpu_features::vsx() const noexcept
    {
#if XSIMD_TARGET_PPC && XSIMD_HAVE_LINUX_GETAUXVAL
#ifdef PPC_FEATURE_HAS_VSX
        return hwcap().has_feature(PPC_FEATURE_HAS_VSX);
#else
        // Possibly missing on older Linux distributions
        return hwcap().has_feature(0x00000080);
#endif
#else
        return XSIMD_WITH_VSX;
#endif
    }
}

#endif
