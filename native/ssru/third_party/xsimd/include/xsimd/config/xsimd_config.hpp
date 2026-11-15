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

#ifndef XSIMD_CONFIG_HPP
#define XSIMD_CONFIG_HPP

#define XSIMD_VERSION_MAJOR 14
#define XSIMD_VERSION_MINOR 3
#define XSIMD_VERSION_PATCH 0

#if defined(__GNUC__) && defined(__BYTE_ORDER__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define XSIMD_LITTLE_ENDIAN
#endif
#elif defined(_WIN32)
// We can safely assume that Windows is always little endian
#define XSIMD_LITTLE_ENDIAN
#elif defined(i386) || defined(i486) || defined(intel) || defined(x86) || defined(i86pc) || defined(__alpha) || defined(__osf__)
#define XSIMD_LITTLE_ENDIAN
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Normalized C++ version number, equivalent to __cplusplus but also works with
 * MSVC which requires /Zc:__cplusplus to set it correctly (otherwise it's always
 * 199711L). Use this instead of __cplusplus throughout the codebase.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#define XSIMD_CPP_VERSION _MSVC_LANG
#else
#define XSIMD_CPP_VERSION __cplusplus
#endif

/**
 * high level free functions
 *
 * @defgroup xsimd_config_macro Instruction Set Detection
 */

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is the x86 architecture family.
 */
#if defined(__x86_64__) || defined(__i386__) || defined(_M_AMD64) || defined(_M_IX86)
#define XSIMD_TARGET_X86 1
#else
#define XSIMD_TARGET_X86 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if GNU-style inline assembly is available, to 0 otherwise.
 */
/* Use __clang__ || __GNUC__ for GNU-style inline asm. clang-cl runs in
 * MSVC-compatibility mode and does not define __GNUC__ by default, but it
 * still defines __clang__. Clang documents __asm__/__asm__ support and broad
 * GCC-extension compatibility:
 * https://clang.llvm.org/docs/LanguageExtensions.html
 * Clang only emits __GNUC__ when GNUCVersion != 0:
 * https://raw.githubusercontent.com/llvm/llvm-project/main/clang/lib/Frontend/InitPreprocessor.cpp
 * and GNUCVersion defaults to 0:
 * https://raw.githubusercontent.com/llvm/llvm-project/main/clang/include/clang/Basic/LangOptions.def
 */
#if defined(__clang__) || defined(__GNUC__)
#define XSIMD_WITH_INLINE_ASM 1
#else
#define XSIMD_WITH_INLINE_ASM 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 when the compiler is allowed to reassociate floating-point
 * operations (e.g. -ffast-math, -fassociative-math).  Detected
 * automatically from __FAST_MATH__ (GCC/Clang) and __ASSOCIATIVE_MATH__
 * (GCC).  Clang does not define a macro for standalone
 * -fassociative-math; users should define XSIMD_REASSOCIATIVE_MATH=1
 * manually in that case.
 */
#ifndef XSIMD_REASSOCIATIVE_MATH
#if defined(__FAST_MATH__) || defined(__ASSOCIATIVE_MATH__)
#define XSIMD_REASSOCIATIVE_MATH 1
#else
#define XSIMD_REASSOCIATIVE_MATH 0
#endif
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if SSE2 is available at compile-time, to 0 otherwise.
 */
#ifdef __SSE2__
#define XSIMD_WITH_SSE2 1
#else
#define XSIMD_WITH_SSE2 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if SSE3 is available at compile-time, to 0 otherwise.
 */
#ifdef __SSE3__
#define XSIMD_WITH_SSE3 1
#else
#define XSIMD_WITH_SSE3 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if SSSE3 is available at compile-time, to 0 otherwise.
 */
#ifdef __SSSE3__
#define XSIMD_WITH_SSSE3 1
#else
#define XSIMD_WITH_SSSE3 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if SSE4.1 is available at compile-time, to 0 otherwise.
 */
#ifdef __SSE4_1__
#define XSIMD_WITH_SSE4_1 1
#else
#define XSIMD_WITH_SSE4_1 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if SSE4.2 is available at compile-time, to 0 otherwise.
 */
#ifdef __SSE4_2__
#define XSIMD_WITH_SSE4_2 1
#else
#define XSIMD_WITH_SSE4_2 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX__
#define XSIMD_WITH_AVX 1
#else
#define XSIMD_WITH_AVX 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX2 is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX2__
#define XSIMD_WITH_AVX2 1
#else
#define XSIMD_WITH_AVX2 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVXVNNI is available at compile-time, to 0 otherwise.
 */
#ifdef __AVXVNNI__
#define XSIMD_WITH_AVXVNNI 1
#else
#define XSIMD_WITH_AVXVNNI 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if FMA3 for SSE is available at compile-time, to 0 otherwise.
 */
#ifdef __FMA__

#if defined(__SSE__)
#ifndef XSIMD_WITH_FMA3_SSE // Leave the opportunity to manually disable it, see #643
#define XSIMD_WITH_FMA3_SSE 1
#endif
#else

#if XSIMD_WITH_FMA3_SSE
#error "Manually set XSIMD_WITH_FMA3_SSE is incompatible with current compiler flags"
#endif

#define XSIMD_WITH_FMA3_SSE 0
#endif

#else

#if XSIMD_WITH_FMA3_SSE
#error "Manually set XSIMD_WITH_FMA3_SSE is incompatible with current compiler flags"
#endif

#define XSIMD_WITH_FMA3_SSE 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if FMA3 for AVX is available at compile-time, to 0 otherwise.
 */
#ifdef __FMA__

#if defined(__AVX__)
#ifndef XSIMD_WITH_FMA3_AVX // Leave the opportunity to manually disable it, see #643
#define XSIMD_WITH_FMA3_AVX 1
#endif
#else

#if XSIMD_WITH_FMA3_AVX
#error "Manually set XSIMD_WITH_FMA3_AVX is incompatible with current compiler flags"
#endif

#define XSIMD_WITH_FMA3_AVX 0
#endif

#if defined(__AVX2__)
#ifndef XSIMD_WITH_FMA3_AVX2 // Leave the opportunity to manually disable it, see #643
#define XSIMD_WITH_FMA3_AVX2 1
#endif
#else

#if XSIMD_WITH_FMA3_AVX2
#error "Manually set XSIMD_WITH_FMA3_AVX2 is incompatible with current compiler flags"
#endif

#define XSIMD_WITH_FMA3_AVX2 0
#endif

#else

#if XSIMD_WITH_FMA3_AVX
#error "Manually set XSIMD_WITH_FMA3_AVX is incompatible with current compiler flags"
#endif

#if XSIMD_WITH_FMA3_AVX2
#error "Manually set XSIMD_WITH_FMA3_AVX2 is incompatible with current compiler flags"
#endif

#define XSIMD_WITH_FMA3_AVX 0
#define XSIMD_WITH_FMA3_AVX2 0

#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if FMA4 is available at compile-time, to 0 otherwise.
 */
#ifdef __FMA4__
#define XSIMD_WITH_FMA4 1
#else
#define XSIMD_WITH_FMA4 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512F is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512F__
// AVX512 instructions are supported starting with gcc 6
// see https://www.gnu.org/software/gcc/gcc-6/changes.html
// check clang first, newer clang always defines __GNUC__ = 4
#if defined(__clang__) && __clang_major__ >= 6
#define XSIMD_WITH_AVX512F 1
#elif defined(__GNUC__) && __GNUC__ < 6
#define XSIMD_WITH_AVX512F 0
#else
#define XSIMD_WITH_AVX512F 1
#if __GNUC__ == 6
#define XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY 1
#endif
#endif
#else
#define XSIMD_WITH_AVX512F 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512CD is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512CD__
// Avoids repeating the GCC workaround over and over
#define XSIMD_WITH_AVX512CD XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512CD 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512VL is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512VL__
#define XSIMD_WITH_AVX512VL XSIMD_WITH_AVX512CD
#else
#define XSIMD_WITH_AVX512VL 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512DQ is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512DQ__
#define XSIMD_WITH_AVX512DQ XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512DQ 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512BW is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512BW__
#define XSIMD_WITH_AVX512BW XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512BW 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512ER is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512ER__
#define XSIMD_WITH_AVX512ER XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512ER 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512PF is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512PF__
#define XSIMD_WITH_AVX512PF XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512PF 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512IFMA is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512IFMA__
#define XSIMD_WITH_AVX512IFMA XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512IFMA 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512VBMI is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512VBMI__
#define XSIMD_WITH_AVX512VBMI XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512VBMI 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512VBMI2 is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512VBMI2__
#define XSIMD_WITH_AVX512VBMI2 XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512VBMI2 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if AVX512VNNI is available at compile-time, to 0 otherwise.
 */
#ifdef __AVX512VNNI__

#if XSIMD_WITH_AVX512VBMI2
#define XSIMD_WITH_AVX512VNNI_AVX512VBMI2 XSIMD_WITH_AVX512F
#define XSIMD_WITH_AVX512VNNI_AVX512BW XSIMD_WITH_AVX512F
#else
#define XSIMD_WITH_AVX512VNNI_AVX512VBMI2 0
#define XSIMD_WITH_AVX512VNNI_AVX512BW XSIMD_WITH_AVX512F
#endif

#else

#define XSIMD_WITH_AVX512VNNI_AVX512VBMI2 0
#define XSIMD_WITH_AVX512VNNI_AVX512BW 0

#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is in the ARM architecture family in 64 bits, to 0 otherwise
 */
#if defined(__aarch64__) || defined(_M_ARM64)
#define XSIMD_TARGET_ARM64 1
#else
#define XSIMD_TARGET_ARM64 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is in the ARM architecture family, to 0 otherwise
 */
#if defined(__arm__) || defined(_M_ARM) || XSIMD_TARGET_ARM64
#define XSIMD_TARGET_ARM 1
#else
#define XSIMD_TARGET_ARM 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if NEON is available at compile-time, to 0 otherwise.
 */
#if (defined(__ARM_NEON) && (__ARM_ARCH >= 7)) || XSIMD_TARGET_ARM64
#define XSIMD_WITH_NEON 1
#else
#define XSIMD_WITH_NEON 0
#endif

// Neon is always available on Arm64, though it is theoritially possible to compile
// without it, such as -march=armv8-a+nosimd.
// Note that MSVC may never define __ARM_NEON even when available.
/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if NEON64 is available at compile-time, to 0 otherwise.
 */
#if XSIMD_TARGET_ARM64
#define XSIMD_WITH_NEON64 1
#else
#define XSIMD_WITH_NEON64 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if i8mm neon64 extension is available at compile-time, to 0 otherwise.
 */
#if defined(__ARM_FEATURE_MATMUL_INT8)
#define XSIMD_WITH_I8MM_NEON64 1
#else
#define XSIMD_WITH_I8MM_NEON64 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if SVE is available and bit width is pre-set at compile-time, to 0 otherwise.
 */
#if defined(__ARM_FEATURE_SVE) && defined(__ARM_FEATURE_SVE_BITS) && __ARM_FEATURE_SVE_BITS > 0
#define XSIMD_WITH_SVE 1
#define XSIMD_SVE_BITS __ARM_FEATURE_SVE_BITS
#else
#define XSIMD_WITH_SVE 0
#define XSIMD_SVE_BITS 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is the RISC-V architecture family.
 */
#ifdef __riscv
#define XSIMD_TARGET_RISCV 1
#else
#define XSIMD_TARGET_RISCV 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if RVV is available and bit width is pre-set at compile-time, to 0 otherwise.
 */
#if defined(__riscv_vector) && defined(__riscv_v_fixed_vlen) && __riscv_v_fixed_vlen > 0
#define XSIMD_WITH_RVV 1
#define XSIMD_RVV_BITS __riscv_v_fixed_vlen
#else
#define XSIMD_WITH_RVV 0
#define XSIMD_RVV_BITS 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if WebAssembly SIMD is available at compile-time, to 0 otherwise.
 */
#ifdef __EMSCRIPTEN__
#define XSIMD_WITH_WASM 1
#else
#define XSIMD_WITH_WASM 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is in the PowerPC architecture family, to 0 otherwise
 */
#if defined(__powerpc__) || defined(__powerpc64__) || defined(_ARCH_PPC) || defined(_ARCH_PPC64)
#define XSIMD_TARGET_PPC 1
#else
#define XSIMD_TARGET_PPC 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if VMX with VSX extension is available at compile-time, to 0 otherwise.
 */
#if defined(__VEC__) && defined(__VSX__)
#define XSIMD_WITH_VSX 1
#else
#define XSIMD_WITH_VSX 0
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is in the IBM Z architecture family, to 0 otherwise
 */
#if defined(__s390x__)
#define XSIMD_TARGET_S390X 1
#else
#define XSIMD_TARGET_S390X 0
#endif

/**
+ * @ingroup xsimd_config_macro
+ *
+ * Set to 1 if s390x VXE is available at compile-time, to 0 otherwise.
+ * Float vectors have been introduced with VXE included with IBM z14.
+ */
#if defined(__VEC__) && __VEC__ >= 10304 && __ARCH__ >= 12
#define XSIMD_WITH_VXE 1
#else
#define XSIMD_WITH_VXE 0
#endif

// Workaround for MSVC compiler
#ifdef _MSC_VER

#if XSIMD_WITH_AVX512

#undef XSIMD_WITH_AVX2
#define XSIMD_WITH_AVX2 1

#endif

#if XSIMD_WITH_AVX2

#undef XSIMD_WITH_AVX
#define XSIMD_WITH_AVX 1

#undef XSIMD_WITH_FMA3_AVX
#define XSIMD_WITH_FMA3_AVX 1

#undef XSIMD_WITH_FMA3_AVX2
#define XSIMD_WITH_FMA3_AVX2 1

#endif

#if XSIMD_WITH_AVX

#undef XSIMD_WITH_SSE4_2
#define XSIMD_WITH_SSE4_2 1

#endif

#if XSIMD_WITH_SSE4_2

#undef XSIMD_WITH_SSE4_1
#define XSIMD_WITH_SSE4_1 1

#endif

#if XSIMD_WITH_SSE4_1

#undef XSIMD_WITH_SSSE3
#define XSIMD_WITH_SSSE3 1

#endif

#if XSIMD_WITH_SSSE3

#undef XSIMD_WITH_SSE3
#define XSIMD_WITH_SSE3 1

#endif

#if XSIMD_WITH_SSE3 || ((defined(_M_AMD64) || defined(_M_X64)) && !defined(_M_ARM64EC)) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#undef XSIMD_WITH_SSE2
#define XSIMD_WITH_SSE2 1
#endif

#endif

#if !XSIMD_WITH_SSE2 && !XSIMD_WITH_SSE3 && !XSIMD_WITH_SSSE3 && !XSIMD_WITH_SSE4_1 && !XSIMD_WITH_SSE4_2 && !XSIMD_WITH_AVX && !XSIMD_WITH_AVX2 && !XSIMD_WITH_AVXVNNI && !XSIMD_WITH_FMA3_SSE && !XSIMD_WITH_FMA4 && !XSIMD_WITH_FMA3_AVX && !XSIMD_WITH_FMA3_AVX2 && !XSIMD_WITH_AVX512F && !XSIMD_WITH_AVX512CD && !XSIMD_WITH_AVX512VL && !XSIMD_WITH_AVX512DQ && !XSIMD_WITH_AVX512BW && !XSIMD_WITH_AVX512ER && !XSIMD_WITH_AVX512PF && !XSIMD_WITH_AVX512IFMA && !XSIMD_WITH_AVX512VBMI && !XSIMD_WITH_AVX512VBMI2 && !XSIMD_WITH_NEON && !XSIMD_WITH_NEON64 && !XSIMD_WITH_SVE && !XSIMD_WITH_RVV && !XSIMD_WITH_WASM && !XSIMD_WITH_VSX && !XSIMD_WITH_EMULATED && !XSIMD_WITH_VXE
#define XSIMD_NO_SUPPORTED_ARCHITECTURE
#endif

/**
 * @ingroup xsimd_config_macro
 *
 * Set to 1 if the target is a linux
 */
#if defined(__linux__) && (!defined(__ANDROID_API__) || __ANDROID_API__ >= 18)
#define XSIMD_HAVE_LINUX_GETAUXVAL 1
#else
#define XSIMD_HAVE_LINUX_GETAUXVAL 0
#endif

#endif
