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

#ifndef XSIMD_MACROS_HPP
#define XSIMD_MACROS_HPP

#include "./xsimd_config.hpp"

#if defined(__VEC__)
#define XSIMD_INLINE inline
#elif defined __has_attribute
#if __has_attribute(always_inline)
#define XSIMD_INLINE inline __attribute__((always_inline))
#else
#define XSIMD_INLINE inline
#endif
#elif defined(_MSC_VER)
#define XSIMD_INLINE inline __forceinline
#else
#define XSIMD_INLINE inline
#endif

#define XSIMD_CONCAT_INNER(a, b) a##b
#define XSIMD_CONCAT(a, b) XSIMD_CONCAT_INNER(a, b)

#if defined(__FAST_MATH__)
#define XSIMD_NO_DENORMALS
#define XSIMD_NO_INFINITIES
#define XSIMD_NO_NANS
#endif

#endif
