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

#ifndef XSIMD_COMMON_FWD_HPP
#define XSIMD_COMMON_FWD_HPP

#include "../config/xsimd_macros.hpp"
#include "../types/xsimd_common_arch.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace xsimd
{
    // Minimal forward declarations used in this header
    template <class T, class A>
    class batch;
    template <class T, class A>
    class batch_bool;
    template <class T, class A, T... Vs>
    struct batch_constant;
    template <class T, class A, bool... Vs>
    struct batch_bool_constant;
    template <class T>
    struct convert;
    template <class A>
    struct requires_arch;
    struct aligned_mode;
    struct unaligned_mode;

    namespace types
    {
        template <typename T, class A>
        struct has_simd_register;

        template <typename T, class A>
        inline constexpr bool has_simd_register_v = has_simd_register<T, A>::value;
    }

    namespace kernel
    {
        // forward declaration
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> mul_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE std::pair<batch<T, A>, batch<T, A>>
        mul_hilo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T reduce_mul(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T, class STy>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, STy other, requires_arch<common>) noexcept;
        template <size_t count, class A, class T>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T, class STy>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, STy other, requires_arch<common>) noexcept;
        template <size_t count, class A, class T>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, requires_arch<common>) noexcept;
        template <class A, class T>
        XSIMD_INLINE batch<T, A> load(T const* mem, aligned_mode, requires_arch<A>) noexcept;
        template <class A, class T>
        XSIMD_INLINE batch<T, A> load(T const* mem, unaligned_mode, requires_arch<A>) noexcept;
        template <class A, class T_in, class T_out, bool... Values, class alignment>
        XSIMD_INLINE batch<T_out, A> load_masked(T_in const* mem, batch_bool_constant<T_out, A, Values...> mask, convert<T_out>, alignment, requires_arch<common>) noexcept;
        template <class A, class T, class Mode>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<common>) noexcept;
        template <class A, class T_in, class T_out, bool... Values, class alignment>
        XSIMD_INLINE void store_masked(T_out* mem, batch<T_in, A> const& src, batch_bool_constant<T_in, A, Values...> mask, alignment, requires_arch<common>) noexcept;
        template <class A, class T, class Mode>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<common>) noexcept;

        // Forward declarations for pack-level helpers
        namespace detail
        {
            template <class T>
            XSIMD_INLINE void reassociation_barrier(T& x, const char*) noexcept;
            template <class T, class A>
            XSIMD_INLINE void reassociation_barrier(batch<T, A>& b, const char* reason) noexcept;

            template <typename T, T... Vs>
            XSIMD_INLINE constexpr bool is_identity() noexcept;
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_identity(batch_constant<T, A, Vs...>) noexcept;
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_dup_lo(batch_constant<T, A, Vs...>) noexcept;
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_dup_hi(batch_constant<T, A, Vs...>) noexcept;
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_cross_lane(batch_constant<T, A, Vs...>) noexcept;
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_only_from_lo(batch_constant<T, A, Vs...>) noexcept;
            template <typename T, class A, T... Vs>
            XSIMD_INLINE constexpr bool is_only_from_hi(batch_constant<T, A, Vs...>) noexcept;

            template <class A, class WMul>
            XSIMD_INLINE batch<uint64_t, A> mulhi_u64_core(batch<uint64_t, A> const& x,
                                                           batch<uint64_t, A> const& y,
                                                           WMul mul_epu32) noexcept;
            template <class A, class WMul>
            XSIMD_INLINE batch<int64_t, A> mulhi_i64_core(batch<int64_t, A> const& x,
                                                          batch<int64_t, A> const& y,
                                                          WMul mul_epu32) noexcept;
            template <class A, class WMul>
            XSIMD_INLINE std::pair<batch<uint64_t, A>, batch<uint64_t, A>>
            mulhilo_u64_core(batch<uint64_t, A> const& x,
                             batch<uint64_t, A> const& y,
                             WMul mul_epu32) noexcept;
        }
    }
}

#endif
