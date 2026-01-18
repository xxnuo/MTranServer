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

#ifndef XSIMD_SVE_HPP
#define XSIMD_SVE_HPP

#include "../config/xsimd_config.hpp"
#include "../config/xsimd_macros.hpp"
#include "../types/xsimd_sve_register.hpp"

#include <complex>
#include <type_traits>

// Define a inline namespace with the explicit SVE vector size to avoid ODR violation
// When dynamically dispatching between different SVE sizes.
// While most code is safe from ODR violation as the size is already encoded in the
// register (and hence batch) types, utilities can quickly fall prone to this issue.
#define XSIMD_SVE_NAMESPACE XSIMD_CONCAT(sve, XSIMD_SVE_BITS)

namespace xsimd
{
    template <typename T, class A, T... Values>
    struct batch_constant;

    namespace kernel
    {
        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                using xsimd::index;
                using xsimd::types::detail::sve_vector_type;

                // predicate creation
                XSIMD_INLINE svbool_t ptrue_impl(index<1>) noexcept { return svptrue_b8(); }
                XSIMD_INLINE svbool_t ptrue_impl(index<2>) noexcept { return svptrue_b16(); }
                XSIMD_INLINE svbool_t ptrue_impl(index<4>) noexcept { return svptrue_b32(); }
                XSIMD_INLINE svbool_t ptrue_impl(index<8>) noexcept { return svptrue_b64(); }

                template <class T>
                XSIMD_INLINE svbool_t ptrue() noexcept { return ptrue_impl(index<sizeof(T)> {}); }

                // count active lanes in a predicate
                XSIMD_INLINE uint64_t pcount_impl(svbool_t p, index<1>) noexcept { return svcntp_b8(p, p); }
                XSIMD_INLINE uint64_t pcount_impl(svbool_t p, index<2>) noexcept { return svcntp_b16(p, p); }
                XSIMD_INLINE uint64_t pcount_impl(svbool_t p, index<4>) noexcept { return svcntp_b32(p, p); }
                XSIMD_INLINE uint64_t pcount_impl(svbool_t p, index<8>) noexcept { return svcntp_b64(p, p); }

                template <class T>
                XSIMD_INLINE uint64_t pcount(svbool_t p) noexcept { return pcount_impl(p, index<sizeof(T)> {}); }

                // enable for signed integers or floating points
                template <class T>
                using enable_signed_int_or_floating_point_t = std::enable_if_t<std::is_signed_v<T>, int>;

                // `sizeless` is the matching sizeless SVE type. xsimd stores SVE
                // vectors as fixed-size attributed types (arm_sve_vector_bits),
                // which clang treats as implicitly convertible to every sizeless
                // SVE type — including multi-vector tuples — making the overloaded
                // svreinterpret_*/svsel/etc. intrinsics ambiguous. Static-casting
                // to `sizeless` first collapses the overload set to the single
                // 1-vector candidate.
                template <class T>
                using sizeless_t = xsimd::types::detail::sizeless_sve_vector_type<T>;
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        /*********
         * Load *
         *********/

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> load_aligned(T const* src, convert<T>, requires_arch<sve>) noexcept
        {
            return svld1(detail_sve::ptrue<T>(), reinterpret_cast<map_to_sized_type_t<T> const*>(src));
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* src, convert<T>, requires_arch<sve>) noexcept
        {
            return load_aligned<A>(src, convert<T>(), sve {});
        }

        // load_masked (compile-time mask): forward to the runtime path; the
        // as_batch_bool predicate is correct at every SVE width. 32/64-bit ints
        // are excluded so the common float/double-reinterpreting dispatchers win
        // partial ordering (else ambiguous with requires_arch<A>).
        template <class A, class T, bool... Values, class Mode,
                  detail::enable_arithmetic_t<T> = 0,
                  std::enable_if_t<!(std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)), int> = 0>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, Values...> mask, convert<T>, Mode m, requires_arch<sve>) noexcept
        {
            return load_masked<A>(mem, mask.as_batch_bool(), convert<T> {}, m, sve {});
        }

        // load_masked (runtime mask)
        template <class A, class T, class Mode, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<sve>) noexcept
        {
            return svld1(mask, reinterpret_cast<map_to_sized_type_t<T> const*>(mem));
        }

        // load_complex
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<std::complex<T>, A> load_complex_aligned(std::complex<T> const* mem, convert<std::complex<T>>, requires_arch<sve>) noexcept
        {
            const T* buf = reinterpret_cast<const T*>(mem);
            const auto tmp = svld2(detail_sve::ptrue<T>(), buf);
            const auto real = svget2(tmp, 0);
            const auto imag = svget2(tmp, 1);
            return batch<std::complex<T>, A> { real, imag };
        }

        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<std::complex<T>, A> load_complex_unaligned(std::complex<T> const* mem, convert<std::complex<T>>, requires_arch<sve>) noexcept
        {
            return load_complex_aligned<A>(mem, convert<std::complex<T>> {}, sve {});
        }

        /*********
         * Store *
         *********/

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE void store_aligned(T* dst, batch<T, A> const& src, requires_arch<sve>) noexcept
        {
            svst1(detail_sve::ptrue<T>(), reinterpret_cast<map_to_sized_type_t<T>*>(dst), src);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE void store_unaligned(T* dst, batch<T, A> const& src, requires_arch<sve>) noexcept
        {
            store_aligned<A>(dst, src, sve {});
        }

        // store_masked (compile-time mask): forward to the runtime-mask
        // path for the same reason as load_masked above; same exclusion of
        // 32/64-bit integers to defer to the common dispatchers.
        template <class A, class T, bool... Values, class Mode,
                  detail::enable_arithmetic_t<T> = 0,
                  std::enable_if_t<!(std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)), int> = 0>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, Values...> mask, Mode m, requires_arch<sve>) noexcept
        {
            store_masked<A>(mem, src, mask.as_batch_bool(), m, sve {});
        }

        // store_masked (runtime mask)
        template <class A, class T, class Mode, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<sve>) noexcept
        {
            svst1(mask, reinterpret_cast<map_to_sized_type_t<T>*>(mem), src);
        }

        // store_complex
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE void store_complex_aligned(std::complex<T>* dst, batch<std::complex<T>, A> const& src, requires_arch<sve>) noexcept
        {
            using v2type = std::conditional_t<(sizeof(T) == 4), svfloat32x2_t, svfloat64x2_t>;
            v2type tmp {};
            tmp = svset2(tmp, 0, src.real());
            tmp = svset2(tmp, 1, src.imag());
            T* buf = reinterpret_cast<T*>(dst);
            svst2(detail_sve::ptrue<T>(), buf, tmp);
        }

        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE void store_complex_unaligned(std::complex<T>* dst, batch<std::complex<T>, A> const& src, requires_arch<sve>) noexcept
        {
            store_complex_aligned(dst, src, sve {});
        }

        /******************
         * scatter/gather *
         ******************/

        namespace detail_sve
        {
            template <class T, class U>
            using enable_sg_t = std::enable_if_t<(sizeof(T) == sizeof(U) && (sizeof(T) == 4 || sizeof(T) == 8)), int>;
        }

        // scatter
        template <class A, class T, class U, detail_sve::enable_sg_t<T, U> = 0>
        XSIMD_INLINE void scatter(batch<T, A> const& src, T* dst, batch<U, A> const& index, kernel::requires_arch<sve>) noexcept
        {
            svst1_scatter_index(detail_sve::ptrue<T>(), dst, index.data, src.data);
        }

        // gather
        template <class A, class T, class U, detail_sve::enable_sg_t<T, U> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index, kernel::requires_arch<sve>) noexcept
        {
            return svld1_gather_index(detail_sve::ptrue<T>(), src, index.data);
        }

        /********************
         * Scalar to vector *
         ********************/

        // broadcast
        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_u8(uint8_t(arg));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_s8(int8_t(arg));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_u16(uint16_t(arg));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_s16(int16_t(arg));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_u32(uint32_t(arg));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_s32(int32_t(arg));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_u64(uint64_t(arg));
        }

        template <class A, class T, detail::enable_sized_signed_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T arg, requires_arch<sve>) noexcept
        {
            return svdup_n_s64(int64_t(arg));
        }

        template <class A>
        XSIMD_INLINE batch<float, A> broadcast(float arg, requires_arch<sve>) noexcept
        {
            return svdup_n_f32(arg);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> broadcast(double arg, requires_arch<sve>) noexcept
        {
            return svdup_n_f64(arg);
        }

        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<sve>) noexcept
        {
            return broadcast<sve>(val, sve {});
        }

        /**************
         * Arithmetic *
         **************/

        // add
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svadd_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // sadd
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svqadd(lhs, rhs);
        }

        // sub
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svsub_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // ssub
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svqsub(lhs, rhs);
        }

        // mul
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svmul_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // mul_hi
        template <class A, class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
        XSIMD_INLINE batch<T, A> mul_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svmulh_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // div
        template <class A, class T, std::enable_if_t<sizeof(T) >= 4, int> = 0>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svdiv_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // max
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svmax_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // min
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svmin_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // neg
        template <class A, class T, detail::enable_sized_unsigned_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svreinterpret_u8(svneg_x(detail_sve::ptrue<T>(), svreinterpret_s8(static_cast<detail_sve::sizeless_t<T>>(arg))));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svreinterpret_u16(svneg_x(detail_sve::ptrue<T>(), svreinterpret_s16(static_cast<detail_sve::sizeless_t<T>>(arg))));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svreinterpret_u32(svneg_x(detail_sve::ptrue<T>(), svreinterpret_s32(static_cast<detail_sve::sizeless_t<T>>(arg))));
        }

        template <class A, class T, detail::enable_sized_unsigned_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svreinterpret_u64(svneg_x(detail_sve::ptrue<T>(), svreinterpret_s64(static_cast<detail_sve::sizeless_t<T>>(arg))));
        }

        template <class A, class T, detail::enable_signed_numeral_t<T> = 0>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svneg_x(detail_sve::ptrue<T>(), arg);
        }

        // abs
        template <class A, class T, detail::enable_unsigned_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return arg;
        }

        template <class A, class T, detail::enable_signed_numeral_t<T> = 0>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svabs_x(detail_sve::ptrue<T>(), arg);
        }

        // fma: x * y + z
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> fma(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<sve>) noexcept
        {
            return svmad_x(detail_sve::ptrue<T>(), x, y, z);
        }

        // fnma: z - x * y
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> fnma(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<sve>) noexcept
        {
            return svmsb_x(detail_sve::ptrue<T>(), x, y, z);
        }

        // fms: x * y - z
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> fms(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<sve>) noexcept
        {
            return -fnma(x, y, z, sve {});
        }

        // fnms: - x * y - z
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> fnms(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<sve>) noexcept
        {
            return -fma(x, y, z, sve {});
        }

        /**********************
         * Logical operations *
         **********************/

        // bitwise_and
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svand_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_and(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(lhs));
            const auto rhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(rhs));
            const auto result_bits = svand_x(detail_sve::ptrue<float>(), lhs_bits, rhs_bits);
            return svreinterpret_f32(result_bits);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_and(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(lhs));
            const auto rhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(rhs));
            const auto result_bits = svand_x(detail_sve::ptrue<double>(), lhs_bits, rhs_bits);
            return svreinterpret_f64(result_bits);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svand_z(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // bitwise_andnot
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svbic_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_andnot(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(lhs));
            const auto rhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(rhs));
            const auto result_bits = svbic_x(detail_sve::ptrue<float>(), lhs_bits, rhs_bits);
            return svreinterpret_f32(result_bits);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_andnot(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(lhs));
            const auto rhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(rhs));
            const auto result_bits = svbic_x(detail_sve::ptrue<double>(), lhs_bits, rhs_bits);
            return svreinterpret_f64(result_bits);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svbic_z(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // bitwise_or
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svorr_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_or(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(lhs));
            const auto rhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(rhs));
            const auto result_bits = svorr_x(detail_sve::ptrue<float>(), lhs_bits, rhs_bits);
            return svreinterpret_f32(result_bits);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_or(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(lhs));
            const auto rhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(rhs));
            const auto result_bits = svorr_x(detail_sve::ptrue<double>(), lhs_bits, rhs_bits);
            return svreinterpret_f64(result_bits);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svorr_z(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // bitwise_xor
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return sveor_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_xor(batch<float, A> const& lhs, batch<float, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(lhs));
            const auto rhs_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(rhs));
            const auto result_bits = sveor_x(detail_sve::ptrue<float>(), lhs_bits, rhs_bits);
            return svreinterpret_f32(result_bits);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_xor(batch<double, A> const& lhs, batch<double, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto lhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(lhs));
            const auto rhs_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(rhs));
            const auto result_bits = sveor_x(detail_sve::ptrue<double>(), lhs_bits, rhs_bits);
            return svreinterpret_f64(result_bits);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_xor(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return sveor_z(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // bitwise_not
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svnot_x(detail_sve::ptrue<T>(), arg);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_not(batch<float, A> const& arg, requires_arch<sve>) noexcept
        {
            const auto arg_bits = svreinterpret_u32(static_cast<detail_sve::sizeless_t<float>>(arg));
            const auto result_bits = svnot_x(detail_sve::ptrue<float>(), arg_bits);
            return svreinterpret_f32(result_bits);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_not(batch<double, A> const& arg, requires_arch<sve>) noexcept
        {
            const auto arg_bits = svreinterpret_u64(static_cast<detail_sve::sizeless_t<double>>(arg));
            const auto result_bits = svnot_x(detail_sve::ptrue<double>(), arg_bits);
            return svreinterpret_f64(result_bits);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svnot_z(detail_sve::ptrue<T>(), arg);
        }

        /**********
         * Shifts *
         **********/

        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                template <class A, class T, class U>
                XSIMD_INLINE batch<U, A> to_unsigned_batch_impl(batch<T, A> const& arg, index<1>) noexcept
                {
                    return svreinterpret_u8(static_cast<sizeless_t<T>>(arg));
                }

                template <class A, class T, class U>
                XSIMD_INLINE batch<U, A> to_unsigned_batch_impl(batch<T, A> const& arg, index<2>) noexcept
                {
                    return svreinterpret_u16(static_cast<sizeless_t<T>>(arg));
                }

                template <class A, class T, class U>
                XSIMD_INLINE batch<U, A> to_unsigned_batch_impl(batch<T, A> const& arg, index<4>) noexcept
                {
                    return svreinterpret_u32(static_cast<sizeless_t<T>>(arg));
                }

                template <class A, class T, class U>
                XSIMD_INLINE batch<U, A> to_unsigned_batch_impl(batch<T, A> const& arg, index<8>) noexcept
                {
                    return svreinterpret_u64(static_cast<sizeless_t<T>>(arg));
                }

                template <class A, class T, class U = as_unsigned_integer_t<T>>
                XSIMD_INLINE batch<U, A> to_unsigned_batch(batch<T, A> const& arg) noexcept
                {
                    return to_unsigned_batch_impl<A, T, U>(arg, index<sizeof(T)> {});
                }
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        // bitwise_lshift
        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& arg, int n, requires_arch<sve>) noexcept
        {
            constexpr std::size_t size = sizeof(typename batch<T, A>::value_type) * 8;
            assert(0 <= n && static_cast<std::size_t>(n) < size && "index in bounds");
            return svlsl_x(detail_sve::ptrue<T>(), arg, n);
        }

        template <class A, class T, detail::enable_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svlsl_x(detail_sve::ptrue<T>(), lhs, detail_sve::to_unsigned_batch<A, T>(rhs));
        }

        // bitwise_rshift
        template <class A, class T, detail::enable_unsigned_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& arg, int n, requires_arch<sve>) noexcept
        {
            constexpr std::size_t size = sizeof(typename batch<T, A>::value_type) * 8;
            assert(0 <= n && static_cast<std::size_t>(n) < size && "index in bounds");
            return svlsr_x(detail_sve::ptrue<T>(), arg, static_cast<T>(n));
        }

        template <class A, class T, detail::enable_unsigned_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svlsr_x(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A, class T, detail::enable_signed_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& arg, int n, requires_arch<sve>) noexcept
        {
            constexpr std::size_t size = sizeof(typename batch<T, A>::value_type) * 8;
            assert(0 <= n && static_cast<std::size_t>(n) < size && "index in bounds");
            return svasr_x(detail_sve::ptrue<T>(), arg, static_cast<as_unsigned_integer_t<T>>(n));
        }

        template <class A, class T, detail::enable_signed_integral_t<T> = 0>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svasr_x(detail_sve::ptrue<T>(), lhs, detail_sve::to_unsigned_batch<A, T>(rhs));
        }

        /**************
         * Reductions *
         **************/

        // reduce_add
        template <class A, class T, class V = typename batch<T, A>::value_type, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE V reduce_add(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            // sve integer reduction results are promoted to 64 bits
            return static_cast<V>(svaddv(detail_sve::ptrue<T>(), arg));
        }

        // reduce_max
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE T reduce_max(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svmaxv(detail_sve::ptrue<T>(), arg);
        }

        // reduce_min
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE T reduce_min(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svminv(detail_sve::ptrue<T>(), arg);
        }

        // haddp
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> haddp(const batch<T, A>* row, requires_arch<sve>) noexcept
        {
            constexpr std::size_t size = batch<T, A>::size;
            T sums[size];
            for (std::size_t i = 0; i < size; ++i)
            {
                sums[i] = reduce_add(row[i], sve {});
            }
            return svld1(detail_sve::ptrue<T>(), sums);
        }

        /***************
         * Comparisons *
         ***************/

        // eq
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svcmpeq(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            const auto neq_result = sveor_z(detail_sve::ptrue<T>(), lhs, rhs);
            return svnot_z(detail_sve::ptrue<T>(), neq_result);
        }

        // neq
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svcmpne(detail_sve::ptrue<T>(), lhs, rhs);
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& lhs, batch_bool<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return sveor_z(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // lt
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svcmplt(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // le
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svcmple(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // gt
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svcmpgt(detail_sve::ptrue<T>(), lhs, rhs);
        }

        // ge
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svcmpge(detail_sve::ptrue<T>(), lhs, rhs);
        }

        /***************
         * Permutation *
         ***************/

        //  rotate_left
        template <size_t N, class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> rotate_left(batch<T, A> const& a, requires_arch<sve>) noexcept
        {
            return svext(a, a, N);
        }

        // swizzle (dynamic)
        template <class A, class T, class I>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& arg, batch<I, A> indices, requires_arch<sve>) noexcept
        {
            return svtbl(arg, indices);
        }

        template <class A, class T, class I>
        XSIMD_INLINE batch<std::complex<T>, A> swizzle(batch<std::complex<T>, A> const& self,
                                                       batch<I, A> indices,
                                                       requires_arch<sve>) noexcept
        {
            const auto real = swizzle(self.real(), indices, sve {});
            const auto imag = swizzle(self.imag(), indices, sve {});
            return batch<std::complex<T>>(real, imag);
        }

        // swizzle (static)
        template <class A, class T, class I, I... idx>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& arg, batch_constant<I, A, idx...> indices, requires_arch<sve>) noexcept
        {
            static_assert(batch<T, A>::size == sizeof...(idx), "invalid swizzle indices");
            return swizzle(arg, indices.as_batch(), sve {});
        }

        template <class A, class T, class I, I... idx>
        XSIMD_INLINE batch<std::complex<T>, A> swizzle(batch<std::complex<T>, A> const& arg,
                                                       batch_constant<I, A, idx...> indices,
                                                       requires_arch<sve>) noexcept
        {
            static_assert(batch<std::complex<T>, A>::size == sizeof...(idx), "invalid swizzle indices");
            return swizzle(arg, indices.as_batch(), sve {});
        }

        /*************
         * Selection *
         *************/

        // extract_pair
        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                template <class A, class T>
                XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const&, batch<T, A> const& /*rhs*/, std::size_t, std::index_sequence<>) noexcept
                {
                    assert(false && "extract_pair out of bounds");
                    return batch<T, A> {};
                }

                template <class A, class T, size_t I, size_t... Is>
                XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, std::index_sequence<I, Is...>) noexcept
                {
                    if (n == I)
                    {
                        return svext(rhs, lhs, I);
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
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& lhs, batch<T, A> const& rhs, std::size_t n, requires_arch<sve>) noexcept
        {
            constexpr std::size_t size = batch<T, A>::size;
            assert(n < size && "index in bounds");
            return detail_sve::extract_pair_impl(lhs, rhs, n, std::make_index_sequence<size>());
        }

        // select
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& a, batch<T, A> const& b, requires_arch<sve>) noexcept
        {
            return svsel(cond, static_cast<detail_sve::sizeless_t<T>>(a), static_cast<detail_sve::sizeless_t<T>>(b));
        }

        template <class A, class T, bool... b>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, b...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<sve>) noexcept
        {
            return select(batch_bool<T, A> { b... }, true_br, false_br, sve {});
        }

        // zip_lo
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> zip_lo(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svzip1(lhs, rhs);
        }

        // zip_hi
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> zip_hi(batch<T, A> const& lhs, batch<T, A> const& rhs, requires_arch<sve>) noexcept
        {
            return svzip2(lhs, rhs);
        }

        /*****************************
         * Floating-point arithmetic *
         *****************************/

        // rsqrt
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> rsqrt(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svrsqrte(arg);
        }

        // sqrt
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> sqrt(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svsqrt_x(detail_sve::ptrue<T>(), arg);
        }

        // reciprocal
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> reciprocal(const batch<T, A>& arg, requires_arch<sve>) noexcept
        {
            return svrecpe(arg);
        }

        /******************************
         * Floating-point conversions *
         ******************************/

        // fast_cast
        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                template <class A, class T, detail::enable_sized_integral_t<T, 4> = 0>
                XSIMD_INLINE batch<float, A> fast_cast(batch<T, A> const& arg, batch<float, A> const&, requires_arch<sve>) noexcept
                {
                    return svcvt_f32_x(detail_sve::ptrue<T>(), arg);
                }

                template <class A, class T, detail::enable_sized_integral_t<T, 8> = 0>
                XSIMD_INLINE batch<double, A> fast_cast(batch<T, A> const& arg, batch<double, A> const&, requires_arch<sve>) noexcept
                {
                    return svcvt_f64_x(detail_sve::ptrue<T>(), arg);
                }

                template <class A>
                XSIMD_INLINE batch<int32_t, A> fast_cast(batch<float, A> const& arg, batch<int32_t, A> const&, requires_arch<sve>) noexcept
                {
                    return svcvt_s32_x(detail_sve::ptrue<float>(), arg);
                }

                template <class A>
                XSIMD_INLINE batch<uint32_t, A> fast_cast(batch<float, A> const& arg, batch<uint32_t, A> const&, requires_arch<sve>) noexcept
                {
                    return svcvt_u32_x(detail_sve::ptrue<float>(), arg);
                }

                template <class A>
                XSIMD_INLINE batch<int64_t, A> fast_cast(batch<double, A> const& arg, batch<int64_t, A> const&, requires_arch<sve>) noexcept
                {
                    return svcvt_s64_x(detail_sve::ptrue<double>(), arg);
                }

                template <class A>
                XSIMD_INLINE batch<uint64_t, A> fast_cast(batch<double, A> const& arg, batch<uint64_t, A> const&, requires_arch<sve>) noexcept
                {
                    return svcvt_u64_x(detail_sve::ptrue<double>(), arg);
                }
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        /*********
         * Miscs *
         *********/

        // set
        template <class A, class T, class... Args>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<sve>, Args... args) noexcept
        {
            return detail_sve::sve_vector_type<T> { args... };
        }

        template <class A, class T, class... Args>
        XSIMD_INLINE batch<std::complex<T>, A> set(batch<std::complex<T>, A> const&, requires_arch<sve>,
                                                   Args... args_complex) noexcept
        {
            return batch<std::complex<T>>(detail_sve::sve_vector_type<T> { args_complex.real()... },
                                          detail_sve::sve_vector_type<T> { args_complex.imag()... });
        }

        template <class A, class T, class... Args>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<sve>, Args... args) noexcept
        {
            using U = as_unsigned_integer_t<T>;
            const auto values = detail_sve::sve_vector_type<U> { static_cast<U>(args)... };
            const auto zero = broadcast<A, U>(static_cast<U>(0), sve {});
            return svcmpne(detail_sve::ptrue<T>(), values, zero);
        }

        // insert
        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                // generate index sequence (iota)
                XSIMD_INLINE svuint8_t iota_impl(index<1>) noexcept { return svindex_u8(0, 1); }
                XSIMD_INLINE svuint16_t iota_impl(index<2>) noexcept { return svindex_u16(0, 1); }
                XSIMD_INLINE svuint32_t iota_impl(index<4>) noexcept { return svindex_u32(0, 1); }
                XSIMD_INLINE svuint64_t iota_impl(index<8>) noexcept { return svindex_u64(0, 1); }

                template <class T, class V = sve_vector_type<as_unsigned_integer_t<T>>>
                XSIMD_INLINE V iota() noexcept { return iota_impl(index<sizeof(T)> {}); }
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        template <class A, class T, size_t I, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& arg, T val, index<I>, requires_arch<sve>) noexcept
        {
            // create a predicate with only the I-th lane activated
            const auto iota = detail_sve::iota<T>();
            const auto index_predicate = svcmpeq(detail_sve::ptrue<T>(), iota, static_cast<as_unsigned_integer_t<T>>(I));
            return svsel(index_predicate, static_cast<detail_sve::sizeless_t<T>>(broadcast<A, T>(val, sve {})), static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        // first
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<sve>) noexcept
        {
            return self.data[0];
        }

        // all
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE bool all(batch_bool<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return detail_sve::pcount<T>(arg) == batch_bool<T, A>::size;
        }

        // any
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE bool any(batch_bool<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svptest_any(arg, arg);
        }

        // bitwise_cast
        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_unsigned_t<R, 1> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_u8(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_signed_t<R, 1> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_s8(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_unsigned_t<R, 2> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_u16(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_signed_t<R, 2> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_s16(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_unsigned_t<R, 4> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_u32(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_signed_t<R, 4> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_s32(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_unsigned_t<R, 8> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_u64(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, class R, detail::enable_arithmetic_t<T> = 0, detail::enable_sized_signed_t<R, 8> = 0>
        XSIMD_INLINE batch<R, A> bitwise_cast(batch<T, A> const& arg, batch<R, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_s64(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<T, A> const& arg, batch<float, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_f32(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<T, A> const& arg, batch<double, A> const&, requires_arch<sve>) noexcept
        {
            return svreinterpret_f64(static_cast<detail_sve::sizeless_t<T>>(arg));
        }

        // batch_bool_cast
        template <class A, class T_out, class T_in, detail::enable_arithmetic_t<T_in> = 0>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& arg, batch_bool<T_out, A> const&, requires_arch<sve>) noexcept
        {
            return arg.data;
        }

        // from_bool
        template <class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return select(arg, batch<T, A>(1), batch<T, A>(0));
        }

        // slide_left
        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                template <size_t N>
                struct slider_left
                {
                    template <class A, class T>
                    XSIMD_INLINE batch<T, A> operator()(batch<T, A> const& arg) noexcept
                    {
                        using u8_vector = batch<uint8_t, A>;
                        const auto left = svdup_n_u8(0);
                        const auto right = bitwise_cast(arg, u8_vector {}, sve {}).data;
                        const u8_vector result(svext(left, right, u8_vector::size - N));
                        return bitwise_cast(result, batch<T, A> {}, sve {});
                    }
                };

                template <>
                struct slider_left<0>
                {
                    template <class A, class T>
                    XSIMD_INLINE batch<T, A> operator()(batch<T, A> const& arg) noexcept
                    {
                        return arg;
                    }
                };
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        template <size_t N, class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return detail_sve::slider_left<N>()(arg);
        }

        // slide_right
        namespace detail_sve
        {
            inline namespace XSIMD_SVE_NAMESPACE
            {
                template <size_t N>
                struct slider_right
                {
                    template <class A, class T>
                    XSIMD_INLINE batch<T, A> operator()(batch<T, A> const& arg) noexcept
                    {
                        using u8_vector = batch<uint8_t, A>;
                        const auto left = bitwise_cast(arg, u8_vector {}, sve {}).data;
                        const auto right = svdup_n_u8(0);
                        const u8_vector result(svext(left, right, N));
                        return bitwise_cast(result, batch<T, A> {}, sve {});
                    }
                };

                template <>
                struct slider_right<batch<uint8_t, sve>::size>
                {
                    template <class A, class T>
                    XSIMD_INLINE batch<T, A> operator()(batch<T, A> const&) noexcept
                    {
                        return batch<T, A> {};
                    }
                };
            } // namespace XSIMD_SVE_NAMESPACE
        } // namespace detail_sve

        template <size_t N, class A, class T, detail::enable_arithmetic_t<T> = 0>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return detail_sve::slider_right<N>()(arg);
        }

        // isnan
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch_bool<T, A> isnan(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return !(arg == arg);
        }

        // nearbyint
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> nearbyint(batch<T, A> const& arg, requires_arch<sve>) noexcept
        {
            return svrintx_x(detail_sve::ptrue<T>(), arg);
        }

        // nearbyint_as_int
        template <class A>
        XSIMD_INLINE batch<int32_t, A> nearbyint_as_int(batch<float, A> const& arg, requires_arch<sve>) noexcept
        {
            const auto nearest = svrintx_x(detail_sve::ptrue<float>(), arg);
            return svcvt_s32_x(detail_sve::ptrue<float>(), nearest);
        }

        template <class A>
        XSIMD_INLINE batch<int64_t, A> nearbyint_as_int(batch<double, A> const& arg, requires_arch<sve>) noexcept
        {
            const auto nearest = svrintx_x(detail_sve::ptrue<double>(), arg);
            return svcvt_s64_x(detail_sve::ptrue<double>(), nearest);
        }

        // ldexp
        template <class A, class T, detail::enable_floating_point_t<T> = 0>
        XSIMD_INLINE batch<T, A> ldexp(const batch<T, A>& x, const batch<as_integer_t<T>, A>& exp, requires_arch<sve>) noexcept
        {
            return svscale_x(detail_sve::ptrue<T>(), x, exp);
        }

    } // namespace kernel
} // namespace xsimd

#endif
