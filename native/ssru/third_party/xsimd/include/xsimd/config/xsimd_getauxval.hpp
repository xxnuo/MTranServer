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

#ifndef XSIMD_GETAUXVAL_HPP
#define XSIMD_GETAUXVAL_HPP

#include "../utils/bits.hpp"
#include "./xsimd_config.hpp"

#if XSIMD_HAVE_LINUX_GETAUXVAL
#include <sys/auxv.h> // getauxval
#endif

namespace xsimd
{
    namespace detail
    {
        using linux_getauxval_t = unsigned long;

        inline linux_getauxval_t linux_getauxval(linux_getauxval_t type) noexcept;
    }

    /*
     * Holds the value of a Linux auxiliary vector entry (e.g. AT_HWCAP).
     *
     * On Linux systems, the kernel exposes some CPU features through the
     * auxiliary vector, which can be queried via `getauxval(AT_HWCAP)`.
     * Well defined on all platforms, and will return always falsw on
     * non-linux platforms.
     *
     * Usage:
     *   auto hwcap = linux_auxval::read(AT_HWCAP);
     *   bool neon = hwcap.has_feature(HWCAP_NEON);
     *
     * @see https://www.kernel.org/doc/Documentation/arm64/elf_hwcaps.txt
     */
    class linux_auxval
    {
    private:
        using getauxval_t = detail::linux_getauxval_t;

    public:
        constexpr linux_auxval() noexcept = default;

        inline static linux_auxval read(getauxval_t type) noexcept
        {
            return linux_auxval(detail::linux_getauxval(type));
        }

        constexpr bool has_feature(getauxval_t feat) const noexcept
        {
            return (m_auxval & feat) == feat;
        }

    private:
        getauxval_t m_auxval = {};

        constexpr explicit linux_auxval(getauxval_t v) noexcept
            : m_auxval(v)
        {
        }
    };

    class linux_hwcap_backend
    {
    public:
        inline linux_auxval hwcap() const noexcept;

        inline linux_auxval hwcap2() const noexcept;

    private:
        enum class status
        {
            hwcap_valid = 0,
            hwcap2_valid = 1,
        };

        using status_bitset = utils::uint_bitset<status, std::uint32_t>;

        mutable status_bitset m_status {};
        mutable xsimd::linux_auxval m_hwcap {};
        mutable xsimd::linux_auxval m_hwcap2 {};
    };

    class linux_hwcap_backend_noop
    {
    public:
        inline linux_auxval hwcap() const noexcept { return {}; }

        inline linux_auxval hwcap2() const noexcept { return {}; }
    };

#if XSIMD_HAVE_LINUX_GETAUXVAL
    using linux_hwcap_backend_default = linux_hwcap_backend;
#else
    // Contrary to CPUID that is only used on one architecture, HWCAP are
    // available on multiple architectures with different meaning for the
    // different bit fields.
    // We use the Linux `HWCAP` constants directly to avoid repetition, so
    // we could not use a default implementation without already being on
    // Linux anyways.
    struct linux_hwcap_backend_default
    {
    };
#endif

    /********************
     *  Implementation  *
     ********************/

    namespace detail
    {
#if XSIMD_HAVE_LINUX_GETAUXVAL
        inline linux_getauxval_t linux_getauxval(linux_getauxval_t type) noexcept
        {
            return getauxval(type);
        }
#else
        inline linux_getauxval_t linux_getauxval(linux_getauxval_t) noexcept
        {
            return {}; // All bits set to 0
        }
#endif
    }

    inline linux_auxval linux_hwcap_backend::hwcap() const noexcept
    {
        if (!m_status.bit_is_set<status::hwcap_valid>())
        {
#if XSIMD_HAVE_LINUX_GETAUXVAL
            m_hwcap = linux_auxval::read(AT_HWCAP);
#endif
            m_status.set_bit<status::hwcap_valid>();
        }
        return m_hwcap;
    }

    inline linux_auxval linux_hwcap_backend::hwcap2() const noexcept
    {
        if (!m_status.bit_is_set<status::hwcap2_valid>())
        {
#if XSIMD_HAVE_LINUX_GETAUXVAL
            m_hwcap2 = linux_auxval::read(AT_HWCAP2);
#endif
            m_status.set_bit<status::hwcap2_valid>();
        }
        return m_hwcap2;
    }
}

#endif
