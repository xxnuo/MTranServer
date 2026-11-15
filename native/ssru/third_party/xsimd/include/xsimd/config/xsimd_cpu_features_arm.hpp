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

#ifndef XSIMD_CPU_FEATURES_ARM_HPP
#define XSIMD_CPU_FEATURES_ARM_HPP

#include "./xsimd_config.hpp"
#include "./xsimd_getauxval.hpp"

#include <cstddef>
#include <cstdint>

#if XSIMD_TARGET_ARM && XSIMD_HAVE_LINUX_GETAUXVAL
// HWCAP_XXX masks to use on getauxval results.
// Header does not exists on all architectures and masks are architecture
// specific.
#include <asm/hwcap.h>
#endif // XSIMD_TARGET_ARM && XSIMD_HAVE_LINUX_GETAUXVAL

namespace xsimd
{

    namespace detail
    {
        using arm_reg64_t = std::uint64_t;

        /**
         * Return the SVE vector length in bytes for the current thread.
         *
         * SVE vector length can be restricted
         * Contrary to `svcntb` this does not require to be compiles with SVE, which
         * should not be done in a dynamic dispatch jump function.
         *
         * Safety: It is the user responsibility to first make sure that SVE is
         * available.
         */
        inline arm_reg64_t arm_rdvl_unsafe();
    }

    /**
     * An opinionated CPU feature detection utility for ARM.
     *
     * Combines compile-time knowledge with runtime detection when available.
     * On Linux, runtime detection uses getauxval to query the auxiliary vector.
     * On other platforms, only compile-time information is used.
     *
     * This is well defined on all architectures.
     * It will always return false on non-ARM architectures.
     */
    class arm_cpu_features : private linux_hwcap_backend_default
    {
    public:
        inline bool neon() const noexcept;
        inline bool neon64() const noexcept;
        inline bool sve() const noexcept;
        inline std::size_t sve_size_bytes() const noexcept;
        inline bool i8mm() const noexcept;
    };

    /********************
     *  Implementation  *
     ********************/

    namespace detail
    {
#if XSIMD_TARGET_ARM64 && (defined(__GNUC__) || defined(__clang__))
        __attribute__((target("arch=armv8-a+sve"))) inline arm_reg64_t arm_rdvl_unsafe()
        {
            arm_reg64_t vl;
            __asm__ volatile("rdvl %0, #1" : "=r"(vl));
            return vl;
        }
#else
        inline arm_reg64_t arm_rdvl_unsafe() { return 0; }
#endif
    }

    inline bool arm_cpu_features::neon() const noexcept
    {
#if XSIMD_TARGET_ARM && !XSIMD_TARGET_ARM64 && XSIMD_HAVE_LINUX_GETAUXVAL
        return hwcap().has_feature(HWCAP_NEON);
#else
        return static_cast<bool>(XSIMD_WITH_NEON);
#endif
    }

    inline bool arm_cpu_features::neon64() const noexcept
    {
        return static_cast<bool>(XSIMD_WITH_NEON64);
    }

    inline bool arm_cpu_features::sve() const noexcept
    {
#if XSIMD_TARGET_ARM64 && XSIMD_HAVE_LINUX_GETAUXVAL
        return hwcap().has_feature(HWCAP_SVE);
#else
        return false;
#endif
    }

    inline std::size_t arm_cpu_features::sve_size_bytes() const noexcept
    {
        if (sve())
        {
            return detail::arm_rdvl_unsafe();
        }
        return 0;
    }

    inline bool arm_cpu_features::i8mm() const noexcept
    {
#if XSIMD_TARGET_ARM64 && XSIMD_HAVE_LINUX_GETAUXVAL
#ifdef HWCAP2_I8MM
        return hwcap2().has_feature(HWCAP2_I8MM);
#else
        // Possibly missing on older Linux distributions
        return hwcap2().has_feature(1 << 13);
#endif
#else
        return false;
#endif
    }
}
#endif
