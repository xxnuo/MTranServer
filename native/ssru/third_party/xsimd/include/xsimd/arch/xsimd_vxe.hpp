/***************************************************************************
 * Copyright (c) Andreas Krebbel                                            *
 * Based on xsimd_vsx.hpp                                                   *
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_VXE_HPP
#define XSIMD_VXE_HPP

#include "../types/xsimd_vxe_register.hpp"

#include <type_traits>

namespace xsimd
{
    namespace kernel
    {
        using namespace types;
        using v1ti = __int128 __attribute__((vector_size(16)));
        using v4sf = float __attribute__((vector_size(16)));
        using v2df = double __attribute__((vector_size(16)));
        using uv2di = unsigned long long int __attribute__((vector_size(16)));
        using v2di = long long int __attribute__((vector_size(16)));
        using uv4si = unsigned int __attribute__((vector_size(16)));
        using v4si = int __attribute__((vector_size(16)));
        using uv8hi = unsigned short int __attribute__((vector_size(16)));
        using v8hi = short int __attribute__((vector_size(16)));
        using uv16qi = unsigned char __attribute__((vector_size(16)));
        using v16qi = signed char __attribute__((vector_size(16)));

        // builtin_t<T> - the scalar type as it would be used for a vector intrinsic
        // VXE vector intrinsics do not support long, unsigned long, and char
        // The builtin<T> definition can be used to map the incoming
        // type to the right one to be used with the intrinsics.
        template <typename T>
        struct builtin_scalar
        {
            using type = T;
        };

        template <>
        struct builtin_scalar<unsigned long>
        {
            using type = unsigned long long;
        };

        template <>
        struct builtin_scalar<long>
        {
            using type = long long;
        };

        template <>
        struct builtin_scalar<char>
        {
            using type = std::conditional_t<std::is_signed_v<char>, signed char, unsigned char>;
        };

        template <typename T>
        using builtin_t = typename builtin_scalar<T>::type;

        // bitwise_cast
        template <class A, class T_in, class T_out>
        XSIMD_INLINE batch<T_out, A> bitwise_cast(batch<T_in, A> const& self, batch<T_out, A> const&, requires_arch<vxe>) noexcept
        {
            return (typename batch<T_out, A>::register_type)(self.data);
        }

        // batch_bool_cast
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<vxe>) noexcept
        {
            return (typename batch_bool<T_out, A>::register_type)self.data;
        }

        // load

        // load_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* mem, convert<T>, requires_arch<vxe>) noexcept
        {
            return (typename batch<T, A>::register_type)vec_xl(0, (builtin_t<T>*)mem);
        }

        // load_aligned
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> load_aligned(T const* mem, convert<T>, requires_arch<vxe>) noexcept
        {
            return load_unaligned<A>(mem, kernel::convert<T> {}, vxe {});
        }

        // load_complex
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<std::complex<float>, A> load_complex(batch<float, A> const& hi, batch<float, A> const& lo, requires_arch<vxe>) noexcept
            {
                // Interleave real and imaginary parts
                // hi = [r0, i0, r1, i1], lo = [r2, i2, r3, i3]
                // We need: real = [r0, r1, r2, r3], imag = [i0, i1, i2, i3]
                using v4sf = float __attribute__((vector_size(16)));
                uv16qi perm_real = (uv16qi) { 0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27 };
                uv16qi perm_imag = (uv16qi) { 4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31 };
                v4sf real = vec_perm((v4sf)hi.data, (v4sf)lo.data, perm_real);
                v4sf imag = vec_perm((v4sf)hi.data, (v4sf)lo.data, perm_imag);
                return { batch<float, A>(real), batch<float, A>(imag) };
            }

            template <class A>
            XSIMD_INLINE batch<std::complex<double>, A> load_complex(batch<double, A> const& hi, batch<double, A> const& lo, requires_arch<vxe>) noexcept
            {
                // hi = [r0, i0], lo = [r1, i1]
                // We need: real = [r0, r1], imag = [i0, i1]
                using v2df = double __attribute__((vector_size(16)));
                uv16qi perm_real = (uv16qi) { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23 };
                uv16qi perm_imag = (uv16qi) { 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31 };
                v2df real = vec_perm((v2df)hi.data, (v2df)lo.data, perm_real);
                v2df imag = vec_perm((v2df)hi.data, (v2df)lo.data, perm_imag);
                return { batch<double, A>(real), batch<double, A>(imag) };
            }

            template <class A>
            XSIMD_INLINE batch<float, A> complex_low(batch<std::complex<float>, A> const& self, requires_arch<vxe>) noexcept
            {
                uv16qi perm = (uv16qi) { 0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23 };
                return batch<float, A>(vec_perm((v4sf)self.real().data, (v4sf)self.imag().data, perm));
            }

            template <class A>
            XSIMD_INLINE batch<float, A> complex_high(batch<std::complex<float>, A> const& self, requires_arch<vxe>) noexcept
            {
                uv16qi perm = (uv16qi) { 8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31 };
                return batch<float, A>(vec_perm((v4sf)self.real().data, (v4sf)self.imag().data, perm));
            }

            template <class A>
            XSIMD_INLINE batch<double, A> complex_low(batch<std::complex<double>, A> const& self, requires_arch<vxe>) noexcept
            {
                uv16qi perm = (uv16qi) { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23 };
                return batch<double, A>(vec_perm((v2df)self.real().data, (v2df)self.imag().data, perm));
            }

            template <class A>
            XSIMD_INLINE batch<double, A> complex_high(batch<std::complex<double>, A> const& self, requires_arch<vxe>) noexcept
            {
                uv16qi perm = (uv16qi) { 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31 };
                return batch<double, A>(vec_perm((v2df)self.real().data, (v2df)self.imag().data, perm));
            }
        }

        // store
        template <class A, class T>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<vxe>) noexcept
        {
            vec_xst(src.data, 0, (builtin_t<T>*)dst);
        }

        template <class A, class T>
        XSIMD_INLINE void store_unaligned(T* dst, batch<T, A> const& src, requires_arch<vxe>) noexcept
        {
            store_aligned<A>(dst, src, vxe {});
        }

        // set
        template <class A, class T, class... Values>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<vxe>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch<T, A>::size, "consistent init");
            return typename batch<T, A>::register_type { values... };
        }

        template <class A, class T, class... Values, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<vxe>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<T, A>::size, "consistent init");
            return typename batch_bool<T, A>::register_type { static_cast<decltype(std::declval<typename batch_bool<T, A>::register_type>()[0])>(values ? -1LL : 0LL)... };
        }
        // first
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return self.data[0];
        }
        // insert
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<vxe>) noexcept
        {
            // vec_insert on float is broken with clang
            batch<T, A> out(self);
            out.data[I] = val;
            return out;
        }

        // eq
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data == other.data;
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data == other.data;
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data < other.data;
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data <= other.data;
        }

        // neq
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return ~(self.data == other.data);
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return bitwise_xor(self, other);
        }

        // sub
        template <class A, class T>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data - other.data;
        }

        // broadcast
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<vxe>) noexcept
        {
            return vec_splats(static_cast<builtin_t<T>>(val));
        }

        // abs
        template <class A, class T, class = std::enable_if_t<std::is_signed_v<T>, void>>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return vec_abs(self.data);
        }
        // bitwise_and
        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return (typename batch<T, A>::register_type)((v4si)self.data & (v4si)other.data);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data & other.data;
        }

        // bitwise_or
        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return (typename batch<T, A>::register_type)((v4si)self.data | (v4si)other.data);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data | other.data;
        }

        // bitwise_xor
        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return (typename batch<T, A>::register_type)((v4si)self.data ^ (v4si)other.data);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data ^ other.data;
        }

        // bitwise_not
        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            // ~ operator does not work on floating point vectors
            return (typename batch<T, A>::register_type)(~(v4si)self.data);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return ~self.data;
        }

        // bitwise_andnot
        template <class A, class T>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return (typename batch<T, A>::register_type)((v4si)self.data & ~(v4si)other.data);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data & ~other.data;
        }

        // div
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data / other.data;
        }

        // neg
        template <class A, class T>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return (typename batch<T, A>::register_type) { 0 } - self.data;
        }

        // add
        template <class A, class T>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data + other.data;
        }

        // all
        template <class A, class T>
        XSIMD_INLINE bool all(batch_bool<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return ((v1ti)self.data)[0] == -1;
        }

        // any
        template <class A, class T>
        XSIMD_INLINE bool any(batch_bool<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return ((v1ti)self.data)[0] != 0;
        }
        // avgr
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return vec_avg(self.data, other.data);
        }

        // max
        template <class A, class T>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return vec_max(self.data, other.data);
        }
        // min
        template <class A, class T>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return vec_min(self.data, other.data);
        }
        // fma
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> fma(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<vxe>) noexcept
        {
            return vec_madd(x.data, y.data, z.data);
        }
        // fms
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> fms(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<vxe>) noexcept
        {
            return vec_msub(x.data, y.data, z.data);
        }

        // mul
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return self.data * other.data;
        }
        // haddp
        template <class A>
        XSIMD_INLINE batch<float, A> haddp(batch<float, A> const* r, requires_arch<vxe>) noexcept
        {
            v4sf lo01, hi01, lo23, hi23, sum01, sum23, sumeven, sumodd;
            lo01 = vec_mergel(r[0].data, r[1].data); // { r[0][2], r[1][2], r[0][3], r[1][3] }
            hi01 = vec_mergeh(r[0].data, r[1].data); // { r[0][0], r[1][0], r[0][1], r[1][1] }
            lo23 = vec_mergel(r[2].data, r[3].data); // { r[2][2], r[2][2], r[3][3], r[3][3] }
            hi23 = vec_mergeh(r[2].data, r[3].data); // { r[2][0], r[2][0], r[3][1], r[3][1] }
            sum01 = lo01 + hi01; // { r[0][0] + r[0][2], r[1][0] + r[1][2], r[0][1] + r[0][3], r[1][1] + r[1][3] }
            sum23 = lo23 + hi23; // { r[2][0] + r[2][2], r[3][0] + r[3][2], r[2][1] + r[2][3], r[3][1] + r[3][3] }
            sumeven = (v4sf)vec_mergeh((v2di)sum01, (v2di)sum23); // { r[0][0] + r[0][2], r[1][0] + r[1][2], r[2][0] + r[2][2], r[3][0] + r[3][2] }
            sumodd = (v4sf)vec_mergel((v2di)sum01, (v2di)sum23); // { r[0][1] + r[0][3], r[1][1] + r[1][3], r[2][1] + r[2][3], r[3][1] + r[3][3] }
            return sumeven + sumodd;
        }
        template <class A>
        XSIMD_INLINE batch<double, A> haddp(batch<double, A> const* row, requires_arch<vxe>) noexcept
        {
            return vec_mergeh(row[0].data, row[1].data) + vec_mergel(row[0].data, row[1].data);
        }

        // reduce_add
        template <class A>
        XSIMD_INLINE float reduce_add(batch<float, A> const& self, requires_arch<vxe>) noexcept
        {
            v4sf shifted_64 = vec_sld(self.data, self.data, 8);
            v4sf added_1 = self.data + shifted_64;
            v4sf shifted_32 = vec_sld(added_1, added_1, 4);
            return (added_1 + shifted_32)[0];
        }

        template <class A>
        XSIMD_INLINE double reduce_add(batch<double, A> const& self, requires_arch<vxe>) noexcept
        {
            return (self.data + vec_sld(self.data, self.data, 8))[0];
        }

        template <class A>
        XSIMD_INLINE uint64_t reduce_add(batch<uint64_t, A> const& self, requires_arch<vxe>) noexcept
        {
            uv2di shifted = vec_sld((uv2di)self.data, (uv2di)self.data, 8);
            uv2di sum = (uv2di)self.data + shifted;
            return (uint64_t)sum[0];
        }
        template <class A>
        XSIMD_INLINE int64_t reduce_add(batch<int64_t, A> const& self, requires_arch<vxe>) noexcept
        {
            v2di shifted = vec_sld((v2di)self.data, (v2di)self.data, 8);
            v2di sum = (v2di)self.data + shifted;
            return (int64_t)sum[0];
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                using t = typename batch<T, A>::register_type;
                t shifted_64 = vec_sld(self.data, self.data, 8);
                t added_1 = self.data + shifted_64;
                t shifted_32 = vec_sld(added_1, added_1, 4);
                return (added_1 + shifted_32)[0];
            }
            else if constexpr (sizeof(T) == 2)
            {
                using t = typename batch<T, A>::register_type;
                t shifted_64 = vec_sld(self.data, self.data, 8);
                t added_1 = self.data + shifted_64;
                t shifted_32 = vec_sld(added_1, added_1, 4);
                t added_2 = added_1 + shifted_32;
                t shifted_16 = vec_sld(added_2, added_2, 2);
                return (added_2 + shifted_16)[0];
            }
            else
            {
                using t = typename batch<T, A>::register_type;
                t shifted_64 = vec_sld(self.data, self.data, 8);
                t added_1 = self.data + shifted_64;
                t shifted_32 = vec_sld(added_1, added_1, 4);
                t added_2 = added_1 + shifted_32;
                t shifted_16 = vec_sld(added_2, added_2, 2);
                t added_3 = added_2 + shifted_16;
                t shifted_8 = vec_sld(added_3, added_3, 1);
                return (added_3 + shifted_8)[0];
            }
        }

        // select
        template <class A, class T>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<vxe>) noexcept
        {
            return vec_sel(false_br.data, true_br.data, cond.data);
        }
        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<vxe>) noexcept
        {
            return select(batch_bool<T, A> { Values... }, true_br, false_br, vxe {});
        }

        // slide_left
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<vxe>) noexcept
        {
            if constexpr (N == batch<T, A>::size * sizeof(T))
            {
                return batch<T, A>(0);
            }
            else
            {
                auto shift_count = vec_splats((uint8_t)(8 * N));
                return vec_sll(x.data, shift_count);
            }
        }

        // slide_right
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<vxe>) noexcept
        {
            if constexpr (N == batch<T, A>::size * sizeof(T))
            {
                return batch<T, A>(0);
            }
            else
            {
                auto shift_count = vec_splats((uint8_t)(8 * N));
                return vec_srl(x.data, shift_count);
            }
        }

        // sqrt
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> sqrt(batch<T, A> const& val, requires_arch<vxe>) noexcept
        {
            return vec_sqrt(val.data);
        }

        // rsqrt
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> rsqrt(batch<T, A> const& val, requires_arch<vxe>) noexcept
        {
            return batch<T, A>(T(1)) / sqrt(val, vxe {});
        }

        // shuffle
        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3>
        XSIMD_INLINE batch<float, A> shuffle(batch<float, A> const& x, batch<float, A> const& y, batch_constant<ITy, A, I0, I1, I2, I3>, requires_arch<vxe>) noexcept
        {
            return vec_perm(x.data, y.data,
                            (__vector unsigned char) {
                                4 * I0 + 0, 4 * I0 + 1, 4 * I0 + 2, 4 * I0 + 3,
                                4 * I1 + 0, 4 * I1 + 1, 4 * I1 + 2, 4 * I1 + 3,
                                4 * I2 + 0, 4 * I2 + 1, 4 * I2 + 2, 4 * I2 + 3,
                                4 * I3 + 0, 4 * I3 + 1, 4 * I3 + 2, 4 * I3 + 3 });
        }

        template <class A, class ITy, ITy I0, ITy I1>
        XSIMD_INLINE batch<double, A> shuffle(batch<double, A> const& x, batch<double, A> const& y, batch_constant<ITy, A, I0, I1>, requires_arch<vxe>) noexcept
        {
            return vec_perm(x.data, y.data,
                            (__vector unsigned char) {
                                8 * I0 + 0,
                                8 * I0 + 1,
                                8 * I0 + 2,
                                8 * I0 + 3,
                                8 * I0 + 4,
                                8 * I0 + 5,
                                8 * I0 + 6,
                                8 * I0 + 7,
                                8 * I1 + 0,
                                8 * I1 + 1,
                                8 * I1 + 2,
                                8 * I1 + 3,
                                8 * I1 + 4,
                                8 * I1 + 5,
                                8 * I1 + 6,
                                8 * I1 + 7,
                            });
        }

        // swizzle
        // 16 x 8bit
        template <class A, uint8_t... Values>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch_constant<uint8_t, A, Values...>, requires_arch<vxe>) noexcept
        {
            static_assert(sizeof...(Values) == batch<uint8_t, A>::size, "consistent init");
            uv16qi perm = (uv16qi) { Values... };
            return vec_perm(self.data, self.data, perm);
        }
        template <class A, uint8_t... Values>
        XSIMD_INLINE batch<int8_t, A> swizzle(batch<int8_t, A> const& self, batch_constant<uint8_t, A, Values...>, requires_arch<vxe>) noexcept
        {
            static_assert(sizeof...(Values) == batch<int8_t, A>::size, "consistent init");
            uv16qi perm = (uv16qi) { Values... };
            return vec_perm(self.data, self.data, perm);
        }

        // 8 x 16 bit
        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7>, requires_arch<vxe>) noexcept
        {
            uv16qi perm = (uv16qi) {
                2 * V0, 2 * V0 + 1,
                2 * V1, 2 * V1 + 1,
                2 * V2, 2 * V2 + 1,
                2 * V3, 2 * V3 + 1,
                2 * V4, 2 * V4 + 1,
                2 * V5, 2 * V5 + 1,
                2 * V6, 2 * V6 + 1,
                2 * V7, 2 * V7 + 1
            };
            return vec_perm(self.data, self.data, perm);
        }
        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7>, requires_arch<vxe>) noexcept
        {
            uv16qi perm = (uv16qi) {
                2 * V0, 2 * V0 + 1,
                2 * V1, 2 * V1 + 1,
                2 * V2, 2 * V2 + 1,
                2 * V3, 2 * V3 + 1,
                2 * V4, 2 * V4 + 1,
                2 * V5, 2 * V5 + 1,
                2 * V6, 2 * V6 + 1,
                2 * V7, 2 * V7 + 1
            };
            return vec_perm(self.data, self.data, perm);
        }

        // 4 x 32 bit
        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<vxe>) noexcept
        {
            uv16qi perm = (uv16qi) {
                4 * V0, 4 * V0 + 1, 4 * V0 + 2, 4 * V0 + 3,
                4 * V1, 4 * V1 + 1, 4 * V1 + 2, 4 * V1 + 3,
                4 * V2, 4 * V2 + 1, 4 * V2 + 2, 4 * V2 + 3,
                4 * V3, 4 * V3 + 1, 4 * V3 + 2, 4 * V3 + 3
            };
            return vec_perm(self.data, self.data, perm);
        }
        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<vxe>) noexcept
        {
            uv16qi perm = (uv16qi) {
                4 * V0, 4 * V0 + 1, 4 * V0 + 2, 4 * V0 + 3,
                4 * V1, 4 * V1 + 1, 4 * V1 + 2, 4 * V1 + 3,
                4 * V2, 4 * V2 + 1, 4 * V2 + 2, 4 * V2 + 3,
                4 * V3, 4 * V3 + 1, 4 * V3 + 2, 4 * V3 + 3
            };
            return vec_perm(self.data, self.data, perm);
        }
        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<vxe>) noexcept
        {
            uv16qi perm = (uv16qi) {
                4 * V0, 4 * V0 + 1, 4 * V0 + 2, 4 * V0 + 3,
                4 * V1, 4 * V1 + 1, 4 * V1 + 2, 4 * V1 + 3,
                4 * V2, 4 * V2 + 1, 4 * V2 + 2, 4 * V2 + 3,
                4 * V3, 4 * V3 + 1, 4 * V3 + 2, 4 * V3 + 3
            };
            return vec_perm(self.data, self.data, perm);
        }

        // 2 x 64 bit
        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<vxe>) noexcept
        {
            using out = typename batch<uint64_t, A>::register_type;
            uv16qi perm = (uv16qi) {
                8 * V0 + 0,
                8 * V0 + 1,
                8 * V0 + 2,
                8 * V0 + 3,
                8 * V0 + 4,
                8 * V0 + 5,
                8 * V0 + 6,
                8 * V0 + 7,
                8 * V1 + 0,
                8 * V1 + 1,
                8 * V1 + 2,
                8 * V1 + 3,
                8 * V1 + 4,
                8 * V1 + 5,
                8 * V1 + 6,
                8 * V1 + 7,
            };
            return (out)vec_perm((uv2di)self.data, (uv2di)self.data, perm);
        }
        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<vxe>) noexcept
        {
            using out = typename batch<int64_t, A>::register_type;
            uv16qi perm = (uv16qi) {
                8 * V0 + 0,
                8 * V0 + 1,
                8 * V0 + 2,
                8 * V0 + 3,
                8 * V0 + 4,
                8 * V0 + 5,
                8 * V0 + 6,
                8 * V0 + 7,
                8 * V1 + 0,
                8 * V1 + 1,
                8 * V1 + 2,
                8 * V1 + 3,
                8 * V1 + 4,
                8 * V1 + 5,
                8 * V1 + 6,
                8 * V1 + 7,
            };
            return (out)vec_perm((v2di)self.data, (v2di)self.data, perm);
        }
        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<vxe>) noexcept
        {
            uv16qi perm = (uv16qi) {
                8 * V0 + 0,
                8 * V0 + 1,
                8 * V0 + 2,
                8 * V0 + 3,
                8 * V0 + 4,
                8 * V0 + 5,
                8 * V0 + 6,
                8 * V0 + 7,
                8 * V1 + 0,
                8 * V1 + 1,
                8 * V1 + 2,
                8 * V1 + 3,
                8 * V1 + 4,
                8 * V1 + 5,
                8 * V1 + 6,
                8 * V1 + 7,
            };
            return vec_perm(self.data, self.data, perm);
        }
        // zip_hi
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return vec_mergel(self.data, other.data);
        }

        // zip_lo
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vxe>) noexcept
        {
            return vec_mergeh(self.data, other.data);
        }
        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<vxe>) noexcept
        {
            return self.data >> other;
        }
        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<vxe>) noexcept
        {
            return self.data << other;
        }

        // isnan
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch_bool<T, A> isnan(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return ~vec_cmpeq(self.data, self.data);
        }

        // ceil
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> ceil(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return vec_ceil(self.data);
        }

        // floor
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> floor(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return vec_floor(self.data);
        }
        // round
        // vec_round rounds ties to even instead of zero
#if defined __has_builtin && __has_builtin(__builtin_s390_vfi)
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> round(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return __builtin_s390_vfi(self.data, 4, 1);
        }
#endif
        // trunc
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> trunc(batch<T, A> const& self, requires_arch<vxe>) noexcept
        {
            return vec_trunc(self.data);
        }
    }
}
#endif
