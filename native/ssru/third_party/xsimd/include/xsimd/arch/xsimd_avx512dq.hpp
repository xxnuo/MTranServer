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

#ifndef XSIMD_AVX512DQ_HPP
#define XSIMD_AVX512DQ_HPP

#include "../types/xsimd_avx512dq_register.hpp"

namespace xsimd
{

    namespace kernel
    {
        using namespace types;

        // load_masked
        template <class A, bool... Values, class Mode>
        XSIMD_INLINE batch<int32_t, A> load_masked(int32_t const* mem, batch_bool_constant<int32_t, A, Values...> mask, convert<int32_t>, Mode, requires_arch<avx512dq>) noexcept
        {
            if constexpr (mask.countr_zero() >= 8)
            {
                constexpr auto mhi = ::xsimd::detail::upper_half<avx2>(mask);
                const auto hi = load_masked<avx2>(mem + 8, mhi, convert<int32_t> {}, Mode {}, avx2 {});
                return _mm512_inserti32x8(_mm512_setzero_si512(), hi, 1);
            }
            return load_masked<A>(mem, mask, convert<int32_t> {}, Mode {}, avx512f {});
        }

        template <class A, bool... Values, class Mode>
        XSIMD_INLINE batch<float, A> load_masked(float const* mem, batch_bool_constant<float, A, Values...> mask, convert<float>, Mode, requires_arch<avx512dq>) noexcept
        {
            if constexpr (mask.countr_zero() >= 8)
            {
                constexpr auto mhi = ::xsimd::detail::upper_half<avx2>(mask);
                const auto hi = load_masked<avx2>(mem + 8, mhi, convert<float> {}, Mode {}, avx2 {});
                return _mm512_insertf32x8(_mm512_setzero_ps(), hi, 1);
            }
            return load_masked<A>(mem, mask, convert<float> {}, Mode {}, avx512f {});
        }

        // bitwise_and
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_and(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_and_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_and(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_and_pd(self, other);
        }

        // bitwise_andnot
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_andnot(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_andnot_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_andnot(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_andnot_pd(other, self);
        }

        // bitwise_not
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_not(batch<float, A> const& self, requires_arch<avx512dq>) noexcept
        {
            return _mm512_xor_ps(self, _mm512_castsi512_ps(_mm512_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_not(batch<double, A> const& self, requires_arch<avx512dq>) noexcept
        {
            return _mm512_xor_pd(self, _mm512_castsi512_pd(_mm512_set1_epi32(-1)));
        }

        // bitwise_or
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_or(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_or_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_or(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_or_pd(self, other);
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512dq>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data | other.data);
        }

        // bitwise_xor
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_xor(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_xor_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_xor(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_xor_pd(self, other);
        }

        // haddp
        template <class A>
        XSIMD_INLINE batch<float, A> haddp(batch<float, A> const* row, requires_arch<avx512dq>) noexcept
        {
            // The following folds over the vector once:
            // tmp1 = [a0..8, b0..8]
            // tmp2 = [a8..f, b8..f]
#define XSIMD_AVX512_HADDP_STEP1(I, a, b)                                \
    batch<float, avx512dq> res##I;                                       \
    {                                                                    \
        auto tmp1 = _mm512_shuffle_f32x4(a, b, _MM_SHUFFLE(1, 0, 1, 0)); \
        auto tmp2 = _mm512_shuffle_f32x4(a, b, _MM_SHUFFLE(3, 2, 3, 2)); \
        res##I = _mm512_add_ps(tmp1, tmp2);                              \
    }

            XSIMD_AVX512_HADDP_STEP1(0, row[0], row[2]);
            XSIMD_AVX512_HADDP_STEP1(1, row[4], row[6]);
            XSIMD_AVX512_HADDP_STEP1(2, row[1], row[3]);
            XSIMD_AVX512_HADDP_STEP1(3, row[5], row[7]);
            XSIMD_AVX512_HADDP_STEP1(4, row[8], row[10]);
            XSIMD_AVX512_HADDP_STEP1(5, row[12], row[14]);
            XSIMD_AVX512_HADDP_STEP1(6, row[9], row[11]);
            XSIMD_AVX512_HADDP_STEP1(7, row[13], row[15]);

#undef XSIMD_AVX512_HADDP_STEP1

            // The following flds the code and shuffles so that hadd_ps produces the correct result
            // tmp1 = [a0..4,  a8..12,  b0..4,  b8..12] (same for tmp3)
            // tmp2 = [a5..8, a12..16, b5..8, b12..16]  (same for tmp4)
            // tmp5 = [r1[0], r1[2], r2[0], r2[2], r1[4], r1[6] ...
#define XSIMD_AVX512_HADDP_STEP2(I, a, b, c, d)                               \
    batch<float, avx2> halfx##I;                                              \
    {                                                                         \
        auto tmp1 = _mm512_shuffle_f32x4(a, b, _MM_SHUFFLE(2, 0, 2, 0));      \
        auto tmp2 = _mm512_shuffle_f32x4(a, b, _MM_SHUFFLE(3, 1, 3, 1));      \
                                                                              \
        auto resx1 = _mm512_add_ps(tmp1, tmp2);                               \
                                                                              \
        auto tmp3 = _mm512_shuffle_f32x4(c, d, _MM_SHUFFLE(2, 0, 2, 0));      \
        auto tmp4 = _mm512_shuffle_f32x4(c, d, _MM_SHUFFLE(3, 1, 3, 1));      \
                                                                              \
        auto resx2 = _mm512_add_ps(tmp3, tmp4);                               \
                                                                              \
        auto tmp5 = _mm512_shuffle_ps(resx1, resx2, _MM_SHUFFLE(2, 0, 2, 0)); \
        auto tmp6 = _mm512_shuffle_ps(resx1, resx2, _MM_SHUFFLE(3, 1, 3, 1)); \
                                                                              \
        auto resx3 = _mm512_add_ps(tmp5, tmp6);                               \
                                                                              \
        halfx##I = _mm256_hadd_ps(_mm512_extractf32x8_ps(resx3, 0),           \
                                  _mm512_extractf32x8_ps(resx3, 1));          \
    }

            XSIMD_AVX512_HADDP_STEP2(0, res0, res1, res2, res3);
            XSIMD_AVX512_HADDP_STEP2(1, res4, res5, res6, res7);

#undef XSIMD_AVX512_HADDP_STEP2

            auto concat = _mm512_castps256_ps512(halfx0);
            concat = _mm512_insertf32x8(concat, halfx1, 1);
            return concat;
        }

        // ldexp
        template <class A>
        XSIMD_INLINE batch<double, A> ldexp(const batch<double, A>& self, const batch<as_integer_t<double>, A>& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_scalef_pd(self, _mm512_cvtepi64_pd(other));
        }

        // mul
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> mul(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_mullo_epi64(self, other);
        }

        template <class A>
        XSIMD_INLINE batch<int64_t, A> mul(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<avx512dq>) noexcept
        {
            return _mm512_mullo_epi64(self, other);
        }

        // nearbyint_as_int
        template <class A>
        XSIMD_INLINE batch<int64_t, A> nearbyint_as_int(batch<double, A> const& self,
                                                        requires_arch<avx512dq>) noexcept
        {
            return _mm512_cvtpd_epi64(self);
        }

        // reduce_add
        template <class A>
        XSIMD_INLINE float reduce_add(batch<float, A> const& rhs, requires_arch<avx512dq>) noexcept
        {
            __m256 tmp1 = _mm512_extractf32x8_ps(rhs, 1);
            __m256 tmp2 = _mm512_extractf32x8_ps(rhs, 0);
            __m256 res1 = _mm256_add_ps(tmp1, tmp2);
            return reduce_add(batch<float, avx2>(res1), avx2 {});
        }

        // reduce_mul
        template <class A>
        XSIMD_INLINE float reduce_mul(batch<float, A> const& rhs, requires_arch<avx512dq>) noexcept
        {
            __m256 tmp1 = _mm512_extractf32x8_ps(rhs, 1);
            __m256 tmp2 = _mm512_extractf32x8_ps(rhs, 0);
            __m256 res1 = _mm256_mul_ps(tmp1, tmp2);
            return reduce_mul(batch<float, avx2>(res1), avx2 {});
        }

        // swizzle constant mask
        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3, uint32_t V4, uint32_t V5, uint32_t V6, uint32_t V7,
                  uint32_t V8, uint32_t V9, uint32_t V10, uint32_t V11, uint32_t V12, uint32_t V13, uint32_t V14, uint32_t V15>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self,
                                             batch_constant<uint32_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15> mask,
                                             requires_arch<avx512dq>) noexcept
        {
            constexpr bool dup_lo = detail::is_dup_lo(mask);
            constexpr bool dup_hi = detail::is_dup_hi(mask);

            if constexpr (dup_lo || dup_hi)
            {
                const batch<float, avx2> half = _mm512_extractf32x8_ps(self, dup_lo ? 0 : 1);
                constexpr std::conditional_t<dup_lo, batch_constant<uint32_t, avx2, V0 % 8, V1 % 8, V2 % 8, V3 % 8, V4 % 8, V5 % 8, V6 % 8, V7 % 8>,
                                             batch_constant<uint32_t, avx2, V8 % 8, V9 % 8, V10 % 8, V11 % 8, V12 % 8, V13 % 8, V14 % 8, V15 % 8>>
                    half_mask {};
                auto permuted = swizzle(half, half_mask, avx2 {});
                // merge the two slices into an AVX512F register:
                return _mm512_broadcast_f32x8(permuted); // duplicates the 256-bit perm into both halves
            }
            return swizzle(self, mask, avx512f {});
        }

        // convert
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<double, A> fast_cast(batch<int64_t, A> const& self, batch<double, A> const&, requires_arch<avx512dq>) noexcept
            {
                return _mm512_cvtepi64_pd(self);
            }

            template <class A>
            XSIMD_INLINE batch<int64_t, A> fast_cast(batch<double, A> const& self, batch<int64_t, A> const&, requires_arch<avx512dq>) noexcept
            {
                return _mm512_cvttpd_epi64(self);
            }

        }

    }

}

#endif
