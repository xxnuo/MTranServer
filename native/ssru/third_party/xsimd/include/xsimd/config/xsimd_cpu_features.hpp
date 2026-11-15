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

#ifndef XSIMD_CPU_FEATURES_HPP
#define XSIMD_CPU_FEATURES_HPP

#include "./xsimd_cpu_features_arm.hpp"
#include "./xsimd_cpu_features_ppc.hpp"
#include "./xsimd_cpu_features_riscv.hpp"
#include "./xsimd_cpu_features_s390x.hpp"
#include "./xsimd_cpu_features_x86.hpp"

namespace xsimd
{

    /**
     * Cross-platform CPU feature detection class.
     *
     * All member functions are safe to work on with all platforms.
     *
     * @warning This class is *not* thread safe.
     * Its internal lazy querying structure makes even `const` member function prone to data race.
     * The structure is also generally not appropriate for directly branching (e.g. on
     * ``cpu_features::avx2``) because it include a branch that the compiler cannot optimize.
     * The current appropriate way to use this class for dynamic dispatching is to store the
     * result of the function calls (e.g. @ref cpu_features) into (static) constants.
     * This is done in @ref xsimd::available_architectures.
     *
     * @see xsimd::dispatch
     * @see xsimd::available_architectures
     */
    class cpu_features : public s390x_cpu_features,
                         public ppc_cpu_features,
                         public riscv_cpu_features,
                         public arm_cpu_features,
                         public x86_cpu_features
    {
    };

}

#endif
