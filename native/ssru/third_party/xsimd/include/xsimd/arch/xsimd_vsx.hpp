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

#ifndef XSIMD_VSX_HPP
#define XSIMD_VSX_HPP

#include "../types/xsimd_batch_fwd.hpp"
#include "../types/xsimd_vsx_register.hpp"
#include "./common/xsimd_common_cast.hpp"

#include <complex>
#include <type_traits>

namespace xsimd
{
    template <typename T, class A, bool... Values>
    struct batch_bool_constant;

    template <class T_out, class T_in, class A>
    XSIMD_INLINE batch<T_out, A> bitwise_cast(batch<T_in, A> const& x) noexcept;

    template <typename T, class A, T... Values>
    struct batch_constant;

    namespace kernel
    {
        // builtin_t<T> - the scalar type as it would be used for a vector intrinsic
        // VSX vector intrinsics do not support long, unsigned long, and char
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

        template <class A, class T>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const&, batch<T, A> const&, requires_arch<common>) noexcept;
        template <class A, class T>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const&, batch<T, A> const&, requires_arch<common>) noexcept;

        // abs
        template <class A>
        XSIMD_INLINE batch<float, A> abs(batch<float, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_abs(self.data);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> abs(batch<double, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_abs(self.data);
        }

        // add
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_add(self.data, other.data);
        }

        // all
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE bool all(batch_bool<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_all_ne(self.data, vec_xor(self.data, self.data));
        }

        // any
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE bool any(batch_bool<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_any_ne(self.data, vec_xor(self.data, self.data));
        }

        // avgr
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T> && sizeof(T) < 8>>
        XSIMD_INLINE batch<T, A> avgr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_avg(self.data, other.data);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> avgr(batch<float, A> const& self, batch<float, A> const& other, requires_arch<vsx>) noexcept
        {
            return avgr(self, other, common {});
        }
        template <class A>
        XSIMD_INLINE batch<double, A> avgr(batch<double, A> const& self, batch<double, A> const& other, requires_arch<vsx>) noexcept
        {
            return avgr(self, other, common {});
        }

        // avg
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> avg(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            if constexpr (sizeof(T) < 8)
            {
                constexpr auto nbit = 8 * sizeof(T) - 1;
                auto adj = bitwise_cast<T>(bitwise_cast<as_unsigned_integer_t<T>>((self ^ other) << nbit) >> nbit);
                return avgr(self, other, A {}) - adj;
            }
            else
            {
                return avg(self, other, common {});
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A> avg(batch<float, A> const& self, batch<float, A> const& other, requires_arch<vsx>) noexcept
        {
            return avg(self, other, common {});
        }
        template <class A>
        XSIMD_INLINE batch<double, A> avg(batch<double, A> const& self, batch<double, A> const& other, requires_arch<vsx>) noexcept
        {
            return avg(self, other, common {});
        }

        // batch_bool_cast
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<vsx>) noexcept
        {
            return (typename batch_bool<T_out, A>::register_type)self.data;
        }

        // bitwise_and
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_and(self.data, other.data);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_and(self.data, other.data);
        }

        // bitwise_andnot
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_and(self.data, vec_nor(other.data, other.data));
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return self.data & ~other.data;
        }

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<vsx>) noexcept
        {
            using shift_type = as_unsigned_integer_t<T>;
            batch<shift_type, A> shift(static_cast<shift_type>(other));
            return vec_sl(self.data, shift.data);
        }

        // bitwise_not
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_nor(self.data, self.data);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_nor(self.data, self.data);
        }

        // bitwise_or
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_or(self.data, other.data);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_or(self.data, other.data);
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<vsx>) noexcept
        {
            using shift_type = as_unsigned_integer_t<T>;
            batch<shift_type, A> shift(static_cast<shift_type>(other));
            if constexpr (std::is_signed_v<T>)
            {
                return vec_sra(self.data, shift.data);
            }
            else
            {
                return vec_sr(self.data, shift.data);
            }
        }

        // bitwise_xor
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_xor(self.data, other.data);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_xor(self.data, other.data);
        }

        // bitwise_cast
        template <class A, class T_in, class T_out>
        XSIMD_INLINE batch<T_out, A> bitwise_cast(batch<T_in, A> const& self, batch<T_out, A> const&, requires_arch<vsx>) noexcept
        {
            return (typename batch<T_out, A>::register_type)(self.data);
        }

        // broadcast
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<vsx>) noexcept
        {
            return vec_splats(static_cast<builtin_t<T>>(val));
        }

        // ceil
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> ceil(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_ceil(self.data);
        }

        // store_complex
        namespace detail
        {
            // complex_low
            template <class A>
            XSIMD_INLINE batch<float, A> complex_low(batch<std::complex<float>, A> const& self, requires_arch<vsx>) noexcept
            {
                return vec_mergeh(self.real().data, self.imag().data);
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_low(batch<std::complex<double>, A> const& self, requires_arch<vsx>) noexcept
            {
                return vec_mergeh(self.real().data, self.imag().data);
            }
            // complex_high
            template <class A>
            XSIMD_INLINE batch<float, A> complex_high(batch<std::complex<float>, A> const& self, requires_arch<vsx>) noexcept
            {
                return vec_mergel(self.real().data, self.imag().data);
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_high(batch<std::complex<double>, A> const& self, requires_arch<vsx>) noexcept
            {
                return vec_mergel(self.real().data, self.imag().data);
            }
        }

        // decr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<vsx>) noexcept
        {
            return self + batch<T, A>((typename batch<T, A>::register_type)mask.data);
        }

        // div
        template <class A>
        XSIMD_INLINE batch<float, A> div(batch<float, A> const& self, batch<float, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_div(self.data, other.data);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> div(batch<double, A> const& self, batch<double, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_div(self.data, other.data);
        }

        // fast_cast
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<int32_t, A> const& self, batch<float, A> const&, requires_arch<vsx>) noexcept
            {
                return vec_ctf(self.data, 0);
            }
            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<uint32_t, A> const& self, batch<float, A> const&, requires_arch<vsx>) noexcept
            {
                return vec_ctf(self.data, 0);
            }

            template <class A>
            XSIMD_INLINE batch<int32_t, A> fast_cast(batch<float, A> const& self, batch<int32_t, A> const&, requires_arch<vsx>) noexcept
            {
                return vec_cts(self.data, 0);
            }

            template <class A>
            XSIMD_INLINE batch<uint32_t, A> fast_cast(batch<float, A> const& self, batch<uint32_t, A> const&, requires_arch<vsx>) noexcept
            {
                return vec_ctu(self.data, 0);
            }
        }

        // fma
        template <class A>
        XSIMD_INLINE batch<float, A> fma(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<vsx>) noexcept
        {
            return vec_madd(x.data, y.data, z.data);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fma(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<vsx>) noexcept
        {
            return vec_madd(x.data, y.data, z.data);
        }

        // fms
        template <class A>
        XSIMD_INLINE batch<float, A> fms(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<vsx>) noexcept
        {
            return vec_msub(x.data, y.data, z.data);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fms(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<vsx>) noexcept
        {
            return vec_msub(x.data, y.data, z.data);
        }

        // eq
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            auto res = vec_cmpeq(self.data, other.data);
            return *reinterpret_cast<typename batch_bool<T, A>::register_type*>(&res);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vsx>) noexcept
        {
            auto res = vec_cmpeq(self.data, other.data);
            return *reinterpret_cast<typename batch_bool<T, A>::register_type*>(&res);
        }

        // first
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_extract(self.data, 0);
        }

        // floor
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> floor(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_floor(self.data);
        }

        // ge
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_cmpge(self.data, other.data);
        }

        // gt
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_cmpgt(self.data, other.data);
        }

        // haddp
        template <class A>
        XSIMD_INLINE batch<float, A> haddp(batch<float, A> const* row, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_mergee(row[0].data, row[1].data); // v00 v10 v02 v12
            auto tmp1 = vec_mergeo(row[0].data, row[1].data); // v01 v11 v03 v13
            auto tmp4 = vec_add(tmp0, tmp1); // (v00 + v01, v10 + v11, v02 + v03, v12 + v13)

            auto tmp2 = vec_mergee(row[2].data, row[3].data); // v20 v30 v22 v32
            auto tmp3 = vec_mergeo(row[2].data, row[3].data); // v21 v31 v23 v33
            auto tmp5 = vec_add(tmp0, tmp1); // (v20 + v21, v30 + v31, v22 + v23, v32 + v33)

            auto tmp6 = vec_perm(tmp4, tmp5, (__vector unsigned char) { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23 }); // (v00 + v01, v10 + v11, v20 + v21, v30 + v31
            auto tmp7 = vec_perm(tmp4, tmp5, (__vector unsigned char) { 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31 }); // (v02 + v03, v12 + v13, v12 + v13, v32 + v33)

            return vec_add(tmp6, tmp7);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> haddp(batch<double, A> const* row, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_mergee(row[0].data, row[1].data); // v00 v10 v02 v12
            auto tmp1 = vec_mergeo(row[0].data, row[1].data); // v01 v11 v03 v13
            return vec_add(tmp0, tmp1);
        }

        // incr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<vsx>) noexcept
        {
            return self - batch<T, A>((typename batch<T, A>::register_type)mask.data);
        }

        // insert
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<vsx>) noexcept
        {
            return vec_insert(val, self.data, I);
        }

        // isnan
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isnan(batch<float, A> const& self, requires_arch<vsx>) noexcept
        {
            return ~vec_cmpeq(self.data, self.data);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isnan(batch<double, A> const& self, requires_arch<vsx>) noexcept
        {
            return ~vec_cmpeq(self.data, self.data);
        }

        // load_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* mem, convert<T>, requires_arch<vsx>) noexcept
        {
            return (typename batch<T, A>::register_type)vec_xl(0, (builtin_t<T>*)mem);
        }

        // load_aligned
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> load_aligned(T const* mem, convert<T>, requires_arch<vsx>) noexcept
        {
            return load_unaligned<A>(mem, kernel::convert<T> {}, vsx {});
        }

        // load_complex
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<std::complex<float>, A> load_complex(batch<float, A> const& hi, batch<float, A> const& lo, requires_arch<vsx>) noexcept
            {
                __vector unsigned char perme = { 0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27 };
                __vector unsigned char permo = { 4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31 };
                return { vec_perm(hi.data, lo.data, perme), vec_perm(hi.data, lo.data, permo) };
            }
            template <class A>
            XSIMD_INLINE batch<std::complex<double>, A> load_complex(batch<double, A> const& hi, batch<double, A> const& lo, requires_arch<vsx>) noexcept
            {
                return { vec_mergee(hi.data, lo.data), vec_mergeo(hi.data, lo.data) };
            }
        }

        // le
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_cmple(self.data, other.data);
        }

        // lt
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_cmplt(self.data, other.data);
        }

        // max
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_max(self.data, other.data);
        }

        // min
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_min(self.data, other.data);
        }

        // mul
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return self.data * other.data;
        }

        // neg
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return -(self.data);
        }

        // neq
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return ~vec_cmpeq(self.data, other.data);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return ~vec_cmpeq(self.data, other.data);
        }

        // reciprocal
        template <class A>
        XSIMD_INLINE batch<float, A> reciprocal(batch<float, A> const& self,
                                                kernel::requires_arch<vsx>)
        {
            return vec_re(self.data);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> reciprocal(batch<double, A> const& self,
                                                 kernel::requires_arch<vsx>)
        {
            return vec_re(self.data);
        }

        // reduce_add
        template <class A>
        XSIMD_INLINE signed reduce_add(batch<signed, A> const& self, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_reve(self.data); // v3, v2, v1, v0
            auto tmp1 = vec_add(self.data, tmp0); // v0 + v3, v1 + v2, v2 + v1, v3 + v0
            auto tmp2 = vec_mergel(tmp1, tmp1); // v2 + v1, v2 + v1, v3 + v0, v3 + v0
            auto tmp3 = vec_add(tmp1, tmp2);
            return vec_extract(tmp3, 0);
        }
        template <class A>
        XSIMD_INLINE unsigned reduce_add(batch<unsigned, A> const& self, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_reve(self.data); // v3, v2, v1, v0
            auto tmp1 = vec_add(self.data, tmp0); // v0 + v3, v1 + v2, v2 + v1, v3 + v0
            auto tmp2 = vec_mergel(tmp1, tmp1); // v2 + v1, v2 + v1, v3 + v0, v3 + v0
            auto tmp3 = vec_add(tmp1, tmp2);
            return vec_extract(tmp3, 0);
        }
        template <class A>
        XSIMD_INLINE float reduce_add(batch<float, A> const& self, requires_arch<vsx>) noexcept
        {
            // FIXME: find an in-order approach
            auto tmp0 = vec_reve(self.data); // v3, v2, v1, v0
            auto tmp1 = vec_add(self.data, tmp0); // v0 + v3, v1 + v2, v2 + v1, v3 + v0
            auto tmp2 = vec_mergel(tmp1, tmp1); // v2 + v1, v2 + v1, v3 + v0, v3 + v0
            auto tmp3 = vec_add(tmp1, tmp2);
            return vec_extract(tmp3, 0);
        }
        template <class A>
        XSIMD_INLINE double reduce_add(batch<double, A> const& self, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_reve(self.data); // v1, v0
            auto tmp1 = vec_add(self.data, tmp0); // v0 + v1, v1 + v0
            return vec_extract(tmp1, 0);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return reduce_add(self, common {});
        }

        // reduce_mul
        template <class A>
        XSIMD_INLINE signed reduce_mul(batch<signed, A> const& self, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_reve(self.data); // v3, v2, v1, v0
            auto tmp1 = vec_mul(self.data, tmp0); // v0 * v3, v1 * v2, v2 * v1, v3 * v0
            auto tmp2 = vec_mergel(tmp1, tmp1); // v2 * v1, v2 * v1, v3 * v0, v3 * v0
            auto tmp3 = vec_mul(tmp1, tmp2);
            return vec_extract(tmp3, 0);
        }
        template <class A>
        XSIMD_INLINE unsigned reduce_mul(batch<unsigned, A> const& self, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_reve(self.data); // v3, v2, v1, v0
            auto tmp1 = vec_mul(self.data, tmp0); // v0 * v3, v1 * v2, v2 * v1, v3 * v0
            auto tmp2 = vec_mergel(tmp1, tmp1); // v2 * v1, v2 * v1, v3 * v0, v3 * v0
            auto tmp3 = vec_mul(tmp1, tmp2);
            return vec_extract(tmp3, 0);
        }
        template <class A>
        XSIMD_INLINE float reduce_mul(batch<float, A> const& self, requires_arch<vsx>) noexcept
        {
            // FIXME: find an in-order approach
            auto tmp0 = vec_reve(self.data); // v3, v2, v1, v0
            auto tmp1 = vec_mul(self.data, tmp0); // v0 * v3, v1 * v2, v2 * v1, v3 * v0
            auto tmp2 = vec_mergel(tmp1, tmp1); // v2 * v1, v2 * v1, v3 * v0, v3 * v0
            auto tmp3 = vec_mul(tmp1, tmp2);
            return vec_extract(tmp3, 0);
        }
        template <class A>
        XSIMD_INLINE double reduce_mul(batch<double, A> const& self, requires_arch<vsx>) noexcept
        {
            auto tmp0 = vec_reve(self.data); // v1, v0
            auto tmp1 = vec_mul(self.data, tmp0); // v0 * v1, v1 * v0
            return vec_extract(tmp1, 0);
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE T reduce_mul(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return reduce_mul(self, common {});
        }

        // round

        // vec_round exists also for float vectors but is mapped to vrfin instruction which uses the wrong rounding mode
#if defined __has_builtin && __has_builtin(__builtin_vsx_xvrspi)
        template <class A>
        XSIMD_INLINE batch<float, A> round(batch<float, A> const& self, requires_arch<vsx>) noexcept
        {
            return __builtin_vsx_xvrspi(self.data);
        }
#endif
        // For double vectors vec_round uses xvrdpi which does the right thing
        template <class A>
        XSIMD_INLINE batch<double, A> round(batch<double, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_round(self.data);
        }

        // rsqrt
        template <class A>
        XSIMD_INLINE batch<float, A> rsqrt(batch<float, A> const& val, requires_arch<vsx>) noexcept
        {
            return vec_rsqrt(val.data);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> rsqrt(batch<double, A> const& val, requires_arch<vsx>) noexcept
        {
            return vec_rsqrt(val.data);
        }

        // select
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<vsx>) noexcept
        {
            return vec_sel(false_br.data, true_br.data, cond.data);
        }
        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<vsx>) noexcept
        {
            return select(batch_bool<T, A> { Values... }, true_br, false_br, vsx {});
        }

        // shuffle
        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3>
        XSIMD_INLINE batch<float, A> shuffle(batch<float, A> const& x, batch<float, A> const& y, batch_constant<ITy, A, I0, I1, I2, I3>, requires_arch<vsx>) noexcept
        {
            return vec_perm(x.data, y.data,
                            (__vector unsigned char) {
                                4 * I0 + 0, 4 * I0 + 1, 4 * I0 + 2, 4 * I0 + 3,
                                4 * I1 + 0, 4 * I1 + 1, 4 * I1 + 2, 4 * I1 + 3,
                                4 * I2 + 0, 4 * I2 + 1, 4 * I2 + 2, 4 * I2 + 3,
                                4 * I3 + 0, 4 * I3 + 1, 4 * I3 + 2, 4 * I3 + 3 });
        }

        template <class A, class ITy, ITy I0, ITy I1>
        XSIMD_INLINE batch<double, A> shuffle(batch<double, A> const& x, batch<double, A> const& y, batch_constant<ITy, A, I0, I1>, requires_arch<vsx>) noexcept
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

        // sqrt
        template <class A>
        XSIMD_INLINE batch<float, A> sqrt(batch<float, A> const& val, requires_arch<vsx>) noexcept
        {
            return vec_sqrt(val.data);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> sqrt(batch<double, A> const& val, requires_arch<vsx>) noexcept
        {
            return vec_sqrt(val.data);
        }

        // slide_left
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<vsx>) noexcept
        {
            if constexpr (N == batch<T, A>::size * sizeof(T))
            {
                return batch<T, A>(0);
            }
            else
            {
                auto slider = vec_splats((uint8_t)(8 * N));
                return (typename batch<T, A>::register_type)vec_slo(x.data, slider);
            }
        }

        // slide_right
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<vsx>) noexcept
        {
            if constexpr (N == batch<T, A>::size * sizeof(T))
            {
                return batch<T, A>(0);
            }
            else
            {
                auto slider = vec_splats((uint8_t)(8 * N));
                return (typename batch<T, A>::register_type)vec_sro((__vector unsigned char)x.data, slider);
            }
        }

        // sadd
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T> && sizeof(T) != 8>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_adds(self.data, other.data);
        }

        // set
        template <class A, class T, class... Values>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<vsx>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch<T, A>::size, "consistent init");
            return typename batch<T, A>::register_type { values... };
        }

        template <class A, class T, class... Values, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<vsx>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<T, A>::size, "consistent init");
            return typename batch_bool<T, A>::register_type { static_cast<decltype(std::declval<typename batch_bool<T, A>::register_type>()[0])>(values ? -1LL : 0LL)... };
        }

        // ssub

        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T> && sizeof(T) == 1>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_subs(self.data, other.data);
        }

        // store_aligned
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            vec_xst((typename batch<T, A>::register_type)self.data, 0, (builtin_t<T>*)mem);
        }

        // store_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            store_aligned<A>(mem, self, vsx {});
        }

        // sub
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_sub(self.data, other.data);
        }

        // swizzle

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<vsx>) noexcept
        {
            return vec_perm(self.data, self.data,
                            (__vector unsigned char) {
                                4 * V0 + 0, 4 * V0 + 1, 4 * V0 + 2, 4 * V0 + 3,
                                4 * V1 + 0, 4 * V1 + 1, 4 * V1 + 2, 4 * V1 + 3,
                                4 * V2 + 0, 4 * V2 + 1, 4 * V2 + 2, 4 * V2 + 3,
                                4 * V3 + 0, 4 * V3 + 1, 4 * V3 + 2, 4 * V3 + 3 });
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<vsx>) noexcept
        {
            return vec_perm(self.data, self.data,
                            (__vector unsigned char) {
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
                            });
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch_constant<uint64_t, A, V0, V1>, requires_arch<vsx>) noexcept
        {
            return vec_perm(self.data, self.data,
                            (__vector unsigned char) {
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
                            });
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self, batch_constant<uint64_t, A, V0, V1> mask, requires_arch<vsx>) noexcept
        {
            return bitwise_cast<int64_t>(swizzle(bitwise_cast<uint64_t>(self), mask, vsx {}));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3>, requires_arch<vsx>) noexcept
        {
            return vec_perm(self.data, self.data,
                            (__vector unsigned char) {
                                4 * V0 + 0, 4 * V0 + 1, 4 * V0 + 2, 4 * V0 + 3,
                                4 * V1 + 0, 4 * V1 + 1, 4 * V1 + 2, 4 * V1 + 3,
                                4 * V2 + 0, 4 * V2 + 1, 4 * V2 + 2, 4 * V2 + 3,
                                4 * V3 + 0, 4 * V3 + 1, 4 * V3 + 2, 4 * V3 + 3 });
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self, batch_constant<uint32_t, A, V0, V1, V2, V3> mask, requires_arch<vsx>) noexcept
        {
            return bitwise_cast<int32_t>(swizzle(bitwise_cast<uint32_t>(self), mask, vsx {}));
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7>, requires_arch<vsx>) noexcept
        {
            return vec_perm(self.data, self.data,
                            (__vector unsigned char) {
                                2 * V0 + 0, 2 * V0 + 1, 2 * V1 + 0, 2 * V1 + 1,
                                2 * V2 + 0, 2 * V2 + 1, 2 * V3 + 0, 2 * V3 + 1,
                                2 * V4 + 0, 2 * V4 + 1, 2 * V5 + 0, 2 * V5 + 1,
                                2 * V6 + 0, 2 * V6 + 1, 2 * V7 + 0, 2 * V7 + 1 });
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self, batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask, requires_arch<vsx>) noexcept
        {
            return bitwise_cast<int16_t>(swizzle(bitwise_cast<uint16_t>(self), mask, vsx {}));
        }

        // trunc
        template <class A, class T, class = std::enable_if_t<std::is_floating_point_v<T>>>
        XSIMD_INLINE batch<T, A> trunc(batch<T, A> const& self, requires_arch<vsx>) noexcept
        {
            return vec_trunc(self.data);
        }

        // widen
        template <class A>
        XSIMD_INLINE std::array<batch<double, A>, 2> widen(batch<float, A> const& x, requires_arch<vsx>) noexcept
        {
            return { batch<double, A>(vec_doublel(x.data)), batch<double, A>(vec_doubleh(x.data)) };
        }
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<vsx>) noexcept
        {
            auto even = vec_mule(x.data, vec_splats(T(1))); // x0, x2, x4, x6
            auto odd = vec_mulo(x.data, vec_splats(T(1))); // x1, x3, x5, x7
            return { batch<widen_t<T>, A>(vec_mergel(even, odd)), batch<widen_t<T>, A>(vec_mergeh(even, odd)) };
        }

        // zip_hi
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_mergel(self.data, other.data);
        }

        // zip_lo
        template <class A, class T, class = std::enable_if_t<std::is_scalar_v<T>>>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<vsx>) noexcept
        {
            return vec_mergeh(self.data, other.data);
        }
    }
}

#endif
