/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 * Copyright (c) Marco Barbone                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_FMA3_AVX2_128_HPP
#define XSIMD_FMA3_AVX2_128_HPP

#include "../types/xsimd_fma3_avx2_128_register.hpp"

// Allow inclusion of xsimd_fma3_sse.hpp
#ifdef XSIMD_FMA3_SSE_HPP
#undef XSIMD_FMA3_SSE_HPP
#define XSIMD_FORCE_FMA3_SSE_HPP
#endif

// Disallow inclusion of ./xsimd_fma3_sse_register.hpp
#ifndef XSIMD_FMA3_SSE_REGISTER_HPP
#define XSIMD_FMA3_SSE_REGISTER_HPP
#define XSIMD_FORCE_FMA3_SSE_REGISTER_HPP
#endif

// Include ./xsimd_fma3_sse.hpp but s/sse4_2/avx2_128
#define sse4_2 avx2_128
#include "./xsimd_fma3_sse.hpp"
#undef sse4_2
#undef XSIMD_FMA3_SSE_HPP

// Carefully restore guards
#ifdef XSIMD_FORCE_FMA3_SSE_HPP
#define XSIMD_FMA3_SSE_HPP
#undef XSIMD_FORCE_FMA3_SSE_HPP
#endif

#ifdef XSIMD_FORCE_FMA3_SSE_REGISTER_HPP
#undef XSIMD_FMA3_SSE_REGISTER_HPP
#undef XSIMD_FORCE_FMA3_SSE_REGISTER_HPP
#endif

#endif
