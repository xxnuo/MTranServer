/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 * Copyright (c) Yibo Cai                                                   *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_RVV_REGISTER_HPP
#define XSIMD_RVV_REGISTER_HPP

#include "../utils/xsimd_type_traits.hpp"
#include "./xsimd_common_arch.hpp"
#include "./xsimd_register.hpp"

#if XSIMD_WITH_RVV
#include <riscv_vector.h>
#endif

namespace xsimd
{
    namespace detail
    {
        /**
         * @ingroup architectures
         *
         * RVV instructions (fixed vector size) for riscv
         */
        template <size_t Width>
        struct rvv : xsimd::common
        {
            static constexpr size_t width = Width;
            static constexpr bool supported() noexcept { return Width == XSIMD_RVV_BITS; }
            static constexpr bool available() noexcept { return true; }
            static constexpr bool requires_alignment() noexcept { return true; }
            static constexpr std::size_t alignment() noexcept { return 16; }
            static constexpr char const* name() noexcept { return "riscv+rvv"; }
        };
    }

#if XSIMD_WITH_RVV

    using rvv = detail::rvv<__riscv_v_fixed_vlen>;

#define XSIMD_RVV_JOINT_(a, b, c) a##b##c
#define XSIMD_RVV_JOINT(a, b, c) XSIMD_RVV_JOINT_(a, b, c)
#define XSIMD_RVV_JOINT5(a, b, c, d, e) XSIMD_RVV_JOINT(XSIMD_RVV_JOINT(a, b, c), d, e)

#define XSIMD_RVV_TYPE_i(S, V) XSIMD_RVV_JOINT5(vint, S, m, V, _t)
#define XSIMD_RVV_TYPE_u(S, V) XSIMD_RVV_JOINT5(vuint, S, m, V, _t)
#define XSIMD_RVV_TYPE_f(S, V) XSIMD_RVV_JOINT5(vfloat, S, m, V, _t)
#define XSIMD_RVV_TYPE(T, S, V) XSIMD_RVV_JOINT(XSIMD_RVV_TYPE, _, T)(S, V)

    namespace types
    {
        namespace detail
        {
            static constexpr size_t rvv_width_mf8 = XSIMD_RVV_BITS / 8;
            static constexpr size_t rvv_width_mf4 = XSIMD_RVV_BITS / 4;
            static constexpr size_t rvv_width_mf2 = XSIMD_RVV_BITS / 2;
            static constexpr size_t rvv_width_m1 = XSIMD_RVV_BITS;

            // Cope with gcc limitation, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=116484
#define XSIMD_RVV_WIDTH_MF8 (XSIMD_RVV_BITS / 8)
#define XSIMD_RVV_WIDTH_MF4 (XSIMD_RVV_BITS / 4)
#define XSIMD_RVV_WIDTH_MF2 (XSIMD_RVV_BITS / 2)
#define XSIMD_RVV_WIDTH_M1 XSIMD_RVV_BITS

            // rvv_type_info is a utility class to convert scalar type and
            // bitwidth into rvv register types.
            //
            // * `type` is the unadorned vector type.
            // * `fixed_type` is the same type, but with the storage attribute
            //    applied.
            // * `byte_type` is the type which is the same size in unsigned
            //    bytes, used as an intermediate step for bit-cast operations,
            //    because only a subset of __riscv_vreinterpret() intrinsics
            //    exist -- but always enough to get us to bytes and back.
            //
            template <class T, size_t Width>
            struct rvv_type_info;
#define XSIMD_RVV_MAKE_TYPE(scalar, t, s, vmul)                                                              \
    template <>                                                                                              \
    struct rvv_type_info<scalar, rvv_width_m1 * vmul>                                                        \
    {                                                                                                        \
        static constexpr size_t width = rvv_width_m1 * vmul;                                                 \
        using type = XSIMD_RVV_TYPE(t, s, vmul);                                                             \
        using byte_type = XSIMD_RVV_TYPE(u, 8, vmul);                                                        \
        using fixed_type = type __attribute__((riscv_rvv_vector_bits(/*width=*/XSIMD_RVV_WIDTH_M1 * vmul))); \
        template <class U>                                                                                   \
        static XSIMD_INLINE type bitcast(U x) noexcept                                                       \
        {                                                                                                    \
            const auto words = XSIMD_RVV_JOINT5(__riscv_vreinterpret_, u, s, m, vmul)(x);                    \
            return XSIMD_RVV_JOINT5(__riscv_vreinterpret_, t, s, m, vmul)(words);                            \
        }                                                                                                    \
        template <class U>                                                                                   \
        static XSIMD_INLINE byte_type as_bytes(U x) noexcept                                                 \
        {                                                                                                    \
            static_assert(std::is_same_v<U, type>, "inconsistent conversion types");                         \
            const auto words = XSIMD_RVV_JOINT5(__riscv_vreinterpret_, u, s, m, vmul)(x);                    \
            return XSIMD_RVV_JOINT5(__riscv_vreinterpret_, u, 8, m, vmul)(words);                            \
        }                                                                                                    \
    };                                                                                                       \
    template <>                                                                                              \
    XSIMD_INLINE XSIMD_RVV_TYPE(t, s, vmul) rvv_type_info<scalar, rvv_width_m1 * vmul>::bitcast<XSIMD_RVV_TYPE(t, s, vmul)>(XSIMD_RVV_TYPE(t, s, vmul) x) noexcept { return x; }

#define XSIMD_RVV_MAKE_TYPES(vmul)             \
    XSIMD_RVV_MAKE_TYPE(int8_t, i, 8, vmul)    \
    XSIMD_RVV_MAKE_TYPE(uint8_t, u, 8, vmul)   \
    XSIMD_RVV_MAKE_TYPE(int16_t, i, 16, vmul)  \
    XSIMD_RVV_MAKE_TYPE(uint16_t, u, 16, vmul) \
    XSIMD_RVV_MAKE_TYPE(int32_t, i, 32, vmul)  \
    XSIMD_RVV_MAKE_TYPE(uint32_t, u, 32, vmul) \
    XSIMD_RVV_MAKE_TYPE(int64_t, i, 64, vmul)  \
    XSIMD_RVV_MAKE_TYPE(uint64_t, u, 64, vmul) \
    XSIMD_RVV_MAKE_TYPE(float, f, 32, vmul)    \
    XSIMD_RVV_MAKE_TYPE(double, f, 64, vmul)

            XSIMD_RVV_MAKE_TYPES(8)
            XSIMD_RVV_MAKE_TYPES(4)
            XSIMD_RVV_MAKE_TYPES(2)
            XSIMD_RVV_MAKE_TYPES(1)
#undef XSIMD_RVV_TYPE
#undef XSIMD_RVV_TYPE_f
#undef XSIMD_RVV_TYPE_u
#undef XSIMD_RVV_TYPE_i
#undef XSIMD_RVV_MAKE_TYPES
#undef XSIMD_RVV_MAKE_TYPE

            // Specialization needed for #1058
            template <>
            XSIMD_INLINE rvv_type_info<int8_t, rvv_width_m1 * 8>::type
            rvv_type_info<int8_t, rvv_width_m1 * 8>::bitcast<__rvv_uint8m8_t>(
                __rvv_uint8m8_t x) noexcept
            {
                return __riscv_vreinterpret_i8m8(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<int8_t, rvv_width_m1 * 1>::type
            rvv_type_info<int8_t, rvv_width_m1 * 1>::bitcast<__rvv_uint8m1_t>(
                __rvv_uint8m1_t x) noexcept
            {
                return __riscv_vreinterpret_i8m1(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<uint16_t, rvv_width_m1 * 1>::type
            rvv_type_info<uint16_t, rvv_width_m1 * 1>::bitcast<__rvv_uint8m1_t>(
                __rvv_uint8m1_t x) noexcept
            {
                return __riscv_vreinterpret_u16m1(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<uint32_t, rvv_width_m1 * 1>::type
            rvv_type_info<uint32_t, rvv_width_m1 * 1>::bitcast<__rvv_uint8m1_t>(
                __rvv_uint8m1_t x) noexcept
            {
                return __riscv_vreinterpret_u32m1(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<uint64_t, rvv_width_m1 * 1>::type
            rvv_type_info<uint64_t, rvv_width_m1 * 1>::bitcast<__rvv_uint8m1_t>(
                __rvv_uint8m1_t x) noexcept
            {
                return __riscv_vreinterpret_u64m1(x);
            }

            //

            template <>
            XSIMD_INLINE rvv_type_info<int8_t, rvv_width_m1 * 8>::byte_type
            rvv_type_info<int8_t, rvv_width_m1 * 8>::as_bytes<__rvv_int8m8_t>(__rvv_int8m8_t x) noexcept
            {
                return __riscv_vreinterpret_u8m8(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<int8_t, rvv_width_m1 * 1>::byte_type
            rvv_type_info<int8_t, rvv_width_m1 * 1>::as_bytes<__rvv_int8m1_t>(__rvv_int8m1_t x) noexcept
            {
                return __riscv_vreinterpret_u8m1(x);
            }

            template <>
            XSIMD_INLINE rvv_type_info<uint8_t, rvv_width_m1 * 1>::byte_type
            rvv_type_info<uint8_t, rvv_width_m1 * 1>::as_bytes<__rvv_uint8m1_t>(__rvv_uint8m1_t x) noexcept
            {
                return x;
            }
            template <>
            XSIMD_INLINE rvv_type_info<uint16_t, rvv_width_m1 * 1>::byte_type
            rvv_type_info<uint16_t, rvv_width_m1 * 1>::as_bytes<__rvv_uint16m1_t>(__rvv_uint16m1_t x) noexcept
            {
                return __riscv_vreinterpret_u8m1(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<uint32_t, rvv_width_m1 * 1>::byte_type
            rvv_type_info<uint32_t, rvv_width_m1 * 1>::as_bytes<__rvv_uint32m1_t>(__rvv_uint32m1_t x) noexcept
            {
                return __riscv_vreinterpret_u8m1(x);
            }
            template <>
            XSIMD_INLINE rvv_type_info<uint64_t, rvv_width_m1 * 1>::byte_type
            rvv_type_info<uint64_t, rvv_width_m1 * 1>::as_bytes<__rvv_uint64m1_t>(__rvv_uint64m1_t x) noexcept
            {
                return __riscv_vreinterpret_u8m1(x);
            }

            // rvv_blob is storage-type abstraction for a vector register.
            template <class T, size_t Width>
            struct rvv_blob : public rvv_type_info<T, Width>
            {
                using super = rvv_type_info<T, Width>;
                using typename super::fixed_type;
                using typename super::type;

                fixed_type value;
                type get() const { return value; }
                void set(type v) { value = v; }
            };

            template <size_t div>
            struct semitype;
            template <>
            struct semitype<2>
            {
                using type = vuint8mf2_t __attribute__((riscv_rvv_vector_bits(XSIMD_RVV_WIDTH_MF2)));
            };
            template <>
            struct semitype<4>
            {
                using type = vuint8mf4_t __attribute__((riscv_rvv_vector_bits(XSIMD_RVV_WIDTH_MF4)));
            };
            template <>
            struct semitype<8>
            {
                using type = vuint8mf8_t __attribute__((riscv_rvv_vector_bits(XSIMD_RVV_WIDTH_MF8)));
            };

            //
            // But sometimes we want our storage type to be less than a whole
            // register, while presenting as a whole register to the outside
            // world.  This is because some partial-register types are not
            // defined, but they can (mostly) be emulated using shorter vl on a
            // full-width register for arithmetic, and cast back to a partial
            // byte register for storage.
            //
            template <class T, size_t divisor>
            struct rvv_semiblob : public rvv_type_info<T, rvv_width_m1>
            {
                using super = rvv_type_info<T, rvv_width_m1>;
                static constexpr size_t width = rvv_width_m1 / divisor;
                using typename super::type;
                using fixed_type = typename semitype<divisor>::type;
                using super::as_bytes;
                using super::bitcast;

                fixed_type value;
                vuint8m1_t get_bytes(std::integral_constant<size_t, 2>) const { return __riscv_vlmul_ext_v_u8mf2_u8m1(value); }
                vuint8m1_t get_bytes(std::integral_constant<size_t, 4>) const { return __riscv_vlmul_ext_v_u8mf4_u8m1(value); }
                vuint8m1_t get_bytes(std::integral_constant<size_t, 8>) const { return __riscv_vlmul_ext_v_u8mf8_u8m1(value); }
                type get() const noexcept
                {
                    vuint8m1_t bytes = get_bytes(std::integral_constant<size_t, divisor>());
                    return bitcast(bytes);
                }
                void set_bytes(vuint8m1_t v, std::integral_constant<size_t, 2>) { value = __riscv_vlmul_trunc_v_u8m1_u8mf2(v); }
                void set_bytes(vuint8m1_t v, std::integral_constant<size_t, 4>) { value = __riscv_vlmul_trunc_v_u8m1_u8mf4(v); }
                void set_bytes(vuint8m1_t v, std::integral_constant<size_t, 8>) { value = __riscv_vlmul_trunc_v_u8m1_u8mf8(v); }
                void set(type v)
                {
                    vuint8m1_t bytes = as_bytes(v);
                    set_bytes(bytes, std::integral_constant<size_t, divisor>());
                }
            };
            template <class T>
            struct rvv_blob<T, rvv_width_mf2> : rvv_semiblob<T, 2>
            {
            };
            template <class T>
            struct rvv_blob<T, rvv_width_mf4> : rvv_semiblob<T, 4>
            {
            };
            template <class T>
            struct rvv_blob<T, rvv_width_mf8> : rvv_semiblob<T, 8>
            {
            };

            // An explicit constructor isn't really explicit enough to allow
            // implicit bit-casting operations between incompatible types, so
            // we add this vacuous flag argument when we're serious:
            //
            enum rvv_bitcast_flag
            {
                XSIMD_RVV_BITCAST
            };

            // the general-purpose vector register type, usable within
            // templates, and supporting arithmetic on partial registers for
            // which there is no intrinsic type (by casting via a full register
            // type).
            //
            template <class T, size_t Width>
            struct rvv_reg
            {
                static constexpr size_t width = Width;
                static constexpr size_t vl = Width / (sizeof(T) * 8);
                using blob_type = rvv_blob<T, Width>;
                using register_type = typename blob_type::type;
                using byte_type = typename blob_type::byte_type;
                blob_type value;
                rvv_reg() noexcept = default;
                rvv_reg(register_type x) noexcept { value.set(x); }
                explicit rvv_reg(byte_type v, rvv_bitcast_flag) { value.set(value.bitcast(v)); }
                template <class U>
                explicit rvv_reg(rvv_reg<U, Width> v, rvv_bitcast_flag)
                    : rvv_reg(v.get_bytes(), XSIMD_RVV_BITCAST)
                {
                }
                byte_type get_bytes() const noexcept
                {
                    return blob_type::as_bytes(value.get());
                }
                operator register_type() const noexcept { return value.get(); }
            };
            template <class T, size_t Width = XSIMD_RVV_BITS>
            using rvv_reg_t = std::conditional_t<!std::is_void_v<T>, rvv_reg<map_to_sized_type_t<T>, Width>, void>;

            // And some more of the same stuff for bool types, which have
            // similar problems and similar workarounds.
            //
            template <size_t>
            struct rvv_bool_info;
#define XSIMD_RVV_MAKE_BOOL_TYPE(i)                                                             \
    template <>                                                                                 \
    struct rvv_bool_info<i>                                                                     \
    {                                                                                           \
        using type = XSIMD_RVV_JOINT(vbool, i, _t);                                             \
        template <class T>                                                                      \
        static XSIMD_INLINE type bitcast(T value) noexcept                                      \
        {                                                                                       \
            return XSIMD_RVV_JOINT(__riscv_vreinterpret_b, i, )(value);                         \
        }                                                                                       \
        /*template <> static XSIMD_INLINE type bitcast(type value) noexcept { return value; }*/ \
    };
            XSIMD_RVV_MAKE_BOOL_TYPE(1);
            XSIMD_RVV_MAKE_BOOL_TYPE(2);
            XSIMD_RVV_MAKE_BOOL_TYPE(4);
            XSIMD_RVV_MAKE_BOOL_TYPE(8);
            XSIMD_RVV_MAKE_BOOL_TYPE(16);
            XSIMD_RVV_MAKE_BOOL_TYPE(32);
            XSIMD_RVV_MAKE_BOOL_TYPE(64);
#undef XSIMD_RVV_MAKE_BOOL_TYPE
#undef XSIMD_RVV_JOINT5
#undef XSIMD_RVV_JOINT
#undef XSIMD_RVV_JOINT_

            template <class T, size_t Width>
            struct rvv_bool
            {
                using bool_info = rvv_bool_info<rvv_width_m1 * sizeof(T) * 8 / Width>;
                using storage_type = vuint8m1_t __attribute__((riscv_rvv_vector_bits(XSIMD_RVV_WIDTH_M1)));
                using type = typename bool_info::type;
                storage_type value;
                rvv_bool() = default;
                rvv_bool(type v) noexcept
                    : value(__riscv_vreinterpret_u8m1(v))
                {
                }
                template <class U, std::enable_if_t<sizeof(T) == sizeof(U), int> = 0>
                rvv_bool(rvv_bool<U, Width> v)
                    : value(v.value)
                {
                }
                explicit rvv_bool(uint8_t mask) noexcept
                    : value(__riscv_vmv_v_x_u8m1(mask, rvv_width_m1 / 8))
                {
                }
                explicit rvv_bool(uint64_t mask) noexcept
                    : value(__riscv_vreinterpret_v_u64m1_u8m1(__riscv_vmv_v_x_u64m1(mask, rvv_width_m1 / 64)))
                {
                }
                operator type() const noexcept { return bool_info::bitcast(value); }
                type as_mask() const noexcept { return (type) * this; }
            };

            template <class T, size_t Width = XSIMD_RVV_BITS>
            using rvv_bool_t = std::enable_if_t < !std::is_void_v<T>,
                  rvv_bool<map_to_sized_type_t<T>, Width<rvv_width_m1 ? rvv_width_m1 : Width>>;

            template <size_t S>
            struct rvv_vector_type_impl;

            template <>
            struct rvv_vector_type_impl<8>
            {
                using signed_type = rvv_reg_t<int8_t>;
                using unsigned_type = rvv_reg_t<uint8_t>;
                using floating_point_type = void;
            };

            template <>
            struct rvv_vector_type_impl<16>
            {
                using signed_type = rvv_reg_t<int16_t>;
                using unsigned_type = rvv_reg_t<uint16_t>;
                using floating_point_type = rvv_reg_t<_Float16>;
            };

            template <>
            struct rvv_vector_type_impl<32>
            {
                using signed_type = rvv_reg_t<int32_t>;
                using unsigned_type = rvv_reg_t<uint32_t>;
                using floating_point_type = rvv_reg_t<float>;
            };

            template <>
            struct rvv_vector_type_impl<64>
            {
                using signed_type = rvv_reg_t<int64_t>;
                using unsigned_type = rvv_reg_t<uint64_t>;
                using floating_point_type = rvv_reg_t<double>;
            };

            template <class T>
            using signed_int_rvv_vector_type = typename rvv_vector_type_impl<8 * sizeof(T)>::signed_type;

            template <class T>
            using unsigned_int_rvv_vector_type = typename rvv_vector_type_impl<8 * sizeof(T)>::unsigned_type;

            template <class T>
            using floating_point_rvv_vector_type = typename rvv_vector_type_impl<8 * sizeof(T)>::floating_point_type;

            template <class T>
            using signed_int_or_floating_point_rvv_vector_type = std::conditional_t<std::is_floating_point_v<T>,
                                                                                    floating_point_rvv_vector_type<T>,
                                                                                    signed_int_rvv_vector_type<T>>;

            template <class T>
            using rvv_vector_type = std::conditional_t<std::is_signed_v<T>,
                                                       signed_int_or_floating_point_rvv_vector_type<T>,
                                                       unsigned_int_rvv_vector_type<T>>;
        } // namespace detail

        XSIMD_DECLARE_SIMD_REGISTER(bool, rvv, detail::rvv_vector_type<unsigned char>);
        XSIMD_DECLARE_SIMD_REGISTER(signed char, rvv, detail::rvv_vector_type<signed char>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned char, rvv, detail::rvv_vector_type<unsigned char>);
        XSIMD_DECLARE_SIMD_REGISTER(char, rvv, detail::rvv_vector_type<char>);
        XSIMD_DECLARE_SIMD_REGISTER(short, rvv, detail::rvv_vector_type<short>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned short, rvv, detail::rvv_vector_type<unsigned short>);
        XSIMD_DECLARE_SIMD_REGISTER(int, rvv, detail::rvv_vector_type<int>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned int, rvv, detail::rvv_vector_type<unsigned int>);
        XSIMD_DECLARE_SIMD_REGISTER(long int, rvv, detail::rvv_vector_type<long int>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned long int, rvv, detail::rvv_vector_type<unsigned long int>);
        XSIMD_DECLARE_SIMD_REGISTER(long long int, rvv, detail::rvv_vector_type<long long int>);
        XSIMD_DECLARE_SIMD_REGISTER(unsigned long long int, rvv, detail::rvv_vector_type<unsigned long long int>);
        XSIMD_DECLARE_SIMD_REGISTER(float, rvv, detail::rvv_vector_type<float>);
        XSIMD_DECLARE_SIMD_REGISTER(double, rvv, detail::rvv_vector_type<double>);

        namespace detail
        {
            template <class T>
            struct rvv_bool_simd_register
            {
                using register_type = rvv_bool_t<T>;
                register_type data;
                operator register_type() const noexcept { return data; }
            };
        } // namespace detail

        template <class T>
        struct get_bool_simd_register<T, rvv>
        {
            using type = detail::rvv_bool_simd_register<T>;
        };
    } // namespace types

#undef XSIMD_RVV_WIDTH_MF8
#undef XSIMD_RVV_WIDTH_MF4
#undef XSIMD_RVV_WIDTH_MF2
#undef XSIMD_RVV_WIDTH_M1

#else
    using rvv = detail::rvv<0xFFFFFFFF>;
#endif
} // namespace xsimd

#endif
