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

#ifndef XSIMD_BATCH_FWD_HPP
#define XSIMD_BATCH_FWD_HPP

#include "../config/xsimd_config.hpp"

// TODO this is somehow redundant with XSIMD_DEFAULT_ARCH but is only supported
// when an architecture is defined.
#if defined(XSIMD_NO_SUPPORTED_ARCHITECTURE)
#define XSIMD_BATCH_DEFAULT_ARCH_IMPL void
#else
#include "../config/xsimd_arch.hpp"
#define XSIMD_BATCH_DEFAULT_ARCH_IMPL default_arch
#endif // XSIMD_NO_SUPPORTED_ARCHITECTURE

namespace xsimd
{
    template <class T, class A = XSIMD_BATCH_DEFAULT_ARCH_IMPL>
    class batch_bool;

    template <typename T, class A, bool... Values>
    struct batch_bool_constant;

    template <class T, class A = XSIMD_BATCH_DEFAULT_ARCH_IMPL>
    class batch;

    template <typename T, class A, T... Values>
    struct batch_constant;
}

#endif
