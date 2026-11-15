/***************************************************************************
 * Copyright (c) Andreas Krebbel                                            *
 * Based on xsimd_vsx_register.hpp                                          *
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_VXE_REGISTER_HPP
#define XSIMD_VXE_REGISTER_HPP

#include "./xsimd_common_arch.hpp"
#include "./xsimd_register.hpp"

#if XSIMD_WITH_VXE
#include <vecintrin.h>
#endif

namespace xsimd
{
    /**
     * @ingroup architectures
     *
     * VXE instructions
     */
    struct vxe : common
    {
        static constexpr bool supported() noexcept { return XSIMD_WITH_VXE; }
        static constexpr bool available() noexcept { return true; }
        static constexpr bool requires_alignment() noexcept { return true; }
        static constexpr std::size_t alignment() noexcept { return 16; }
        static constexpr char const* name() noexcept { return "vxe"; }
    };

#if XSIMD_WITH_VXE
    namespace types
    {

#define XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(T, Tv, Tb)              \
    template <>                                                      \
    struct get_bool_simd_register<T, vxe>                            \
    {                                                                \
        struct type                                                  \
        {                                                            \
            using register_type = __vector __bool Tb;                \
            register_type data;                                      \
            type() = default;                                        \
            type(register_type r)                                    \
                : data(r)                                            \
            {                                                        \
            }                                                        \
            operator register_type() const noexcept { return data; } \
        };                                                           \
    };                                                               \
    XSIMD_DECLARE_SIMD_REGISTER(T, vxe, __vector Tv)

        // The VXE vector intrinsics do not support long, unsigned long,
        // and char data types.  batches of these types are vectors of
        // equivalent types.
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(signed char, signed char, char);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(unsigned char, unsigned char, char);
#ifdef __CHAR_UNSIGNED__
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(char, unsigned char, char);
#else
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(char, signed char, char);
#endif
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(unsigned short, unsigned short, short);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(short, short, short);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(unsigned int, unsigned int, int);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(int, int, int);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(unsigned long, unsigned long long, long long);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(long, long long, long long);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(float, float, int);
        XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER(double, double, long long);

#undef XSIMD_DECLARE_SIMD_BOOL_VXE_REGISTER
    }
#endif
}

#endif
