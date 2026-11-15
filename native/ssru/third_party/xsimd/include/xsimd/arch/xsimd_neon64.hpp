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

#ifndef XSIMD_NEON64_HPP
#define XSIMD_NEON64_HPP

#include "../types/xsimd_neon64_register.hpp"
#include "../types/xsimd_utils.hpp"
#include "./xsimd_neon.hpp"

#include <cassert>
#include <complex>
#include <cstddef>
#include <cstring>
#include <utility>

namespace xsimd
{
    template <typename T, class A, bool... Values>
    struct batch_bool_constant;

    namespace kernel
    {
        using namespace types;

        namespace detail
        {

            template <class T>
            using enable_neon64_type_t = std::enable_if_t<std::is_integral_v<T> || std::is_same_v<T, float> || std::is_same_v<T, double>,
                                                          int>;
        }

        // get
        template <class A, size_t I>
        XSIMD_INLINE double get(batch<double, A> const& self, ::xsimd::index<I>, requires_arch<neon64>) noexcept
        {
            return vgetq_lane_f64(self, I);
        }

        // first
        template <class A>
        XSIMD_INLINE double first(batch<double, A> const& self, requires_arch<neon64>) noexcept
        {
            return vgetq_lane_f64(self, 0);
        }

        /*******
         * all *
         *******/

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return vminvq_u32(arg) == ~0U;
        }

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return all(batch_bool<uint32_t, A>(vreinterpretq_u32_u8(arg)), neon64 {});
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return all(batch_bool<uint32_t, A>(vreinterpretq_u32_u16(arg)), neon64 {});
        }

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return all(batch_bool<uint32_t, A>(vreinterpretq_u32_u64(arg)), neon64 {});
        }

        /*******
         * any *
         *******/

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return vmaxvq_u32(arg) != 0;
        }

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return any(batch_bool<uint32_t, A>(vreinterpretq_u32_u8(arg)), neon64 {});
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return any(batch_bool<uint32_t, A>(vreinterpretq_u32_u16(arg)), neon64 {});
        }

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            return any(batch_bool<uint32_t, A>(vreinterpretq_u32_u64(arg)), neon64 {});
        }

        /*************
         * broadcast *
         *************/

        // Required to avoid ambiguous call
        template <class A, class T>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<neon64>) noexcept
        {
            return broadcast<A>(val, neon {});
        }

        template <class A>
        XSIMD_INLINE batch<double, A> broadcast(double val, requires_arch<neon64>) noexcept
        {
            return vdupq_n_f64(val);
        }

        /*************
         * from_bool *
         *************/

        template <class A>
        XSIMD_INLINE batch<double, A> from_bool(batch_bool<double, A> const& arg, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_f64_u64(vandq_u64(arg, vreinterpretq_u64_f64(vdupq_n_f64(1.))));
        }

        /********
         * load *
         ********/
#if defined(__clang__) || defined(__GNUC__)
#define xsimd_aligned_load(inst, type, expr) inst((type)__builtin_assume_aligned(expr, 16))
#else
#define xsimd_aligned_load(inst, type, expr) inst((type)expr)
#endif

        template <class A>
        XSIMD_INLINE batch<double, A> load_aligned(double const* src, convert<double>, requires_arch<neon64>) noexcept
        {
            return xsimd_aligned_load(vld1q_f64, double*, src);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> load_unaligned(double const* src, convert<double>, requires_arch<neon64>) noexcept
        {
            return vld1q_f64(src);
        }
#undef xsimd_aligned_load

        /*********
         * store *
         *********/

        template <class A>
        XSIMD_INLINE void store_aligned(double* dst, batch<double, A> const& src, requires_arch<neon64>) noexcept
        {
            vst1q_f64(dst, src);
        }

        template <class A>
        XSIMD_INLINE void store_unaligned(double* dst, batch<double, A> const& src, requires_arch<neon64>) noexcept
        {
            return store_aligned<A>(dst, src, A {});
        }

        /****************
         * store_stream *
         ****************/

#if defined(__GNUC__)
        template <class A>
        XSIMD_INLINE void store_stream(float* mem, batch<float, A> const& val, requires_arch<neon64>) noexcept
        {
            float32x2_t lo = vget_low_f32(val);
            float32x2_t hi = vget_high_f32(val);
            __asm__ __volatile__("stnp %d[lo], %d[hi], [%[mem]]"
                                 :
                                 : [lo] "w"(lo), [hi] "w"(hi), [mem] "r"(mem)
                                 : "memory");
        }

        template <class A>
        XSIMD_INLINE void store_stream(double* mem, batch<double, A> const& val, requires_arch<neon64>) noexcept
        {
            float64x1_t lo = vget_low_f64(val);
            float64x1_t hi = vget_high_f64(val);
            __asm__ __volatile__("stnp %d[lo], %d[hi], [%[mem]]"
                                 :
                                 : [lo] "w"(lo), [hi] "w"(hi), [mem] "r"(mem)
                                 : "memory");
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE void store_stream(T* mem, batch<T, A> const& val, requires_arch<neon64>) noexcept
        {
            uint64x2_t u64;
            std::memcpy(&u64, &val, sizeof(u64));
            uint64x1_t lo = vget_low_u64(u64);
            uint64x1_t hi = vget_high_u64(u64);
            __asm__ __volatile__("stnp %d[lo], %d[hi], [%[mem]]"
                                 :
                                 : [lo] "w"(lo), [hi] "w"(hi), [mem] "r"(mem)
                                 : "memory");
        }
#endif

        /***************
         * load_stream *
         ***************/

#if defined(__GNUC__)
        template <class A>
        XSIMD_INLINE batch<float, A> load_stream(float const* mem, convert<float>, requires_arch<neon64>) noexcept
        {
            float32x2_t lo, hi;
            __asm__ __volatile__("ldnp %d[lo], %d[hi], [%[mem]]"
                                 : [lo] "=w"(lo), [hi] "=w"(hi)
                                 : [mem] "r"(mem)
                                 : "memory");
            return vcombine_f32(lo, hi);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> load_stream(double const* mem, convert<double>, requires_arch<neon64>) noexcept
        {
            float64x1_t lo, hi;
            __asm__ __volatile__("ldnp %d[lo], %d[hi], [%[mem]]"
                                 : [lo] "=w"(lo), [hi] "=w"(hi)
                                 : [mem] "r"(mem)
                                 : "memory");
            return vcombine_f64(lo, hi);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE batch<T, A> load_stream(T const* mem, convert<T>, requires_arch<neon64>) noexcept
        {
            uint64x1_t lo, hi;
            __asm__ __volatile__("ldnp %d[lo], %d[hi], [%[mem]]"
                                 : [lo] "=w"(lo), [hi] "=w"(hi)
                                 : [mem] "r"(mem)
                                 : "memory");
            uint64x2_t u64 = vcombine_u64(lo, hi);
            batch<T, A> result;
            std::memcpy(&result, &u64, sizeof(u64));
            return result;
        }
#endif

        /*********************
         * store<batch_bool> *
         *********************/

        template <class A>
        XSIMD_INLINE void store(batch_bool<double, A> b, bool* mem, requires_arch<neon>) noexcept
        {
            store(batch_bool<uint64_t, A>(b.data), mem, A {});
        }

        /****************
         * load_complex *
         ****************/

        template <class A>
        XSIMD_INLINE batch<std::complex<double>, A> load_complex_aligned(std::complex<double> const* mem, convert<std::complex<double>>, requires_arch<neon64>) noexcept
        {
            using real_batch = batch<double, A>;
            const double* buf = reinterpret_cast<const double*>(mem);
            float64x2x2_t tmp = vld2q_f64(buf);
            real_batch real = tmp.val[0],
                       imag = tmp.val[1];
            return batch<std::complex<double>, A> { real, imag };
        }

        template <class A>
        XSIMD_INLINE batch<std::complex<double>, A> load_complex_unaligned(std::complex<double> const* mem, convert<std::complex<double>> cvt, requires_arch<neon64>) noexcept
        {
            return load_complex_aligned<A>(mem, cvt, A {});
        }

        /*****************
         * store_complex *
         *****************/

        template <class A>
        XSIMD_INLINE void store_complex_aligned(std::complex<double>* dst, batch<std::complex<double>, A> const& src, requires_arch<neon64>) noexcept
        {
            float64x2x2_t tmp;
            tmp.val[0] = src.real();
            tmp.val[1] = src.imag();
            double* buf = reinterpret_cast<double*>(dst);
            vst2q_f64(buf, tmp);
        }

        template <class A>
        XSIMD_INLINE void store_complex_unaligned(std::complex<double>* dst, batch<std::complex<double>, A> const& src, requires_arch<neon64>) noexcept
        {
            store_complex_aligned(dst, src, A {});
        }

        /*******
         * set *
         *******/

        template <class A>
        XSIMD_INLINE batch<double, A> set(batch<double, A> const&, requires_arch<neon64> req, double d0, double d1) noexcept
        {
            alignas(A::alignment()) double data[] = { d0, d1 };
            return load_aligned<A>(data, {}, req);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> set(batch_bool<double, A> const&, requires_arch<neon64>, bool b0, bool b1) noexcept
        {
            using unsigned_type = as_unsigned_integer_t<double>;
            auto const out = batch<unsigned_type, A> {
                static_cast<unsigned_type>(b0 ? -1LL : 0LL),
                static_cast<unsigned_type>(b1 ? -1LL : 0LL)
            };
            return { out.data };
        }

        /*******
         * neg *
         *******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_u64_s64(vnegq_s64(vreinterpretq_s64_u64(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vnegq_s64(rhs);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> neg(batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vnegq_f64(rhs);
        }

        /*******
         * add *
         *******/

        template <class A>
        XSIMD_INLINE batch<double, A> add(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vaddq_f64(lhs, rhs);
        }

        /********
         * sadd *
         ********/

        template <class A>
        XSIMD_INLINE batch<double, A> sadd(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return add(lhs, rhs, neon64 {});
        }

        /*******
         * sub *
         *******/

        template <class A>
        XSIMD_INLINE batch<double, A> sub(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vsubq_f64(lhs, rhs);
        }

        /********
         * ssub *
         ********/

        template <class A>
        XSIMD_INLINE batch<double, A> ssub(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return sub(lhs, rhs, neon64 {});
        }

        /*******
         * mul *
         *******/

        template <class A>
        XSIMD_INLINE batch<double, A> mul(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vmulq_f64(lhs, rhs);
        }

        /*******
         * div *
         *******/

#if defined(XSIMD_FAST_INTEGER_DIVISION)
        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcvtq_u64_f64(vcvtq_f64_u64(lhs) / vcvtq_f64_u64(rhs));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcvtq_s64_f64(vcvtq_f64_s64(lhs) / vcvtq_f64_s64(rhs));
        }
#endif
        template <class A>
        XSIMD_INLINE batch<double, A> div(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vdivq_f64(lhs, rhs);
        }

        /******
         * eq *
         ******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vceqq_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vceqq_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vceqq_f64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vceqq_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vceqq_u64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch_bool<double, A> const& lhs, batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vceqq_u64(lhs, rhs);
        }

        /*************
         * fast_cast *
         *************/
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<int64_t, A> const& x, batch<double, A> const&, requires_arch<neon64>) noexcept
            {
                return vcvtq_f64_s64(x);
            }

            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<uint64_t, A> const& x, batch<double, A> const&, requires_arch<neon64>) noexcept
            {
                return vcvtq_f64_u64(x);
            }

            template <class A>
            XSIMD_INLINE batch<int64_t, A> fast_cast(batch<double, A> const& x, batch<int64_t, A> const&, requires_arch<neon64>) noexcept
            {
                return vcvtq_s64_f64(x);
            }

            template <class A>
            XSIMD_INLINE batch<uint64_t, A> fast_cast(batch<double, A> const& x, batch<uint64_t, A> const&, requires_arch<neon64>) noexcept
            {
                return vcvtq_u64_f64(x);
            }

        }

        /******
         * lt *
         ******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcltq_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcltq_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> lt(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcltq_f64(lhs, rhs);
        }

        /******
         * le *
         ******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcleq_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcleq_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> le(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcleq_f64(lhs, rhs);
        }

        /******
         * gt *
         ******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcgtq_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcgtq_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> gt(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcgtq_f64(lhs, rhs);
        }

        /******
         * ge *
         ******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcgeq_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcgeq_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> ge(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vcgeq_f64(lhs, rhs);
        }

        /*******************
         * batch_bool_cast *
         *******************/

        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<neon64>) noexcept
        {
            using register_type = typename batch_bool<T_out, A>::register_type;
            return register_type(self);
        }

        /***************
         * bitwise_and *
         ***************/

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_and(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_f64_u64(vandq_u64(vreinterpretq_u64_f64(lhs),
                                                   vreinterpretq_u64_f64(rhs)));
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_and(batch_bool<double, A> const& lhs, batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vandq_u64(lhs, rhs);
        }

        /**************
         * bitwise_or *
         **************/

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_or(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_f64_u64(vorrq_u64(vreinterpretq_u64_f64(lhs),
                                                   vreinterpretq_u64_f64(rhs)));
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_or(batch_bool<double, A> const& lhs, batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vorrq_u64(lhs, rhs);
        }

        /***************
         * bitwise_xor *
         ***************/

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_xor(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_f64_u64(veorq_u64(vreinterpretq_u64_f64(lhs),
                                                   vreinterpretq_u64_f64(rhs)));
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_xor(batch_bool<double, A> const& lhs, batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return veorq_u64(lhs, rhs);
        }

        /*******
         * neq *
         *******/

        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch_bool<double, A> const& lhs, batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return bitwise_xor(lhs, rhs, A {});
        }

        /***************
         * bitwise_not *
         ***************/

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_not(batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_f64_u32(vmvnq_u32(vreinterpretq_u32_f64(rhs)));
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_not(batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_u64_u32(vmvnq_u32(vreinterpretq_u32_u64(rhs)));
        }

        /******************
         * bitwise_andnot *
         ******************/

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_andnot(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vreinterpretq_f64_u64(vbicq_u64(vreinterpretq_u64_f64(lhs),
                                                   vreinterpretq_u64_f64(rhs)));
        }

        template <class A>
        XSIMD_INLINE batch_bool<double, A> bitwise_andnot(batch_bool<double, A> const& lhs, batch_bool<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vbicq_u64(lhs, rhs);
        }

        /*******
         * min *
         *******/

        template <class A>
        XSIMD_INLINE batch<double, A> min(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vminq_f64(lhs, rhs);
        }

        /*******
         * max *
         *******/

        template <class A>
        XSIMD_INLINE batch<double, A> max(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vmaxq_f64(lhs, rhs);
        }

        /********
         * mask *
         ********/

        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            // From https://github.com/DLTcollab/sse2neon/blob/master/sse2neon.h
            // Extract most significant bit
            uint8x16_t msbs = vshrq_n_u8(self, 7);
            // Position it appropriately
            static constexpr int8_t shift_table[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 };
            int8x16_t shifts = vld1q_s8(shift_table);
            uint8x16_t positioned = vshlq_u8(msbs, shifts);
            // Horizontal reduction
            return vaddv_u8(vget_low_u8(positioned)) | (vaddv_u8(vget_high_u8(positioned)) << 8);
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            // Extract most significant bit
            uint16x8_t msbs = vshrq_n_u16(self, 15);
            // Position it appropriately
            static constexpr int16_t shift_table[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            int16x8_t shifts = vld1q_s16(shift_table);
            uint16x8_t positioned = vshlq_u16(msbs, shifts);
            // Horizontal reduction
            return vaddvq_u16(positioned);
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            // Extract most significant bit
            uint32x4_t msbs = vshrq_n_u32(self, 31);
            // Position it appropriately
            static constexpr int32_t shift_table[4] = { 0, 1, 2, 3 };
            int32x4_t shifts = vld1q_s32(shift_table);
            uint32x4_t positioned = vshlq_u32(msbs, shifts);
            // Horizontal reduction
            return vaddvq_u32(positioned);
        }

        /*********
         * count *
         *********/
        template <class A, class T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            return vaddvq_u8(vshrq_n_u8(self, 7));
        }

        template <class A, class T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            return vaddvq_u16(vshrq_n_u16(self, 15));
        }

        template <class A, class T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            return vaddvq_u32(vshrq_n_u32(self, 31));
        }

        template <class A, class T, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<neon64>) noexcept
        {
            return vaddvq_u64(vshrq_n_u64(self, 63));
        }

        /*******
         * abs *
         *******/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return rhs;
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vabsq_s64(rhs);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> abs(batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vabsq_f64(rhs);
        }

        template <class A>
        XSIMD_INLINE batch<int32_t, A> nearbyint_as_int(batch<float, A> const& self,
                                                        requires_arch<neon64>) noexcept
        {
            return vcvtnq_s32_f32(self);
        }

#if !defined(__GNUC__)
        template <class A>
        XSIMD_INLINE batch<int64_t, A> nearbyint_as_int(batch<double, A> const& self,
                                                        requires_arch<neon64>) noexcept
        {
            return vcvtnq_s64_f64(self);
        }
#endif

        /**************
         * reciprocal *
         **************/

        template <class A>
        XSIMD_INLINE batch<double, A>
        reciprocal(const batch<double, A>& x,
                   kernel::requires_arch<neon64>) noexcept
        {
            return vrecpeq_f64(x);
        }

        /********
         * rsqrt *
         ********/

        template <class A>
        XSIMD_INLINE batch<double, A> rsqrt(batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vrsqrteq_f64(rhs);
        }

        /********
         * sqrt *
         ********/

        template <class A>
        XSIMD_INLINE batch<double, A> sqrt(batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vsqrtq_f64(rhs);
        }

        /********************
         * Fused operations *
         ********************/

#ifdef __ARM_FEATURE_FMA
        template <class A>
        XSIMD_INLINE batch<double, A> fma(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<neon64>) noexcept
        {
            return vfmaq_f64(z, x, y);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fms(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<neon64>) noexcept
        {
            return vfmaq_f64(-z, x, y);
        }
#endif

        /*********
         * haddp *
         *********/

        template <class A>
        XSIMD_INLINE batch<double, A> haddp(const batch<double, A>* row, requires_arch<neon64>) noexcept
        {
            return vpaddq_f64(row[0], row[1]);
        }

        /**********
         * insert *
         **********/

        template <class A, size_t I>
        XSIMD_INLINE batch<double, A> insert(batch<double, A> const& self, double val, index<I>, requires_arch<neon64>) noexcept
        {
            return vsetq_lane_f64(val, self, I);
        }

        /**************
         * reduce_add *
         **************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8_t x_vaddvq(uint8x16_t a) noexcept { return vaddvq_u8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8_t x_vaddvq(int8x16_t a) noexcept { return vaddvq_s8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16_t x_vaddvq(uint16x8_t a) noexcept { return vaddvq_u16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16_t x_vaddvq(int16x8_t a) noexcept { return vaddvq_s16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32_t x_vaddvq(uint32x4_t a) noexcept { return vaddvq_u32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32_t x_vaddvq(int32x4_t a) noexcept { return vaddvq_s32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64_t x_vaddvq(uint64x2_t a) noexcept { return vaddvq_u64(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64_t x_vaddvq(int64x2_t a) noexcept { return vaddvq_s64(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float x_vaddvq(float32x4_t a) noexcept { return vaddvq_f32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE double x_vaddvq(float64x2_t a) noexcept { return vaddvq_f64(a); }
        }

        template <class A, class T, detail::enable_neon64_type_t<T> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_add(batch<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vaddvq<T>(register_type(arg));
        }

        /**************
         * reduce_max *
         **************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8_t x_vmaxvq(uint8x16_t a) noexcept { return vmaxvq_u8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8_t x_vmaxvq(int8x16_t a) noexcept { return vmaxvq_s8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16_t x_vmaxvq(uint16x8_t a) noexcept { return vmaxvq_u16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16_t x_vmaxvq(int16x8_t a) noexcept { return vmaxvq_s16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32_t x_vmaxvq(uint32x4_t a) noexcept { return vmaxvq_u32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32_t x_vmaxvq(int32x4_t a) noexcept { return vmaxvq_s32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float x_vmaxvq(float32x4_t a) noexcept { return vmaxvq_f32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE double x_vmaxvq(float64x2_t a) noexcept { return vmaxvq_f64(a); }

            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64_t x_vmaxvq(uint64x2_t a) noexcept
            {
                return std::max(vdupd_laneq_u64(a, 0), vdupd_laneq_u64(a, 1));
            }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64_t x_vmaxvq(int64x2_t a) noexcept
            {
                return std::max(vdupd_laneq_s64(a, 0), vdupd_laneq_s64(a, 1));
            }
        }

        template <class A, class T, detail::enable_neon64_type_t<T> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_max(batch<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vmaxvq<T>(register_type(arg));
        }

        /**************
         * reduce_min *
         **************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class T, std::enable_if_t<std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE uint8_t x_vminvq(uint8x16_t a) noexcept { return vminvq_u8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE int8_t x_vminvq(int8x16_t a) noexcept { return vminvq_s8(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE uint16_t x_vminvq(uint16x8_t a) noexcept { return vminvq_u16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE int16_t x_vminvq(int16x8_t a) noexcept { return vminvq_s16(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE uint32_t x_vminvq(uint32x4_t a) noexcept { return vminvq_u32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE int32_t x_vminvq(int32x4_t a) noexcept { return vminvq_s32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float x_vminvq(float32x4_t a) noexcept { return vminvq_f32(a); }
            template <class T, std::enable_if_t<std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE double x_vminvq(float64x2_t a) noexcept { return vminvq_f64(a); }

            template <class T, std::enable_if_t<std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE uint64_t x_vminvq(uint64x2_t a) noexcept
            {
                return std::min(vdupd_laneq_u64(a, 0), vdupd_laneq_u64(a, 1));
            }
            template <class T, std::enable_if_t<std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE int64_t x_vminvq(int64x2_t a) noexcept
            {
                return std::min(vdupd_laneq_s64(a, 0), vdupd_laneq_s64(a, 1));
            }
        }

        template <class A, class T, detail::enable_neon64_type_t<T> = 0>
        XSIMD_INLINE typename batch<T, A>::value_type reduce_min(batch<T, A> const& arg, requires_arch<neon64>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vminvq<T>(register_type(arg));
        }

        /**********
         * select *
         **********/

        template <class A>
        XSIMD_INLINE batch<double, A> select(batch_bool<double, A> const& cond, batch<double, A> const& a, batch<double, A> const& b, requires_arch<neon64>) noexcept
        {
            return vbslq_f64(cond, a, b);
        }

        template <class A, bool... b>
        XSIMD_INLINE batch<double, A> select(batch_bool_constant<double, A, b...> const&,
                                             batch<double, A> const& true_br,
                                             batch<double, A> const& false_br,
                                             requires_arch<neon64>) noexcept
        {
            return select(batch_bool<double, A> { b... }, true_br, false_br, neon64 {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<double, A>* matrix_begin, batch<double, A>* matrix_end, requires_arch<neon64>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<double, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1];
            matrix_begin[0] = vzip1q_f64(r0, r1);
            matrix_begin[1] = vzip2q_f64(r0, r1);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE void transpose(batch<T, A>* matrix_begin, batch<T, A>* matrix_end, requires_arch<neon64>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<T, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1];
            matrix_begin[0] = vzip1q_u64(r0, r1);
            matrix_begin[1] = vzip2q_u64(r0, r1);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE void transpose(batch<T, A>* matrix_begin, batch<T, A>* matrix_end, requires_arch<neon64>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<T, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto r0 = matrix_begin[0], r1 = matrix_begin[1];
            matrix_begin[0] = vzip1q_s64(r0, r1);
            matrix_begin[1] = vzip2q_s64(r0, r1);
        }

        /**********
         * zip_lo *
         **********/
        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_u8(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_s8(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_u16(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_s16(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_u32(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_s32(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> zip_lo(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_f32(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> zip_lo(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip1q_f64(lhs, rhs);
        }

        /**********
         * zip_hi *
         **********/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_u8(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_s8(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_u16(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_s16(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_u32(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_s32(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_u64(lhs, rhs);
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_s64(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> zip_hi(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_f32(lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> zip_hi(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vzip2q_f64(lhs, rhs);
        }

        /****************
         * extract_pair *
         ****************/

        namespace detail
        {
            template <class A, size_t I, size_t... Is>
            XSIMD_INLINE batch<double, A> extract_pair(batch<double, A> const& lhs, batch<double, A> const& rhs, std::size_t n,
                                                       std::index_sequence<I, Is...>) noexcept
            {
                if (n == I)
                {
                    return vextq_f64(rhs, lhs, I);
                }
                else
                {
                    return extract_pair(lhs, rhs, n, std::index_sequence<Is...>());
                }
            }
        }

        template <class A>
        XSIMD_INLINE batch<double, A> extract_pair(batch<double, A> const& lhs, batch<double, A> const& rhs, std::size_t n, requires_arch<neon64>) noexcept
        {
            constexpr std::size_t size = batch<double, A>::size;
            assert(n < size && "index in bounds");
            return detail::extract_pair(lhs, rhs, n, std::make_index_sequence<size>());
        }

        /******************
         * bitwise_rshift *
         ******************/

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, requires_arch<neon64>) noexcept
        {
            return bitwise_rshift<A>(lhs, n, neon {});
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            // Blindly converting to signed since out of bounds shifts are UB anyways
            assert(detail::shifts_all_positive(rhs));
            return vshlq_u64(lhs, vnegq_s64(vreinterpretq_s64_u64(rhs)));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, int n, requires_arch<neon64>) noexcept
        {
            return bitwise_rshift<A>(lhs, n, neon {});
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<neon64>) noexcept
        {
            return vshlq_s64(lhs, vnegq_s64(rhs));
        }

        /****************
         * bitwise_cast *
         ****************/

        namespace wrap
        {
            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, uint8_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(uint8x16_t a) noexcept { return vreinterpretq_f64_u8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, int8_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(int8x16_t a) noexcept { return vreinterpretq_f64_s8(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, uint16_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(uint16x8_t a) noexcept { return vreinterpretq_f64_u16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, int16_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(int16x8_t a) noexcept { return vreinterpretq_f64_s16(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, uint32_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(uint32x4_t a) noexcept { return vreinterpretq_f64_u32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, int32_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(int32x4_t a) noexcept { return vreinterpretq_f64_s32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, uint64_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(uint64x2_t a) noexcept { return vreinterpretq_f64_u64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, int64_t>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(int64x2_t a) noexcept { return vreinterpretq_f64_s64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, float>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(float32x4_t a) noexcept { return vreinterpretq_f64_f32(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, double> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE float64x2_t x_vreinterpretq(float64x2_t a) noexcept { return a; }

            // TODO(c++17): Make a single function with if constexpr  switch
            // Templating on the scalar type `T` is required because in some compilers (e.g. MSVC)
            // the vector types are all aliases of the same type.
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint8_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE uint8x16_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_u8_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int8_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE int8x16_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_s8_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint16_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE uint16x8_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_u16_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int16_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE int16x8_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_s16_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint32_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE uint32x4_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_u32_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int32_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE int32x4_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_s32_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, uint64_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE uint64x2_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_u64_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, int64_t> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE int64x2_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_s64_f64(a); }
            template <class R, class T, std::enable_if_t<std::is_same_v<R, float> && std::is_same_v<T, double>, int> = 0>
            XSIMD_INLINE float32x4_t x_vreinterpretq(float64x2_t a) noexcept { return vreinterpretq_f32_f64(a); }
        }

        template <class A, class T>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<T, A> const& arg, batch<double, A> const&, requires_arch<neon64>) noexcept
        {
            using register_type = typename batch<T, A>::register_type;
            return wrap::x_vreinterpretq<double, map_to_sized_type_t<T>>(register_type(arg));
        }

        template <class A, class R>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<double, A> const& arg, batch<R, A> const&, requires_arch<neon64>) noexcept
        {
            using src_register_type = typename batch<double, A>::register_type;
            return wrap::x_vreinterpretq<map_to_sized_type_t<R>, double>(src_register_type(arg));
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<double, A> const& arg, batch<double, A> const&, requires_arch<neon64>) noexcept
        {
            return arg;
        }

        /*********
         * isnan *
         *********/

        template <class A>
        XSIMD_INLINE batch_bool<double, A> isnan(batch<double, A> const& arg, requires_arch<neon64>) noexcept
        {
            return !(arg == arg);
        }

        /****************
         * rotate_left *
         ****************/
        template <size_t N, class A>
        XSIMD_INLINE batch<double, A> rotate_left(batch<double, A> const& a, requires_arch<neon64>) noexcept
        {
            return vextq_f64(a, a, N);
        }
    }

    template <typename T, class A, T... Values>
    struct batch_constant;

    namespace kernel
    {
        /*********************
         * swizzle (dynamic) *
         *********************/
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch<uint8_t, A> idx,
                                               requires_arch<neon64>) noexcept
        {
            return vqtbl1q_u8(self, idx);
        }

        template <class A>
        XSIMD_INLINE batch<int8_t, A> swizzle(batch<int8_t, A> const& self, batch<uint8_t, A> idx,
                                              requires_arch<neon64>) noexcept
        {
            return vqtbl1q_s8(self, idx);
        }

        template <class A>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self,
                                                batch<uint16_t, A> idx,
                                                requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            using index_type = batch<uint8_t, A>;
            return vreinterpretq_u16_u8(swizzle(batch_type(vreinterpretq_u8_u16(self)),
                                                index_type(vreinterpretq_u8_u16(idx * 0x0202 + 0x0100)),
                                                neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self,
                                               batch<uint16_t, A> idx,
                                               requires_arch<neon64>) noexcept
        {
            return bitwise_cast<int16_t>(swizzle(bitwise_cast<uint16_t>(self), idx, neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self,
                                                batch<uint32_t, A> idx,
                                                requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            using index_type = batch<uint8_t, A>;
            return vreinterpretq_u32_u8(swizzle(batch_type(vreinterpretq_u8_u32(self)),
                                                index_type(vreinterpretq_u8_u32(idx * 0x04040404 + 0x03020100)),
                                                neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self,
                                               batch<uint32_t, A> idx,
                                               requires_arch<neon64>) noexcept
        {
            return bitwise_cast<int32_t>(swizzle(bitwise_cast<uint32_t>(self), idx, neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self,
                                                batch<uint64_t, A> idx,
                                                requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            using index_type = batch<uint8_t, A>;
            return vreinterpretq_u64_u8(swizzle(batch_type(vreinterpretq_u8_u64(self)),
                                                index_type(vreinterpretq_u8_u64(idx * 0x0808080808080808ull + 0x0706050403020100ull)),
                                                neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self,
                                               batch<uint64_t, A> idx,
                                               requires_arch<neon64>) noexcept
        {
            return bitwise_cast<int64_t>(swizzle(bitwise_cast<uint64_t>(self), idx, neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self,
                                             batch<uint32_t, A> idx,
                                             requires_arch<neon64>) noexcept
        {
            return bitwise_cast<float>(swizzle(bitwise_cast<uint32_t>(self), idx, neon64 {}));
        }

        template <class A>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self,
                                              batch<uint64_t, A> idx,
                                              requires_arch<neon64>) noexcept
        {
            return bitwise_cast<double>(swizzle(bitwise_cast<uint64_t>(self), idx, neon64 {}));
        }

        /********************
         * swizzle (static) *
         ********************/

        namespace detail
        {
            using ::xsimd::batch_constant;

            template <class CB1, class CB2, class IS>
            struct index_burst_impl;

            template <typename T1, class A, typename T2, T2... V,
                      T2... incr>
            struct index_burst_impl<batch_constant<T1, A>, batch_constant<T2, A, V...>,
                                    std::integer_sequence<T2, incr...>>
            {
                using type = batch_constant<T2, A, V...>;
            };

            template <typename T1, class A, T1 V0, T1... V1,
                      typename T2, T2... V2, T2... incr>
            struct index_burst_impl<batch_constant<T1, A, V0, V1...>, batch_constant<T2, A, V2...>,
                                    std::integer_sequence<T2, incr...>>
            {
                using next_input = batch_constant<T1, A, V1...>;
                using next_output = batch_constant<T2, A, V2..., (V0 + incr)...>;
                using type = typename index_burst_impl<next_input, next_output, std::integer_sequence<T2, incr...>>::type;
            };

            template <class B, class T>
            struct index_burst;

            template <typename Tp, class A, Tp... V, typename T>
            struct index_burst<batch_constant<Tp, A, V...>, T>
            {
                static constexpr size_t mul = sizeof(Tp) / sizeof(T);
                using input = batch_constant<Tp, A, (mul * V)...>;
                using output = batch_constant<T, A>;
                using type = typename index_burst_impl<input, output, std::make_integer_sequence<T, mul>>::type;
            };

            template <class B, typename T>
            using index_burst_t = typename index_burst<B, T>::type;

            template <typename T, class B>
            XSIMD_INLINE index_burst_t<B, T> burst_index(B)
            {
                return index_burst_t<B, T>();
            }
        }

        template <class A, uint8_t V0, uint8_t V1, uint8_t V2, uint8_t V3, uint8_t V4, uint8_t V5, uint8_t V6, uint8_t V7,
                  uint8_t V8, uint8_t V9, uint8_t V10, uint8_t V11, uint8_t V12, uint8_t V13, uint8_t V14, uint8_t V15>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self,
                                               batch_constant<uint8_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15> idx,
                                               requires_arch<neon64>) noexcept
        {
            return vqtbl1q_u8(self, idx.as_batch());
        }

        template <class A, uint8_t V0, uint8_t V1, uint8_t V2, uint8_t V3, uint8_t V4, uint8_t V5, uint8_t V6, uint8_t V7,
                  uint8_t V8, uint8_t V9, uint8_t V10, uint8_t V11, uint8_t V12, uint8_t V13, uint8_t V14, uint8_t V15>
        XSIMD_INLINE batch<int8_t, A> swizzle(batch<int8_t, A> const& self,
                                              batch_constant<uint8_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15> idx,
                                              requires_arch<neon64>) noexcept
        {
            return vqtbl1q_s8(self, idx.as_batch());
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self,
                                                batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7> idx,
                                                requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            return vreinterpretq_u16_u8(swizzle<A>(batch_type(vreinterpretq_u8_u16(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint16_t V0, uint16_t V1, uint16_t V2, uint16_t V3, uint16_t V4, uint16_t V5, uint16_t V6, uint16_t V7>
        XSIMD_INLINE batch<int16_t, A> swizzle(batch<int16_t, A> const& self,
                                               batch_constant<uint16_t, A, V0, V1, V2, V3, V4, V5, V6, V7> idx,
                                               requires_arch<neon64>) noexcept
        {
            using batch_type = batch<int8_t, A>;
            return vreinterpretq_s16_s8(swizzle<A>(batch_type(vreinterpretq_s8_s16(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self,
                                                batch_constant<uint32_t, A, V0, V1, V2, V3> idx,
                                                requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            return vreinterpretq_u32_u8(swizzle<A>(batch_type(vreinterpretq_u8_u32(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self,
                                               batch_constant<uint32_t, A, V0, V1, V2, V3> idx,
                                               requires_arch<neon64>) noexcept
        {
            using batch_type = batch<int8_t, A>;
            return vreinterpretq_s32_s8(swizzle<A>(batch_type(vreinterpretq_s8_s32(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self,
                                                batch_constant<uint64_t, A, V0, V1> idx,
                                                requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            return vreinterpretq_u64_u8(swizzle<A>(batch_type(vreinterpretq_u8_u64(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self,
                                               batch_constant<uint64_t, A, V0, V1> idx,
                                               requires_arch<neon64>) noexcept
        {
            using batch_type = batch<int8_t, A>;
            return vreinterpretq_s64_s8(swizzle<A>(batch_type(vreinterpretq_s8_s64(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self,
                                             batch_constant<uint32_t, A, V0, V1, V2, V3> idx,
                                             requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            return vreinterpretq_f32_u8(swizzle<A>(batch_type(vreinterpretq_u8_f32(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self,
                                              batch_constant<uint64_t, A, V0, V1> idx,
                                              requires_arch<neon64>) noexcept
        {
            using batch_type = batch<uint8_t, A>;
            return vreinterpretq_f64_u8(swizzle<A>(batch_type(vreinterpretq_u8_f64(self)), detail::burst_index<uint8_t>(idx), A()));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3>
        XSIMD_INLINE batch<std::complex<float>, A> swizzle(batch<std::complex<float>, A> const& self,
                                                           batch_constant<uint32_t, A, V0, V1, V2, V3> idx,
                                                           requires_arch<neon64>) noexcept
        {
            return batch<std::complex<float>>(swizzle(self.real(), idx, A()), swizzle(self.imag(), idx, A()));
        }

        template <class A, uint64_t V0, uint64_t V1>
        XSIMD_INLINE batch<std::complex<double>, A> swizzle(batch<std::complex<double>, A> const& self,
                                                            batch_constant<uint64_t, A, V0, V1> idx,
                                                            requires_arch<neon64>) noexcept
        {
            return batch<std::complex<double>>(swizzle(self.real(), idx, A()), swizzle(self.imag(), idx, A()));
        }

        /*********
         * widen *
         *********/
        template <class A>
        XSIMD_INLINE std::array<batch<double, A>, 2> widen(batch<float, A> const& x, requires_arch<neon64>) noexcept
        {
            return { batch<double, A>(vcvt_f64_f32(vget_low_f32(x))), batch<double, A>(vcvt_high_f64_f32(x)) };
        }
    }
}

#endif
