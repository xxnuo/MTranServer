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

#ifndef XSIMD_CPU_FEATURES_RISCV_HPP
#define XSIMD_CPU_FEATURES_RISCV_HPP

#include "./xsimd_config.hpp"
#include "./xsimd_getauxval.hpp"

#include <cstddef>
#include <cstdint>

#if XSIMD_TARGET_RISCV && XSIMD_HAVE_LINUX_GETAUXVAL
// HWCAP_XXX masks to use on getauxval results.
// Header does not exists on all architectures and masks are architecture
// specific.
#include <asm/hwcap.h>
#endif // XSIMD_TARGET_RISCV && XSIMD_HAVE_LINUX_GETAUXVAL

namespace xsimd
{
    namespace detail
    {
        using riscv_reg64_t = std::uint64_t;

        /**
         * Return the RVV vector length in bytes.
         *
         * This does not require to be compiles with SVE, which should not
         * be done in a dynamic dispatch jump function.
         *
         * Safety: It is the user responsibility to first make sure that RVV is
         * available.
         */
        inline riscv_reg64_t riscv_csrr_unsafe();
    }

    class riscv_cpu_features : private linux_hwcap_backend_default
    {
    public:
        inline bool rvv() const noexcept;
        inline std::size_t rvv_size_bytes() const noexcept;
    };

    /********************
     *  Implementation  *
     ********************/

    namespace detail
    {
#if XSIMD_TARGET_RISCV && (defined(__GNUC__) || defined(__clang__))
        __attribute__((target("arch=+v"))) inline riscv_reg64_t riscv_csrr_unsafe()
        {
            riscv_reg64_t vlenb;
            __asm__ volatile("csrr %0, vlenb" : "=r"(vlenb));
            return vlenb;
        }
#else
        inline riscv_reg64_t riscv_csrr_unsafe() { return 0; }
#endif
    }

    inline bool riscv_cpu_features::rvv() const noexcept
    {
#if XSIMD_TARGET_RISCV && XSIMD_HAVE_LINUX_GETAUXVAL
#ifdef HWCAP_V
        return hwcap().has_feature(HWCAP_V);
#else
        // Possibly missing on older Linux distributions
        return hwcap().has_feature(1 << ('V' - 'A'));
#endif
#else
        return false;
#endif
    }

    inline std::size_t riscv_cpu_features::rvv_size_bytes() const noexcept
    {
        if (rvv())
        {
            return detail::riscv_csrr_unsafe();
        }
        return 0;
    }
}

#endif
