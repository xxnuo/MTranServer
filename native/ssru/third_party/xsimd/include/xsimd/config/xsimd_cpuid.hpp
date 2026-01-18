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

#ifndef XSIMD_CPUID_HPP
#define XSIMD_CPUID_HPP

#include "../types/xsimd_all_registers.hpp"
#include "./xsimd_cpu_features.hpp"
#include "./xsimd_macros.hpp"

namespace xsimd
{
    namespace detail
    {
        struct supported_arch
        {

#define ARCH_FIELD_EX(arch, field_name) \
    unsigned field_name = 0;            \
    XSIMD_INLINE bool has(::xsimd::arch) const { return this->field_name; }

#define ARCH_FIELD_EX_REUSE(arch, field_name) \
    XSIMD_INLINE bool has(::xsimd::arch) const { return this->field_name; }

#define ARCH_FIELD(name) ARCH_FIELD_EX(name, name)

            ARCH_FIELD(sse2)
            ARCH_FIELD(sse3)

            ARCH_FIELD(ssse3)
            ARCH_FIELD(sse4_1)
            ARCH_FIELD(sse4_2)
            // ARCH_FIELD(sse4a)
            ARCH_FIELD_EX(fma3<::xsimd::sse4_2>, fma3_sse42)
            ARCH_FIELD(fma4)
            // ARCH_FIELD(xop)
            ARCH_FIELD(avx)
            ARCH_FIELD_EX(fma3<::xsimd::avx>, fma3_avx)
            ARCH_FIELD(avx2)
            ARCH_FIELD(avxvnni)
            ARCH_FIELD_EX(fma3<::xsimd::avx2>, fma3_avx2)
            ARCH_FIELD(avx512f)
            ARCH_FIELD(avx512cd)
            ARCH_FIELD(avx512vl)
            ARCH_FIELD(avx512dq)
            ARCH_FIELD(avx512bw)
            ARCH_FIELD(avx512er)
            ARCH_FIELD(avx512pf)
            ARCH_FIELD(avx512ifma)
            ARCH_FIELD(avx512vbmi)
            ARCH_FIELD(avx512vbmi2)
            ARCH_FIELD_EX(avx512vnni<::xsimd::avx512bw>, avx512vnni_bw)
            ARCH_FIELD_EX(avx512vnni<::xsimd::avx512vbmi2>, avx512vnni_vbmi2)
            ARCH_FIELD(neon)
            ARCH_FIELD(neon64)
            ARCH_FIELD_EX(i8mm<::xsimd::neon64>, i8mm_neon64)
            ARCH_FIELD_EX(detail::sve<512>, sve512)
            ARCH_FIELD_EX(detail::sve<256>, sve256)
            ARCH_FIELD_EX(detail::sve<128>, sve128)
            ARCH_FIELD_EX(detail::rvv<512>, rvv512)
            ARCH_FIELD_EX(detail::rvv<256>, rvv256)
            ARCH_FIELD_EX(detail::rvv<128>, rvv128)
            ARCH_FIELD(wasm)
            ARCH_FIELD(vsx)
            ARCH_FIELD(vxe)

#undef ARCH_FIELD

            XSIMD_INLINE supported_arch() noexcept
            {
#if XSIMD_WITH_WASM
                wasm = 1;
#endif

                const auto cpu = xsimd::cpu_features();

                vxe = cpu.vxe();

                vsx = cpu.vsx();

                rvv128 = cpu.rvv() && (cpu.rvv_size_bytes() >= (128 / 8));
                rvv256 = cpu.rvv() && (cpu.rvv_size_bytes() >= (256 / 8));
                rvv512 = cpu.rvv() && (cpu.rvv_size_bytes() >= (512 / 8));

                neon = cpu.neon();
                neon64 = cpu.neon64();
                i8mm_neon64 = cpu.neon64() && cpu.i8mm();

                // Running SVE128 on a SVE256 machine is more tricky than the x86 equivalent
                // of running SSE code on an AVX machine and requires to explicitly change the
                // vector length using `prctl` (per thread setting).
                // This is something we have not tested and not integrated in xsimd so the safe
                // default is to assume only one valid SVE width at runtime.
                sve128 = cpu.sve() && (cpu.sve_size_bytes() * 8 == 128);
                sve256 = cpu.sve() && (cpu.sve_size_bytes() * 8 == 256);
                sve512 = cpu.sve() && (cpu.sve_size_bytes() * 8 == 512);

                sse2 = cpu.sse2();
                sse3 = cpu.sse3();
                ssse3 = cpu.ssse3();
                sse4_1 = cpu.sse4_1();
                sse4_2 = cpu.sse4_2();
                fma3_sse42 = cpu.fma3();

                // sse4a not implemented in cpu_id yet
                // xop not implemented in cpu_id yet

                avx = cpu.avx();
                fma3_avx = avx && fma3_sse42;
                fma4 = cpu.fma4();
                avx2 = cpu.avx2();
                avxvnni = cpu.avxvnni();
                fma3_avx2 = avx2 && fma3_sse42;

                avx512f = cpu.avx512f();
                avx512cd = cpu.avx512cd();
                avx512vl = cpu.avx512vl();
                avx512dq = cpu.avx512dq();
                avx512bw = cpu.avx512bw();
                avx512er = cpu.avx512er();
                avx512pf = cpu.avx512pf();
                avx512ifma = cpu.avx512ifma();
                avx512vbmi = cpu.avx512vbmi();
                avx512vbmi2 = cpu.avx512vbmi2();
                avx512vnni_bw = cpu.avx512vnni_bw();
                avx512vnni_vbmi2 = avx512vbmi2 && avx512vnni_bw;
            }
        };
    } // namespace detail

    XSIMD_INLINE detail::supported_arch available_architectures() noexcept
    {
        static detail::supported_arch supported;
        return supported;
    }
}

#endif
