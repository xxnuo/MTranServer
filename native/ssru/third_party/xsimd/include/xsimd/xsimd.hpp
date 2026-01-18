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

#ifndef XSIMD_HPP
#define XSIMD_HPP

#include "./arch/xsimd_scalar.hpp"
#include "./config/xsimd_config.hpp"
#include "./config/xsimd_macros.hpp"
#include "./memory/xsimd_aligned_allocator.hpp"
#include "./types/xsimd_batch_fwd.hpp"

#if defined(XSIMD_NO_SUPPORTED_ARCHITECTURE)
namespace xsimd
{
    // no type definition or anything apart from scalar definition and aligned allocator
    template <class T, class A>
    class batch
    {
        static constexpr bool supported_architecture = sizeof(A*) == 0; // type-dependant but always false
        static_assert(supported_architecture, "No SIMD architecture detected, cannot instantiate a batch");
    };
}

#else
#include "./types/xsimd_batch.hpp"
#include "./types/xsimd_batch_constant.hpp"
#include "./types/xsimd_traits.hpp"

// This include must come last
#include "./types/xsimd_api.hpp"
#endif // XSIMD_NO_SUPPORTED_ARCHITECTURE

#endif
