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

#ifndef XSIMD_CPU_FEATURES_X86_HPP
#define XSIMD_CPU_FEATURES_X86_HPP

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#if XSIMD_CPP_VERSION >= 201703L
#include <string_view>
#endif

#include "../utils/bits.hpp"
#include "./xsimd_config.hpp"

#if XSIMD_TARGET_X86 && defined(_MSC_VER)
#include <intrin.h> // Contains the definition of __cpuidex
#endif

namespace xsimd
{
    namespace detail
    {
        using x86_reg32_t = std::uint32_t;

        using cpuid_reg_t = std::array<x86_reg32_t, 4>;

        /**
         * CPU Identification (CPUID) instruction results.
         *
         * The CPUID instruction provides detailed information about the processor,
         * including supported instruction set extensions (SSE, AVX, AVX-512, etc.).
         * This function is well defined on all architectures but will return all zeros
         * on all non-x86 architectures.
         *
         * @param leaf The value inputted to the EAX register.
         * @param subleaf The value inputted to the ECX register.
         *
         * @see https://en.wikipedia.org/wiki/CPUID
         */
        inline cpuid_reg_t x86_cpuid(int leaf, int subleaf = 0) noexcept;

        inline x86_reg32_t x86_xcr0_low() noexcept;

        /** A strongly type bitset for a 32 bits register. */
        template <typename E>
        using x86_reg32_bitset = utils::uint_bitset<E, x86_reg32_t>;

        /** A wrapper to attach a register bitfield descriptor and its CPUID index. */
        template <typename E, x86_reg32_t I>
        struct x86_reg_id
        {
            static constexpr x86_reg32_t index = I;
            using bits = E;

            static_assert(index >= 0 && index < 4, "At most 4 register in CPUID");
        };

        /** Find the register id with index k. */
        template <x86_reg32_t K, typename... reg_ids>
        struct find_reg_k;

        /** Find the register id with index k (empty / nothing found case). */
        template <x86_reg32_t K, typename... reg_ids>
        struct find_reg_k
        {
            using type = x86_reg_id<void, 0>;
        };

        /** Find the register id with index k (recursive case). */
        template <x86_reg32_t K, typename reg_id_head, typename... reg_id_tail>
        struct find_reg_k<K, reg_id_head, reg_id_tail...>
        {
            using type = std::conditional_t<
                reg_id_head::index == K,
                reg_id_head,
                typename find_reg_k<K, reg_id_tail...>::type>;
        };

        /**
         * A class with strongly typed bitfield for `CPUID` registers.
         *
         * The class stores a variable number of register (up to four) from the CPUID
         * output. This is a space optimization to avoid storing many zeros in the
         * final `x86_cpu_features`.
         * As a result, some of the type aliases `eax`, `ebx`, `ecx`, `edx` may be `void`.
         */
        template <x86_reg32_t leaf_num, x86_reg32_t subleaf_num, typename... reg_ids>
        class x86_cpuid_regs : private x86_reg32_bitset<typename reg_ids::bits>...
        {
        private:
            static_assert(sizeof...(reg_ids) <= 4, "At most 4 register in CPUID");

            /* Parse CPUINFO register value into individual bit components.*/
            constexpr explicit x86_cpuid_regs(const cpuid_reg_t& regs) noexcept
                : x86_reg32_bitset<typename reg_ids::bits>(regs[reg_ids::index])...
            {
            }

        public:
            static constexpr x86_reg32_t leaf = leaf_num;
            static constexpr x86_reg32_t subleaf = subleaf_num;

            using eax = typename find_reg_k<0, reg_ids...>::type::bits;
            using ebx = typename find_reg_k<1, reg_ids...>::type::bits;
            using ecx = typename find_reg_k<2, reg_ids...>::type::bits;
            using edx = typename find_reg_k<3, reg_ids...>::type::bits;

            inline static x86_cpuid_regs read()
            {
                return x86_cpuid_regs(detail::x86_cpuid(leaf, subleaf));
            }

            constexpr x86_cpuid_regs() noexcept = default;

            // TODO(C++17) compact version for which this was designed.
            // The else clause contains a very verbose port.
#if 0
            using x86_reg32_bitset<typename reg_ids::bits>::all_bits_set...;
            using x86_reg32_bitset<typename reg_ids::bits>::get_range...;
#else

        private:
            template <int N>
            struct m_empty_reg
            {
                enum class type {};
            };

            using eax_or_empty = std::conditional_t<std::is_void_v<eax>, typename m_empty_reg<0>::type, eax>;
            using ebx_or_empty = std::conditional_t<std::is_void_v<ebx>, typename m_empty_reg<1>::type, ebx>;
            using ecx_or_empty = std::conditional_t<std::is_void_v<ecx>, typename m_empty_reg<2>::type, ecx>;
            using edx_or_empty = std::conditional_t<std::is_void_v<edx>, typename m_empty_reg<3>::type, edx>;

        public:
            template <eax_or_empty... bits, typename T = eax, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr bool all_bits_set() const noexcept
            {
                return x86_reg32_bitset<eax>::template all_bits_set<bits...>();
            }

            template <eax_or_empty start, eax_or_empty end, typename T = eax, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr x86_reg32_t get_range() const noexcept
            {
                return x86_reg32_bitset<eax>::template get_range<start, end>();
            }

            template <ebx_or_empty... bits, typename T = ebx, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr bool all_bits_set() const noexcept
            {
                return x86_reg32_bitset<ebx>::template all_bits_set<bits...>();
            }

            template <ebx_or_empty start, ebx_or_empty end, typename T = ebx, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr x86_reg32_t get_range() const noexcept
            {
                return x86_reg32_bitset<ebx>::template get_range<start, end>();
            }

            template <ecx_or_empty... bits, typename T = ecx, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr bool all_bits_set() const noexcept
            {
                return x86_reg32_bitset<ecx>::template all_bits_set<bits...>();
            }

            template <ecx_or_empty start, ecx_or_empty end, typename T = ecx, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr x86_reg32_t get_range() const noexcept
            {
                return x86_reg32_bitset<ecx>::template get_range<start, end>();
            }

            template <edx_or_empty... bits, typename T = edx, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr bool all_bits_set() const noexcept
            {
                return x86_reg32_bitset<edx>::template all_bits_set<bits...>();
            }

            template <edx_or_empty start, edx_or_empty end, typename T = edx, std::enable_if_t<!std::is_void_v<T>, int> = 0>
            constexpr x86_reg32_t get_range() const noexcept
            {
                return x86_reg32_bitset<edx>::template get_range<start, end>();
            }

#endif // C++17
        };

        template <bool extended>
        struct x86_cpuid_highest_func
        {
        private:
            using x86_reg32_t = detail::x86_reg32_t;
            using manufacturer_str = std::array<char, 3 * sizeof(x86_reg32_t)>;

        public:
            static constexpr x86_reg32_t leaf = extended ? 0x80000000 : 0x0;

            inline static x86_cpuid_highest_func read()
            {
                auto regs = detail::x86_cpuid(0);
                x86_cpuid_highest_func out {};
                // Highest function parameter in EAX
                out.m_highest_leaf = regs[0];

                // Manufacturer string in EBX, EDX, ECX (in that order)
                char* manuf = out.m_manufacturer_id.data();
                std::memcpy(manuf + 0 * sizeof(x86_reg32_t), &regs[1], sizeof(x86_reg32_t));
                std::memcpy(manuf + 1 * sizeof(x86_reg32_t), &regs[3], sizeof(x86_reg32_t));
                std::memcpy(manuf + 2 * sizeof(x86_reg32_t), &regs[2], sizeof(x86_reg32_t));

                return out;
            }

            constexpr x86_cpuid_highest_func() noexcept = default;

            /**
             * Highest available leaf in CPUID non-extended range.
             *
             * This is the highest function parameter (EAX) that can be passed to CPUID.
             * This is valid in the specified range:
             *   - if `extended` is `false`, that is below `0x80000000`,
             *   - if `extended` is `true`, that is above `0x80000000`,
             */
            constexpr x86_reg32_t highest_leaf() const noexcept
            {
                return m_highest_leaf;
            }

            /**
             * The manufacturer ID string in a static array.
             *
             * This raw character array is case specific and may contain both leading
             * and trailing whitespaces.
             * It cannot be assumed to be null terminated.
             * This is not implemented for all manufacturer when `extended` is `true`.
             */
            constexpr manufacturer_str manufacturer_id_raw() const noexcept
            {
                return m_manufacturer_id;
            }

#if XSIMD_CPP_VERSION >= 201703L
            constexpr std::string_view manufacturer_id() const noexcept
            {
                return { m_manufacturer_id.data(), m_manufacturer_id.size() };
            }
#endif

        private:
            manufacturer_str m_manufacturer_id {};
            x86_reg32_t m_highest_leaf {};
        };
    }

    /**
     * Highest CPUID Function Parameter and Manufacturer ID (EAX=0).
     *
     * Returns the highest leaf value supported by CPUID in the standard range
     * (below 0x80000000), and the processor manufacturer ID string.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    using x86_cpuid_leaf0 = detail::x86_cpuid_highest_func<false>;

    /**
     * Known processor manufacturer ID strings returned by CPUID leaf 0.
     *
     * The 12-byte manufacturer ID is stored in EBX, EDX, ECX (in that order).
     * Some strings are shared across physical CPUs, emulators, and virtual machines.
     * Obscure, defunct, and soft-core CPUs are not represented; they map to `unknown`.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    enum class x86_manufacturer
    {
        /**
         * AMD ("AuthenticAMD", "AMD ISBETTER").
         *
         * "AMD ISBETTER" was used by early K5 engineering samples.
         */
        amd,
        /**
         * Intel ("GenuineIntel", "GenuineIotel").
         *
         * "GenuineIotel" is a rare typo variant seen on some chips.
         */
        intel,
        /**
         * VIA / Centaur ("CentaurHauls", "VIA VIA VIA ").
         *
         * Centaur Technology was acquired by VIA in 1999;
         * older chips report "CentaurHauls", newer ones "VIA VIA VIA ".
         */
        via,
        /** Zhaoxin ("  Shanghai  "). */
        zhaoxin,
        /** Hygon ("HygonGenuine"). */
        hygon,
        /**
         * Transmeta ("TransmetaCPU", "GenuineTMx86").
         *
         * Two different ID strings were used across product lines.
         */
        transmeta,
        /** MCST Elbrus ("E2K MACHINE "). */
        elbrus,
        /** Microsoft Virtual PC / x86-to-ARM ("Virtual CPU "). */
        microsoft_vpc,
        /** Unrecognized manufacturer ID string. */
        unknown,
    };

    /**
     * Parse a 12-byte CPUID manufacturer ID into an @ref x86_manufacturer value.
     *
     * The input is the raw character array returned by @ref x86_cpuid_leaf0::manufacturer_id_raw.
     * Unrecognized strings map to @ref x86_manufacturer::unknown.
     */
    inline x86_manufacturer x86_parse_manufacturer(const std::array<char, 12>& id) noexcept
    {
        auto eq = [&id](const char(&s)[13]) noexcept -> bool
        {
            return std::memcmp(id.data(), s, 12) == 0;
        };
        if (eq("GenuineIntel") || eq("GenuineIotel"))
            return x86_manufacturer::intel;
        if (eq("AuthenticAMD") || eq("AMD ISBETTER"))
            return x86_manufacturer::amd;
        if (eq("CentaurHauls") || eq("VIA VIA VIA "))
            return x86_manufacturer::via;
        if (eq("  Shanghai  "))
            return x86_manufacturer::zhaoxin;
        if (eq("HygonGenuine"))
            return x86_manufacturer::hygon;
        if (eq("TransmetaCPU") || eq("GenuineTMx86"))
            return x86_manufacturer::transmeta;
        if (eq("E2K MACHINE "))
            return x86_manufacturer::elbrus;
        if (eq("Virtual CPU "))
            return x86_manufacturer::microsoft_vpc;
        return x86_manufacturer::unknown;
    };

    /** Return a string representation of an @ref x86_manufacturer value. */
    constexpr const char* x86_manufacturer_name(x86_manufacturer m) noexcept
    {
        switch (m)
        {
        case x86_manufacturer::intel:
            return "intel";
        case x86_manufacturer::amd:
            return "amd";
        case x86_manufacturer::via:
            return "via";
        case x86_manufacturer::zhaoxin:
            return "zhaoxin";
        case x86_manufacturer::hygon:
            return "hygon";
        case x86_manufacturer::transmeta:
            return "transmeta";
        case x86_manufacturer::elbrus:
            return "elbrus";
        case x86_manufacturer::microsoft_vpc:
            return "microsoft_vpc";
        case x86_manufacturer::unknown:
            return "unknown";
        }
        return "invalid";
    }

    struct x86_cpuid_leaf1_traits
    {
        static constexpr detail::x86_reg32_t leaf = 1;
        static constexpr detail::x86_reg32_t subleaf = 0;

        enum class eax
        {
            /* Stepping ID bit range. */
            stepping_start = 0,
            stepping_end = 4,
            /* Model bit range. */
            model_start = 4,
            model_end = 8,
            /* Family ID bit range. */
            family_id_start = 8,
            family_id_end = 12,
            /* Extended Model ID bit range. */
            ext_model_start = 16,
            ext_model_end = 20,
            /* Extended Family ID bit range. */
            ext_family_start = 20,
            ext_family_end = 28,
        };
        enum class ecx
        {
            /* Streaming SIMD Extensions 3. */
            sse3 = 0,
            /* Supplemental Streaming SIMD Extensions 3. */
            ssse3 = 9,
            /* Fused multiply-add with 3 operands (FMA3). */
            fma3 = 12,
            /* Streaming SIMD Extensions 4.1. */
            sse4_1 = 19,
            /* Streaming SIMD Extensions 4.2. */
            sse4_2 = 20,
            /* Population count instruction (POPCNT). */
            popcnt = 23,
            /* Advanced Encryption Standard instruction set. */
            aes_ni = 25,
            /* OS has enabled XSAVE/XRSTOR for extended processor state management. */
            osxsave = 27,
            /* Advanced Vector Extensions (256-bit SIMD). */
            avx = 28,
            /* Half to single floating point conversion. */
            f16c = 29,
            /* On-chip random number generator. */
            rdrnd = 30,
        };
        enum class edx
        {
            /* Streaming SIMD Extensions 2. */
            sse2 = 26,
        };

        using regs_t = detail::x86_cpuid_regs<leaf, subleaf,
                                              detail::x86_reg_id<eax, 0>,
                                              detail::x86_reg_id<ecx, 2>,
                                              detail::x86_reg_id<edx, 3>>;
    };

    /**
     * Processor Info and Feature Bits.
     *
     * Utility class that can read and parse the registers for the first leaf level
     * of the CPUID instruction.
     * This is well defined on all architectures but will return all false on all
     * non-x86 architectures.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    using x86_cpuid_leaf1 = typename x86_cpuid_leaf1_traits::regs_t;

    struct x86_cpuid_leaf7_traits
    {
        static constexpr detail::x86_reg32_t leaf = 7;
        static constexpr detail::x86_reg32_t subleaf = 0;

        enum class eax
        {
            /* Start bit for the encoding of the highest subleaf available. */
            highest_subleaf_start = 0,
            /* End bit for the encoding of the highest subleaf available. */
            highest_subleaf_end = 32,
        };
        enum class ebx
        {
            /* Bit Manipulation Instruction Set 1. */
            bmi1 = 3,
            /* Advanced Vector Extensions 2 (integer 256-bit SIMD). */
            avx2 = 5,
            /* Bit Manipulation Instruction Set 2. */
            bmi2 = 8,
            /* AVX-512 Foundation instructions. */
            avx512f = 16,
            /* AVX-512 Doubleword and Quadword instructions. */
            avx512dq = 17,
            /* Low-level access to the entropy-generating hardware. */
            rdseed = 18,
            /* Intel arbitrary precision add carry. */
            adx = 19,
            /* AVX-512 Integer Fused Multiply-Add instructions. */
            avx512ifma = 21,
            /* AVX-512 Prefetch instructions. */
            avx512pf = 26,
            /* AVX-512 Exponential and Reciprocal instructions. */
            avx512er = 27,
            /* AVX-512 Conflict Detection instructions. */
            avx512cd = 28,
            /* Sha-1 and Sha-256 extension. */
            sha = 29,
            /* AVX-512 Byte and Word instructions. */
            avx512bw = 30,
            /* AVX-512 Vector Length Extensions for xmm and ymm registers. */
            avx512vl = 31,
        };
        enum class ecx
        {
            /* AVX-512 Vector Bit Manipulation instructions. */
            avx512vbmi = 1,
            /* AVX-512 Vector Bit Manipulation instructions 2. */
            avx512vbmi2 = 6,
            /* Galois Field instructions. */
            gfni = 8,
            /* Vector Advanced Encryption Standard instructions. */
            vaes = 9,
            /* Carry-less multiplication quadword instruction. */
            vpclmulqdq = 10,
            /* AVX-512 Vector Neural Network instructions. */
            avx512vnni_bw = 11,
            /* AVX-512 bit algorithm instructions (BITALG). */
            avx512_bitalg = 12,
            /* AVX-512 vector population count for doubleword and quadword. */
            avx512_vpopcntdq = 14,
        };
        enum class edx
        {
            /* AVX-512 4-register neural network instructions (word variable precision). */
            avx512_4vnniw = 2,
            /* AVX-512 4-register multiply-accumulate single precision. */
            avx512_4fmaps = 3,
            /* AVX-512 intersect pairs of packed doubleword/quadword integers. */
            avx512_vp2intersect = 8,
            /* AVX-512 16-bit floating-point instructions. */
            avx512_fp16 = 23,

        };

        using regs_t = detail::x86_cpuid_regs<leaf, subleaf,
                                              detail::x86_reg_id<eax, 0>,
                                              detail::x86_reg_id<ebx, 1>,
                                              detail::x86_reg_id<ecx, 2>,
                                              detail::x86_reg_id<edx, 3>>;
    };

    /**
     * Extended Feature Bits (EAX=7, ECX=0).
     *
     * Utility class that can read and parse the registers for the extended
     * feature bits leaf of the CPUID instruction.
     * This is well defined on all architectures but will return all false on all
     * non-x86 architectures.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    using x86_cpuid_leaf7 = typename x86_cpuid_leaf7_traits::regs_t;

    struct x86_cpuid_leaf7sub1_traits
    {
        static constexpr detail::x86_reg32_t leaf = 7;
        static constexpr detail::x86_reg32_t subleaf = 1;

        enum class eax
        {
            /* AVX (VEX-encoded) Vector Neural Network instructions. */
            avxvnni = 4,
            /* AVX-512 BFloat16 instructions. */
            avx512_bf16 = 5,
        };

        using regs_t = detail::x86_cpuid_regs<leaf, subleaf,
                                              detail::x86_reg_id<eax, 0>>;
    };

    /**
     * Extended Feature Bits (EAX=7, ECX=1).
     *
     * Utility class that can read and parse the registers for the extended
     * feature bits, subleaf 1, of the CPUID instruction.
     * This is well defined on all architectures but will return all false on all
     * non-x86 architectures.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    using x86_cpuid_leaf7sub1 = typename x86_cpuid_leaf7sub1_traits::regs_t;

    /**
     * Highest Extended CPUID Function Parameter (EAX=0x80000000).
     *
     * Returns the highest leaf value supported by CPUID in the extended range
     * (at or above 0x80000000), and the processor manufacturer ID string.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    using x86_cpuid_leaf80000000 = detail::x86_cpuid_highest_func<true>;

    struct x86_cpuid_leaf80000001_traits
    {
        static constexpr detail::x86_reg32_t leaf = 0x80000001;
        static constexpr detail::x86_reg32_t subleaf = 0;

        enum class ecx
        {
            /* AMD Fused multiply-add with 4 operands (FMA4). */
            fma4 = 16,
        };

        using regs_t = detail::x86_cpuid_regs<leaf, subleaf,
                                              detail::x86_reg_id<ecx, 2>>;
    };

    /**
     * Extended Processor Info and Feature Bits.
     *
     * Utility class that can read and parse the registers for the extended
     * processor info leaf of the CPUID instruction.
     * This is well defined on all architectures but will return all false on all
     * non-x86 architectures.
     *
     * @see https://en.wikipedia.org/wiki/CPUID
     */
    using x86_cpuid_leaf80000001 = typename x86_cpuid_leaf80000001_traits::regs_t;

    /*
     * Extended Control Register 0 (XCR0).
     *
     * Operating systems can explicitly disable the usage of instruction set (such
     * as SSE or AVX extensions) by setting an appropriate flag in XCR0 register.
     * This utility parses such bit values.
     *
     * @see https://docs.kernel.org/admin-guide/hw-vuln/gather_data_sampling.html
     */
    class x86_xcr0
    {
    public:
        enum class xcr0
        {
            /** x87 FPU/MMX support (must be 1). */
            x87 = 0,
            /** XSAVE support for MXCSR and XMM registers. */
            sse = 1,
            /** AVX enabled and XSAVE support for upper halves of YMM registers. */
            avx = 2,
            /** MPX enabled and XSAVE support for BND0-BND3 registers. */
            bndreg = 3,
            /** MPX enabled and XSAVE support for BNDCFGU and BNDSTATUS registers. */
            bndcsr = 4,
            /** AVX-512 enabled and XSAVE support for opmask registers k0-k7. */
            opmask = 5,
            /** AVX-512 enabled and XSAVE support for upper halves of lower ZMM registers. */
            zmm_hi256 = 6,
            /** AVX-512 enabled and XSAVE support for upper ZMM registers. */
            hi16_zmm = 7,
            /** Saving/restoring Intel Processor Trace state via XSAVE enabled.*/
            processor_trace = 8,
            /** XSAVE support for PKRU register. */
            pkru = 9,
        };

        /**
         * Create a default value with only SSE enabled.
         *
         * AVX and AVX512 strictly require OSXSAVE to be enabled by the OS.
         * If OSXSAVE is disabled (e.g., via bcdedit /set xsavedisable 1), AVX state won't
         * be preserved across context switches, so AVX cannot be used.
         * SSE is therefore the only value safe to assume.
         */
        constexpr static x86_xcr0 safe_default() noexcept
        {
            x86_reg32_t low = {};
            low = utils::make_bit_mask(static_cast<x86_reg32_t>(xcr0::sse));
            return x86_xcr0(low);
        }

        /**
         * Read the XCR0 register from the CPU if on the correct architecture.
         *
         * This is only safe to call if bit 18 of CR4.OSXSAVE has been set.
         *
         * @see cpu_id::osxsave
         */
        inline static x86_xcr0 read()
        {
            assert(x86_cpuid_leaf1::read().all_bits_set<x86_cpuid_leaf1::ecx::osxsave>());
            return x86_xcr0(detail::x86_xcr0_low());
        }

        template <xcr0... bits>
        constexpr bool all_bits_set() const noexcept
        {
            return m_low.all_bits_set<bits...>();
        }

        /** Create a value which return false to everything. */
        constexpr x86_xcr0() noexcept = default;

    private:
        using x86_reg32_t = detail::x86_reg32_t;

        using xcr0_reg_t = detail::x86_reg32_bitset<xcr0>;

        /** Parse a XCR0 value into individual components. */
        constexpr explicit x86_xcr0(x86_reg32_t low) noexcept
            : m_low(low)
        {
        }

        xcr0_reg_t m_low {};
    };

    /**
     * Orchestrator for `CPUID` calls.
     *
     * This class orchestrate `CPUID` and `XCR0` calls so that they are made in the appropriate
     * order. It also implements lazy calling and cache mechanism around those calls.
     * Works on all platforms, and return all zeros on non `x86` platforms.
     */
    class x86_cpu_features_backend_cpuid
    {
    public:
        x86_cpu_features_backend_cpuid() noexcept = default;

        inline x86_xcr0 const& xcr0() const noexcept;
        inline x86_cpuid_leaf0 const& leaf0() const;
        inline x86_cpuid_leaf80000000 const& leaf80000000() const;
        inline x86_cpuid_leaf1 const& leaf1() const;
        inline x86_cpuid_leaf7 const& leaf7() const;
        inline x86_cpuid_leaf7sub1 const& leaf7sub1() const;
        inline x86_cpuid_leaf80000001 const& leaf80000001() const;

    private:
        enum class status
        {
            leaf0_valid = 0,
            leaf1_valid = 1,
            leaf7_valid = 2,
            leaf7sub1_valid = 3,
            leaf80000000_valid = 4,
            leaf80000001_valid = 5,
            xcr0_valid = 6,
        };

        using status_bitset = utils::uint_bitset<status, std::uint32_t>;

        mutable x86_cpuid_leaf0 m_leaf0 {};
        mutable x86_cpuid_leaf1 m_leaf1 {};
        mutable x86_cpuid_leaf7 m_leaf7 {};
        mutable x86_cpuid_leaf7sub1 m_leaf7sub1 {};
        mutable x86_cpuid_leaf80000000 m_leaf80000000 {};
        mutable x86_cpuid_leaf80000001 m_leaf80000001 {};
        mutable x86_xcr0 m_xcr0 {};
        mutable status_bitset m_status {};

        inline bool osxsave() const noexcept;

        /**
         * Internal utility to lazily read and cache a CPUID leaf.
         *
         * @tparam status_id The status bit tracking whether this leaf has been read and cached.
         * @tparam L The CPUID leaf type (e.g. x86_cpuid_leaf1, x86_cpuid_leaf7).
         * @param leaf_cache A non-const reference to the class member that stores the leaf
         *        value. It must be non-const because this function may write to it on first
         *        call. It is passed explicitly (rather than accessed via `this`) to allow
         *        factoring the caching logic across different leaf members.
         * @return A const reference to `leaf_cache`. The non-const input / const-ref output
         *         asymmetry is intentional: callers must not modify the cached value, but
         *         this function needs write access to populate it.
         *
         * On first call, checks whether the leaf number is within the range advertised as
         * supported by CPUID (via leaf 0 for the standard range, leaf 0x80000000 for the
         * extended range). If supported, reads the leaf from the CPU; otherwise leaves
         * `leaf_cache` at its zero-initialized default (all feature bits false). Either
         * way, `status_id` is set so subsequent calls return immediately.
         */
        template <status status_id, typename L>
        inline auto const& safe_read_leaf(L& leaf_cache) const;
    };

    /**
     * No-Op orchestrator for `CPUID` calls
     *
     * This does nothing and return zero-constructed objects on all calls.
     * This is meant as an optimization on non `x86` platforms as the
     * `x86_cpu_features_backend_cpuid` can be slightly large (hundred of bytes).
     */
    class x86_cpu_features_backend_noop
    {
    public:
        constexpr x86_xcr0 xcr0() const noexcept { return {}; }
        constexpr x86_cpuid_leaf0 leaf0() const { return {}; }
        constexpr x86_cpuid_leaf80000000 leaf80000000() const { return {}; }
        constexpr x86_cpuid_leaf1 leaf1() const { return {}; }
        constexpr x86_cpuid_leaf7 leaf7() const { return {}; }
        constexpr x86_cpuid_leaf7sub1 leaf7sub1() const { return {}; }
        constexpr x86_cpuid_leaf80000001 leaf80000001() const { return {}; }
    };

#if XSIMD_TARGET_X86
    using x86_cpu_features_backend_default = x86_cpu_features_backend_cpuid;
#else
    using x86_cpu_features_backend_default = x86_cpu_features_backend_noop;
#endif

    /**
     * An opiniated CPU feature detection utility for x86.
     *
     * These are high level features that combine multiple registers reads in sequence.
     * Instead of looking directly at raw CPUID results, this utility also checks that
     * permissions (e.g. OSXSAVE) are enabled, and otherwise return conservative defaults.
     *
     * This is well defined on all architectures. It will always return false on
     * non-x86 architectures.
     */
    class x86_cpu_features : private x86_cpu_features_backend_default
    {
    public:
        x86_cpu_features() noexcept = default;

        inline bool sse_enabled() const noexcept
        {
            return xcr0().all_bits_set<x86_xcr0::xcr0::sse>();
        }

        inline bool avx_enabled() const noexcept
        {
            // Check both SSE and AVX bits even though AVX must imply SSE
            return xcr0().all_bits_set<x86_xcr0::xcr0::sse, x86_xcr0::xcr0::avx>();
        }

        inline bool avx512_enabled() const noexcept
        {
            // Check all SSE, AVX, optmask, and AVX512 bits even though AVX512 must
            // imply AVX, SSE, and masked operations.
            return xcr0().all_bits_set<x86_xcr0::xcr0::sse, x86_xcr0::xcr0::avx, x86_xcr0::xcr0::opmask, x86_xcr0::xcr0::zmm_hi256>();
        }

        /**
         * The manufacturer ID string in a static array.
         *
         * This raw character array is case specific and may contain both leading
         * and trailing whitespaces.
         * It cannot be assumed to be null terminated.
         */
        inline auto manufacturer_id_raw() const noexcept
        {
            return leaf0().manufacturer_id_raw();
        }

#if XSIMD_CPP_VERSION >= 201703L
        inline std::string_view manufacturer_id() const noexcept
        {
            return leaf0().manufacturer_id();
        }
#endif

        /** The manufacturer ID string parsed into known common vendors. */
        inline x86_manufacturer known_manufacturer() const noexcept
        {
            return x86_parse_manufacturer(manufacturer_id_raw());
        }

        /**
         * Indicates whether the OS has enabled extended state management.
         *
         * When true, the OS has set bit 18 (OSXSAVE) in the CR4 control register,
         * enabling the XGETBV/XSETBV instructions to access XCR0 and support
         * processor extended state management using XSAVE/XRSTOR.
         *
         * This value is read from CPUID leaf 0x1, ECX bit 27, which reflects
         * the state of CR4.OSXSAVE.
         */
        inline bool osxsave() const noexcept { return leaf1().all_bits_set<x86_cpuid_leaf1::ecx::osxsave>(); }

        /**
         * Effective processor family.
         *
         * Per wikipedia CPUID page:
         * > The actual processor family is derived from the Family ID and Extended Family ID fields.
         * > If the Family ID field is equal to 15, the family is equal to the sum of the Extended
         * > Family ID and the Family ID fields. Otherwise, the family is equal to the value of the
         * > Family ID field.
         *
         * @see https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits
         */
        inline detail::x86_reg32_t cpu_family() const noexcept
        {
            using eax = x86_cpuid_leaf1::eax;
            auto family_id = leaf1().get_range<eax::family_id_start, eax::family_id_end>();
            auto ext_family_id = leaf1().get_range<eax::ext_family_start, eax::ext_family_end>();
            return family_id + (family_id == 15 ? ext_family_id : 0);
        }

        /**
         * Effective processor model.
         *
         * Per wikipedia CPUID page:
         * > The actual processor model is derived from the Model, Extended Model ID and Family ID
         * > fields. If the Family ID field is either 6 or 15, the model is equal to the sum of the
         * > Extended Model ID field shifted left by 4 bits and the Model field.
         * > Otherwise, the model is equal to the value of the Model field.
         *
         * @see https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits
         */
        inline detail::x86_reg32_t cpu_model() const noexcept
        {
            using eax = x86_cpuid_leaf1::eax;
            auto model = leaf1().get_range<eax::model_start, eax::model_end>();
            auto ext_model = leaf1().get_range<eax::ext_model_start, eax::ext_model_end>();
            auto family_id = leaf1().get_range<eax::family_id_start, eax::family_id_end>();
            return (family_id == 15 || family_id == 6) ? ((ext_model << 4) + model) : model;
        }

        inline bool sse2() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::edx::sse2>(); }

        inline bool sse3() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::sse3>(); }

        inline bool ssse3() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::ssse3>(); }

        inline bool sse4_1() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::sse4_1>(); }

        inline bool sse4_2() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::sse4_2>(); }

        inline bool popcnt() const noexcept { return leaf1().all_bits_set<x86_cpuid_leaf1::ecx::popcnt>(); }

        inline bool fma3() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::fma3>(); }

        inline bool avx() const noexcept { return avx_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::avx>(); }

        inline bool avx_128() const noexcept
        {
            // Avx 128 bit instructions use the same xmm registers from SSE so checking if those
            // are enabled is sufficient.
            return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::avx>();
        }

        inline bool aes_ni() const noexcept { return sse_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::aes_ni>(); }

        inline bool f16c() const noexcept { return avx_enabled() && leaf1().all_bits_set<x86_cpuid_leaf1::ecx::f16c>(); }

        inline bool rdrnd() const noexcept { return leaf1().all_bits_set<x86_cpuid_leaf1::ecx::rdrnd>(); }

        inline bool bmi1() const noexcept { return leaf7().all_bits_set<x86_cpuid_leaf7::ebx::bmi1>(); }

        inline bool avx2() const noexcept { return avx_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx2>(); }

        inline bool avx2_128() const noexcept
        {
            // Avx 128 bit instructions use the same xmm registers from SSE so checking if those
            // are enabled is sufficient.
            return sse_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx2>();
        }

        inline bool bmi2() const noexcept { return leaf7().all_bits_set<x86_cpuid_leaf7::ebx::bmi2>(); }

        /**
         * BMI2 support with efficient PEXT and PDEP instructions.
         *
         * > AMD processors before Zen 3 that implement PDEP and PEXT do so in microcode, with a
         * > latency of 18 cycles rather than (Zen 3) 3 cycles. As a result it is often faster
         * > to use other instructions on these processors.
         *
         * @see https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set
         */
        inline bool efficient_bmi2() const noexcept
        {
            if (known_manufacturer() == x86_manufacturer::amd)
            {
                // Zen 3 and Zen 4 report family 0x19; Zen 5 reports family 0x1A.
                // Earlier AMD microarchitectures (Zen / Zen+ / Zen 2) report family 0x17.
                return bmi2() && (cpu_family() >= 0x19);
            }
            return bmi2();
        }

        inline bool avx512f() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512f>(); }

        inline bool avx512dq() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512dq>(); }

        inline bool rdseed() const noexcept { return leaf7().all_bits_set<x86_cpuid_leaf7::ebx::rdseed>(); }

        inline bool adx() const noexcept { return leaf7().all_bits_set<x86_cpuid_leaf7::ebx::adx>(); }

        inline bool avx512ifma() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512ifma>(); }

        inline bool avx512pf() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512pf>(); }

        inline bool avx512er() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512er>(); }

        inline bool avx512cd() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512cd>(); }

        inline bool sha() const noexcept { return leaf7().all_bits_set<x86_cpuid_leaf7::ebx::sha>(); }

        inline bool avx512bw() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512bw>(); }

        inline bool avx512vl() const noexcept
        {
            return xcr0().all_bits_set<x86_xcr0::xcr0::opmask>()
                && leaf7().all_bits_set<x86_cpuid_leaf7::ebx::avx512f, x86_cpuid_leaf7::ebx::avx512vl>();
        }

        inline bool avx512vl_128() const noexcept
        {
            // AVX512 128 bit instructions use the same xmm registers from SSE so checking if those
            // are enabled is sufficient.
            return sse_enabled() && avx512vl();
        }

        inline bool avx512vl_256() const noexcept
        {
            // AVX512 256 bit instructions use the same ymm registers from AVX so checking if those
            // are enabled is sufficient.
            return avx_enabled() && avx512vl();
        }

        inline bool avx512vbmi() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::avx512vbmi>(); }

        inline bool avx512vbmi2() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::avx512vbmi2>(); }

        inline bool gfni() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::gfni>(); }

        inline bool vaes() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::vaes>(); }

        inline bool vpclmulqdq() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::vpclmulqdq>(); }

        inline bool avx512vnni_bw() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::avx512vnni_bw>(); }

        inline bool avx512_bitalg() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::avx512_bitalg>(); }

        inline bool avx512_vpopcntdq() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::ecx::avx512_vpopcntdq>(); }

        inline bool avx512_4vnniw() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::edx::avx512_4vnniw>(); }

        inline bool avx512_4fmaps() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::edx::avx512_4fmaps>(); }

        inline bool avx512_vp2intersect() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::edx::avx512_vp2intersect>(); }

        inline bool avx512_fp16() const noexcept { return avx512_enabled() && leaf7().all_bits_set<x86_cpuid_leaf7::edx::avx512_fp16>(); }

        inline bool avxvnni() const noexcept { return avx_enabled() && leaf7sub1().all_bits_set<x86_cpuid_leaf7sub1::eax::avxvnni>(); }

        inline bool avx512_bf16() const noexcept { return avx512_enabled() && leaf7sub1().all_bits_set<x86_cpuid_leaf7sub1::eax::avx512_bf16>(); }

        inline bool fma4() const noexcept { return avx_enabled() && leaf80000001().all_bits_set<x86_cpuid_leaf80000001::ecx::fma4>(); }
    };

    /********************
     *  Implementation  *
     ********************/

    template <x86_cpu_features_backend_cpuid::status status_id, typename L>
    inline auto const& x86_cpu_features_backend_cpuid::safe_read_leaf(L& leaf_cache) const
    {
        // Check if already initialized
        if (m_status.bit_is_set<status_id>())
        {
            return leaf_cache;
        }

        // Limit where we need to check leaf0 or leaf 80000000.
        constexpr auto extended_threshold = x86_cpuid_leaf80000000::leaf;

        // Check if it is safe to call CPUID with this value.
        // First we identify if the leaf is in the regular or extended range.
        // TODO(C++17): if constexpr
        if (L::leaf < extended_threshold)
        {
            // Check leaf0 in regular range
            if (L::leaf <= leaf0().highest_leaf())
            {
                leaf_cache = L::read();
            }
        }
        else
        {
            // Check leaf80000000 in extended range
            if (L::leaf <= leaf80000000().highest_leaf())
            {
                leaf_cache = L::read();
            }
        }

        // Mark as valid in all cases, including if it was not read.
        // In this case it will be filled with zeros (all false).
        m_status.set_bit<status_id>();
        return leaf_cache;
    }

    inline x86_xcr0 const& x86_cpu_features_backend_cpuid::xcr0() const noexcept
    {
        if (!m_status.bit_is_set<status::xcr0_valid>())
        {
            m_xcr0 = osxsave() ? x86_xcr0::read() : x86_xcr0::safe_default();
            m_status.set_bit<status::xcr0_valid>();
        }
        return m_xcr0;
    }

    inline x86_cpuid_leaf0 const& x86_cpu_features_backend_cpuid::leaf0() const
    {
        if (!m_status.bit_is_set<status::leaf0_valid>())
        {
            m_leaf0 = x86_cpuid_leaf0::read();
            m_status.set_bit<status::leaf0_valid>();
        }
        return m_leaf0;
    }

    inline x86_cpuid_leaf80000000 const& x86_cpu_features_backend_cpuid::leaf80000000() const
    {
        if (!m_status.bit_is_set<status::leaf80000000_valid>())
        {
            m_leaf80000000 = x86_cpuid_leaf80000000::read();
            m_status.set_bit<status::leaf80000000_valid>();
        }
        return m_leaf80000000;
    }

    inline x86_cpuid_leaf1 const& x86_cpu_features_backend_cpuid::leaf1() const
    {
        return safe_read_leaf<status::leaf1_valid>(m_leaf1);
    }

    inline x86_cpuid_leaf7 const& x86_cpu_features_backend_cpuid::leaf7() const
    {
        return safe_read_leaf<status::leaf7_valid>(m_leaf7);
    }

    inline x86_cpuid_leaf7sub1 const& x86_cpu_features_backend_cpuid::leaf7sub1() const
    {
        // Check if already initialized
        if (m_status.bit_is_set<status::leaf7sub1_valid>())
        {
            return m_leaf7sub1;
        }

        // Check if safe to call CPUID with this value as subleaf.
        constexpr auto start = x86_cpuid_leaf7::eax::highest_subleaf_start;
        constexpr auto end = x86_cpuid_leaf7::eax::highest_subleaf_end;
        const auto highest_subleaf7 = leaf7().get_range<start, end>();
        if (x86_cpuid_leaf7sub1::subleaf <= highest_subleaf7)
        {
            m_leaf7sub1 = x86_cpuid_leaf7sub1::read();
        }

        // Mark as valid in all cases, including if it was not read.
        // In this case it will be filled with zeros (all false).
        m_status.set_bit<status::leaf7sub1_valid>();
        return m_leaf7sub1;
    }

    inline x86_cpuid_leaf80000001 const& x86_cpu_features_backend_cpuid::leaf80000001() const
    {
        return safe_read_leaf<status::leaf80000001_valid>(m_leaf80000001);
    }

    inline bool x86_cpu_features_backend_cpuid::osxsave() const noexcept
    {
        return leaf1().all_bits_set<x86_cpuid_leaf1::ecx::osxsave>();
    }

    namespace detail
    {
#if XSIMD_TARGET_X86

        inline cpuid_reg_t x86_cpuid(int leaf, int subleaf) noexcept
        {
            cpuid_reg_t reg = {};
#if defined(_MSC_VER)
            int buf[4];
            __cpuidex(buf, leaf, subleaf);
            std::memcpy(reg.data(), buf, sizeof(buf));

// Intel compiler has long had support for `__cpuid`, but only recently for `__cpuidex`.
// Modern Clang and GCC also now support `__cpuidex`.
// It was decided to keep the inline ASM version for maximum compatibility, as the difference
// in ASM is negligible compared to the cost of CPUID.
// https://github.com/xtensor-stack/xsimd/pull/1278
#elif XSIMD_WITH_INLINE_ASM

#if defined(__i386__) && defined(__PIC__)
            // %ebx may be the PIC register
            __asm__("xchg{l}\t{%%}ebx, %1\n\t"
                    "cpuid\n\t"
                    "xchg{l}\t{%%}ebx, %1\n\t"
                    : "=a"(reg[0]), "=r"(reg[1]), "=c"(reg[2]), "=d"(reg[3])
                    : "0"(leaf), "2"(subleaf));

#else
            __asm__("cpuid\n\t"
                    : "=a"(reg[0]), "=b"(reg[1]), "=c"(reg[2]), "=d"(reg[3])
                    : "0"(leaf), "2"(subleaf));
#endif
#endif
            return reg;
        }

        inline x86_reg32_t x86_xcr0_low() noexcept
        {
#if defined(_MSC_VER)
#if _MSC_VER >= 1400
            return static_cast<x86_reg32_t>(_xgetbv(0));
#else
#error "_MSC_VER < 1400 is not supported"
#endif

#elif XSIMD_WITH_INLINE_ASM
            x86_reg32_t xcr0 = {};
            __asm__(
                "xorl %%ecx, %%ecx\n"
                "xgetbv\n"
                : "=a"(xcr0)
                :
#if defined(__i386__)
                : "ecx", "edx"
#else
                : "rcx", "rdx"
#endif
            );
            return xcr0;
#endif
        }

#else // XSIMD_TARGET_X86

        inline cpuid_reg_t x86_cpuid(int /* leaf */, int /* subleaf */) noexcept
        {
            return {}; // All bits to zero
        }

        inline x86_reg32_t x86_xcr0_low() noexcept
        {
            return {}; // All bits to zero
        }

#endif // XSIMD_TARGET_X86
    }
}
#endif
