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

#ifndef XSIMD_NEON_HPP
#define XSIMD_NEON_HPP

#include "../types/xsimd_batch_fwd.hpp"
#include "../types/xsimd_neon_register.hpp"
#include "../types/xsimd_utils.hpp"
#include "../utils/xsimd_type_traits.hpp"
#include "./common/xsimd_common_bit.hpp"
#include "./common/xsimd_common_cast.hpp"
#include "./xsimd_common_fwd.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <complex>
#include <type_traits>

namespace xsimd
{
    namespace kernel
    {
        using namespace types;

        namespace detail
        {
            /**************************************
             * enabling / disabling metafunctions *
             **************************************/

            template <class T>
            using enable_neon_type_t = std::enable_if_t<std::is_integral_v<T> || std::is_same_v<T, float>,
                                                        int>;

            template <class T>
            using exclude_int64_neon_t
                = std::enable_if_t<(std::is_integral_v<T> && sizeof(T) != 8) || std::is_same_v<T, float>, int>;
        }

        /****************
         * bitwise_cast *
         ****************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(uint8x16_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_u8_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_u8_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_u8_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_u8_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_u8_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_u8_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_u8_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_u8_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_s8_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(int8x16_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_s8_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_s8_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_s8_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_s8_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_s8_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_s8_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_s8_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_u16_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_u16_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(uint16x8_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_u16_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_u16_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_u16_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_u16_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_u16_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_u16_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_s16_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_s16_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_s16_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(int16x8_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_s16_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_s16_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_s16_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_s16_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_s16_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_u32_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_u32_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_u32_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_u32_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(uint32x4_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_u32_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_u32_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_u32_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_u32_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_s32_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_s32_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_s32_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_s32_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_s32_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(int32x4_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_s32_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_s32_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_s32_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_u64_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_u64_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_u64_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_u64_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_u64_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_u64_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(uint64x2_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_u64_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_u64_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_s64_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_s64_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_s64_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_s64_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_s64_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_s64_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_s64_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(int64x2_t a) noexcept { return a; }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_s64_f32(a); }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_f32_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_f32_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_f32_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_f32_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_f32_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_f32_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_f32_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_f32_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(float32x4_t a) noexcept { return a; }
        }

        template <class A, class T, class R>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<neon>) noexcept
        {
            using src_register_type = typename batch<T, A>::register_type;
            return wrap::x_vreinterpretq<map_to_sized_type_t<R>, map_to_sized_type_t<T>>(src_register_type(arg));
        }

        /*************
         * broadcast *
         *************/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_u8(uint8_t(val));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_s8(int8_t(val));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_u16(uint16_t(val));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_s16(int16_t(val));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_u32(uint32_t(val));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_s32(int32_t(val));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_u64(uint64_t(val));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon>) noexcept
        {
            return vdupq_n_s64(int64_t(val));
        }

        template <class A>
        XSIMD_INLINE batch<float, A> broadcast(float val, requires_arch<neon>) noexcept
        {
            return vdupq_n_f32(val);
        }

        /*************
         * from_bool *
         *************/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vandq_u8(arg, vdupq_n_u8(1));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vreinterpretq_s8_u8(vandq_u8(arg.data, vdupq_n_u8(1)));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vandq_u16(arg, vdupq_n_u16(1));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vreinterpretq_s16_u16(vandq_u16(arg.data, vdupq_n_u16(1)));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vandq_u32(arg, vdupq_n_u32(1));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vreinterpretq_s32_u32(vandq_u32(arg.data, vdupq_n_u32(1)));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vandq_u64(arg, vdupq_n_u64(1));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return vreinterpretq_s64_u64(vandq_u64(arg.data, vdupq_n_u64(1)));
        }

        template <class A>
        XSIMD_INLINE batch<float, A> from_bool(batch_bool<float, A> const& arg, requires_arch<neon>) noexcept
        {
            return vreinterpretq_f32_u32(vandq_u32(arg, vreinterpretq_u32_f32(vdupq_n_f32(1.f))));
        }

        /********
         * load *
         ********/

        // It is not possible to use a call to A::alignment() here, so use an
        // immediate instead.
#if defined(__clang__) || defined(__GNUC__)
#define xsimd_aligned_load(inst, type, expr) inst((type)__builtin_assume_aligned(expr, 16))
#else
#define xsimd_aligned_load(inst, type, expr) inst((type)expr)
#endif

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_u8, uint8_t*, src);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_s8, int8_t*, src);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_u16, uint16_t*, src);
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_s16, int16_t*, src);
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_u32, uint32_t*, src);
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_s32, int32_t*, src);
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_u64, uint64_t*, src);
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_s64, int64_t*, src);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> load_aligned(float const* src, convert<float>, requires_arch<neon>) noexcept
        {
            return xsimd_aligned_load(vld1q_f32, float*, src);
        }

#undef xsimd_aligned_load

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_u8((uint8_t*)src);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_s8((int8_t*)src);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_u16((uint16_t*)src);
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_s16((int16_t*)src);
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_u32((uint32_t*)src);
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_s32((int32_t*)src);
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_u64((uint64_t*)src);
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<neon>) noexcept
        {
            return vld1q_s64((int64_t*)src);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> load_unaligned(float const* src, convert<float>, requires_arch<neon>) noexcept
        {
            return vld1q_f32(src);
        }

        /* batch bool version */
        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<neon>) noexcept
        {
            auto vmem = load_unaligned<A>((unsigned char const*)mem, convert<unsigned char> {}, A {});
            auto const zero = batch<unsigned char, A> { 0 };
            return { (zero - vmem).data };
        }
        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE batch_bool<T, A> load_aligned(bool const* mem, batch_bool<T, A> t, requires_arch<neon> r) noexcept
        {
            return load_unaligned(mem, t, r);
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<neon>) noexcept
        {
            auto const vmem = batch<uint16_t, A>(vmovl_u8(vld1_u8((unsigned char const*)mem)));
            auto const zero = batch<uint16_t, A> { 0 };
            return { (zero - vmem).data };
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE batch_bool<T, A> load_aligned(bool const* mem, batch_bool<T, A> t, requires_arch<neon> r) noexcept
        {
            return load_unaligned(mem, t, r);
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<neon>) noexcept
        {
            uint8x8_t tmp = vreinterpret_u8_u32(vset_lane_u32(*(unsigned int*)mem, vdup_n_u32(0), 0));
            auto const vmem = batch<uint32_t, A>(vmovl_u16(vget_low_u16(vmovl_u8(tmp))));
            auto const zero = batch<uint32_t, A> { 0 };
            return { (zero - vmem).data };
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE batch_bool<T, A> load_aligned(bool const* mem, batch_bool<T, A> t, requires_arch<neon> r) noexcept
        {
            return load_unaligned(mem, t, r);
        }

        /* masked version */
        namespace detail
        {
            template <bool... Values>
            struct load_masked;

            template <>
            struct load_masked<>
            {
                template <size_t I, class A, class T>
                static XSIMD_INLINE batch<T, A> apply(T const* /* mem */, batch<T, A> acc) noexcept
                {
                    return acc;
                }
            };
            template <bool Value, bool... Values>
            struct load_masked<Value, Values...>
            {
                template <size_t I, class A, class T>
                static XSIMD_INLINE batch<T, A> apply(T const* mem, batch<T, A> acc) noexcept
                {
                    if constexpr (Value)
                    {
                        acc = insert(acc, mem[I], index<I> {});
                    }
                    return load_masked<Values...>::template apply<I + 1>(mem, acc);
                }
            };
        }

        template <class A, class T, bool Value, bool... Values, class Mode>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, Value, Values...> /* mask */, convert<T>, Mode, requires_arch<neon>) noexcept
        {
            return detail::load_masked<Value, Values...>::template apply<0>(mem, batch<T, A>(T(0)));
        }

        /*********
         * store *
         *********/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_u8((uint8_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_s8((int8_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_u16((uint16_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_s16((int16_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_u32((uint32_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_s32((int32_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_u64((uint64_t*)dst, src);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_s64((int64_t*)dst, src);
        }

        template <class A>
        XSIMD_INLINE void store_aligned(float* dst, batch<float, A> const& src, requires_arch<neon>) noexcept
        {
            vst1q_f32(dst, src);
        }

        template <class A, class T>
        XSIMD_INLINE void store_unaligned(T* dst, batch<T, A> const& src, requires_arch<neon>) noexcept
        {
            store_aligned<A>(dst, src, A {});
        }

        /****************
         * load_complex *
         ****************/

        template <class A>
        XSIMD_INLINE batch<std::complex<float>, A> load_complex_aligned(std::complex<float> const* mem, convert<std::complex<float>>, requires_arch<neon>) noexcept
        {
            using real_batch = batch<float, A>;
            const float* buf = reinterpret_cast<const float*>(mem);
            float32x4x2_t tmp = vld2q_f32(buf);
            real_batch real = tmp.val[0],
                       imag = tmp.val[1];
            return batch<std::complex<float>, A> { real, imag };
        }

        template <class A>
        XSIMD_INLINE batch<std::complex<float>, A> load_complex_unaligned(std::complex<float> const* mem, convert<std::complex<float>> cvt, requires_arch<neon>) noexcept
        {
            return load_complex_aligned<A>(mem, cvt, A {});
        }

        /*****************
         * store_complex *
         *****************/

        template <class A>
        XSIMD_INLINE void store_complex_aligned(std::complex<float>* dst, batch<std::complex<float>, A> const& src, requires_arch<neon>) noexcept
        {
            float32x4x2_t tmp;
            tmp.val[0] = src.real();
            tmp.val[1] = src.imag();
            float* buf = reinterpret_cast<float*>(dst);
            vst2q_f32(buf, tmp);
        }

        template <class A>
        XSIMD_INLINE void store_complex_unaligned(std::complex<float>* dst, batch<std::complex<float>, A> const& src, requires_arch<neon>) noexcept
        {
            store_complex_aligned(dst, src, A {});
        }

        /*********************
         * store<batch_bool> *
         *********************/
        template <class T, class A, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE void store(batch_bool<T, A> b, bool* mem, requires_arch<neon>) noexcept
        {
            uint8x16_t val = vshrq_n_u8(b.data, 7);
            alignas(A::alignment()) uint8_t buffer[batch_bool<T, A>::size];
            vst1q_u8(buffer, val);
            memcpy(mem, buffer, sizeof(buffer));
        }

        template <class T, class A, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE void store(batch_bool<T, A> b, bool* mem, requires_arch<neon>) noexcept
        {
            uint8x8_t val = vshr_n_u8(vqmovn_u16(b.data), 7);
            alignas(A::alignment()) uint8_t buffer[batch_bool<T, A>::size];
            vst1_u8(buffer, val);
            memcpy(mem, buffer, sizeof(buffer));
        }

        template <class T, class A, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE void store(batch_bool<T, A> b, bool* mem, requires_arch<neon>) noexcept
        {
            uint8x8_t val = vshr_n_u8(vqmovn_u16(vcombine_u16(vqmovn_u32(b.data), vdup_n_u16(0))), 7);
            alignas(A::alignment()) uint8_t buffer[8];
            vst1_u8(buffer, val);
            memcpy(mem, buffer, batch_bool<T, A>::size);
        }

        template <class T, class A, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE void store(batch_bool<T, A> b, bool* mem, requires_arch<neon>) noexcept
        {
            uint8x8_t val = vshr_n_u8(vqmovn_u16(vcombine_u16(vqmovn_u32(vcombine_u32(vqmovn_u64(b.data), vdup_n_u32(0))), vdup_n_u16(0))), 7);
            alignas(A::alignment()) uint8_t buffer[8];
            vst1_u8(buffer, val);
            memcpy(mem, buffer, batch_bool<T, A>::size);
        }

        template <class A>
        XSIMD_INLINE void store(batch_bool<float, A> b, bool* mem, requires_arch<neon>) noexcept
        {
            store(batch_bool<uint32_t, A>(b.data), mem, A {});
        }

        /*******
         * set *
         *******/

        template <class A, class T, class... Args, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<neon> req, Args... args) noexcept
        {
            alignas(A::alignment()) T data[] = { static_cast<T>(args)... };
            return load_aligned<A, T>(data, {}, req);
        }

        template <class A, class T, class... Args, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<neon>, Args... args) noexcept
        {
            using unsigned_type = as_unsigned_integer_t<T>;
            auto const out = batch<unsigned_type, A> { static_cast<unsigned_type>(args ? -1LL : 0LL)... };
            return { out.data };
        }

        template <class A>
        XSIMD_INLINE batch<float, A> set(batch<float, A> const&, requires_arch<neon> req, float f0, float f1, float f2, float f3) noexcept
        {
            alignas(A::alignment()) float data[] = { f0, f1, f2, f3 };
            return load_aligned<A>(data, {}, req);
        }

        template <class A>
        XSIMD_INLINE batch<std::complex<float>, A> set(batch<std::complex<float>, A> const&, requires_arch<neon>,
                                                       std::complex<float> c0, std::complex<float> c1,
                                                       std::complex<float> c2, std::complex<float> c3) noexcept
        {
            return batch<std::complex<float>, A>(float32x4_t { c0.real(), c1.real(), c2.real(), c3.real() },
                                                 float32x4_t { c0.imag(), c1.imag(), c2.imag(), c3.imag() });
        }

        template <class A, class... Args>
        XSIMD_INLINE batch_bool<float, A> set(batch_bool<float, A> const&, requires_arch<neon>, Args... args) noexcept
        {
            using unsigned_type = as_unsigned_integer_t<float>;
            auto const out = batch<unsigned_type, A> { static_cast<unsigned_type>(args ? -1LL : 0LL)... };
            return { out.data };
        }

        /*******
         * neg *
         *******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vreinterpretq_u8_s8(vnegq_s8(vreinterpretq_s8_u8(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vnegq_s8(rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vreinterpretq_u16_s16(vnegq_s16(vreinterpretq_s16_u16(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vnegq_s16(rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vreinterpretq_u32_s32(vnegq_s32(vreinterpretq_s32_u32(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vnegq_s32(rhs);
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return 0 - rhs;
        }

        template <class A>
        XSIMD_INLINE batch<float, A> neg(batch<float, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vnegq_f32(rhs);
        }

        /*******
         * add *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vaddq(uint8x16_t a, uint8x16_t b) noexcept { return vaddq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vaddq(int8x16_t a, int8x16_t b) noexcept { return vaddq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vaddq(uint16x8_t a, uint16x8_t b) noexcept { return vaddq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vaddq(int16x8_t a, int16x8_t b) noexcept { return vaddq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vaddq(uint32x4_t a, uint32x4_t b) noexcept { return vaddq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vaddq(int32x4_t a, int32x4_t b) noexcept { return vaddq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vaddq(uint64x2_t a, uint64x2_t b) noexcept { return vaddq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vaddq(int64x2_t a, int64x2_t b) noexcept { return vaddq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vaddq(float32x4_t a, float32x4_t b) noexcept { return vaddq_f32(a, b); }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vaddq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /*******
         * avg *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vhaddq(uint8x16_t a, uint8x16_t b) noexcept { return vhaddq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vhaddq(uint16x8_t a, uint16x8_t b) noexcept { return vhaddq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vhaddq(uint32x4_t a, uint32x4_t b) noexcept { return vhaddq_u32(a, b); }
        }

        template <class A, class T, class = std::enable_if_t<(std::is_unsigned_v<T> && sizeof(T) != 8)>>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vhaddq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /********
         * avgr *
         ********/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vrhaddq(uint8x16_t a, uint8x16_t b) noexcept { return vrhaddq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vrhaddq(uint16x8_t a, uint16x8_t b) noexcept { return vrhaddq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vrhaddq(uint32x4_t a, uint32x4_t b) noexcept { return vrhaddq_u32(a, b); }
        }

        template <class A, class T, class = std::enable_if_t<(std::is_unsigned_v<T> && sizeof(T) != 8)>>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vrhaddq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /********
         * sadd *
         ********/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vqaddq(uint8x16_t a, uint8x16_t b) noexcept { return vqaddq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vqaddq(int8x16_t a, int8x16_t b) noexcept { return vqaddq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vqaddq(uint16x8_t a, uint16x8_t b) noexcept { return vqaddq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vqaddq(int16x8_t a, int16x8_t b) noexcept { return vqaddq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vqaddq(uint32x4_t a, uint32x4_t b) noexcept { return vqaddq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vqaddq(int32x4_t a, int32x4_t b) noexcept { return vqaddq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vqaddq(uint64x2_t a, uint64x2_t b) noexcept { return vqaddq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vqaddq(int64x2_t a, int64x2_t b) noexcept { return vqaddq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vqaddq(float32x4_t a, float32x4_t b) noexcept { return vaddq_f32(a, b); }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vqaddq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /*******
         * sub *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vsubq(uint8x16_t a, uint8x16_t b) noexcept { return vsubq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vsubq(int8x16_t a, int8x16_t b) noexcept { return vsubq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vsubq(uint16x8_t a, uint16x8_t b) noexcept { return vsubq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vsubq(int16x8_t a, int16x8_t b) noexcept { return vsubq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vsubq(uint32x4_t a, uint32x4_t b) noexcept { return vsubq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vsubq(int32x4_t a, int32x4_t b) noexcept { return vsubq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vsubq(uint64x2_t a, uint64x2_t b) noexcept { return vsubq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vsubq(int64x2_t a, int64x2_t b) noexcept { return vsubq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vsubq(float32x4_t a, float32x4_t b) noexcept { return vsubq_f32(a, b); }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vsubq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /********
         * ssub *
         ********/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vqsubq(uint8x16_t a, uint8x16_t b) noexcept { return vqsubq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vqsubq(int8x16_t a, int8x16_t b) noexcept { return vqsubq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vqsubq(uint16x8_t a, uint16x8_t b) noexcept { return vqsubq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vqsubq(int16x8_t a, int16x8_t b) noexcept { return vqsubq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vqsubq(uint32x4_t a, uint32x4_t b) noexcept { return vqsubq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vqsubq(int32x4_t a, int32x4_t b) noexcept { return vqsubq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vqsubq(uint64x2_t a, uint64x2_t b) noexcept { return vqsubq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vqsubq(int64x2_t a, int64x2_t b) noexcept { return vqsubq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vqsubq(float32x4_t a, float32x4_t b) noexcept { return vsubq_f32(a, b); }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vqsubq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /*******
         * mul *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vmulq(uint8x16_t a, uint8x16_t b) noexcept { return vmulq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vmulq(int8x16_t a, int8x16_t b) noexcept { return vmulq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vmulq(uint16x8_t a, uint16x8_t b) noexcept { return vmulq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vmulq(int16x8_t a, int16x8_t b) noexcept { return vmulq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vmulq(uint32x4_t a, uint32x4_t b) noexcept { return vmulq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vmulq(int32x4_t a, int32x4_t b) noexcept { return vmulq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vmulq(float32x4_t a, float32x4_t b) noexcept { return vmulq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vmulq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        /*********
         * mul_hi *
         *********/

        template <class A>
        XSIMD_INLINE batch<int8_t, A> mul_hi(batch<int8_t, A> const& lhs, batch<int8_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            int16x8_t lo = vmull_s8(vget_low_s8(lhs), vget_low_s8(rhs));
            int16x8_t hi = vmull_s8(vget_high_s8(lhs), vget_high_s8(rhs));
            return vuzpq_s8(vreinterpretq_s8_s16(lo), vreinterpretq_s8_s16(hi)).val[1];
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> mul_hi(batch<uint8_t, A> const& lhs, batch<uint8_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint16x8_t lo = vmull_u8(vget_low_u8(lhs), vget_low_u8(rhs));
            uint16x8_t hi = vmull_u8(vget_high_u8(lhs), vget_high_u8(rhs));
            return vuzpq_u8(vreinterpretq_u8_u16(lo), vreinterpretq_u8_u16(hi)).val[1];
        }
        template <class A>
        XSIMD_INLINE batch<int16_t, A> mul_hi(batch<int16_t, A> const& lhs, batch<int16_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            int32x4_t lo = vmull_s16(vget_low_s16(lhs), vget_low_s16(rhs));
            int32x4_t hi = vmull_s16(vget_high_s16(lhs), vget_high_s16(rhs));
            return vuzpq_s16(vreinterpretq_s16_s32(lo), vreinterpretq_s16_s32(hi)).val[1];
        }
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> mul_hi(batch<uint16_t, A> const& lhs, batch<uint16_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint32x4_t lo = vmull_u16(vget_low_u16(lhs), vget_low_u16(rhs));
            uint32x4_t hi = vmull_u16(vget_high_u16(lhs), vget_high_u16(rhs));
            return vuzpq_u16(vreinterpretq_u16_u32(lo), vreinterpretq_u16_u32(hi)).val[1];
        }
        template <class A>
        XSIMD_INLINE batch<int32_t, A> mul_hi(batch<int32_t, A> const& lhs, batch<int32_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            int64x2_t lo = vmull_s32(vget_low_s32(lhs), vget_low_s32(rhs));
            int64x2_t hi = vmull_s32(vget_high_s32(lhs), vget_high_s32(rhs));
            return vuzpq_s32(vreinterpretq_s32_s64(lo), vreinterpretq_s32_s64(hi)).val[1];
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> mul_hi(batch<uint32_t, A> const& lhs, batch<uint32_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint64x2_t lo = vmull_u32(vget_low_u32(lhs), vget_low_u32(rhs));
            uint64x2_t hi = vmull_u32(vget_high_u32(lhs), vget_high_u32(rhs));
            return vuzpq_u32(vreinterpretq_u32_u64(lo), vreinterpretq_u32_u64(hi)).val[1];
        }
        // 64-bit intentionally falls through to the common scalar fallback

        /************
         * mul_hilo *
         ************/

        template <class A>
        XSIMD_INLINE std::pair<batch<int8_t, A>, batch<int8_t, A>>
        mul_hilo(batch<int8_t, A> const& lhs, batch<int8_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            int16x8_t lo = vmull_s8(vget_low_s8(lhs), vget_low_s8(rhs));
            int16x8_t hi = vmull_s8(vget_high_s8(lhs), vget_high_s8(rhs));
            int8x16x2_t uzp = vuzpq_s8(vreinterpretq_s8_s16(lo), vreinterpretq_s8_s16(hi));
            return { batch<int8_t, A>(uzp.val[1]), batch<int8_t, A>(uzp.val[0]) };
        }
        template <class A>
        XSIMD_INLINE std::pair<batch<uint8_t, A>, batch<uint8_t, A>>
        mul_hilo(batch<uint8_t, A> const& lhs, batch<uint8_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint16x8_t lo = vmull_u8(vget_low_u8(lhs), vget_low_u8(rhs));
            uint16x8_t hi = vmull_u8(vget_high_u8(lhs), vget_high_u8(rhs));
            uint8x16x2_t uzp = vuzpq_u8(vreinterpretq_u8_u16(lo), vreinterpretq_u8_u16(hi));
            return { batch<uint8_t, A>(uzp.val[1]), batch<uint8_t, A>(uzp.val[0]) };
        }
        template <class A>
        XSIMD_INLINE std::pair<batch<int16_t, A>, batch<int16_t, A>>
        mul_hilo(batch<int16_t, A> const& lhs, batch<int16_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            int32x4_t lo = vmull_s16(vget_low_s16(lhs), vget_low_s16(rhs));
            int32x4_t hi = vmull_s16(vget_high_s16(lhs), vget_high_s16(rhs));
            int16x8x2_t uzp = vuzpq_s16(vreinterpretq_s16_s32(lo), vreinterpretq_s16_s32(hi));
            return { batch<int16_t, A>(uzp.val[1]), batch<int16_t, A>(uzp.val[0]) };
        }
        template <class A>
        XSIMD_INLINE std::pair<batch<uint16_t, A>, batch<uint16_t, A>>
        mul_hilo(batch<uint16_t, A> const& lhs, batch<uint16_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint32x4_t lo = vmull_u16(vget_low_u16(lhs), vget_low_u16(rhs));
            uint32x4_t hi = vmull_u16(vget_high_u16(lhs), vget_high_u16(rhs));
            uint16x8x2_t uzp = vuzpq_u16(vreinterpretq_u16_u32(lo), vreinterpretq_u16_u32(hi));
            return { batch<uint16_t, A>(uzp.val[1]), batch<uint16_t, A>(uzp.val[0]) };
        }
        template <class A>
        XSIMD_INLINE std::pair<batch<int32_t, A>, batch<int32_t, A>>
        mul_hilo(batch<int32_t, A> const& lhs, batch<int32_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            int64x2_t lo = vmull_s32(vget_low_s32(lhs), vget_low_s32(rhs));
            int64x2_t hi = vmull_s32(vget_high_s32(lhs), vget_high_s32(rhs));
            int32x4x2_t uzp = vuzpq_s32(vreinterpretq_s32_s64(lo), vreinterpretq_s32_s64(hi));
            return { batch<int32_t, A>(uzp.val[1]), batch<int32_t, A>(uzp.val[0]) };
        }
        template <class A>
        XSIMD_INLINE std::pair<batch<uint32_t, A>, batch<uint32_t, A>>
        mul_hilo(batch<uint32_t, A> const& lhs, batch<uint32_t, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint64x2_t lo = vmull_u32(vget_low_u32(lhs), vget_low_u32(rhs));
            uint64x2_t hi = vmull_u32(vget_high_u32(lhs), vget_high_u32(rhs));
            uint32x4x2_t uzp = vuzpq_u32(vreinterpretq_u32_u64(lo), vreinterpretq_u32_u64(hi));
            return { batch<uint32_t, A>(uzp.val[1]), batch<uint32_t, A>(uzp.val[0]) };
        }

        /*******
         * div *
         *******/

#if defined(XSIMD_FAST_INTEGER_DIVISION)
        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vcvtq_s32_f32(vcvtq_f32_s32(lhs) / vcvtq_f32_s32(rhs));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vcvtq_u32_f32(vcvtq_f32_u32(lhs) / vcvtq_f32_u32(rhs));
        }
#endif

        template <class A>
        XSIMD_INLINE batch<float, A> div(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<neon>) noexcept
        {
            // from stackoverflow & https://projectne10.github.io/Ne10/doc/NE10__divc_8neon_8c_source.html
            // get an initial estimate of 1/b.
            float32x4_t rcp = reciprocal(rhs);

            // use a couple Newton-Raphson steps to refine the estimate.  Depending on your
            // application's accuracy requirements, you may be able to get away with only
            // one refinement (instead of the two used here).  Be sure to test!
            rcp = vmulq_f32(vrecpsq_f32(rhs, rcp), rcp);
            rcp = vmulq_f32(vrecpsq_f32(rhs, rcp), rcp);

            // and finally, compute a / b = a * (1 / b)
            return vmulq_f32(lhs, rcp);
        }

        /******
         * eq *
         ******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vceqq(uint8x16_t a, uint8x16_t b) noexcept { return vceqq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vceqq(int8x16_t a, int8x16_t b) noexcept { return vceqq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vceqq(uint16x8_t a, uint16x8_t b) noexcept { return vceqq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vceqq(int16x8_t a, int16x8_t b) noexcept { return vceqq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vceqq(uint32x4_t a, uint32x4_t b) noexcept { return vceqq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vceqq(int32x4_t a, int32x4_t b) noexcept { return vceqq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vceqq(float32x4_t a, float32x4_t b) noexcept { return vceqq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vceqq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return wrap::x_vceqq<sized_uint_t<sizeof(T)>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            auto eq32 = vceqq_u32(vreinterpretq_u32_u64(lhs.data), vreinterpretq_u32_u64(rhs.data));
            auto rev32 = vrev64q_u32(eq32);
            auto eq64 = vandq_u32(eq32, rev32);
            return batch_bool<T, A>(vreinterpretq_u64_u32(eq64));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            auto eq32 = vceqq_u32(vreinterpretq_u32_s64(lhs.data), vreinterpretq_u32_s64(rhs.data));
            auto rev32 = vrev64q_u32(eq32);
            auto eq64 = vandq_u32(eq32, rev32);
            return batch_bool<T, A>(vreinterpretq_u64_u32(eq64));
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return eq(batch<T, A> { lhs.data }, batch<T, A> { rhs.data }, A {});
        }

        /*************
         * fast_cast *
         *************/

        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<int32_t, A> const& self, batch<float, A> const&, requires_arch<neon>) noexcept
            {
                return vcvtq_f32_s32(self);
            }

            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<uint32_t, A> const& self, batch<float, A> const&, requires_arch<neon>) noexcept
            {
                return vcvtq_f32_u32(self);
            }

            template <class A>
            XSIMD_INLINE batch<int32_t, A> fast_cast(batch<float, A> const& self, batch<int32_t, A> const&, requires_arch<neon>) noexcept
            {
                return vcvtq_s32_f32(self);
            }

            template <class A>
            XSIMD_INLINE batch<uint32_t, A> fast_cast(batch<float, A> const& self, batch<uint32_t, A> const&, requires_arch<neon>) noexcept
            {
                return vcvtq_u32_f32(self);
            }

        }

        /******
         * lt *
         ******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcltq(uint8x16_t a, uint8x16_t b) noexcept { return vcltq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcltq(int8x16_t a, int8x16_t b) noexcept { return vcltq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcltq(uint16x8_t a, uint16x8_t b) noexcept { return vcltq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcltq(int16x8_t a, int16x8_t b) noexcept { return vcltq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcltq(uint32x4_t a, uint32x4_t b) noexcept { return vcltq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcltq(int32x4_t a, int32x4_t b) noexcept { return vcltq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcltq(float32x4_t a, float32x4_t b) noexcept { return vcltq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vcltq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return batch_bool<T, A>(vreinterpretq_u64_s64(vshrq_n_s64(vqsubq_s64(register_type(lhs), register_type(rhs)), 63)));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            register_type acc = { 0x7FFFFFFFFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull };
            return batch_bool<T, A>(vreinterpretq_u64_s64(vshrq_n_s64(vreinterpretq_s64_u64(vqaddq_u64(vqsubq_u64(register_type(rhs), register_type(lhs)), acc)), 63)));
        }

        /******
         * le *
         ******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcleq(uint8x16_t a, uint8x16_t b) noexcept { return vcleq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcleq(int8x16_t a, int8x16_t b) noexcept { return vcleq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcleq(uint16x8_t a, uint16x8_t b) noexcept { return vcleq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcleq(int16x8_t a, int16x8_t b) noexcept { return vcleq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcleq(uint32x4_t a, uint32x4_t b) noexcept { return vcleq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcleq(int32x4_t a, int32x4_t b) noexcept { return vcleq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcleq(float32x4_t a, float32x4_t b) noexcept { return vcleq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vcleq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return !(lhs > rhs);
        }

        /******
         * gt *
         ******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcgtq(uint8x16_t a, uint8x16_t b) noexcept { return vcgtq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcgtq(int8x16_t a, int8x16_t b) noexcept { return vcgtq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcgtq(uint16x8_t a, uint16x8_t b) noexcept { return vcgtq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcgtq(int16x8_t a, int16x8_t b) noexcept { return vcgtq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcgtq(uint32x4_t a, uint32x4_t b) noexcept { return vcgtq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcgtq(int32x4_t a, int32x4_t b) noexcept { return vcgtq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcgtq(float32x4_t a, float32x4_t b) noexcept { return vcgtq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vcgtq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return batch_bool<T, A>(vreinterpretq_u64_s64(vshrq_n_s64(vqsubq_s64(register_type(rhs), register_type(lhs)), 63)));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            register_type acc = { 0x7FFFFFFFFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull };
            return batch_bool<T, A>(vreinterpretq_u64_s64(vshrq_n_s64(vreinterpretq_s64_u64(vqaddq_u64(vqsubq_u64(register_type(lhs), register_type(rhs)), acc)), 63)));
        }

        /******
         * ge *
         ******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcgeq(uint8x16_t a, uint8x16_t b) noexcept { return vcgeq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vcgeq(int8x16_t a, int8x16_t b) noexcept { return vcgeq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcgeq(uint16x8_t a, uint16x8_t b) noexcept { return vcgeq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vcgeq(int16x8_t a, int16x8_t b) noexcept { return vcgeq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcgeq(uint32x4_t a, uint32x4_t b) noexcept { return vcgeq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcgeq(int32x4_t a, int32x4_t b) noexcept { return vcgeq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vcgeq(float32x4_t a, float32x4_t b) noexcept { return vcgeq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vcgeq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return !(lhs < rhs);
        }

        /*******************
         * batch_bool_cast *
         *******************/

        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T_out, A>::register_type;
            return register_type(self);
        }

        /***************
         * bitwise_and *
         ***************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vandq(uint8x16_t a, uint8x16_t b) noexcept { return vandq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vandq(int8x16_t a, int8x16_t b) noexcept { return vandq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vandq(uint16x8_t a, uint16x8_t b) noexcept { return vandq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vandq(int16x8_t a, int16x8_t b) noexcept { return vandq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vandq(uint32x4_t a, uint32x4_t b) noexcept { return vandq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vandq(int32x4_t a, int32x4_t b) noexcept { return vandq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vandq(uint64x2_t a, uint64x2_t b) noexcept { return vandq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vandq(int64x2_t a, int64x2_t b) noexcept { return vandq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vandq(float32x4_t a, float32x4_t b) noexcept
            {
                return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a),
                                                       vreinterpretq_u32_f32(b)));
            }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vandq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return wrap::x_vandq<sized_uint_t<sizeof(T)>>(register_type(lhs), register_type(rhs));
        }

        /**************
         * bitwise_or *
         **************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vorrq(uint8x16_t a, uint8x16_t b) noexcept { return vorrq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vorrq(int8x16_t a, int8x16_t b) noexcept { return vorrq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vorrq(uint16x8_t a, uint16x8_t b) noexcept { return vorrq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vorrq(int16x8_t a, int16x8_t b) noexcept { return vorrq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vorrq(uint32x4_t a, uint32x4_t b) noexcept { return vorrq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vorrq(int32x4_t a, int32x4_t b) noexcept { return vorrq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vorrq(uint64x2_t a, uint64x2_t b) noexcept { return vorrq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vorrq(int64x2_t a, int64x2_t b) noexcept { return vorrq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vorrq(float32x4_t a, float32x4_t b) noexcept
            {
                return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(a),
                                                       vreinterpretq_u32_f32(b)));
            }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vorrq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return wrap::x_vorrq<sized_uint_t<sizeof(T)>>(register_type(lhs), register_type(rhs));
        }

        /***************
         * bitwise_xor *
         ***************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_veorq(uint8x16_t a, uint8x16_t b) noexcept { return veorq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_veorq(int8x16_t a, int8x16_t b) noexcept { return veorq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_veorq(uint16x8_t a, uint16x8_t b) noexcept { return veorq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_veorq(int16x8_t a, int16x8_t b) noexcept { return veorq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_veorq(uint32x4_t a, uint32x4_t b) noexcept { return veorq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_veorq(int32x4_t a, int32x4_t b) noexcept { return veorq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_veorq(uint64x2_t a, uint64x2_t b) noexcept { return veorq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_veorq(int64x2_t a, int64x2_t b) noexcept { return veorq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_veorq(float32x4_t a, float32x4_t b) noexcept
            {
                return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a),
                                                       vreinterpretq_u32_f32(b)));
            }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_veorq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_xor(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return wrap::x_veorq<sized_uint_t<sizeof(T)>>(register_type(lhs), register_type(rhs));
        }

        /*******
         * neq *
         *******/

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return bitwise_xor(lhs, rhs, A {});
        }

        /***************
         * bitwise_not *
         ***************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vmvnq(uint8x16_t a) noexcept { return vmvnq_u8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vmvnq(int8x16_t a) noexcept { return vmvnq_s8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vmvnq(uint16x8_t a) noexcept { return vmvnq_u16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vmvnq(int16x8_t a) noexcept { return vmvnq_s16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vmvnq(uint32x4_t a) noexcept { return vmvnq_u32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vmvnq(int32x4_t a) noexcept { return vmvnq_s32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vmvnq(uint64x2_t a) noexcept
            {
                return vreinterpretq_u64_u32(vmvnq_u32(vreinterpretq_u32_u64(a)));
            }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vmvnq(int64x2_t a) noexcept
            {
                return vreinterpretq_s64_s32(vmvnq_s32(vreinterpretq_s32_s64(a)));
            }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vmvnq(float32x4_t a) noexcept
            {
                return vreinterpretq_f32_u32(vmvnq_u32(vreinterpretq_u32_f32(a)));
            }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vmvnq<map_to_sized_type_t<T>>(register_type(arg));
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return wrap::x_vmvnq<sized_uint_t<sizeof(T)>>(register_type(arg));
        }

        /******************
         * bitwise_andnot *
         ******************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vbicq(uint8x16_t a, uint8x16_t b) noexcept { return vbicq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vbicq(int8x16_t a, int8x16_t b) noexcept { return vbicq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vbicq(uint16x8_t a, uint16x8_t b) noexcept { return vbicq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vbicq(int16x8_t a, int16x8_t b) noexcept { return vbicq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vbicq(uint32x4_t a, uint32x4_t b) noexcept { return vbicq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vbicq(int32x4_t a, int32x4_t b) noexcept { return vbicq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vbicq(uint64x2_t a, uint64x2_t b) noexcept { return vbicq_u64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vbicq(int64x2_t a, int64x2_t b) noexcept { return vbicq_s64(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vbicq(float32x4_t a, float32x4_t b) noexcept
            {
                return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
            }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vbicq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return wrap::x_vbicq<sized_uint_t<sizeof(T)>>(register_type(lhs), register_type(rhs));
        }

        /*******
         * min *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vminq(uint8x16_t a, uint8x16_t b) noexcept { return vminq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vminq(int8x16_t a, int8x16_t b) noexcept { return vminq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vminq(uint16x8_t a, uint16x8_t b) noexcept { return vminq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vminq(int16x8_t a, int16x8_t b) noexcept { return vminq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vminq(uint32x4_t a, uint32x4_t b) noexcept { return vminq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vminq(int32x4_t a, int32x4_t b) noexcept { return vminq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vminq(float32x4_t a, float32x4_t b) noexcept { return vminq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vminq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return select(lhs > rhs, rhs, lhs);
        }

        /*******
         * max *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vmaxq(uint8x16_t a, uint8x16_t b) noexcept { return vmaxq_u8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vmaxq(int8x16_t a, int8x16_t b) noexcept { return vmaxq_s8(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vmaxq(uint16x8_t a, uint16x8_t b) noexcept { return vmaxq_u16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vmaxq(int16x8_t a, int16x8_t b) noexcept { return vmaxq_s16(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vmaxq(uint32x4_t a, uint32x4_t b) noexcept { return vmaxq_u32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vmaxq(int32x4_t a, int32x4_t b) noexcept { return vmaxq_s32(a, b); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vmaxq(float32x4_t a, float32x4_t b) noexcept { return vmaxq_f32(a, b); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vmaxq<map_to_sized_type_t<T>>(register_type(lhs), register_type(rhs));
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return select(lhs > rhs, lhs, rhs);
        }

        /*******
         * abs *
         *******/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vabsq(uint8x16_t a) noexcept { return a; }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vabsq(int8x16_t a) noexcept { return vabsq_s8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vabsq(uint16x8_t a) noexcept { return a; }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vabsq(int16x8_t a) noexcept { return vabsq_s16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vabsq(uint32x4_t a) noexcept { return a; }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vabsq(int32x4_t a) noexcept { return vabsq_s32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vabsq(float32x4_t a) noexcept { return vabsq_f32(a); }
        }

        template <class A, class T, detail::exclude_int64_neon_t<T> = 0>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vabsq<map_to_sized_type_t<T>>(register_type(arg));
        }

        /********
         * rsqrt *
         ********/

        template <class A>
        XSIMD_INLINE batch<float, A> rsqrt(batch<float, A> const& arg, requires_arch<neon>) noexcept
        {
            return vrsqrteq_f32(arg);
        }

        /********
         * sqrt *
         ********/

        template <class A>
        XSIMD_INLINE batch<float, A> sqrt(batch<float, A> const& arg, requires_arch<neon>) noexcept
        {
            batch<float, A> sqrt_reciprocal = vrsqrteq_f32(arg);
            // one iter
            sqrt_reciprocal = sqrt_reciprocal * batch<float, A>(vrsqrtsq_f32(arg * sqrt_reciprocal, sqrt_reciprocal));
            batch<float, A> sqrt_approx = arg * sqrt_reciprocal * batch<float, A>(vrsqrtsq_f32(arg * sqrt_reciprocal, sqrt_reciprocal));
            batch<float, A> zero(0.f);
            return select(arg == zero, zero, sqrt_approx);
        }

        /********************
         * Fused operations *
         ********************/

#ifdef __ARM_FEATURE_FMA
        template <class A>
        XSIMD_INLINE batch<float, A> fma(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<neon>) noexcept
        {
            return vfmaq_f32(z, x, y);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> fms(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<neon>) noexcept
        {
            return vfmaq_f32(-z, x, y);
        }
#endif

        /*********
         * haddp *
         *********/

        template <class A>
        XSIMD_INLINE batch<float, A> haddp(const batch<float, A>* row, requires_arch<neon>) noexcept
        {
            // row = (a,b,c,d)
            float32x2_t tmp1, tmp2, tmp3;
            // tmp1 = (a0 + a2, a1 + a3)
            tmp1 = vpadd_f32(vget_low_f32(row[0]), vget_high_f32(row[0]));
            // tmp2 = (b0 + b2, b1 + b3)
            tmp2 = vpadd_f32(vget_low_f32(row[1]), vget_high_f32(row[1]));
            // tmp1 = (a0..3, b0..3)
            tmp1 = vpadd_f32(tmp1, tmp2);
            // tmp2 = (c0 + c2, c1 + c3)
            tmp2 = vpadd_f32(vget_low_f32(row[2]), vget_high_f32(row[2]));
            // tmp3 = (d0 + d2, d1 + d3)
            tmp3 = vpadd_f32(vget_low_f32(row[3]), vget_high_f32(row[3]));
            // tmp1 = (c0..3, d0..3)
            tmp2 = vpadd_f32(tmp2, tmp3);
            // return = (a0..3, b0..3, c0..3, d0..3)
            return vcombine_f32(tmp1, tmp2);
        }

        /**************
         * reciprocal *
         **************/

        template <class A>
        XSIMD_INLINE batch<float, A>
        reciprocal(const batch<float, A>& x,
                   kernel::requires_arch<neon>) noexcept
        {
            return vrecpeq_f32(x);
        }

        /**********
         * insert *
         **********/

        template <class A, class T, size_t I, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_u8(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_s8(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_u16(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<int16_t, A> insert(batch<int16_t, A> const& self, int16_t val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_s16(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_u32(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_s32(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_u64(val, self, I);
        }

        template <class A, class T, size_t I, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_s64(val, self, I);
        }

        template <class A, size_t I>
        XSIMD_INLINE batch<float, A> insert(batch<float, A> const& self, float val, index<I>, requires_arch<neon>) noexcept
        {
            return vsetq_lane_f32(val, self, I);
        }

        /********************
         * nearbyint_as_int *
         *******************/

        template <class A>
        XSIMD_INLINE batch<int32_t, A> nearbyint_as_int(batch<float, A> const& self,
                                                        requires_arch<neon>) noexcept
        {
            /* origin: https://github.com/DLTcollab/sse2neon/blob/cad518a93b326f0f644b7972d488d04eaa2b0475/sse2neon.h#L4028-L4047 */
            //  Contributors to this work are:
            //   John W. Ratcliff <jratcliffscarab@gmail.com>
            //   Brandon Rowlett <browlett@nvidia.com>
            //   Ken Fast <kfast@gdeb.com>
            //   Eric van Beurden <evanbeurden@nvidia.com>
            //   Alexander Potylitsin <apotylitsin@nvidia.com>
            //   Hasindu Gamaarachchi <hasindu2008@gmail.com>
            //   Jim Huang <jserv@biilabs.io>
            //   Mark Cheng <marktwtn@biilabs.io>
            //   Malcolm James MacLeod <malcolm@gulden.com>
            //   Devin Hussey (easyaspi314) <husseydevin@gmail.com>
            //   Sebastian Pop <spop@amazon.com>
            //   Developer Ecosystem Engineering <DeveloperEcosystemEngineering@apple.com>
            //   Danila Kutenin <danilak@google.com>
            //   François Turban (JishinMaster) <francois.turban@gmail.com>
            //   Pei-Hsuan Hung <afcidk@gmail.com>
            //   Yang-Hao Yuan <yanghau@biilabs.io>
            //   Syoyo Fujita <syoyo@lighttransport.com>
            //   Brecht Van Lommel <brecht@blender.org>

            /*
             * sse2neon is freely redistributable under the MIT License.
             *
             * Permission is hereby granted, free of charge, to any person obtaining a copy
             * of this software and associated documentation files (the "Software"), to deal
             * in the Software without restriction, including without limitation the rights
             * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
             * copies of the Software, and to permit persons to whom the Software is
             * furnished to do so, subject to the following conditions:
             *
             * The above copyright notice and this permission notice shall be included in
             * all copies or substantial portions of the Software.
             *
             * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
             * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
             * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
             * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
             * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
             * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
             * SOFTWARE.
             */

            const auto signmask = vdupq_n_u32(0x80000000);
            const auto half = vbslq_f32(signmask, self,
                                        vdupq_n_f32(0.5f)); /* +/- 0.5 */
            const auto r_normal = vcvtq_s32_f32(vaddq_f32(
                self, half)); /* round to integer: [a + 0.5]*/
            const auto r_trunc = vcvtq_s32_f32(self); /* truncate to integer: [a] */
            const auto plusone = vreinterpretq_s32_u32(vshrq_n_u32(
                vreinterpretq_u32_s32(vnegq_s32(r_trunc)), 31)); /* 1 or 0 */
            const auto r_even = vbicq_s32(vaddq_s32(r_trunc, plusone),
                                          vdupq_n_s32(1)); /* ([a] + {0,1}) & ~1 */
            const auto delta = vsubq_f32(
                self,
                vcvtq_f32_s32(r_trunc)); /* compute delta: delta = (a - [a]) */
            const auto is_delta_half = vceqq_f32(delta, half); /* delta == +/- 0.5 */
            return vbslq_s32(is_delta_half, r_even, r_normal);
        }

        /**************
         * reduce_add *
         **************/

        namespace detail
        {
            template <class T, class A, class V>
            XSIMD_INLINE T sum_batch(V const& arg) noexcept
            {
                T res = T(0);
                for (std::size_t i = 0; i < batch<T, A>::size; ++i)
                {
                    res += arg[i];
                }
                return res;
            }
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            uint8x8_t tmp = vpadd_u8(vget_low_u8(arg), vget_high_u8(arg));
            tmp = vpadd_u8(tmp, tmp);
            tmp = vpadd_u8(tmp, tmp);
            tmp = vpadd_u8(tmp, tmp);
            return vget_lane_u8(tmp, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            int8x8_t tmp = vpadd_s8(vget_low_s8(arg), vget_high_s8(arg));
            tmp = vpadd_s8(tmp, tmp);
            tmp = vpadd_s8(tmp, tmp);
            tmp = vpadd_s8(tmp, tmp);
            return vget_lane_s8(tmp, 0);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            uint16x4_t tmp = vpadd_u16(vget_low_u16(arg), vget_high_u16(arg));
            tmp = vpadd_u16(tmp, tmp);
            tmp = vpadd_u16(tmp, tmp);
            return vget_lane_u16(tmp, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            int16x4_t tmp = vpadd_s16(vget_low_s16(arg), vget_high_s16(arg));
            tmp = vpadd_s16(tmp, tmp);
            tmp = vpadd_s16(tmp, tmp);
            return vget_lane_s16(tmp, 0);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            uint32x2_t tmp = vpadd_u32(vget_low_u32(arg), vget_high_u32(arg));
            tmp = vpadd_u32(tmp, tmp);
            return vget_lane_u32(tmp, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            int32x2_t tmp = vpadd_s32(vget_low_s32(arg), vget_high_s32(arg));
            tmp = vpadd_s32(tmp, tmp);
            return vget_lane_s32(tmp, 0);
        }

        template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return arg.get(0) + arg.get(1);
        }

        template <class A>
        XSIMD_INLINE float reduce_add(batch<float, A> const& arg, requires_arch<neon>) noexcept
        {
            float32x2_t tmp = vpadd_f32(vget_low_f32(arg), vget_high_f32(arg));
            tmp = vpadd_f32(tmp, tmp);
            return vget_lane_f32(tmp, 0);
        }

        /**************
         * reduce_max *
         **************/

        // Using common implementation because ARM does not provide intrinsics
        // for this operation

        /**************
         * reduce_min *
         **************/

        // Using common implementation because ARM does not provide intrinsics
        // for this operation

        /**************
         * reduce_mul *
         **************/

        // Using common implementation because ARM does not provide intrinsics
        // for this operation

        /**********
         * select *
         **********/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vbslq(uint8x16_t a, uint8x16_t b, uint8x16_t c) noexcept { return vbslq_u8(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_vbslq(uint8x16_t a, int8x16_t b, int8x16_t c) noexcept { return vbslq_s8(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vbslq(uint16x8_t a, uint16x8_t b, uint16x8_t c) noexcept { return vbslq_u16(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_vbslq(uint16x8_t a, int16x8_t b, int16x8_t c) noexcept { return vbslq_s16(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vbslq(uint32x4_t a, uint32x4_t b, uint32x4_t c) noexcept { return vbslq_u32(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_vbslq(uint32x4_t a, int32x4_t b, int32x4_t c) noexcept { return vbslq_s32(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vbslq(uint64x2_t a, uint64x2_t b, uint64x2_t c) noexcept { return vbslq_u64(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_vbslq(uint64x2_t a, int64x2_t b, int64x2_t c) noexcept { return vbslq_s64(a, b, c); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_vbslq(uint32x4_t a, float32x4_t b, float32x4_t c) noexcept { return vbslq_f32(a, b, c); }
        }

        template <class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& a, batch<T, A> const& b, requires_arch<neon>) noexcept
        {
            using bool_register_type = typename batch_bool<T, A>::register_type;
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vbslq<map_to_sized_type_t<T>>(bool_register_type(cond), register_type(a), register_type(b));
        }

        template <class A, class T, bool... b, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, b...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<neon>) noexcept
        {
            return select(batch_bool<T, A> { b... }, true_br, false_br, neon {});
        }

        /*************
         * transpose *
         *************/
        template <class A>
        XSIMD_INLINE void transpose(batch<float, A>* matrix_begin, batch<float, A>* matrix_end, requires_arch<neon>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<float, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1], r2 = matrix_begin[2], r3 = matrix_begin[3];
            auto t01 = vtrnq_f32(r0, r1);
            auto t23 = vtrnq_f32(r2, r3);
            matrix_begin[0] = vcombine_f32(vget_low_f32(t01.val[0]), vget_low_f32(t23.val[0]));
            matrix_begin[1] = vcombine_f32(vget_low_f32(t01.val[1]), vget_low_f32(t23.val[1]));
            matrix_begin[2] = vcombine_f32(vget_high_f32(t01.val[0]), vget_high_f32(t23.val[0]));
            matrix_begin[3] = vcombine_f32(vget_high_f32(t01.val[1]), vget_high_f32(t23.val[1]));
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<uint32_t, A>* matrix_begin, batch<uint32_t, A>* matrix_end, requires_arch<neon>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<uint32_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1], r2 = matrix_begin[2], r3 = matrix_begin[3];
            auto t01 = vtrnq_u32(r0, r1);
            auto t23 = vtrnq_u32(r2, r3);
            matrix_begin[0] = vcombine_u32(vget_low_u32(t01.val[0]), vget_low_u32(t23.val[0]));
            matrix_begin[1] = vcombine_u32(vget_low_u32(t01.val[1]), vget_low_u32(t23.val[1]));
            matrix_begin[2] = vcombine_u32(vget_high_u32(t01.val[0]), vget_high_u32(t23.val[0]));
            matrix_begin[3] = vcombine_u32(vget_high_u32(t01.val[1]), vget_high_u32(t23.val[1]));
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int32_t, A>* matrix_begin, batch<int32_t, A>* matrix_end, requires_arch<neon>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<int32_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1], r2 = matrix_begin[2], r3 = matrix_begin[3];
            auto t01 = vtrnq_s32(r0, r1);
            auto t23 = vtrnq_s32(r2, r3);
            matrix_begin[0] = vcombine_s32(vget_low_s32(t01.val[0]), vget_low_s32(t23.val[0]));
            matrix_begin[1] = vcombine_s32(vget_low_s32(t01.val[1]), vget_low_s32(t23.val[1]));
            matrix_begin[2] = vcombine_s32(vget_high_s32(t01.val[0]), vget_high_s32(t23.val[0]));
            matrix_begin[3] = vcombine_s32(vget_high_s32(t01.val[1]), vget_high_s32(t23.val[1]));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE void transpose(batch<T, A>* matrix_begin, batch<T, A>* matrix_end, requires_arch<neon>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<T, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1];
            matrix_begin[0] = vcombine_u64(vget_low_u64(r0), vget_low_u64(r1));
            matrix_begin[1] = vcombine_u64(vget_high_u64(r0), vget_high_u64(r1));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE void transpose(batch<T, A>* matrix_begin, batch<T, A>* matrix_end, requires_arch<neon>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<T, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1];
            matrix_begin[0] = vcombine_s64(vget_low_s64(r0), vget_low_s64(r1));
            matrix_begin[1] = vcombine_s64(vget_high_s64(r0), vget_high_s64(r1));
        }

        /**********
         * zip_lo *
         **********/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint8x8x2_t tmp = vzip_u8(vget_low_u8(lhs), vget_low_u8(rhs));
            return vcombine_u8(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            int8x8x2_t tmp = vzip_s8(vget_low_s8(lhs), vget_low_s8(rhs));
            return vcombine_s8(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint16x4x2_t tmp = vzip_u16(vget_low_u16(lhs), vget_low_u16(rhs));
            return vcombine_u16(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            int16x4x2_t tmp = vzip_s16(vget_low_s16(lhs), vget_low_s16(rhs));
            return vcombine_s16(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint32x2x2_t tmp = vzip_u32(vget_low_u32(lhs), vget_low_u32(rhs));
            return vcombine_u32(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            int32x2x2_t tmp = vzip_s32(vget_low_s32(lhs), vget_low_s32(rhs));
            return vcombine_s32(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vcombine_u64(vget_low_u64(lhs), vget_low_u64(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vcombine_s64(vget_low_s64(lhs), vget_low_s64(rhs));
        }

        template <class A>
        XSIMD_INLINE batch<float, A> zip_lo(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<neon>) noexcept
        {
            float32x2x2_t tmp = vzip_f32(vget_low_f32(lhs), vget_low_f32(rhs));
            return vcombine_f32(tmp.val[0], tmp.val[1]);
        }

        /**********
         * zip_hi *
         **********/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint8x8x2_t tmp = vzip_u8(vget_high_u8(lhs), vget_high_u8(rhs));
            return vcombine_u8(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            int8x8x2_t tmp = vzip_s8(vget_high_s8(lhs), vget_high_s8(rhs));
            return vcombine_s8(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint16x4x2_t tmp = vzip_u16(vget_high_u16(lhs), vget_high_u16(rhs));
            return vcombine_u16(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            int16x4x2_t tmp = vzip_s16(vget_high_s16(lhs), vget_high_s16(rhs));
            return vcombine_s16(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            uint32x2x2_t tmp = vzip_u32(vget_high_u32(lhs), vget_high_u32(rhs));
            return vcombine_u32(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            int32x2x2_t tmp = vzip_s32(vget_high_s32(lhs), vget_high_s32(rhs));
            return vcombine_s32(tmp.val[0], tmp.val[1]);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vcombine_u64(vget_high_u64(lhs), vget_high_u64(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vcombine_s64(vget_high_s64(lhs), vget_high_s64(rhs));
        }

        template <class A>
        XSIMD_INLINE batch<float, A> zip_hi(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<neon>) noexcept
        {
            float32x2x2_t tmp = vzip_f32(vget_high_f32(lhs), vget_high_f32(rhs));
            return vcombine_f32(tmp.val[0], tmp.val[1]);
        }

        /****************
         * extract_pair *
         ****************/

        namespace detail
        {
            template <class A, class T>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const&, batch<T, A> const& /*rhs*/, std::size_t, std::index_sequence<>) noexcept
            {
                assert(false && "extract_pair out of bounds");
                return batch<T, A> {};
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_unsigned_t<T, 1> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_u8(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_signed_t<T, 1> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_s8(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_unsigned_t<T, 2> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_u16(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_signed_t<T, 2> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_s16(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_unsigned_t<T, 4> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_u32(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_signed_t<T, 4> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_s32(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_unsigned_t<T, 8> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_u64(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t I, size_t... Is, detail::enable_sized_signed_t<T, 8> = 0>
            XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_s64(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, size_t I, size_t... Is>
            XSIMD_INLINE batch<float, A> extract_pair(batch<float, A> const& lhs, batch<float, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_f32(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }

            template <class A, class T, size_t... Is>
            XSIMD_INLINE batch<T, A> extract_pair_impl(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<0, Is...>) noexcept
            {
                if (n == 0)
                {
                    return rhs;
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, requires_arch<neon>) noexcept
        {
            constexpr std::size_t size = batch<T, A>::size;
            assert(n < size && "index in bounds");
            return detail::extract_pair_impl(lhs, rhs, n, std::make_index_sequence<size>());
        }

        /******************
         * bitwise_lshift *
         ******************/

        namespace detail
        {
            template <class A, class T>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& /*lhs*/, int /*n*/, ::xsimd::detail::int_sequence<>) noexcept
            {
                assert(false && "bitwise_lshift out of bounds");
                return batch<T, A> {};
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 1> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_u8(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 1> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_s8(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 2> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_u16(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 2> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_s16(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 4> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_u32(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 4> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_s32(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 8> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_u64(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 8> = 0>
            XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshlq_n_s64(lhs, I);
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int... Is>
            XSIMD_INLINE batch<T, A> bitwise_lshift_impl(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<0, Is...>) noexcept
            {
                if (n == 0)
                {
                    return lhs;
                }
                else
                {
                    return bitwise_lshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T>
            XSIMD_INLINE bool shifts_all_positive(batch<T, A> const& b) noexcept
            {
                std::array<T, batch<T, A>::size> tmp = {};
                b.store_unaligned(tmp.begin());
                return std::all_of(tmp.begin(), tmp.end(), [](T x)
                                   { return x >= 0; });
            }
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, int n, requires_arch<neon>) noexcept
        {
            constexpr int size = sizeof(typename batch<T, A>::value_type) * 8;
            assert(0 <= n && n < size && "index in bounds");
            return detail::bitwise_lshift_impl(lhs, n, ::xsimd::detail::make_int_sequence<size>());
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u8(lhs, vreinterpretq_s8_u8(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s8(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u16(lhs, vreinterpretq_s16_u16(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s16(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u32(lhs, vreinterpretq_s32_u32(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s32(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u64(lhs, vreinterpretq_s64_u64(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s64(lhs, rhs);
        }

        // immediate variant
        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_u8(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_s8(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_u16(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_s16(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_u32(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_s32(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_u64(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshlq_n_s64(x, shift);
        }

        /******************
         * bitwise_rshift *
         ******************/

        namespace detail
        {
            template <class A, class T>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& /*lhs*/, int /*n*/, ::xsimd::detail::int_sequence<>) noexcept
            {
                assert(false && "bitwise_rshift out of bounds");
                return batch<T, A> {};
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 1> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_u8(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 1> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_s8(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 2> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_u16(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 2> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_s16(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 4> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_u32(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 4> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_s32(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_unsigned_t<T, 8> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_u64(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int I, int... Is, detail::enable_sized_signed_t<T, 8> = 0>
            XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vshrq_n_s64(lhs, I);
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }

            template <class A, class T, int... Is>
            XSIMD_INLINE batch<T, A> bitwise_rshift_impl(batch<T, A> const& lhs, int n, ::xsimd::detail::int_sequence<0, Is...>) noexcept
            {
                if (n == 0)
                {
                    return lhs;
                }
                else
                {
                    return bitwise_rshift(lhs, n, ::xsimd::detail::int_sequence<Is...>());
                }
            }
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, requires_arch<neon>) noexcept
        {
            constexpr int size = sizeof(typename batch<T, A>::value_type) * 8;
            assert(0 <= n && n < size && "index in bounds");
            return detail::bitwise_rshift_impl(lhs, n, ::xsimd::detail::make_int_sequence<size>());
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u8(lhs, vnegq_s8(vreinterpretq_s8_u8(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s8(lhs, vnegq_s8(rhs));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u16(lhs, vnegq_s16(vreinterpretq_s16_u16(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s16(lhs, vnegq_s16(rhs));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u32(lhs, vnegq_s32(vreinterpretq_s32_u32(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s32(lhs, vnegq_s32(rhs));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon> req) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            using S = std::make_signed_t<T>;
            return vshlq_u64(lhs, neg(batch<S, A>(vreinterpretq_s64_u64(rhs)), req).data);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon>) noexcept
        {
            return vshlq_s64(lhs, neg(rhs, neon {}).data);
        }

        // immediate variant
        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_u8(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_s8(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_u16(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_s16(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_u32(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_s32(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_u64(x, shift);
        }

        template <size_t shift, class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return vshrq_n_s64(x, shift);
        }

        // get
        template <class A, size_t I>
        XSIMD_INLINE float get(batch<float, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_f32(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u8(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s8(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u16(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s16(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u32(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s32(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u64(self, I);
        }

        template <class A, size_t I, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s64(self, I);
        }

        // first
        template <class A>
        XSIMD_INLINE float first(batch<float, A> const& self, requires_arch<neon>) noexcept
        {
            return vgetq_lane_f32(self, 0);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u8(val, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s8(val, 0);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u16(val, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s16(val, 0);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u32(val, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s32(val, 0);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_u64(val, 0);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE T first(batch<T, A> val, requires_arch<neon>) noexcept
        {
            return vgetq_lane_s64(val, 0);
        }

        // Overloads of bitwise shifts accepting two batches of uint64/int64 are not available with ARMv7

        /*******
         * all *
         *******/

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            uint64x1_t tmp = vand_u64(vget_low_u64(arg), vget_high_u64(arg));
            return vget_lane_u64(tmp, 0) == ~0ULL;
        }

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return all(batch_bool<uint64_t, A>(vreinterpretq_u64_u8(arg)), neon {});
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return all(batch_bool<uint64_t, A>(vreinterpretq_u64_u16(arg)), neon {});
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return all(batch_bool<uint64_t, A>(vreinterpretq_u64_u32(arg)), neon {});
        }

        /*******
         * any *
         *******/

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            uint32x2_t tmp = vqmovn_u64(arg);
            return vget_lane_u64(vreinterpret_u64_u32(tmp), 0) != 0;
        }

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return any(batch_bool<uint64_t, A>(vreinterpretq_u64_u8(arg)), neon {});
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return any(batch_bool<uint64_t, A>(vreinterpretq_u64_u16(arg)), neon {});
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon>) noexcept
        {
            return any(batch_bool<uint64_t, A>(vreinterpretq_u64_u32(arg)), neon {});
        }

        /*********
         * isnan *
         *********/

        template <class A>
        XSIMD_INLINE batch_bool<float, A> isnan(batch<float, A> const& arg, requires_arch<neon>) noexcept
        {
            return !(arg == arg);
        }

        // slide_left
        namespace detail
        {
            template <size_t N>
            struct slider_left
            {
                template <class A, class T>
                XSIMD_INLINE batch<T, A> operator()(batch<T, A> const& x, requires_arch<neon>) noexcept
                {
                    const auto left = vdupq_n_u8(0);
                    const auto right = bitwise_cast<uint8_t>(x).data;
                    const batch<uint8_t, A> res(vextq_u8(left, right, 16 - N));
                    return bitwise_cast<T>(res);
                }
            };

            template <>
            struct slider_left<0>
            {
                template <class A, class T>
                XSIMD_INLINE batch<T, A> operator()(batch<T, A> const& x, requires_arch<neon>) noexcept
                {
                    return x;
                }
            };
        } // namespace detail

        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return detail::slider_left<N> {}(x, A {});
        }

        // slide_right
        namespace detail
        {
            template <size_t N>
            struct slider_right
            {
                template <class A, class T>
                XSIMD_INLINE batch<T, A> operator()(batch<T, A> const& x, requires_arch<neon>) noexcept
                {
                    const auto left = bitwise_cast<uint8_t>(x).data;
                    const auto right = vdupq_n_u8(0);
                    const batch<uint8_t, A> res(vextq_u8(left, right, N));
                    return bitwise_cast<T>(res);
                }
            };

            template <>
            struct slider_right<16>
            {
                template <class A, class T>
                XSIMD_INLINE batch<T, A> operator()(batch<T, A> const&, requires_arch<neon>) noexcept
                {
                    return batch<T, A> {};
                }
            };
        } // namespace detail

        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return detail::slider_right<N> {}(x, A {});
        }

        /****************
         * rotate_left *
         ****************/
        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8x16_t x_rotate_left(uint8x16_t a, uint8x16_t b) noexcept { return vextq_u8(a, b, N); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8x16_t x_rotate_left(int8x16_t a, int8x16_t b) noexcept { return vextq_s8(a, b, N); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16x8_t x_rotate_left(uint16x8_t a, uint16x8_t b) noexcept { return vextq_u16(a, b, N % 8); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16x8_t x_rotate_left(int16x8_t a, int16x8_t b) noexcept { return vextq_s16(a, b, N % 8); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32x4_t x_rotate_left(uint32x4_t a, uint32x4_t b) noexcept { return vextq_u32(a, b, N % 4); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32x4_t x_rotate_left(int32x4_t a, int32x4_t b) noexcept { return vextq_s32(a, b, N % 4); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64x2_t x_rotate_left(uint64x2_t a, uint64x2_t b) noexcept { return vextq_u64(a, b, N % 2); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64x2_t x_rotate_left(int64x2_t a, int64x2_t b) noexcept { return vextq_s64(a, b, N % 2); }
            template <size_t N, class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float32x4_t x_rotate_left(float32x4_t a, float32x4_t b) noexcept { return vextq_f32(a, b, N % 4); }
        }

        template <size_t N, class A, class T, detail::enable_neon_type_t<T> = 0>
        XSIMD_INLINE batch<T, A> rotate_left(batch<T, A> const& a, requires_arch<neon>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_rotate_left<N, map_to_sized_type_t<T>>(register_type(a), register_type(a));
        }
    }

    template <typename T, class A, T... Values>
    struct batch_constant;

    namespace kernel
    {
        /***********
         * swizzle *
         ***********/

        template <class A, class T, class I, I... idx>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self,
                                         batch_constant<I, A, idx...>,
                                         requires_arch<neon>) noexcept
        {
            static_assert(batch<T, A>::size == sizeof...(idx), "valid swizzle indices");
            // std::array is only aligned to alignof(T), while store_aligned requires
            // the full batch alignment
            alignas(A::alignment()) std::array<T, batch<T, A>::size> data;
            self.store_aligned(data.data());
            return set(batch<T, A>(), A(), data[idx]...);
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self,
                                                batch_constant<uint64_t, A, V0, V1>,
                                                requires_arch<neon>) noexcept
        {
            if constexpr (V0 == 0 && V1 == 0)
            {
                auto lo = vget_low_u64(self);
                return vcombine_u64(lo, lo);
            }
            if constexpr (V0 == 1 && V1 == 1)
            {
                auto hi = vget_high_u64(self);
                return vcombine_u64(hi, hi);
            }
            if constexpr (V0 == 0 && V1 == 1)
            {
                return self;
            }
            else
            {
                return vextq_u64(self, self, 1);
            }
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self,
                                               batch_constant<int64_t, A, V0, V1> mask,
                                               requires_arch<neon>) noexcept
        {
            return vreinterpretq_s64_u64(swizzle(vreinterpretq_u64_s64(self), mask, A {}));
        }

        namespace detail
        {
            template <uint32_t Va, uint32_t Vb>
            XSIMD_INLINE uint8x8_t make_mask()
            {
                uint8x8_t res = {
                    static_cast<uint8_t>((Va % 2) * 4 + 0),
                    static_cast<uint8_t>((Va % 2) * 4 + 1),
                    static_cast<uint8_t>((Va % 2) * 4 + 2),
                    static_cast<uint8_t>((Va % 2) * 4 + 3),
                    static_cast<uint8_t>((Vb % 2) * 4 + 0),
                    static_cast<uint8_t>((Vb % 2) * 4 + 1),
                    static_cast<uint8_t>((Vb % 2) * 4 + 2),
                    static_cast<uint8_t>((Vb % 2) * 4 + 3),
                };
                return res;
            }
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self,
                                                batch_constant<uint32_t, A, V0, V1, V2, V3> mask,
                                                requires_arch<neon>) noexcept
        {
            constexpr bool is_identity = detail::is_identity(mask);
            constexpr bool is_dup_lo = detail::is_dup_lo(mask);
            constexpr bool is_dup_hi = detail::is_dup_hi(mask);

            if constexpr (is_identity)
            {
                return self;
            }
            if constexpr (is_dup_lo)
            {
                if constexpr (V0 == 0 && V1 == 1)
                {
                    return vreinterpretq_u32_u64(vdupq_lane_u64(vget_low_u64(vreinterpretq_u64_u32(self)), 0));
                }
                if constexpr (V0 == 1 && V1 == 0)
                {
                    return vreinterpretq_u32_u64(vdupq_lane_u64(vreinterpret_u64_u32(vrev64_u32(vget_low_u32(self))), 0));
                }
                return vdupq_n_u32(vgetq_lane_u32(self, V0));
            }
            if constexpr (is_dup_hi)
            {
                if constexpr (V0 == 2 && V1 == 3)
                {
                    return vreinterpretq_u32_u64(vdupq_lane_u64(vget_high_u64(vreinterpretq_u64_u32(self)), 0));
                }
                if constexpr (V0 == 3 && V1 == 2)
                {
                    return vreinterpretq_u32_u64(vdupq_lane_u64(vreinterpret_u64_u32(vrev64_u32(vget_high_u32(self))), 0));
                }
                return vdupq_n_u32(vgetq_lane_u32(self, V0));
            }
            if constexpr (V0 < 2 && V1 < 2 && V2 < 2 && V3 < 2)
            {
                uint8x8_t low = vreinterpret_u8_u64(vget_low_u64(vreinterpretq_u64_u32(self)));
                uint8x8_t mask_lo = detail::make_mask<V0, V1>();
                uint8x8_t mask_hi = detail::make_mask<V2, V3>();
                uint8x8_t lo = vtbl1_u8(low, mask_lo);
                uint8x8_t hi = vtbl1_u8(low, mask_hi);
                return vreinterpretq_u32_u8(vcombine_u8(lo, hi));
            }
            if constexpr (V0 >= 2 && V1 >= 2 && V2 >= 2 && V3 >= 2)
            {
                uint8x8_t high = vreinterpret_u8_u64(vget_high_u64(vreinterpretq_u64_u32(self)));
                uint8x8_t mask_lo = detail::make_mask<V0, V1>();
                uint8x8_t mask_hi = detail::make_mask<V2, V3>();
                uint8x8_t lo = vtbl1_u8(high, mask_lo);
                uint8x8_t hi = vtbl1_u8(high, mask_hi);
                return vreinterpretq_u32_u8(vcombine_u8(lo, hi));
            }

            uint8x8_t mask_lo = detail::make_mask<V0, V1>();
            uint8x8_t mask_hi = detail::make_mask<V2, V3>();

            uint8x8_t low = vreinterpret_u8_u64(vget_low_u64(vreinterpretq_u64_u32(self)));
            uint8x8_t lol = vtbl1_u8(low, mask_lo);
            uint8x8_t loh = vtbl1_u8(low, mask_hi);
            uint32x4_t true_br = vreinterpretq_u32_u8(vcombine_u8(lol, loh));

            uint8x8_t high = vreinterpret_u8_u64(vget_high_u64(vreinterpretq_u64_u32(self)));
            uint8x8_t hil = vtbl1_u8(high, mask_lo);
            uint8x8_t hih = vtbl1_u8(high, mask_hi);
            uint32x4_t false_br = vreinterpretq_u32_u8(vcombine_u8(hil, hih));

            batch_bool_constant<uint32_t, A, (V0 < 2), (V1 < 2), (V2 < 2), (V3 < 2)> blend_mask;
            return select(blend_mask, batch<uint32_t, A>(true_br), batch<uint32_t, A>(false_br), A {});
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self,
                                               batch_constant<int32_t, A, V0, V1, V2, V3> mask,
                                               requires_arch<neon>) noexcept
        {
            return vreinterpretq_s32_u32(swizzle(vreinterpretq_u32_s32(self), mask, A {}));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self,
                                             batch_constant<uint32_t, A, V0, V1, V2, V3> mask,
                                             requires_arch<neon>) noexcept
        {
            return vreinterpretq_f32_u32(swizzle(batch<uint32_t, A>(vreinterpretq_u32_f32(self)), mask, A {}));
        }

        /*********
         * widen *
         *********/
        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return { batch<widen_t<T>, A>(vaddl_s8(vget_low_s8(x), vdup_n_s8(0))), batch<widen_t<T>, A>(vaddl_s8(vget_high_s8(x), vdup_n_s8(0))) };
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return { batch<widen_t<T>, A>(vaddl_u8(vget_low_u8(x), vdup_n_u8(0))), batch<widen_t<T>, A>(vaddl_u8(vget_high_u8(x), vdup_n_u8(0))) };
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return { batch<widen_t<T>, A>(vaddl_s16(vget_low_s16(x), vdup_n_s16(0))), batch<widen_t<T>, A>(vaddl_s16(vget_high_s16(x), vdup_n_s16(0))) };
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return { batch<widen_t<T>, A>(vaddl_u16(vget_low_u16(x), vdup_n_u16(0))), batch<widen_t<T>, A>(vaddl_u16(vget_high_u16(x), vdup_n_u16(0))) };
        }
        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return { batch<widen_t<T>, A>(vaddl_s32(vget_low_s32(x), vdup_n_s32(0))), batch<widen_t<T>, A>(vaddl_s32(vget_high_s32(x), vdup_n_s32(0))) };
        }
        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<neon>) noexcept
        {
            return { batch<widen_t<T>, A>(vaddl_u32(vget_low_u32(x), vdup_n_u32(0))), batch<widen_t<T>, A>(vaddl_u32(vget_high_u32(x), vdup_n_u32(0))) };
        }

        /********
         * mask *
         ********/
        namespace detail
        {
#ifdef XSIMD_LITTLE_ENDIAN
            static constexpr bool do_swap = false;
#else
            static constexpr bool do_swap = true;
#endif
        }

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            // From https://github.com/DLTcollab/sse2neon/blob/master/sse2neon.h
            uint8x16_t msbs = vshrq_n_u8(self, 7);
            if constexpr (detail::do_swap)
            {
                msbs = vrev64q_u8(msbs);
            }

            uint64x2_t bits = vreinterpretq_u64_u8(msbs);
            bits = vsraq_n_u64(bits, bits, 7);
            bits = vsraq_n_u64(bits, bits, 14);
            bits = vsraq_n_u64(bits, bits, 28);

            uint8x16_t output = vreinterpretq_u8_u64(bits);
            constexpr int offset = detail::do_swap ? 7 : 0;

            return vgetq_lane_u8(output, offset) | vgetq_lane_u8(output, offset + 8) << 8;
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            // Adapted from https://github.com/DLTcollab/sse2neon/blob/master/sse2neon.h
            uint16x8_t msbs = vshrq_n_u16(self, 15);
            if constexpr (detail::do_swap)
            {
                msbs = vrev64q_u16(msbs);
            }

            uint64x2_t bits = vreinterpretq_u64_u16(msbs);
            bits = vsraq_n_u64(bits, bits, 15);
            bits = vsraq_n_u64(bits, bits, 30);

            uint8x16_t output = vreinterpretq_u8_u64(bits);
            constexpr int offset = detail::do_swap ? 7 : 0;

            return vgetq_lane_u8(output, offset) | vgetq_lane_u8(output, offset + 8) << 4;
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            // Adapted from https://github.com/DLTcollab/sse2neon/blob/master/sse2neon.h
            uint32x4_t msbs = vshrq_n_u32(self, 31);
            if constexpr (detail::do_swap)
            {
                msbs = vrev64q_u32(msbs);
            }

            uint64x2_t bits = vreinterpretq_u64_u32(msbs);
            bits = vsraq_n_u64(bits, bits, 31);

            uint8x16_t output = vreinterpretq_u8_u64(bits);
            constexpr int offset = detail::do_swap ? 7 : 0;

            return vgetq_lane_u8(output, offset) | vgetq_lane_u8(output, offset + 8) << 2;
        }

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            uint64_t mask_lo = vgetq_lane_u64(self, 0);
            uint64_t mask_hi = vgetq_lane_u64(self, 1);
            return ((mask_lo >> 63) | (mask_hi << 1)) & 0x3;
        }

        /*********
         * count *
         *********/

        // NOTE: Extracting a u32 for the return value saves two instructions on 32-bit ARM:
        // <https://godbolt.org/z/PYn4na8sY>.

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            uint8x16_t msbs = vshrq_n_u8(self, 7);
            uint64x2_t psum = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(msbs)));
            uint64x1_t total = vadd_u64(vget_low_u64(psum), vget_high_u64(psum));

            assert(vget_lane_u64(total, 0) <= std::numeric_limits<uint32_t>::max());
            return vget_lane_u32(vreinterpret_u32_u64(total), 0);
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            uint16x8_t msbs = vshrq_n_u16(self, 15);
            uint64x2_t psum = vpaddlq_u32(vpaddlq_u16(msbs));
            uint64x1_t total = vadd_u64(vget_low_u64(psum), vget_high_u64(psum));

            assert(vget_lane_u64(total, 0) <= std::numeric_limits<uint32_t>::max());
            return vget_lane_u32(vreinterpret_u32_u64(total), 0);
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            uint32x4_t msbs = vshrq_n_u32(self, 31);
            uint64x2_t psum = vpaddlq_u32(msbs);
            uint64x1_t total = vadd_u64(vget_low_u64(psum), vget_high_u64(psum));

            assert(vget_lane_u64(total, 0) <= std::numeric_limits<uint32_t>::max());
            return vget_lane_u32(vreinterpret_u32_u64(total), 0);
        }

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon>) noexcept
        {
            uint64x2_t msbs = vshrq_n_u64(self, 63);
            uint64x1_t total = vadd_u64(vget_low_u64(msbs), vget_high_u64(msbs));

            assert(vget_lane_u64(total, 0) <= std::numeric_limits<uint32_t>::max());
            return vget_lane_u32(vreinterpret_u32_u64(total), 0);
        }

#define WRAP_MASK_OP(OP)                                                               \
    template <class A, class T, detail::enable_sized_t<T, 1> = 0>                      \
    XSIMD_INLINE size_t OP(batch_bool<T, A> const& self, requires_arch<neon>) noexcept \
    {                                                                                  \
        uint8x16_t inner = self;                                                       \
        if constexpr (detail::do_swap)                                                 \
        {                                                                              \
            inner = vrev16q_u8(inner);                                                 \
        }                                                                              \
                                                                                       \
        uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(inner), 4);              \
        if constexpr (detail::do_swap)                                                 \
        {                                                                              \
            narrowed = vrev64_u8(narrowed);                                            \
        }                                                                              \
                                                                                       \
        uint64_t result = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);             \
        return xsimd::detail::OP(result) / 4;                                          \
    }                                                                                  \
    template <class A, class T, detail::enable_sized_t<T, 2> = 0>                      \
    XSIMD_INLINE size_t OP(batch_bool<T, A> const& self, requires_arch<neon>) noexcept \
    {                                                                                  \
        uint8x8_t narrowed = vmovn_u16(self);                                          \
        if constexpr (detail::do_swap)                                                 \
        {                                                                              \
            narrowed = vrev64_u8(narrowed);                                            \
        }                                                                              \
                                                                                       \
        uint64_t result = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);             \
        return xsimd::detail::OP(result) / 8;                                          \
    }                                                                                  \
    template <class A, class T, detail::enable_sized_t<T, 4> = 0>                      \
    XSIMD_INLINE size_t OP(batch_bool<T, A> const& self, requires_arch<neon>) noexcept \
    {                                                                                  \
        uint16x4_t narrowed = vmovn_u32(self);                                         \
        if constexpr (detail::do_swap)                                                 \
        {                                                                              \
            narrowed = vrev64_u16(narrowed);                                           \
        }                                                                              \
                                                                                       \
        uint64_t result = vget_lane_u64(vreinterpret_u64_u16(narrowed), 0);            \
        return xsimd::detail::OP(result) / 16;                                         \
    }                                                                                  \
    template <class A, class T, detail::enable_sized_t<T, 8> = 0>                      \
    XSIMD_INLINE size_t OP(batch_bool<T, A> const& self, requires_arch<neon>) noexcept \
    {                                                                                  \
        uint32x2_t narrowed = vmovn_u64(self);                                         \
        if constexpr (detail::do_swap)                                                 \
        {                                                                              \
            narrowed = vrev64_u32(narrowed);                                           \
        }                                                                              \
                                                                                       \
        uint64_t result = vget_lane_u64(vreinterpret_u64_u32(narrowed), 0);            \
        return xsimd::detail::OP(result) / 32;                                         \
    }

        WRAP_MASK_OP(countl_zero)
        WRAP_MASK_OP(countl_one)
        WRAP_MASK_OP(countr_zero)
        WRAP_MASK_OP(countr_one)

#undef WRAP_MASK_OP
    }

}

#endif
