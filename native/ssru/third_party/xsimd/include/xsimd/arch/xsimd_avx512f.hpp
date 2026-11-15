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

#ifndef XSIMD_AVX512F_HPP
#define XSIMD_AVX512F_HPP

#include "../types/xsimd_avx512f_register.hpp"
#include "../types/xsimd_batch_constant.hpp"
#include "../utils/bits.hpp"

#include <complex>
#include <limits>
#include <type_traits>

namespace xsimd
{

    namespace kernel
    {
        using namespace types;

        // fwd
        template <class A, class T, class Mask>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, Mask const& mask, requires_arch<common>) noexcept;
        template <class A, class T, class Mask>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, Mask const& mask, requires_arch<common>) noexcept;
        template <class A, class T, size_t I>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<common>) noexcept;
        template <class A, class T, class ITy, ITy... Is>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<ITy, A, Is...>, requires_arch<common>) noexcept;
        template <class A>
        XSIMD_INLINE void transpose(batch<uint16_t, A>* matrix_begin, batch<uint16_t, A>* matrix_end, requires_arch<common>) noexcept;
        template <class A>
        XSIMD_INLINE void transpose(batch<uint8_t, A>* matrix_begin, batch<uint8_t, A>* matrix_end, requires_arch<common>) noexcept;

        namespace detail
        {
            XSIMD_INLINE __m256 lower_half(__m512 self) noexcept
            {
                return _mm512_castps512_ps256(self);
            }
            XSIMD_INLINE __m256d lower_half(__m512d self) noexcept
            {
                return _mm512_castpd512_pd256(self);
            }
            XSIMD_INLINE __m256i lower_half(__m512i self) noexcept
            {
                return _mm512_castsi512_si256(self);
            }
            XSIMD_INLINE __m256 upper_half(__m512 self) noexcept
            {
                return _mm256_castsi256_ps(_mm512_extracti64x4_epi64(_mm512_castps_si512(self), 1));
            }
            XSIMD_INLINE __m256d upper_half(__m512d self) noexcept
            {
                return _mm512_extractf64x4_pd(self, 1);
            }
            XSIMD_INLINE __m256i upper_half(__m512i self) noexcept
            {
                return _mm512_extracti64x4_epi64(self, 1);
            }
            XSIMD_INLINE __m512i merge_avx(__m256i low, __m256i high) noexcept
            {
                return _mm512_inserti64x4(_mm512_castsi256_si512(low), high, 1);
            }
            XSIMD_INLINE __m512 merge_avx(__m256 low, __m256 high) noexcept
            {
                return _mm512_castpd_ps(_mm512_insertf64x4(_mm512_castpd256_pd512(_mm256_castps_pd(low)), _mm256_castps_pd(high), 1));
            }
            XSIMD_INLINE __m512d merge_avx(__m256d low, __m256d high) noexcept
            {
                return _mm512_insertf64x4(_mm512_castpd256_pd512(low), high, 1);
            }

            template <class F>
            __m512i fwd_to_avx(F f, __m512i self)
            {
                __m256i self_low = lower_half(self), self_high = upper_half(self);
                __m256i res_low = f(self_low);
                __m256i res_high = f(self_high);
                return merge_avx(res_low, res_high);
            }
            template <class F>
            __m512i fwd_to_avx(F f, __m512i self, __m512i other)
            {
                __m256i self_low = lower_half(self), self_high = upper_half(self),
                        other_low = lower_half(other), other_high = upper_half(other);
                __m256i res_low = f(self_low, other_low);
                __m256i res_high = f(self_high, other_high);
                return merge_avx(res_low, res_high);
            }
            template <class F>
            __m512i fwd_to_avx(F f, __m512i self, int32_t other)
            {
                __m256i self_low = lower_half(self), self_high = upper_half(self);
                __m256i res_low = f(self_low, other);
                __m256i res_high = f(self_high, other);
                return merge_avx(res_low, res_high);
            }
        }
        namespace detail
        {

            XSIMD_INLINE uint32_t morton(uint16_t x, uint16_t y) noexcept
            {

                static const unsigned short MortonTable256[256] = {
                    0x0000, 0x0001, 0x0004, 0x0005, 0x0010, 0x0011, 0x0014, 0x0015,
                    0x0040, 0x0041, 0x0044, 0x0045, 0x0050, 0x0051, 0x0054, 0x0055,
                    0x0100, 0x0101, 0x0104, 0x0105, 0x0110, 0x0111, 0x0114, 0x0115,
                    0x0140, 0x0141, 0x0144, 0x0145, 0x0150, 0x0151, 0x0154, 0x0155,
                    0x0400, 0x0401, 0x0404, 0x0405, 0x0410, 0x0411, 0x0414, 0x0415,
                    0x0440, 0x0441, 0x0444, 0x0445, 0x0450, 0x0451, 0x0454, 0x0455,
                    0x0500, 0x0501, 0x0504, 0x0505, 0x0510, 0x0511, 0x0514, 0x0515,
                    0x0540, 0x0541, 0x0544, 0x0545, 0x0550, 0x0551, 0x0554, 0x0555,
                    0x1000, 0x1001, 0x1004, 0x1005, 0x1010, 0x1011, 0x1014, 0x1015,
                    0x1040, 0x1041, 0x1044, 0x1045, 0x1050, 0x1051, 0x1054, 0x1055,
                    0x1100, 0x1101, 0x1104, 0x1105, 0x1110, 0x1111, 0x1114, 0x1115,
                    0x1140, 0x1141, 0x1144, 0x1145, 0x1150, 0x1151, 0x1154, 0x1155,
                    0x1400, 0x1401, 0x1404, 0x1405, 0x1410, 0x1411, 0x1414, 0x1415,
                    0x1440, 0x1441, 0x1444, 0x1445, 0x1450, 0x1451, 0x1454, 0x1455,
                    0x1500, 0x1501, 0x1504, 0x1505, 0x1510, 0x1511, 0x1514, 0x1515,
                    0x1540, 0x1541, 0x1544, 0x1545, 0x1550, 0x1551, 0x1554, 0x1555,
                    0x4000, 0x4001, 0x4004, 0x4005, 0x4010, 0x4011, 0x4014, 0x4015,
                    0x4040, 0x4041, 0x4044, 0x4045, 0x4050, 0x4051, 0x4054, 0x4055,
                    0x4100, 0x4101, 0x4104, 0x4105, 0x4110, 0x4111, 0x4114, 0x4115,
                    0x4140, 0x4141, 0x4144, 0x4145, 0x4150, 0x4151, 0x4154, 0x4155,
                    0x4400, 0x4401, 0x4404, 0x4405, 0x4410, 0x4411, 0x4414, 0x4415,
                    0x4440, 0x4441, 0x4444, 0x4445, 0x4450, 0x4451, 0x4454, 0x4455,
                    0x4500, 0x4501, 0x4504, 0x4505, 0x4510, 0x4511, 0x4514, 0x4515,
                    0x4540, 0x4541, 0x4544, 0x4545, 0x4550, 0x4551, 0x4554, 0x4555,
                    0x5000, 0x5001, 0x5004, 0x5005, 0x5010, 0x5011, 0x5014, 0x5015,
                    0x5040, 0x5041, 0x5044, 0x5045, 0x5050, 0x5051, 0x5054, 0x5055,
                    0x5100, 0x5101, 0x5104, 0x5105, 0x5110, 0x5111, 0x5114, 0x5115,
                    0x5140, 0x5141, 0x5144, 0x5145, 0x5150, 0x5151, 0x5154, 0x5155,
                    0x5400, 0x5401, 0x5404, 0x5405, 0x5410, 0x5411, 0x5414, 0x5415,
                    0x5440, 0x5441, 0x5444, 0x5445, 0x5450, 0x5451, 0x5454, 0x5455,
                    0x5500, 0x5501, 0x5504, 0x5505, 0x5510, 0x5511, 0x5514, 0x5515,
                    0x5540, 0x5541, 0x5544, 0x5545, 0x5550, 0x5551, 0x5554, 0x5555
                };

                uint32_t z = MortonTable256[y >> 8] << 17 | MortonTable256[x >> 8] << 16 | MortonTable256[y & 0xFF] << 1 | MortonTable256[x & 0xFF];
                return z;
            }

            template <class A, class T, int Cmp>
            XSIMD_INLINE batch_bool<T, A> compare_int_avx512f(batch<T, A> const& self, batch<T, A> const& other) noexcept
            {
                using register_type = typename batch_bool<T, A>::register_type;
                if (std::is_signed_v<T>)
                {
                    if constexpr (sizeof(T) == 1)
                    {
                        // shifting to take sign into account
                        uint64_t mask_low0 = _mm512_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x000000FF)) << 24,
                                                                   (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x000000FF)) << 24,
                                                                   Cmp);
                        uint64_t mask_low1 = _mm512_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x0000FF00)) << 16,
                                                                   (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x0000FF00)) << 16,
                                                                   Cmp);
                        uint64_t mask_high0 = _mm512_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x00FF0000)) << 8,
                                                                    (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x00FF0000)) << 8,
                                                                    Cmp);
                        uint64_t mask_high1 = _mm512_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0xFF000000)),
                                                                    (batch<int32_t, A>(other.data) & batch<int32_t, A>(0xFF000000)),
                                                                    Cmp);
                        uint64_t mask = 0;
                        for (unsigned i = 0; i < 16; ++i)
                        {
                            mask |= (mask_low0 & (uint64_t(1) << i)) << (3 * i + 0);
                            mask |= (mask_low1 & (uint64_t(1) << i)) << (3 * i + 1);
                            mask |= (mask_high0 & (uint64_t(1) << i)) << (3 * i + 2);
                            mask |= (mask_high1 & (uint64_t(1) << i)) << (3 * i + 3);
                        }
                        return (register_type)mask;
                    }
                    else if constexpr (sizeof(T) == 2)
                    {
                        // shifting to take sign into account
                        uint16_t mask_low = _mm512_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x0000FFFF)) << 16,
                                                                  (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x0000FFFF)) << 16,
                                                                  Cmp);
                        uint16_t mask_high = _mm512_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0xFFFF0000)),
                                                                   (batch<int32_t, A>(other.data) & batch<int32_t, A>(0xFFFF0000)),
                                                                   Cmp);
                        return static_cast<register_type>(morton(mask_low, mask_high));
                    }
                    else if constexpr (sizeof(T) == 4)
                    {
                        return (register_type)_mm512_cmp_epi32_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 8)
                    {
                        return (register_type)_mm512_cmp_epi64_mask(self, other, Cmp);
                    }
                }
                else
                {
                    if constexpr (sizeof(T) == 1)
                    {
                        uint64_t mask_low0 = _mm512_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x000000FF)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x000000FF)), Cmp);
                        uint64_t mask_low1 = _mm512_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x0000FF00)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x0000FF00)), Cmp);
                        uint64_t mask_high0 = _mm512_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x00FF0000)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x00FF0000)), Cmp);
                        uint64_t mask_high1 = _mm512_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0xFF000000)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0xFF000000)), Cmp);
                        uint64_t mask = 0;
                        for (unsigned i = 0; i < 16; ++i)
                        {
                            mask |= (mask_low0 & (uint64_t(1) << i)) << (3 * i + 0);
                            mask |= (mask_low1 & (uint64_t(1) << i)) << (3 * i + 1);
                            mask |= (mask_high0 & (uint64_t(1) << i)) << (3 * i + 2);
                            mask |= (mask_high1 & (uint64_t(1) << i)) << (3 * i + 3);
                        }
                        return (register_type)mask;
                    }
                    else if constexpr (sizeof(T) == 2)
                    {
                        uint16_t mask_low = _mm512_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x0000FFFF)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x0000FFFF)), Cmp);
                        uint16_t mask_high = _mm512_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0xFFFF0000)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0xFFFF0000)), Cmp);
                        return static_cast<register_type>(morton(mask_low, mask_high));
                    }
                    else if constexpr (sizeof(T) == 4)
                    {
                        return (register_type)_mm512_cmp_epu32_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 8)
                    {
                        return (register_type)_mm512_cmp_epu64_mask(self, other, Cmp);
                    }
                }
            }
        }

        namespace detail
        {
            // --- compact unified helpers for masked loads/stores & zero-extension ---
            struct high_tag
            {
            };

            // zero-extend 256 -> 512 (low) and place-into-high variants
            XSIMD_INLINE __m512i zero_extend(__m256i lo) noexcept { return _mm512_zextsi256_si512(lo); }
            XSIMD_INLINE __m512 zero_extend(__m256 lo) noexcept { return _mm512_zextps256_ps512(lo); }
            XSIMD_INLINE __m512d zero_extend(__m256d lo) noexcept { return _mm512_zextpd256_pd512(lo); }

            XSIMD_INLINE __m512i zero_extend(__m256i hi, high_tag) noexcept { return _mm512_inserti64x4(_mm512_setzero_si512(), hi, 1); }
            XSIMD_INLINE __m512 zero_extend(__m256 hi, high_tag) noexcept
            {
                return _mm512_castpd_ps(_mm512_insertf64x4(_mm512_castps_pd(_mm512_setzero_ps()), _mm256_castps_pd(hi), 1));
            }
            XSIMD_INLINE __m512d zero_extend(__m256d hi, high_tag) noexcept { return _mm512_insertf64x4(_mm512_setzero_pd(), hi, 1); }

            // pointer-level masked loads (overloads by pointer/value size and alignment)
            XSIMD_INLINE __m512 load_masked(const float* mem, uint64_t m, aligned_mode) noexcept { return _mm512_maskz_load_ps((__mmask16)m, mem); }
            XSIMD_INLINE __m512 load_masked(const float* mem, uint64_t m, unaligned_mode) noexcept { return _mm512_maskz_loadu_ps((__mmask16)m, mem); }
            XSIMD_INLINE __m512d load_masked(const double* mem, uint64_t m, aligned_mode) noexcept { return _mm512_maskz_load_pd((__mmask8)m, mem); }
            XSIMD_INLINE __m512d load_masked(const double* mem, uint64_t m, unaligned_mode) noexcept { return _mm512_maskz_loadu_pd((__mmask8)m, mem); }

            XSIMD_INLINE __m512i load_masked(const int32_t* mem, uint64_t m, aligned_mode) noexcept { return _mm512_maskz_load_epi32((__mmask16)m, mem); }
            XSIMD_INLINE __m512i load_masked(const int32_t* mem, uint64_t m, unaligned_mode) noexcept { return _mm512_maskz_loadu_epi32((__mmask16)m, mem); }
            XSIMD_INLINE __m512i load_masked(const uint32_t* mem, uint64_t m, aligned_mode) noexcept { return _mm512_maskz_load_epi32((__mmask16)m, reinterpret_cast<const int32_t*>(mem)); }
            XSIMD_INLINE __m512i load_masked(const uint32_t* mem, uint64_t m, unaligned_mode) noexcept { return _mm512_maskz_loadu_epi32((__mmask16)m, reinterpret_cast<const int32_t*>(mem)); }

            XSIMD_INLINE __m512i load_masked(const int64_t* mem, uint64_t m, aligned_mode) noexcept { return _mm512_maskz_load_epi64((__mmask8)m, mem); }
            XSIMD_INLINE __m512i load_masked(const int64_t* mem, uint64_t m, unaligned_mode) noexcept { return _mm512_maskz_loadu_epi64((__mmask8)m, mem); }
            XSIMD_INLINE __m512i load_masked(const uint64_t* mem, uint64_t m, aligned_mode) noexcept { return _mm512_maskz_load_epi64((__mmask8)m, reinterpret_cast<const int64_t*>(mem)); }
            XSIMD_INLINE __m512i load_masked(const uint64_t* mem, uint64_t m, unaligned_mode) noexcept { return _mm512_maskz_loadu_epi64((__mmask8)m, reinterpret_cast<const int64_t*>(mem)); }

            // Register-level AVX2-forward helpers: accept 256-bit halves and return 512-bit
            XSIMD_INLINE __m512 load_masked(__m256 lo) noexcept { return zero_extend(lo); }
            XSIMD_INLINE __m512d load_masked(__m256d lo) noexcept { return zero_extend(lo); }
            XSIMD_INLINE __m512i load_masked(__m256i lo) noexcept { return zero_extend(lo); }
            XSIMD_INLINE __m512 load_masked(__m256 hi, high_tag) noexcept { return zero_extend(hi, high_tag {}); }
            XSIMD_INLINE __m512d load_masked(__m256d hi, high_tag) noexcept { return zero_extend(hi, high_tag {}); }
            XSIMD_INLINE __m512i load_masked(__m256i hi, high_tag) noexcept { return zero_extend(hi, high_tag {}); }

            // pointer-level masked stores (overloads by pointer/value size and alignment)
            XSIMD_INLINE void store_masked(float* mem, __m512 src, uint64_t m, aligned_mode) noexcept { _mm512_mask_store_ps(mem, (__mmask16)m, src); }
            XSIMD_INLINE void store_masked(float* mem, __m512 src, uint64_t m, unaligned_mode) noexcept { _mm512_mask_storeu_ps(mem, (__mmask16)m, src); }
            XSIMD_INLINE void store_masked(double* mem, __m512d src, uint64_t m, aligned_mode) noexcept { _mm512_mask_store_pd(mem, (__mmask8)m, src); }
            XSIMD_INLINE void store_masked(double* mem, __m512d src, uint64_t m, unaligned_mode) noexcept { _mm512_mask_storeu_pd(mem, (__mmask8)m, src); }

            XSIMD_INLINE void store_masked(int32_t* mem, __m512i src, uint64_t m, aligned_mode) noexcept { _mm512_mask_store_epi32(mem, (__mmask16)m, src); }
            XSIMD_INLINE void store_masked(int32_t* mem, __m512i src, uint64_t m, unaligned_mode) noexcept { _mm512_mask_storeu_epi32(mem, (__mmask16)m, src); }
            XSIMD_INLINE void store_masked(uint32_t* mem, __m512i src, uint64_t m, aligned_mode) noexcept { _mm512_mask_store_epi32(reinterpret_cast<int32_t*>(mem), (__mmask16)m, src); }
            XSIMD_INLINE void store_masked(uint32_t* mem, __m512i src, uint64_t m, unaligned_mode) noexcept { _mm512_mask_storeu_epi32(reinterpret_cast<int32_t*>(mem), (__mmask16)m, src); }

            XSIMD_INLINE void store_masked(int64_t* mem, __m512i src, uint64_t m, aligned_mode) noexcept { _mm512_mask_store_epi64(mem, (__mmask8)m, src); }
            XSIMD_INLINE void store_masked(int64_t* mem, __m512i src, uint64_t m, unaligned_mode) noexcept { _mm512_mask_storeu_epi64(mem, (__mmask8)m, src); }
            XSIMD_INLINE void store_masked(uint64_t* mem, __m512i src, uint64_t m, aligned_mode) noexcept { _mm512_mask_store_epi64(reinterpret_cast<int64_t*>(mem), (__mmask8)m, src); }
            XSIMD_INLINE void store_masked(uint64_t* mem, __m512i src, uint64_t m, unaligned_mode) noexcept { _mm512_mask_storeu_epi64(reinterpret_cast<int64_t*>(mem), (__mmask8)m, src); }

        } // namespace detail

        template <class A, class T, bool... Values, class Mode,
                  typename = std::enable_if_t<(sizeof(T) >= 4)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem,
                                             batch_bool_constant<T, A, Values...> mask,
                                             convert<T>, Mode, requires_arch<avx512f>) noexcept
        {
            constexpr auto half = batch<T, A>::size / 2;
            using half_arch = typename ::xsimd::make_sized_batch_t<T, half>::arch_type;
            if constexpr (mask.countl_zero() >= half) // lower 256-bit half
            {
                constexpr auto mlo = ::xsimd::detail::lower_half<half_arch>(mask);
                const auto lo = load_masked<half_arch>(mem, mlo, convert<T> {}, Mode {}, half_arch {});
                return detail::load_masked(lo); // zero-extend low half
            }
            else if constexpr (mask.countr_zero() >= half) // upper 256-bit half
            {
                constexpr auto mhi = ::xsimd::detail::upper_half<half_arch>(mask);
                const auto hi = load_masked<half_arch>(mem + half, mhi, convert<T> {}, Mode {}, half_arch {});
                return detail::load_masked(hi, detail::high_tag {});
            }
            else
            {
                // fallback to centralized pointer-level helper
                return detail::load_masked(mem, mask.mask(), Mode {});
            }
        }

        template <class A, class T, bool... Values, class Mode,
                  typename = std::enable_if_t<(sizeof(T) >= 4)>>
        XSIMD_INLINE void store_masked(T* mem,
                                       batch<T, A> const& src,
                                       batch_bool_constant<T, A, Values...> mask,
                                       Mode, requires_arch<avx512f>) noexcept
        {
            constexpr auto half = batch<T, A>::size / 2;
            using half_batch = ::xsimd::make_sized_batch_t<T, half>;
            using half_arch = typename half_batch::arch_type;
            if constexpr (mask.countl_zero() >= half) // lower 256-bit half
            {
                constexpr auto mlo = ::xsimd::detail::lower_half<half_arch>(mask);
                const half_batch lo = detail::lower_half(src);
                store_masked<half_arch>(mem, lo, mlo, Mode {}, half_arch {});
            }
            else if constexpr (mask.countr_zero() >= half) // upper 256-bit half
            {
                constexpr auto mhi = ::xsimd::detail::upper_half<half_arch>(mask);
                const half_batch hi = detail::upper_half(src);
                store_masked<half_arch>(mem + half, hi, mhi, Mode {}, half_arch {});
            }
            else
            {
                // fallback to centralized pointer-level helper
                detail::store_masked(mem, src, mask.mask(), Mode {});
            }
        }

        // Runtime-mask load/store: same native k-register path as the constant
        // overloads above, minus the compile-time half-forwarding. 8/16-bit
        // elements are handled natively by avx512bw (vmovdqu8 / vmovdqu16);
        // without AVX512BW they fall back to the common scalar path.
        template <class A, class T, class Mode,
                  typename = std::enable_if_t<(sizeof(T) >= 4)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx512f>) noexcept
        {
            return detail::load_masked(mem, mask.mask(), Mode {});
        }

        template <class A, class T, class Mode,
                  typename = std::enable_if_t<(sizeof(T) >= 4)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx512f>) noexcept
        {
            detail::store_masked(mem, src, mask.mask(), Mode {});
        }

        // abs
        template <class A>
        XSIMD_INLINE batch<float, A> abs(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            __m512 self_asf = (__m512)self;
            __m512i self_asi = *reinterpret_cast<__m512i*>(&self_asf);
            __m512i res_asi = _mm512_and_epi32(_mm512_set1_epi32(0x7FFFFFFF), self_asi);
            return *reinterpret_cast<__m512*>(&res_asi);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> abs(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            __m512d self_asd = (__m512d)self;
            __m512i self_asi = *reinterpret_cast<__m512i*>(&self_asd);
            __m512i res_asi = _mm512_and_epi64(_mm512_set1_epi64(0x7FFFFFFFFFFFFFFF),
                                               self_asi);
            return *reinterpret_cast<__m512d*>(&res_asi);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> abs(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            if (std::is_unsigned_v<T>)
            {
                return self;
            }

            if constexpr (sizeof(T) == 1)
            {
                return detail::fwd_to_avx([](__m256i s) noexcept
                                          { return abs(batch<T, avx2>(s)); },
                                          self);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return detail::fwd_to_avx([](__m256i s) noexcept
                                          { return abs(batch<T, avx2>(s)); },
                                          self);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_abs_epi32(self);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_abs_epi64(self);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // add
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> add(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return add(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                          self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return add(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                          self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_add_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_add_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A> add(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_add_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> add(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_add_pd(self, other);
        }

        // all
        template <class A, class T>
        XSIMD_INLINE bool all(batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return self.data == register_type(-1);
        }

        // any
        template <class A, class T>
        XSIMD_INLINE bool any(batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return self.data != register_type(0);
        }

        // batch_bool_cast
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<avx512f>) noexcept
        {
            return self.data;
        }

        // bitwise_and
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_and(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
#if defined(_MSC_VER)
            return _mm512_and_ps(self, other);
#else
            return _mm512_castsi512_ps(_mm512_and_si512(_mm512_castps_si512(self), _mm512_castps_si512(other)));
#endif
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_and(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(_mm512_and_si512(_mm512_castpd_si512(self), _mm512_castpd_si512(other)));
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_and_si512(self, other);
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data & other.data);
        }

        // bitwise_andnot
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_andnot(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_ps(_mm512_andnot_si512(_mm512_castps_si512(other), _mm512_castps_si512(self)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_andnot(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(_mm512_andnot_si512(_mm512_castpd_si512(other), _mm512_castpd_si512(self)));
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_andnot(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_andnot_si512(other, self);
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data & ~other.data);
        }

        // bitwise_lshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, int32_t other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                __m512i tmp = _mm512_sllv_epi32(self, _mm512_set1_epi32(other));
#else
                __m512i tmp = _mm512_slli_epi32(self, other);
#endif
                return _mm512_and_si512(_mm512_set1_epi8(0xFF << other), tmp);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return detail::fwd_to_avx([](__m256i s, int32_t o) noexcept
                                          { return bitwise_lshift(batch<T, avx2>(s), o, avx2 {}); },
                                          self, other);
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_sllv_epi32(self, _mm512_set1_epi32(other));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_sllv_epi64(self, _mm512_set1_epi64(other));
#else
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_slli_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_slli_epi64(self, other);
#endif
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // bitwise_not
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_not(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_xor_si512(self, _mm512_set1_epi32(-1));
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(~self.data);
        }

        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_not(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_ps(_mm512_xor_si512(_mm512_castps_si512(self), _mm512_set1_epi32(-1)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_not(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(_mm512_xor_si512(_mm512_castpd_si512(self), _mm512_set1_epi32(-1)));
        }

        // bitwise_or
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_or(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_ps(_mm512_or_si512(_mm512_castps_si512(self), _mm512_castps_si512(other)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_or(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(_mm512_or_si512(_mm512_castpd_si512(self), _mm512_castpd_si512(other)));
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data | other.data);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_or_si512(self, other);
        }

        // bitwise_rshift
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<avx512f>) noexcept
        {
            if (std::is_signed_v<T>)
            {
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_srav_epi32(self, _mm512_set1_epi32(other));
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_srav_epi64(self, _mm512_set1_epi64(other));
#else
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_srai_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_srai_epi64(self, other);
#endif
                }
                else
                {
                    return detail::fwd_to_avx([](__m256i s, int32_t o) noexcept
                                              { return bitwise_rshift(batch<T, avx2>(s), o, avx2 {}); },
                                              self, other);
                }
            }
            else
            {
                if constexpr (sizeof(T) == 1)
                {
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                    __m512i tmp = _mm512_srlv_epi32(self, _mm512_set1_epi32(other));
#else
                    __m512i tmp = _mm512_srli_epi32(self, other);
#endif
                    return _mm512_and_si512(_mm512_set1_epi8(0xFF >> other), tmp);
#if defined(XSIMD_AVX512_SHIFT_INTRINSICS_IMM_ONLY)
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm512_srlv_epi32(self, _mm512_set1_epi32(other));
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_srlv_epi64(self, _mm512_set1_epi64(other));
#else
                }
                else if constexpr (sizeof(T) == 4)
                {
                    return _mm512_srli_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_srli_epi64(self, other);
#endif
                }
                else
                {
                    return detail::fwd_to_avx([](__m256i s, int32_t o) noexcept
                                              { return bitwise_rshift(batch<T, avx2>(s), o, avx2 {}); },
                                              self, other);
                }
            }
        }

        // rotl
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm512_rolv_epi32(self, other);
            }
            if constexpr (sizeof(T) == 8)
            {
                return _mm512_rolv_epi64(self, other);
            }
            return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                      { return rotl(batch<T, avx2>(s), batch<T, avx2>(o), avx2 {}); },
                                      self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, int32_t other, requires_arch<avx512f>) noexcept
        {
            return rotl(self, batch<T, A>(other), A {});
        }
        template <size_t count, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count must be less than the number of bits in T");
            if constexpr (sizeof(T) == 4)
            {
                return _mm512_rol_epi32(self, count);
            }
            if constexpr (sizeof(T) == 8)
            {
                return _mm512_rol_epi64(self, count);
            }

            return detail::fwd_to_avx([](__m256i s) noexcept
                                      { return rotl<count>(batch<T, avx2>(s), avx2 {}); },
                                      self);
        }

        // rotr
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) < 4)
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return rotr(batch<T, avx2>(s), batch<T, avx2>(o), avx2 {}); },
                                          self, other);
            }
            if constexpr (std::is_unsigned_v<T>)
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_rorv_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_rorv_epi64(self, other);
                }
            }
            return rotr(self, other, common {});
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, int32_t other, requires_arch<avx512f>) noexcept
        {
            return rotr(self, batch<T, A>(other), A {});
        }

        template <size_t count, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count must be less than the number of bits in T");
            if constexpr (sizeof(T) < 4)
            {
                return detail::fwd_to_avx([](__m256i s) noexcept
                                          { return rotr<count>(batch<T, avx2>(s), avx2 {}); },
                                          self);
            }
            if constexpr (std::is_unsigned_v<T>)
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_ror_epi32(self, count);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_ror_epi64(self, count);
                }
            }
            return rotr<count>(self, common {});
        }

        // bitwise_xor
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_xor(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_ps(_mm512_xor_si512(_mm512_castps_si512(self), _mm512_castps_si512(other)));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_xor(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(_mm512_xor_si512(_mm512_castpd_si512(self), _mm512_castpd_si512(other)));
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data ^ other.data);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_xor(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_xor_si512(self, other);
        }

        // bitwise_cast
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<T, A> const& self, batch<float, A> const&, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_ps(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<T, A> const& self, batch<double, A> const&, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(self);
        }
        template <class A, class T, class Tp, class = std::enable_if_t<std::is_integral_v<std::common_type_t<T, Tp>>>>
        XSIMD_INLINE batch<Tp, A> bitwise_cast(batch<T, A> const& self, batch<Tp, A> const&, requires_arch<avx512f>) noexcept
        {
            return batch<Tp, A>(self.data);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> bitwise_cast(batch<float, A> const& self, batch<double, A> const&, requires_arch<avx512f>) noexcept
        {
            return _mm512_castps_pd(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_cast(batch<float, A> const& self, batch<T, A> const&, requires_arch<avx512f>) noexcept
        {
            return _mm512_castps_si512(self);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> bitwise_cast(batch<double, A> const& self, batch<float, A> const&, requires_arch<avx512f>) noexcept
        {
            return _mm512_castpd_ps(self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_cast(batch<double, A> const& self, batch<T, A> const&, requires_arch<avx512f>) noexcept
        {
            return _mm512_castpd_si512(self);
        }

        // broadcast
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> broadcast(T val, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return _mm512_set1_epi8(val);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return _mm512_set1_epi16(val);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_set1_epi32(val);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_set1_epi64(val);
            }
            else
            {
                assert(false && "unsupported");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A> broadcast(float val, requires_arch<avx512f>) noexcept
        {
            return _mm512_set1_ps(val);
        }
        template <class A>
        batch<double, A> XSIMD_INLINE broadcast(double val, requires_arch<avx512f>) noexcept
        {
            return _mm512_set1_pd(val);
        }

        // ceil
        template <class A>
        XSIMD_INLINE batch<float, A> ceil(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_ps(self, _MM_FROUND_TO_POS_INF);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> ceil(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_pd(self, _MM_FROUND_TO_POS_INF);
        }

        // compress
        template <class A>
        XSIMD_INLINE batch<float, A> compress(batch<float, A> const& self, batch_bool<float, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_compress_ps(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> compress(batch<double, A> const& self, batch_bool<double, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_compress_pd(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int32_t, A> compress(batch<int32_t, A> const& self, batch_bool<int32_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_compress_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> compress(batch<uint32_t, A> const& self, batch_bool<uint32_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_compress_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> compress(batch<int64_t, A> const& self, batch_bool<int64_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_compress_epi64(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> compress(batch<uint64_t, A> const& self, batch_bool<uint64_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_compress_epi64(mask.mask(), self);
        }

        // convert
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<int32_t, A> const& self, batch<float, A> const&, requires_arch<avx512f>) noexcept
            {
                return _mm512_cvtepi32_ps(self);
            }

            template <class A>
            XSIMD_INLINE batch<int32_t, A> fast_cast(batch<float, A> const& self, batch<int32_t, A> const&, requires_arch<avx512f>) noexcept
            {
                return _mm512_cvttps_epi32(self);
            }

            template <class A>
            XSIMD_INLINE batch<float, A> fast_cast(batch<uint32_t, A> const& self, batch<float, A> const&, requires_arch<avx512f>) noexcept
            {
                return _mm512_cvtepu32_ps(self);
            }

            template <class A>
            batch<uint32_t, A> fast_cast(batch<float, A> const& self, batch<uint32_t, A> const&, requires_arch<avx512f>)
            {
                return _mm512_cvttps_epu32(self);
            }
        }

        namespace detail
        {
            // complex_low
            template <class A>
            XSIMD_INLINE batch<float, A> complex_low(batch<std::complex<float>, A> const& self, requires_arch<avx512f>) noexcept
            {
                __m512i idx = _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
                return _mm512_permutex2var_ps(self.real(), idx, self.imag());
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_low(batch<std::complex<double>, A> const& self, requires_arch<avx512f>) noexcept
            {
                __m512i idx = _mm512_setr_epi64(0, 8, 1, 9, 2, 10, 3, 11);
                return _mm512_permutex2var_pd(self.real(), idx, self.imag());
            }

            // complex_high
            template <class A>
            XSIMD_INLINE batch<float, A> complex_high(batch<std::complex<float>, A> const& self, requires_arch<avx512f>) noexcept
            {
                __m512i idx = _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
                return _mm512_permutex2var_ps(self.real(), idx, self.imag());
            }
            template <class A>
            XSIMD_INLINE batch<double, A> complex_high(batch<std::complex<double>, A> const& self, requires_arch<avx512f>) noexcept
            {
                __m512i idx = _mm512_setr_epi64(4, 12, 5, 13, 6, 14, 7, 15);
                return _mm512_permutex2var_pd(self.real(), idx, self.imag());
            }
        }
        // incr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx512f>) noexcept
        {

            if constexpr (sizeof(T) == 4)
            {
                return _mm512_mask_sub_epi32(self, mask.data, self, _mm512_set1_epi32(1));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_mask_sub_epi64(self, mask.data, self, _mm512_set1_epi64(1));
            }
            else
            {
                return decr_if(self, mask, common {});
            }
        }

        // div
        template <class A>
        XSIMD_INLINE batch<float, A> div(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_div_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> div(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_div_pd(self, other);
        }

        // eq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> eq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, other, _CMP_EQ_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, other, _CMP_EQ_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::compare_int_avx512f<A, T, _MM_CMPINT_EQ>(self, other);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(~self.data ^ other.data);
        }

        // expand
        template <class A>
        XSIMD_INLINE batch<float, A> expand(batch<float, A> const& self, batch_bool<float, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_expand_ps(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> expand(batch<double, A> const& self, batch_bool<double, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_expand_pd(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int32_t, A> expand(batch<int32_t, A> const& self, batch_bool<int32_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_expand_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> expand(batch<uint32_t, A> const& self, batch_bool<uint32_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_expand_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> expand(batch<int64_t, A> const& self, batch_bool<int64_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_expand_epi64(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> expand(batch<uint64_t, A> const& self, batch_bool<uint64_t, A> const& mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_maskz_expand_epi64(mask.mask(), self);
        }

        // floor
        template <class A>
        XSIMD_INLINE batch<float, A> floor(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_ps(self, _MM_FROUND_TO_NEG_INF);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> floor(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_pd(self, _MM_FROUND_TO_NEG_INF);
        }

        // fnma
        template <class A>
        XSIMD_INLINE batch<float, A> fnma(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fnmadd_ps(x, y, z);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fnma(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fnmadd_pd(x, y, z);
        }

        // fma
        template <class A>
        XSIMD_INLINE batch<float, A> fma(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fmadd_ps(x, y, z);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fma(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fmadd_pd(x, y, z);
        }

        // fms
        template <class A>
        XSIMD_INLINE batch<float, A> fms(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fmsub_ps(x, y, z);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fms(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fmsub_pd(x, y, z);
        }
        // fmas
        template <class A>
        XSIMD_INLINE batch<float, A> fmas(batch<float, A> const& x, batch<float, A> const& y, batch<float, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fmaddsub_ps(x, y, z);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> fmas(batch<double, A> const& x, batch<double, A> const& y, batch<double, A> const& z, requires_arch<avx512f>) noexcept
        {
            return _mm512_fmaddsub_pd(x, y, z);
        }

        // from bool
        template <class A, class T>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return select(self, batch<T, A>(1), batch<T, A>(0));
        }

        // from_mask
        template <class T, class A>
        XSIMD_INLINE batch_bool<T, A> from_mask(batch_bool<T, A> const&, uint64_t mask, requires_arch<avx512f>) noexcept
        {
            return static_cast<typename batch_bool<T, A>::register_type>(mask);
        }

        // gather
        template <class T, class A, class U, detail::enable_sized_integral_t<T, 4> = 0, detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx512f>) noexcept
        {
            return _mm512_i32gather_epi32(index, static_cast<const void*>(src), sizeof(T));
        }

        template <class T, class A, class U, detail::enable_sized_integral_t<T, 8> = 0, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx512f>) noexcept
        {
            return _mm512_i64gather_epi64(index, static_cast<const void*>(src), sizeof(T));
        }

        template <class A, class U, detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<float, A> gather(batch<float, A> const&, float const* src,
                                            batch<U, A> const& index,
                                            kernel::requires_arch<avx512f>) noexcept
        {
            return _mm512_i32gather_ps(index, src, sizeof(float));
        }

        template <class A, class U, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<double, A>
        gather(batch<double, A> const&, double const* src, batch<U, A> const& index,
               kernel::requires_arch<avx512f>) noexcept
        {
            return _mm512_i64gather_pd(index, src, sizeof(double));
        }

        // gather: handmade conversions
        template <class A, class V, detail::enable_sized_integral_t<V, 4> = 0>
        XSIMD_INLINE batch<float, A> gather(batch<float, A> const&, double const* src,
                                            batch<V, A> const& index,
                                            requires_arch<avx512f>) noexcept
        {
            const batch<double, A> low(_mm512_i32gather_pd(_mm512_castsi512_si256(index.data), src, sizeof(double)));
            const batch<double, A> high(_mm512_i32gather_pd(_mm256_castpd_si256(_mm512_extractf64x4_pd(_mm512_castsi512_pd(index.data), 1)), src, sizeof(double)));
            return detail::merge_avx(_mm512_cvtpd_ps(low.data), _mm512_cvtpd_ps(high.data));
        }

        template <class A, class V, detail::enable_sized_integral_t<V, 4> = 0>
        XSIMD_INLINE batch<int32_t, A> gather(batch<int32_t, A> const&, double const* src,
                                              batch<V, A> const& index,
                                              requires_arch<avx512f>) noexcept
        {
            const batch<double, A> low(_mm512_i32gather_pd(_mm512_castsi512_si256(index.data), src, sizeof(double)));
            const batch<double, A> high(_mm512_i32gather_pd(_mm256_castpd_si256(_mm512_extractf64x4_pd(_mm512_castsi512_pd(index.data), 1)), src, sizeof(double)));
            return detail::merge_avx(_mm512_cvtpd_epi32(low.data), _mm512_cvtpd_epi32(high.data));
        }

        // ge
        template <class A>
        XSIMD_INLINE batch_bool<float, A> ge(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, other, _CMP_GE_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> ge(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, other, _CMP_GE_OQ);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::compare_int_avx512f<A, T, _MM_CMPINT_GE>(self, other);
        }

        // gt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> gt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, other, _CMP_GT_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> gt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, other, _CMP_GT_OQ);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::compare_int_avx512f<A, T, _MM_CMPINT_GT>(self, other);
        }

        // haddp
        template <class A>
        XSIMD_INLINE batch<float, A> haddp(batch<float, A> const* row, requires_arch<avx512f>) noexcept
        {
            // The following folds over the vector once:
            // tmp1 = [a0..8, b0..8]
            // tmp2 = [a8..f, b8..f]
#define XSIMD_AVX512_HADDP_STEP1(I, a, b)                                \
    batch<float, avx512f> res##I;                                        \
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
#define XSIMD_AVX512_HADDP_STEP2(I, a, b, c, d)                                                                                                         \
    batch<float, avx2> halfx##I;                                                                                                                        \
    {                                                                                                                                                   \
        auto tmp1 = _mm512_shuffle_f32x4(a, b, _MM_SHUFFLE(2, 0, 2, 0));                                                                                \
        auto tmp2 = _mm512_shuffle_f32x4(a, b, _MM_SHUFFLE(3, 1, 3, 1));                                                                                \
                                                                                                                                                        \
        auto resx1 = _mm512_add_ps(tmp1, tmp2);                                                                                                         \
                                                                                                                                                        \
        auto tmp3 = _mm512_shuffle_f32x4(c, d, _MM_SHUFFLE(2, 0, 2, 0));                                                                                \
        auto tmp4 = _mm512_shuffle_f32x4(c, d, _MM_SHUFFLE(3, 1, 3, 1));                                                                                \
                                                                                                                                                        \
        auto resx2 = _mm512_add_ps(tmp3, tmp4);                                                                                                         \
                                                                                                                                                        \
        auto tmp5 = _mm512_shuffle_ps(resx1, resx2, _MM_SHUFFLE(2, 0, 2, 0));                                                                           \
        auto tmp6 = _mm512_shuffle_ps(resx1, resx2, _MM_SHUFFLE(3, 1, 3, 1));                                                                           \
                                                                                                                                                        \
        auto resx3 = _mm512_add_ps(tmp5, tmp6);                                                                                                         \
                                                                                                                                                        \
        halfx##I = _mm256_hadd_ps(_mm256_insertf128_ps(_mm256_castps128_ps256(_mm512_extractf32x4_ps(resx3, 0)), _mm512_extractf32x4_ps(resx3, 1), 1),  \
                                  _mm256_insertf128_ps(_mm256_castps128_ps256(_mm512_extractf32x4_ps(resx3, 2)), _mm512_extractf32x4_ps(resx3, 3), 1)); \
    }

            XSIMD_AVX512_HADDP_STEP2(0, res0, res1, res2, res3);
            XSIMD_AVX512_HADDP_STEP2(1, res4, res5, res6, res7);

#undef XSIMD_AVX512_HADDP_STEP2

            auto concat = _mm512_castps256_ps512(halfx0);
            concat = _mm512_castpd_ps(_mm512_insertf64x4(_mm512_castps_pd(concat), _mm256_castps_pd(halfx1), 1));
            return concat;
        }

        template <class A>
        XSIMD_INLINE batch<double, A> haddp(batch<double, A> const* row, requires_arch<avx512f>) noexcept
        {
#define step1(I, a, b)                                                   \
    batch<double, avx512f> res##I;                                       \
    {                                                                    \
        auto tmp1 = _mm512_shuffle_f64x2(a, b, _MM_SHUFFLE(1, 0, 1, 0)); \
        auto tmp2 = _mm512_shuffle_f64x2(a, b, _MM_SHUFFLE(3, 2, 3, 2)); \
        res##I = _mm512_add_pd(tmp1, tmp2);                              \
    }

            step1(1, row[0], row[2]);
            step1(2, row[4], row[6]);
            step1(3, row[1], row[3]);
            step1(4, row[5], row[7]);

#undef step1

            auto tmp5 = _mm512_shuffle_f64x2(res1, res2, _MM_SHUFFLE(2, 0, 2, 0));
            auto tmp6 = _mm512_shuffle_f64x2(res1, res2, _MM_SHUFFLE(3, 1, 3, 1));

            auto resx1 = _mm512_add_pd(tmp5, tmp6);

            auto tmp7 = _mm512_shuffle_f64x2(res3, res4, _MM_SHUFFLE(2, 0, 2, 0));
            auto tmp8 = _mm512_shuffle_f64x2(res3, res4, _MM_SHUFFLE(3, 1, 3, 1));

            auto resx2 = _mm512_add_pd(tmp7, tmp8);

            auto tmpx = _mm512_shuffle_pd(resx1, resx2, 0b00000000);
            auto tmpy = _mm512_shuffle_pd(resx1, resx2, 0b11111111);

            return _mm512_add_pd(tmpx, tmpy);
        }

        // incr_if
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx512f>) noexcept
        {

            if constexpr (sizeof(T) == 4)
            {
                return _mm512_mask_add_epi32(self, mask.data, self, _mm512_set1_epi32(1));
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_mask_add_epi64(self, mask.data, self, _mm512_set1_epi64(1));
            }
            else
            {
                return incr_if(self, mask, common {});
            }
        }

        // first (must precede get for two-phase lookup)
        template <class A>
        XSIMD_INLINE float first(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_cvtss_f32(self);
        }

        template <class A>
        XSIMD_INLINE double first(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_cvtsd_f64(self);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return static_cast<T>(_mm_cvtsi128_si32(_mm512_castsi512_si128(self)) & 0xFF);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return static_cast<T>(_mm_cvtsi128_si32(_mm512_castsi512_si128(self)) & 0xFFFF);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return static_cast<T>(_mm_cvtsi128_si32(_mm512_castsi512_si128(self)));
            }
            else if constexpr (sizeof(T) == 8)
            {
                batch<T, sse4_2> low = _mm512_castsi512_si128(self);
                return first(low, sse4_2 {});
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }

        // get: use valignd/valignq to rotate lane I into position 0 in a single op.
        template <class A, size_t I>
        XSIMD_INLINE float get(batch<float, A> const& self, ::xsimd::index<I>, requires_arch<avx512f>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, avx512f {});
            }
            const auto rotated = _mm512_alignr_epi32(_mm512_castps_si512(self), _mm512_castps_si512(self), I);
            return _mm_cvtss_f32(_mm512_castps512_ps128(_mm512_castsi512_ps(rotated)));
        }

        template <class A, size_t I>
        XSIMD_INLINE double get(batch<double, A> const& self, ::xsimd::index<I>, requires_arch<avx512f>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, avx512f {});
            }
            const auto rotated = _mm512_alignr_epi64(_mm512_castpd_si512(self), _mm512_castpd_si512(self), I);
            return _mm_cvtsd_f64(_mm512_castpd512_pd128(_mm512_castsi512_pd(rotated)));
        }

        template <class A, size_t I, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<avx512f>) noexcept
        {
            if constexpr (I == 0)
            {
                return first(self, avx512f {});
            }
            else if constexpr (sizeof(T) == 4)
            {
                const auto rotated = _mm512_alignr_epi32(self, self, I);
                return first(batch<T, sse4_2>(_mm512_castsi512_si128(rotated)), sse4_2 {});
            }
            else if constexpr (sizeof(T) == 8)
            {
                const auto rotated = _mm512_alignr_epi64(self, self, I);
                return first(batch<T, sse4_2>(_mm512_castsi512_si128(rotated)), sse4_2 {});
            }
            else
            {
                // 8/16-bit lanes have no sub-dword rotate in AVX-512F; delegate to AVX halves.
                constexpr size_t elements_per_lane = batch<T, avx>::size;
                constexpr size_t lane = I / elements_per_lane;
                constexpr size_t sub_index = I % elements_per_lane;
                const auto half = (lane == 0) ? detail::lower_half(self) : detail::upper_half(self);
                return kernel::get(batch<T, avx>(half), ::xsimd::index<sub_index> {}, avx {});
            }
        }

        // insert
        template <class A, size_t I>
        XSIMD_INLINE batch<float, A> insert(batch<float, A> const& self, float val, index<I>, requires_arch<avx512f>) noexcept
        {

            int32_t tmp = bit_cast<int32_t>(val);
            return _mm512_castsi512_ps(_mm512_mask_set1_epi32(_mm512_castps_si512(self), __mmask16(1 << (I & 15)), tmp));
        }

        template <class A, size_t I>
        XSIMD_INLINE batch<double, A> insert(batch<double, A> const& self, double val, index<I>, requires_arch<avx512f>) noexcept
        {
            int64_t tmp = bit_cast<int64_t>(val);
            return _mm512_castsi512_pd(_mm512_mask_set1_epi64(_mm512_castpd_si512(self), __mmask8(1 << (I & 7)), tmp));
        }
        template <class A, class T, size_t I, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I> pos, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm512_mask_set1_epi32(self, __mmask16(1 << (I & 15)), val);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_mask_set1_epi64(self, __mmask8(1 << (I & 7)), val);
            }
            else
            {
                return insert(self, val, pos, common {});
            }
        }

        // isnan
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isnan(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, self, _CMP_UNORD_Q);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isnan(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, self, _CMP_UNORD_Q);
        }

        // ldexp
        template <class A>
        XSIMD_INLINE batch<float, A> ldexp(const batch<float, A>& self, const batch<as_integer_t<float>, A>& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_scalef_ps(self, _mm512_cvtepi32_ps(other));
        }

        template <class A>
        XSIMD_INLINE batch<double, A> ldexp(const batch<double, A>& self, const batch<as_integer_t<double>, A>& other, requires_arch<avx512f>) noexcept
        {
            // FIXME: potential data loss here when converting other elements to
            // int32 before converting them back to double.
            __m512d adjusted_index = _mm512_cvtepi32_pd(_mm512_cvtepi64_epi32(other));
            return _mm512_scalef_pd(self, adjusted_index);
        }

        // le
        template <class A>
        XSIMD_INLINE batch_bool<float, A> le(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, other, _CMP_LE_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> le(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, other, _CMP_LE_OQ);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::compare_int_avx512f<A, T, _MM_CMPINT_LE>(self, other);
        }

        namespace detail
        {
            // Adapted from https://github.com/serge-sans-paille/fast-bitset-from-bool-array
            // Generate a bitset from an array of boolean.
            template <size_t N>
            XSIMD_INLINE unsigned char tobitset(unsigned char unpacked[N])
            {
                static_assert(N == 8 || N == 4 || N == 2, "valid pack size");
                if constexpr (N == 8)
                {
                    uint64_t data;
                    memcpy(&data, unpacked, sizeof(uint64_t));

                    const uint64_t magic = (0x80 + 0x4000 + 0x200000 + 0x10000000 + 0x0800000000 + 0x040000000000 + 0x02000000000000 + 0x0100000000000000);

                    unsigned char res = ((data * magic) >> 56) & 0xFF;
                    return res;
                }
                else if constexpr (N == 4)
                {
                    uint32_t data;
                    memcpy(&data, unpacked, sizeof(uint32_t));

                    const uint32_t magic = (0x80 + 0x4000 + 0x200000 + 0x10000000);

                    unsigned char res = ((data * magic) >> 24) & 0xFF;
                    return res;
                }
                else if constexpr (N == 2)
                {
                    uint16_t data;
                    memcpy(&data, unpacked, sizeof(uint16_t));

                    const uint16_t magic = (0x80 + 0x4000);

                    unsigned char res = ((data * magic) >> 8) & 0xFF;
                    return res;
                }
            }
        }

        // load mask
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            constexpr auto size = batch_bool<T, A>::size;
            constexpr auto iter = size / 8;
            static_assert((size % 8) == 0, "incorrect size of bool batch");
            register_type mask = 0;
            for (std::size_t i = 0; i < iter; ++i)
            {
                unsigned char block = detail::tobitset<8>((unsigned char*)mem + i * 8);
                mask |= (register_type(block) << (i * 8));
            }
            return mask;
        }

        // load_aligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_aligned(T const* mem, convert<T>, requires_arch<avx512f>) noexcept
        {
            return _mm512_load_si512((__m512i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_aligned(float const* mem, convert<float>, requires_arch<avx512f>) noexcept
        {
            return _mm512_load_ps(mem);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_aligned(double const* mem, convert<double>, requires_arch<avx512f>) noexcept
        {
            return _mm512_load_pd(mem);
        }

        // load_complex
        namespace detail
        {
            template <class A>
            XSIMD_INLINE batch<std::complex<float>, A> load_complex(batch<float, A> const& hi, batch<float, A> const& lo, requires_arch<avx512f>) noexcept
            {
                __m512i real_idx = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
                __m512i imag_idx = _mm512_setr_epi32(1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
                auto real = _mm512_permutex2var_ps(hi, real_idx, lo);
                auto imag = _mm512_permutex2var_ps(hi, imag_idx, lo);
                return { real, imag };
            }
            template <class A>
            XSIMD_INLINE batch<std::complex<double>, A> load_complex(batch<double, A> const& hi, batch<double, A> const& lo, requires_arch<avx512f>) noexcept
            {
                __m512i real_idx = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
                __m512i imag_idx = _mm512_setr_epi64(1, 3, 5, 7, 9, 11, 13, 15);
                auto real = _mm512_permutex2var_pd(hi, real_idx, lo);
                auto imag = _mm512_permutex2var_pd(hi, imag_idx, lo);
                return { real, imag };
            }
        }

        // load_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> load_unaligned(T const* mem, convert<T>, requires_arch<avx512f>) noexcept
        {
            return _mm512_loadu_si512((__m512i const*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_unaligned(float const* mem, convert<float>, requires_arch<avx512f>) noexcept
        {
            return _mm512_loadu_ps(mem);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_unaligned(double const* mem, convert<double>, requires_arch<avx512f>) noexcept
        {
            return _mm512_loadu_pd(mem);
        }

        // load_stream
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE batch<T, A> load_stream(T const* mem, convert<T>, requires_arch<avx512f>) noexcept
        {
            return _mm512_stream_load_si512((__m512i*)mem);
        }
        template <class A>
        XSIMD_INLINE batch<float, A> load_stream(float const* mem, convert<float>, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_ps(_mm512_stream_load_si512((__m512i*)mem));
        }
        template <class A>
        XSIMD_INLINE batch<double, A> load_stream(double const* mem, convert<double>, requires_arch<avx512f>) noexcept
        {
            return _mm512_castsi512_pd(_mm512_stream_load_si512((__m512i*)mem));
        }

        // lt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> lt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, other, _CMP_LT_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> lt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, other, _CMP_LT_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::compare_int_avx512f<A, T, _MM_CMPINT_LT>(self, other);
        }

        // mask
        template <class A, class T>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return self.data;
        }

        // max
        template <class A>
        XSIMD_INLINE batch<float, A> max(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_max_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> max(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_max_pd(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> max(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_max_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_max_epi64(self, other);
                }
                else
                {
                    return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                              { return max(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                              self, other);
                }
            }
            else
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_max_epu32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_max_epu64(self, other);
                }
                else
                {
                    return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                              { return max(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                              self, other);
                }
            }
        }

        // min
        template <class A>
        XSIMD_INLINE batch<float, A> min(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_min_ps(other, self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> min(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_min_pd(other, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> min(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_min_epi32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_min_epi64(self, other);
                }
                else
                {
                    return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                              { return min(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                              self, other);
                }
            }
            else
            {
                if constexpr (sizeof(T) == 4)
                {
                    return _mm512_min_epu32(self, other);
                }
                else if constexpr (sizeof(T) == 8)
                {
                    return _mm512_min_epu64(self, other);
                }
                else
                {
                    return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                              { return min(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                              self, other);
                }
            }
        }

        // mul
        template <class A>
        XSIMD_INLINE batch<float, A> mul(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_mul_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> mul(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_mul_pd(self, other);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm512_mullo_epi32(self, other);
            }
            else
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return mul(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                          self, other);
            }
        }

        // mul_hi
        template <class A>
        XSIMD_INLINE batch<int32_t, A> mul_hi(batch<int32_t, A> const& self, batch<int32_t, A> const& other, requires_arch<avx512f>) noexcept
        {
            __m512i even = _mm512_mul_epi32(self, other);
            __m512i odd = _mm512_mul_epi32(_mm512_shuffle_epi32(self, _MM_PERM_ENUM(_MM_SHUFFLE(3, 3, 1, 1))),
                                           _mm512_shuffle_epi32(other, _MM_PERM_ENUM(_MM_SHUFFLE(3, 3, 1, 1))));
            __m512i even_hi = _mm512_srli_epi64(even, 32);
            // merge: even_hi has hi in low-32 of each 64, odd has hi in high-32 of each 64
            return _mm512_mask_blend_epi32(static_cast<__mmask16>(0xAAAA), even_hi, odd);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> mul_hi(batch<uint32_t, A> const& self, batch<uint32_t, A> const& other, requires_arch<avx512f>) noexcept
        {
            __m512i even = _mm512_mul_epu32(self, other);
            __m512i odd = _mm512_mul_epu32(_mm512_srli_epi64(self, 32), _mm512_srli_epi64(other, 32));
            __m512i even_hi = _mm512_srli_epi64(even, 32);
            return _mm512_mask_blend_epi32(static_cast<__mmask16>(0xAAAA), even_hi, odd);
        }

        template <class A>
        XSIMD_INLINE batch<uint64_t, A> mul_hi(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::mulhi_u64_core<A>(self, other,
                                             [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                             { return batch<uint64_t, A>(_mm512_mul_epu32(a, b)); });
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> mul_hi(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::mulhi_i64_core<A>(self, other,
                                             [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                             { return batch<uint64_t, A>(_mm512_mul_epu32(a, b)); });
        }

        // mul_hilo
        template <class A>
        XSIMD_INLINE std::pair<batch<uint64_t, A>, batch<uint64_t, A>>
        mul_hilo(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx512f>) noexcept
        {
            return detail::mulhilo_u64_core<A>(self, other,
                                               [](batch<uint64_t, A> a, batch<uint64_t, A> b)
                                               { return batch<uint64_t, A>(_mm512_mul_epu32(a, b)); });
        }

        // nearbyint
        template <class A>
        XSIMD_INLINE batch<float, A> nearbyint(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_round_ps(self, _MM_FROUND_TO_NEAREST_INT, _MM_FROUND_CUR_DIRECTION);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> nearbyint(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_round_pd(self, _MM_FROUND_TO_NEAREST_INT, _MM_FROUND_CUR_DIRECTION);
        }

        // nearbyint_as_int
        template <class A>
        XSIMD_INLINE batch<int32_t, A> nearbyint_as_int(batch<float, A> const& self,
                                                        requires_arch<avx512f>) noexcept
        {
            return _mm512_cvtps_epi32(self);
        }

        // neg
        template <class A, class T>
        XSIMD_INLINE batch<T, A> neg(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return 0 - self;
        }

        // neq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> neq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_ps_mask(self, other, _CMP_NEQ_UQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_cmp_pd_mask(self, other, _CMP_NEQ_UQ);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            return ~(self == other);
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data ^ other.data);
        }

        // reciprocal
        template <class A>
        XSIMD_INLINE batch<float, A>
        reciprocal(batch<float, A> const& self,
                   kernel::requires_arch<avx512f>) noexcept
        {
            return _mm512_rcp14_ps(self);
        }

        template <class A>
        XSIMD_INLINE batch<double, A>
        reciprocal(batch<double, A> const& self,
                   kernel::requires_arch<avx512f>) noexcept
        {
            return _mm512_rcp14_pd(self);
        }

        // reduce_add
        template <class A>
        XSIMD_INLINE float reduce_add(batch<float, A> const& rhs, requires_arch<avx512f>) noexcept
        {
            return _mm512_reduce_add_ps(rhs);
        }
        template <class A>
        XSIMD_INLINE double reduce_add(batch<double, A> const& rhs, requires_arch<avx512f>) noexcept
        {
            return _mm512_reduce_add_pd(rhs);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T reduce_add(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            __m256i low = detail::lower_half(self), high = detail::upper_half(self);
            batch<T, avx2> blow(low), bhigh(high);
            return reduce_add(blow, avx2 {}) + reduce_add(bhigh, avx2 {});
        }

        // reduce_max
        template <class A, class T, class = std::enable_if_t<(sizeof(T) == 1)>>
        XSIMD_INLINE T reduce_max(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            constexpr batch_constant<uint64_t, A, 5, 6, 7, 8, 0, 0, 0, 0> mask;
            batch<T, A> step = _mm512_permutexvar_epi64(mask.as_batch(), self);
            batch<T, A> acc = max(self, step);
            __m256i low = _mm512_castsi512_si256(acc);
            return reduce_max(batch<T, avx2>(low));
        }

        // reduce_min
        template <class A, class T, class = std::enable_if_t<(sizeof(T) == 1)>>
        XSIMD_INLINE T reduce_min(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            constexpr batch_constant<uint64_t, A, 5, 6, 7, 8, 0, 0, 0, 0> mask;
            batch<T, A> step = _mm512_permutexvar_epi64(mask.as_batch(), self);
            batch<T, A> acc = min(self, step);
            __m256i low = _mm512_castsi512_si256(acc);
            return reduce_min(batch<T, avx2>(low));
        }

        // reduce_mul
        template <class A>
        XSIMD_INLINE float reduce_mul(batch<float, A> const& rhs, requires_arch<avx512f>) noexcept
        {
            return _mm512_reduce_mul_ps(rhs);
        }
        template <class A>
        XSIMD_INLINE double reduce_mul(batch<double, A> const& rhs, requires_arch<avx512f>) noexcept
        {
            return _mm512_reduce_mul_pd(rhs);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE T reduce_mul(batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm512_reduce_mul_epi32(self);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_reduce_mul_epi64(self);
            }
            else
            {
                __m256i low = detail::lower_half(self), high = detail::upper_half(self);
                batch<T, avx2> blow(low), bhigh(high);
                return reduce_mul(blow, avx2 {}) * reduce_mul(bhigh, avx2 {});
            }
        }

        // rsqrt
        template <class A>
        XSIMD_INLINE batch<float, A> rsqrt(batch<float, A> const& val, requires_arch<avx512f>) noexcept
        {
            return _mm512_rsqrt14_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> rsqrt(batch<double, A> const& val, requires_arch<avx512f>) noexcept
        {
            return _mm512_rsqrt14_pd(val);
        }

        // sadd
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                auto mask = other < 0;
                auto self_pos_branch = min(std::numeric_limits<T>::max() - other, self);
                auto self_neg_branch = max(std::numeric_limits<T>::min() - other, self);
                return other + select(mask, self_neg_branch, self_pos_branch);
            }
            else
            {
                const auto diffmax = std::numeric_limits<T>::max() - self;
                const auto mindiff = min(diffmax, other);
                return self + mindiff;
            }
        }

        // scatter
        template <class A, class T,
                  class = std::enable_if_t<std::is_same_v<uint32_t, T> || std::is_same_v<int32_t, T>>>
        XSIMD_INLINE void scatter(batch<T, A> const& src, T* dst,
                                  batch<int32_t, A> const& index,
                                  kernel::requires_arch<avx512f>) noexcept
        {
            _mm512_i32scatter_epi32(dst, index, src, sizeof(T));
        }

        template <class A, class T,
                  class = std::enable_if_t<std::is_same_v<uint64_t, T> || std::is_same_v<int64_t, T>>>
        XSIMD_INLINE void scatter(batch<T, A> const& src, T* dst,
                                  batch<int64_t, A> const& index,
                                  kernel::requires_arch<avx512f>) noexcept
        {
            _mm512_i64scatter_epi64(dst, index, src, sizeof(T));
        }

        template <class A>
        XSIMD_INLINE void scatter(batch<float, A> const& src, float* dst,
                                  batch<int32_t, A> const& index,
                                  kernel::requires_arch<avx512f>) noexcept
        {
            _mm512_i32scatter_ps(dst, index, src, sizeof(float));
        }

        template <class A>
        XSIMD_INLINE void scatter(batch<double, A> const& src, double* dst,
                                  batch<int64_t, A> const& index,
                                  kernel::requires_arch<avx512f>) noexcept
        {
            _mm512_i64scatter_pd(dst, index, src, sizeof(double));
        }

        // select
        template <class A>
        XSIMD_INLINE batch<float, A> select(batch_bool<float, A> const& cond, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<avx512f>) noexcept
        {
            return _mm512_mask_blend_ps(cond, false_br, true_br);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> select(batch_bool<double, A> const& cond, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<avx512f>) noexcept
        {
            return _mm512_mask_blend_pd(cond, false_br, true_br);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                alignas(avx2::alignment()) uint8_t buffer[64];
                // FIXME: ultra inefficient
                for (int i = 0; i < 64; ++i)
                    buffer[i] = cond.data & (1ull << i) ? 0xFF : 0;
                __m256i cond_low = batch<uint8_t, avx2>::load_aligned(&buffer[0]);
                __m256i cond_hi = batch<uint8_t, avx2>::load_aligned(&buffer[32]);

                __m256i true_low = detail::lower_half(true_br), true_hi = detail::upper_half(true_br);
                __m256i false_low = detail::lower_half(false_br), false_hi = detail::upper_half(false_br);

                __m256i res_low = select(batch_bool<T, avx2>(cond_low), batch<T, avx2>(true_low), batch<T, avx2>(false_low), avx2 {});
                __m256i res_hi = select(batch_bool<T, avx2>(cond_hi), batch<T, avx2>(true_hi), batch<T, avx2>(false_hi), avx2 {});
                return detail::merge_avx(res_low, res_hi);
            }
            else if constexpr (sizeof(T) == 2)
            {
                __m256i cond_low = _mm512_maskz_cvtepi32_epi16((uint64_t)cond.data & 0xFFFF, _mm512_set1_epi32(~0));
                __m256i cond_hi = _mm512_maskz_cvtepi32_epi16((uint64_t)cond.data >> 16, _mm512_set1_epi32(~0));

                __m256i true_low = detail::lower_half(true_br), true_hi = detail::upper_half(true_br);
                __m256i false_low = detail::lower_half(false_br), false_hi = detail::upper_half(false_br);

                __m256i res_low = select(batch_bool<T, avx2>(cond_low), batch<T, avx2>(true_low), batch<T, avx2>(false_low), avx2 {});
                __m256i res_hi = select(batch_bool<T, avx2>(cond_hi), batch<T, avx2>(true_hi), batch<T, avx2>(false_hi), avx2 {});
                return detail::merge_avx(res_low, res_hi);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_mask_blend_epi32(cond, false_br, true_br);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_mask_blend_epi64(cond, false_br, true_br);
            }
            else
            {
                assert(false && "unsupported arch/type combination");
                return {};
            }
        }

        template <class A, class T, bool... Values, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx512f>) noexcept
        {
            return select(batch_bool<T, A> { Values... }, true_br, false_br, avx512f {});
        }

        namespace detail
        {
            template <class T>
            using enable_signed_integer_t = std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>,
                                                             int>;

            template <class T>
            using enable_unsigned_integer_t = std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>,
                                                               int>;
        }

        // set
        template <class A>
        XSIMD_INLINE batch<float, A> set(batch<float, A> const&, requires_arch<avx512f>, float v0, float v1, float v2, float v3, float v4, float v5, float v6, float v7, float v8, float v9, float v10, float v11, float v12, float v13, float v14, float v15) noexcept
        {
            return _mm512_setr_ps(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> set(batch<double, A> const&, requires_arch<avx512f>, double v0, double v1, double v2, double v3, double v4, double v5, double v6, double v7) noexcept
        {
            return _mm512_setr_pd(v0, v1, v2, v3, v4, v5, v6, v7);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx512f>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7) noexcept
        {
            return _mm512_set_epi64(v7, v6, v5, v4, v3, v2, v1, v0);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx512f>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7,
                                     T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15) noexcept
        {
            return _mm512_setr_epi32(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
        }
        template <class A, class T, detail::enable_signed_integer_t<T> = 0>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx512f>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7,
                                     T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15,
                                     T v16, T v17, T v18, T v19, T v20, T v21, T v22, T v23,
                                     T v24, T v25, T v26, T v27, T v28, T v29, T v30, T v31) noexcept
        {
#if defined(__clang__) || __GNUC__
            return __extension__(__m512i)(__v32hi) {
                v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15,
                v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31
            };
#else
            return _mm512_set_epi16(v31, v30, v29, v28, v27, v26, v25, v24, v23, v22, v21, v20, v19, v18, v17, v16,
                                    v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0);
#endif
        }

        template <class A, class T, detail::enable_unsigned_integer_t<T> = 0>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx512f>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7,
                                     T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15,
                                     T v16, T v17, T v18, T v19, T v20, T v21, T v22, T v23,
                                     T v24, T v25, T v26, T v27, T v28, T v29, T v30, T v31) noexcept
        {
#if defined(__clang__) || __GNUC__
            return __extension__(__m512i)(__v32hu) {
                v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15,
                v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31
            };
#else
            return _mm512_set_epi16(v31, v30, v29, v28, v27, v26, v25, v24, v23, v22, v21, v20, v19, v18, v17, v16,
                                    v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0);
#endif
        }

        template <class A, class T, detail::enable_signed_integer_t<T> = 0>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx512f>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7,
                                     T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15,
                                     T v16, T v17, T v18, T v19, T v20, T v21, T v22, T v23,
                                     T v24, T v25, T v26, T v27, T v28, T v29, T v30, T v31,
                                     T v32, T v33, T v34, T v35, T v36, T v37, T v38, T v39,
                                     T v40, T v41, T v42, T v43, T v44, T v45, T v46, T v47,
                                     T v48, T v49, T v50, T v51, T v52, T v53, T v54, T v55,
                                     T v56, T v57, T v58, T v59, T v60, T v61, T v62, T v63) noexcept
        {

#if defined(__clang__) || __GNUC__
            return __extension__(__m512i)(__v64qi) {
                v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15,
                v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31,
                v32, v33, v34, v35, v36, v37, v38, v39, v40, v41, v42, v43, v44, v45, v46, v47,
                v48, v49, v50, v51, v52, v53, v54, v55, v56, v57, v58, v59, v60, v61, v62, v63
            };
#else
            return _mm512_set_epi8(v63, v62, v61, v60, v59, v58, v57, v56, v55, v54, v53, v52, v51, v50, v49, v48,
                                   v47, v46, v45, v44, v43, v42, v41, v40, v39, v38, v37, v36, v35, v34, v33, v32,
                                   v31, v30, v29, v28, v27, v26, v25, v24, v23, v22, v21, v20, v19, v18, v17, v16,
                                   v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0);
#endif
        }
        template <class A, class T, detail::enable_unsigned_integer_t<T> = 0>
        XSIMD_INLINE batch<T, A> set(batch<T, A> const&, requires_arch<avx512f>, T v0, T v1, T v2, T v3, T v4, T v5, T v6, T v7,
                                     T v8, T v9, T v10, T v11, T v12, T v13, T v14, T v15,
                                     T v16, T v17, T v18, T v19, T v20, T v21, T v22, T v23,
                                     T v24, T v25, T v26, T v27, T v28, T v29, T v30, T v31,
                                     T v32, T v33, T v34, T v35, T v36, T v37, T v38, T v39,
                                     T v40, T v41, T v42, T v43, T v44, T v45, T v46, T v47,
                                     T v48, T v49, T v50, T v51, T v52, T v53, T v54, T v55,
                                     T v56, T v57, T v58, T v59, T v60, T v61, T v62, T v63) noexcept
        {

#if defined(__clang__) || __GNUC__
            return __extension__(__m512i)(__v64qu) {
                v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15,
                v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31,
                v32, v33, v34, v35, v36, v37, v38, v39, v40, v41, v42, v43, v44, v45, v46, v47,
                v48, v49, v50, v51, v52, v53, v54, v55, v56, v57, v58, v59, v60, v61, v62, v63
            };
#else
            return _mm512_set_epi8(v63, v62, v61, v60, v59, v58, v57, v56, v55, v54, v53, v52, v51, v50, v49, v48,
                                   v47, v46, v45, v44, v43, v42, v41, v40, v39, v38, v37, v36, v35, v34, v33, v32,
                                   v31, v30, v29, v28, v27, v26, v25, v24, v23, v22, v21, v20, v19, v18, v17, v16,
                                   v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0);
#endif
        }

        template <class A, class T, class... Values>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<avx512f>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<T, A>::size, "consistent init");
            using reg_t = typename batch_bool<T, A>::register_type;
            return ::xsimd::utils::make_bit_mask_from_bools<reg_t>(values...);
        }

        // shuffle
        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3, ITy I4, ITy I5, ITy I6, ITy I7, ITy I8, ITy I9, ITy I10, ITy I11, ITy I12, ITy I13, ITy I14, ITy I15>
        XSIMD_INLINE batch<float, A> shuffle(batch<float, A> const& x, batch<float, A> const& y,
                                             batch_constant<ITy, A, I0, I1, I2, I3, I4, I5, I6, I7, I8, I9, I10, I11, I12, I13, I14, I15> mask,
                                             requires_arch<avx512f>) noexcept
        {
            constexpr uint32_t smask = (I0 & 0x3) | ((I1 & 0x3) << 2) | ((I2 & 0x3) << 4) | ((I3 & 0x3) << 6);

            // shuffle within lane
            if ((I4 == I0 + 4) && (I5 == I1 + 4) && (I6 == I2 + 4) && (I7 == I3 + 4) && (I8 == I0 + 8) && (I9 == I1 + 8) && (I10 == I2 + 8) && (I11 == I3 + 8) && (I12 == I0 + 12) && (I13 == I1 + 12) && (I14 == I2 + 12) && (I15 == I3 + 12) && I0 < 4 && I1 < 4 && I2 >= 16 && I2 < 20 && I3 >= 16 && I3 < 20)
                return _mm512_shuffle_ps(x, y, smask);

            // shuffle within opposite lane
            if ((I4 == I0 + 4) && (I5 == I1 + 4) && (I6 == I2 + 4) && (I7 == I3 + 4) && (I8 == I0 + 8) && (I9 == I1 + 8) && (I10 == I2 + 8) && (I11 == I3 + 8) && (I12 == I0 + 12) && (I13 == I1 + 12) && (I14 == I2 + 12) && (I15 == I3 + 12) && I2 < 4 && I3 < 4 && I0 >= 16 && I0 < 20 && I1 >= 16 && I1 < 20)
                return _mm512_shuffle_ps(y, x, smask);

            return shuffle(x, y, mask, common {});
        }

        template <class A, class ITy, ITy I0, ITy I1, ITy I2, ITy I3, ITy I4, ITy I5, ITy I6, ITy I7>
        XSIMD_INLINE batch<double, A> shuffle(batch<double, A> const& x, batch<double, A> const& y, batch_constant<ITy, A, I0, I1, I2, I3, I4, I5, I6, I7> mask, requires_arch<avx512f>) noexcept
        {
            constexpr uint32_t smask = (I0 & 0x1) | ((I1 & 0x1) << 1) | ((I2 & 0x1) << 2) | ((I3 & 0x1) << 3) | ((I4 & 0x1) << 4) | ((I5 & 0x1) << 5) | ((I6 & 0x1) << 6) | ((I7 & 0x1) << 7);
            // shuffle within lane
            if (I0 < 2 && I1 >= 8 && I1 < 10 && I2 >= 2 && I2 < 4 && I3 >= 10 && I3 < 12 && I4 >= 4 && I4 < 6 && I5 >= 12 && I5 < 14 && I6 >= 6 && I6 < 8 && I7 >= 14)
                return _mm512_shuffle_pd(x, y, smask);

            // shuffle within opposite lane
            if (I1 < 2 && I0 >= 8 && I0 < 10 && I3 >= 2 && I3 < 4 && I2 >= 10 && I2 < 12 && I5 >= 4 && I5 < 6 && I4 >= 12 && I4 < 14 && I7 >= 6 && I7 < 8 && I6 >= 14)
                return _mm512_shuffle_pd(y, x, smask);

            return shuffle(x, y, mask, common {});
        }

        // slide_left
        namespace detail
        {
            template <size_t N>
            struct make_slide_left_pattern
            {
                static constexpr size_t get(size_t i, size_t)
                {
                    return i >= N ? i - N : 0;
                }
            };

            template <size_t N, class A, class T>
            XSIMD_INLINE batch<T, A> slide_left_aligned_u32(batch<T, A> const& x, requires_arch<avx512f>) noexcept
            {
                static_assert((N & 3) == 0 || N >= 64, "N must be aligned to 32 bits");

                if (N == 0)
                {
                    return x;
                }
                if (N >= 64)
                {
                    return batch<T, A>(T(0));
                }

                __mmask16 mask = uint16_t(0xFFFFu << (N / 4));

                if ((N & 15) == 0)
                {
                    const uint8_t imm8 = uint8_t(0xe4 << (2 * (N / 16)));
                    return _mm512_maskz_shuffle_i32x4(mask, x, x, imm8);
                }

                auto slide_pattern = make_batch_constant<uint32_t, detail::make_slide_left_pattern<N / 4>, A>();
                return _mm512_maskz_permutexvar_epi32(mask, slide_pattern.as_batch(), x);
            }
        }

        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_left(batch<T, A> const& x, requires_arch<avx512f>) noexcept
        {
            constexpr size_t NN = N & ~3;
            if (N == NN || NN >= 64)
            {
                // Call fast path
                return detail::slide_left_aligned_u32<NN>(x, A {});
            }

            __m512i xl = detail::slide_left_aligned_u32<NN, A, T>(_mm512_slli_epi32(x, 8 * (N - NN)), A {});
            __m512i xr = detail::slide_left_aligned_u32<NN + 4, A, T>(_mm512_srli_epi32(x, 32 - 8 * (N - NN)), A {});
            return _mm512_or_epi32(xl, xr);
        }

        // slide_right
        namespace detail
        {
            template <size_t N>
            struct make_slide_right_pattern
            {
                static constexpr size_t get(size_t i, size_t n)
                {
                    return i < (n - N) ? i + N : 0;
                }
            };

            template <size_t N, class A, class T>
            XSIMD_INLINE batch<T, A> slide_right_aligned_u32(batch<T, A> const& x, requires_arch<avx512f>) noexcept
            {
                static_assert((N & 3) == 0 || N >= 64, "N must be aligned to 32 bits");

                if (N == 0)
                {
                    return x;
                }
                if (N >= 64)
                {
                    return batch<T, A>(T(0));
                }

                __mmask16 mask = 0xFFFFu >> (N / 4);

                if ((N & 15) == 0)
                {
                    const uint8_t imm8 = 0xe4 >> (2 * (N / 16));
                    return _mm512_maskz_shuffle_i32x4(mask, x, x, imm8);
                }

                auto slide_pattern = make_batch_constant<uint32_t, detail::make_slide_right_pattern<N / 4>, A>();
                return _mm512_maskz_permutexvar_epi32(mask, slide_pattern.as_batch(), x);
            }
        }
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> slide_right(batch<T, A> const& x, requires_arch<avx512f>) noexcept
        {
            constexpr size_t NN = N & ~3;
            if (N == NN || NN >= 64)
            {
                // Call fast path
                return detail::slide_right_aligned_u32<NN>(x, A {});
            }

            __m512i xl = detail::slide_right_aligned_u32<NN + 4, A, T>(_mm512_slli_epi32(x, 32 - 8 * (N - NN)), A {});
            __m512i xr = detail::slide_right_aligned_u32<NN, A, T>(_mm512_srli_epi32(x, 8 * (N - NN)), A {});
            return _mm512_or_epi32(xl, xr);
        }

        // sqrt
        template <class A>
        XSIMD_INLINE batch<float, A> sqrt(batch<float, A> const& val, requires_arch<avx512f>) noexcept
        {
            return _mm512_sqrt_ps(val);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sqrt(batch<double, A> const& val, requires_arch<avx512f>) noexcept
        {
            return _mm512_sqrt_pd(val);
        }

        // ssub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            // Saturating sub for 8/16-bit integers needs AVX512BW; on AVX512F
            // fall back to the AVX2 implementation on each 256-bit half.
            if constexpr (sizeof(T) == 1 || sizeof(T) == 2)
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return ssub(batch<T, avx2>(s), batch<T, avx2>(o), avx2 {}); },
                                          self, other);
            }
            else if (std::is_signed_v<T>)
            {
                auto mask = other < 0;
                auto self_overflow_branch = min(std::numeric_limits<T>::max() + other, self);
                auto self_underflow_branch = max(std::numeric_limits<T>::min() + other, self);
                return select(mask, self_overflow_branch, self_underflow_branch) - other;
            }
            else
            {
                const auto diff = min(self, other);
                return self - diff;
            }
        }

        // store
        template <class T, class A>
        XSIMD_INLINE void store(batch_bool<T, A> const& self, bool* mem, requires_arch<avx512f>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            constexpr auto size = batch_bool<T, A>::size;
            for (std::size_t i = 0; i < size; ++i)
                mem[i] = self.data & (register_type(1) << i);
        }

        // store_aligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_store_si512((__m512i*)mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_aligned(T* mem, batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_store_si512((__m512i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_aligned(float* mem, batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_store_ps(mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_aligned(double* mem, batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_store_pd(mem, self);
        }

        // store_unaligned
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_storeu_si512((__m512i*)mem, self);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE void store_unaligned(T* mem, batch_bool<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_storeu_si512((__m512i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_unaligned(float* mem, batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_storeu_ps(mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_unaligned(double* mem, batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_storeu_pd(mem, self);
        }

        // store_stream
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>, void>>
        XSIMD_INLINE void store_stream(T* mem, batch<T, A> const& self, requires_arch<avx512f>) noexcept
        {
            _mm512_stream_si512((__m512i*)mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_stream(float* mem, batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            _mm512_stream_ps(mem, self);
        }
        template <class A>
        XSIMD_INLINE void store_stream(double* mem, batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            _mm512_stream_pd(mem, self);
        }

        // sub
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return sub(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                          self, other);
            }
            else if constexpr (sizeof(T) == 2)
            {
                return detail::fwd_to_avx([](__m256i s, __m256i o) noexcept
                                          { return sub(batch<T, avx2>(s), batch<T, avx2>(o)); },
                                          self, other);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm512_sub_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm512_sub_epi64(self, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A> sub(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_sub_ps(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sub(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            return _mm512_sub_pd(self, other);
        }

        // swizzle (dynamic version)
        template <class A>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch<uint32_t, A> mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_permutexvar_ps(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch<uint64_t, A> mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_permutexvar_pd(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch<uint64_t, A> mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_permutexvar_epi64(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self, batch<uint64_t, A> mask, requires_arch<avx512f>) noexcept
        {
            return bitwise_cast<int64_t>(swizzle(bitwise_cast<uint64_t>(self), mask, avx512f {}));
        }

        template <class A>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch<uint32_t, A> mask, requires_arch<avx512f>) noexcept
        {
            return _mm512_permutexvar_epi32(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self, batch<uint32_t, A> mask, requires_arch<avx512f>) noexcept
        {
            return bitwise_cast<int32_t>(swizzle(bitwise_cast<uint32_t>(self), mask, avx512f {}));
        }

        template <class A, uint32_t V0, uint32_t V1, uint32_t V2, uint32_t V3, uint32_t V4, uint32_t V5, uint32_t V6, uint32_t V7,
                  uint32_t V8, uint32_t V9, uint32_t V10, uint32_t V11, uint32_t V12, uint32_t V13, uint32_t V14, uint32_t V15>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self,
                                             batch_constant<uint32_t, A, V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15> mask,
                                             requires_arch<avx512f>) noexcept
        {
            if constexpr (detail::is_identity(mask))
            {
                return self;
            }
            if constexpr (!detail::is_cross_lane(mask))
            {
                constexpr int imm0 = detail::mod_shuffle(V0, V1, V2, V3);
                constexpr int imm1 = detail::mod_shuffle(V4, V5, V6, V7);
                constexpr int imm2 = detail::mod_shuffle(V8, V9, V10, V11);
                constexpr int imm3 = detail::mod_shuffle(V12, V13, V14, V15);
                if constexpr (imm0 == imm1 && imm0 == imm2 && imm0 == imm3)
                {
                    return _mm512_permute_ps(self, imm0);
                }
            }
            return swizzle(self, mask.as_batch(), avx512f {});
        }
        template <class A, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3, uint64_t V4, uint64_t V5, uint64_t V6, uint64_t V7>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self,
                                              batch_constant<uint64_t, A, V0, V1, V2, V3, V4, V5, V6, V7> mask,
                                              requires_arch<avx512f>) noexcept
        {
            if constexpr (detail::is_identity(mask))
            {
                return self;
            }
            if constexpr (!detail::is_cross_lane(mask))
            {
                constexpr auto imm = ((V0 & 1) << 0) | ((V1 & 1) << 1) | ((V2 & 1) << 2) | ((V3 & 1) << 3) | ((V4 & 1) << 4) | ((V5 & 1) << 5) | ((V6 & 1) << 6) | ((V7 & 1) << 7);
                return _mm512_permute_pd(self, imm);
            }
            constexpr bool dup_lo = detail::is_dup_lo(mask);
            constexpr bool dup_hi = detail::is_dup_hi(mask);
            if constexpr (dup_lo || dup_hi)
            {
                const batch<double, avx2> half = _mm512_extractf64x4_pd(self, dup_lo ? 0 : 1);
                constexpr std::conditional_t<dup_lo, batch_constant<uint64_t, avx2, V0 % 4, V1 % 4, V2 % 4, V3 % 4>,
                                             batch_constant<uint64_t, avx2, V4 % 4, V5 % 4, V6 % 4, V7 % 4>>
                    half_mask {};
                return _mm512_broadcast_f64x4(swizzle(half, half_mask, avx2 {}));
            }
            // General case
            return swizzle(self, mask.as_batch(), avx512f {});
        }

        template <class A, uint64_t... Vs>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch_constant<uint64_t, A, Vs...> mask, requires_arch<avx512f>) noexcept
        {
            return swizzle(self, mask.as_batch(), avx512f {});
        }

        template <class A, uint64_t... Vs>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self, batch_constant<uint64_t, A, Vs...> mask, requires_arch<avx512f>) noexcept
        {
            return swizzle(self, mask.as_batch(), avx512f {});
        }

        template <class A, uint32_t... Vs>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch_constant<uint32_t, A, Vs...> mask, requires_arch<avx512f>) noexcept
        {
            return swizzle(self, mask.as_batch(), avx512f {});
        }

        template <class A, uint32_t... Vs>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self, batch_constant<uint32_t, A, Vs...> mask, requires_arch<avx512f>) noexcept
        {
            return swizzle(self, mask.as_batch(), avx512f {});
        }

        namespace detail
        {
            template <class T, T... Idx, std::size_t... Is>
            constexpr bool is_pair_of_contiguous_indices_impl(std::index_sequence<Is...>) noexcept
            {
                constexpr T idx[] = { Idx... };
                return (... && (idx[2 * Is] % 2 == 0 && idx[2 * Is] + 1 == idx[2 * Is + 1]));
            }

            template <class T, T... Idx>
            constexpr bool is_pair_of_contiguous_indices() noexcept
            {
                static_assert(sizeof...(Idx) % 2 == 0, "indices come in pairs");
                return is_pair_of_contiguous_indices_impl<T, Idx...>(std::make_index_sequence<sizeof...(Idx) / 2>());
            }

            template <class A, uint16_t I0, uint16_t I1, uint16_t I2, uint16_t I3, uint16_t I4, uint16_t I5, uint16_t I6, uint16_t I7,
                      uint16_t I8, uint16_t I9, uint16_t I10, uint16_t I11, uint16_t I12, uint16_t I13, uint16_t I14, uint16_t I15,
                      uint16_t I16, uint16_t I17, uint16_t I18, uint16_t I19, uint16_t I20, uint16_t I21, uint16_t I22, uint16_t I23,
                      uint16_t I24, uint16_t I25, uint16_t I26, uint16_t I27, uint16_t I28, uint16_t I29, uint16_t I30, uint16_t I31>
            struct fold_batch_constant
            {
                using type = batch_constant<uint32_t, A, I0 / 2, I2 / 2, I4 / 2, I6 / 2, I8 / 2, I10 / 2, I12 / 2, I14 / 2,
                                            I16 / 2, I18 / 2, I20 / 2, I22 / 2, I24 / 2, I26 / 2, I28 / 2, I30 / 2>;
            };

            template <class A, uint16_t... Is>
            constexpr bool is_reduce_pattern()
            {
                // The actual pattern is {1, 1, 0, 1, 0, 1, ..., 0, 1}
                if (sizeof...(Is) != batch<uint16_t, A>::size)
                    return false;
                uint16_t pattern[] = { Is... };
                if (pattern[0] != 1)
                    return false;
                for (size_t i = 1; i < sizeof...(Is); i += 1)
                {
                    if (pattern[i] != (i & 1))
                        return false;
                }
                return true;
            }
        }

        template <class A, uint16_t... Idx>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch_constant<uint16_t, A, Idx...> mask, requires_arch<avx512f>) noexcept
        {
            if constexpr (detail::is_pair_of_contiguous_indices<uint16_t, Idx...>())
            {
                constexpr typename detail::fold_batch_constant<A, Idx...>::type mask32;
                return _mm512_permutexvar_epi32(static_cast<batch<uint32_t, A>>(mask32), self);
            }
            else if constexpr (detail::is_reduce_pattern<A, Idx...>())
            {
                // FIXME: this sequence is very inefficient, but it's here to catch
                // a pattern generated by detail::reduce from xsimd_common_math.hpp.
                // The whole pattern is actually decently folded by GCC and Clang,
                // so bare with it.
                constexpr batch_constant<uint32_t, A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0> mask32;
                auto tmp = _mm512_permutexvar_epi32(static_cast<batch<uint32_t, A>>(mask32), self);

                alignas(A::alignment()) uint16_t buffer[32];
                _mm512_store_si512((__m512i*)&buffer[0], tmp);
                buffer[0] = buffer[1];
                return _mm512_load_si512(&buffer[0]);
            }
            else
            {
                return swizzle(self, mask, common {});
            }
        }

        template <class A, uint16_t... Vs>
        XSIMD_INLINE batch<int16_t, A>
        swizzle(batch<int16_t, A> const& self, batch_constant<uint16_t, A, Vs...> mask, requires_arch<avx512f>) noexcept
        {
            return bitwise_cast<int16_t>(swizzle(bitwise_cast<uint16_t>(self), mask, avx512f {}));
        }

        // transpose
        template <class A>
        XSIMD_INLINE void transpose(batch<uint16_t, A>* matrix_begin, batch<uint16_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<uint16_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            batch<uint16_t, avx2> tmp_lo0[16];
            for (int i = 0; i < 16; ++i)
                tmp_lo0[i] = _mm512_castsi512_si256(matrix_begin[i]);
            transpose(tmp_lo0 + 0, tmp_lo0 + 16, avx2 {});

            batch<uint16_t, avx2> tmp_hi0[16];
            for (int i = 0; i < 16; ++i)
                tmp_hi0[i] = _mm512_castsi512_si256(matrix_begin[16 + i]);
            transpose(tmp_hi0 + 0, tmp_hi0 + 16, avx2 {});

            batch<uint16_t, avx2> tmp_lo1[16];
            for (int i = 0; i < 16; ++i)
                tmp_lo1[i] = _mm512_extracti64x4_epi64(matrix_begin[i], 1);
            transpose(tmp_lo1 + 0, tmp_lo1 + 16, avx2 {});

            batch<uint16_t, avx2> tmp_hi1[16];
            for (int i = 0; i < 16; ++i)
                tmp_hi1[i] = _mm512_extracti64x4_epi64(matrix_begin[16 + i], 1);
            transpose(tmp_hi1 + 0, tmp_hi1 + 16, avx2 {});

            for (int i = 0; i < 16; ++i)
                matrix_begin[i] = detail::merge_avx(tmp_lo0[i], tmp_hi0[i]);
            for (int i = 0; i < 16; ++i)
                matrix_begin[i + 16] = detail::merge_avx(tmp_lo1[i], tmp_hi1[i]);
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int16_t, A>* matrix_begin, batch<int16_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            return transpose(reinterpret_cast<batch<uint16_t, A>*>(matrix_begin), reinterpret_cast<batch<uint16_t, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint8_t, A>* matrix_begin, batch<uint8_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<uint8_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            batch<uint8_t, avx2> tmp_lo0[32];
            for (int i = 0; i < 32; ++i)
                tmp_lo0[i] = _mm512_castsi512_si256(matrix_begin[i]);
            transpose(tmp_lo0 + 0, tmp_lo0 + 32, avx2 {});

            batch<uint8_t, avx2> tmp_hi0[32];
            for (int i = 0; i < 32; ++i)
                tmp_hi0[i] = _mm512_castsi512_si256(matrix_begin[32 + i]);
            transpose(tmp_hi0 + 0, tmp_hi0 + 32, avx2 {});

            batch<uint8_t, avx2> tmp_lo1[32];
            for (int i = 0; i < 32; ++i)
                tmp_lo1[i] = _mm512_extracti64x4_epi64(matrix_begin[i], 1);
            transpose(tmp_lo1 + 0, tmp_lo1 + 32, avx2 {});

            batch<uint8_t, avx2> tmp_hi1[32];
            for (int i = 0; i < 32; ++i)
                tmp_hi1[i] = _mm512_extracti64x4_epi64(matrix_begin[32 + i], 1);
            transpose(tmp_hi1 + 0, tmp_hi1 + 32, avx2 {});

            for (int i = 0; i < 32; ++i)
                matrix_begin[i] = detail::merge_avx(tmp_lo0[i], tmp_hi0[i]);
            for (int i = 0; i < 32; ++i)
                matrix_begin[i + 32] = detail::merge_avx(tmp_lo1[i], tmp_hi1[i]);
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int8_t, A>* matrix_begin, batch<int8_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            return transpose(reinterpret_cast<batch<uint8_t, A>*>(matrix_begin), reinterpret_cast<batch<uint8_t, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<float, A>* matrix_begin, batch<float, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<float, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            __m512 t[16], u[16];
            for (int k = 0; k < 8; ++k)
            {
                t[2 * k + 0] = _mm512_unpacklo_ps(matrix_begin[2 * k], matrix_begin[2 * k + 1]);
                t[2 * k + 1] = _mm512_unpackhi_ps(matrix_begin[2 * k], matrix_begin[2 * k + 1]);
            }
            for (int k = 0; k < 4; ++k)
            {
                u[4 * k + 0] = _mm512_shuffle_ps(t[4 * k + 0], t[4 * k + 2], 0x44);
                u[4 * k + 1] = _mm512_shuffle_ps(t[4 * k + 0], t[4 * k + 2], 0xEE);
                u[4 * k + 2] = _mm512_shuffle_ps(t[4 * k + 1], t[4 * k + 3], 0x44);
                u[4 * k + 3] = _mm512_shuffle_ps(t[4 * k + 1], t[4 * k + 3], 0xEE);
            }
            for (int k = 0; k < 4; ++k)
            {
                t[k + 0] = _mm512_shuffle_f32x4(u[k + 0], u[k + 4], 0x88);
                t[k + 4] = _mm512_shuffle_f32x4(u[k + 0], u[k + 4], 0xdd);
                t[k + 8] = _mm512_shuffle_f32x4(u[k + 8], u[k + 12], 0x88);
                t[k + 12] = _mm512_shuffle_f32x4(u[k + 8], u[k + 12], 0xdd);
            }
            for (int k = 0; k < 8; ++k)
            {
                matrix_begin[k + 0] = _mm512_shuffle_f32x4(t[k], t[k + 8], 0x88);
                matrix_begin[k + 8] = _mm512_shuffle_f32x4(t[k], t[k + 8], 0xdd);
            }
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<uint32_t, A>* matrix_begin, batch<uint32_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            return transpose(reinterpret_cast<batch<float, A>*>(matrix_begin), reinterpret_cast<batch<float, A>*>(matrix_end), A {});
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int32_t, A>* matrix_begin, batch<int32_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            return transpose(reinterpret_cast<batch<float, A>*>(matrix_begin), reinterpret_cast<batch<float, A>*>(matrix_end), A {});
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<double, A>* matrix_begin, batch<double, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<double, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            __m512d t[8], u[8];
            for (int k = 0; k < 4; ++k)
            {
                t[2 * k + 0] = _mm512_unpacklo_pd(matrix_begin[2 * k], matrix_begin[2 * k + 1]);
                t[2 * k + 1] = _mm512_unpackhi_pd(matrix_begin[2 * k], matrix_begin[2 * k + 1]);
            }
            for (int k = 0; k < 2; ++k)
            {
                u[4 * k + 0] = _mm512_shuffle_f64x2(t[4 * k + 0], t[4 * k + 2], 0x88);
                u[4 * k + 1] = _mm512_shuffle_f64x2(t[4 * k + 1], t[4 * k + 3], 0x88);
                u[4 * k + 2] = _mm512_shuffle_f64x2(t[4 * k + 0], t[4 * k + 2], 0xdd);
                u[4 * k + 3] = _mm512_shuffle_f64x2(t[4 * k + 1], t[4 * k + 3], 0xdd);
            }
            for (int k = 0; k < 4; ++k)
            {
                matrix_begin[k + 0] = _mm512_shuffle_f64x2(u[k], u[k + 4], 0x88);
                matrix_begin[k + 4] = _mm512_shuffle_f64x2(u[k], u[k + 4], 0xdd);
            }
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<uint64_t, A>* matrix_begin, batch<uint64_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            return transpose(reinterpret_cast<batch<double, A>*>(matrix_begin), reinterpret_cast<batch<double, A>*>(matrix_end), A {});
        }
        template <class A>
        XSIMD_INLINE void transpose(batch<int64_t, A>* matrix_begin, batch<int64_t, A>* matrix_end, requires_arch<avx512f>) noexcept
        {
            return transpose(reinterpret_cast<batch<double, A>*>(matrix_begin), reinterpret_cast<batch<double, A>*>(matrix_end), A {});
        }

        // trunc
        template <class A>
        XSIMD_INLINE batch<float, A>
        trunc(batch<float, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_round_ps(self, _MM_FROUND_TO_ZERO, _MM_FROUND_CUR_DIRECTION);
        }
        template <class A>
        XSIMD_INLINE batch<double, A>
        trunc(batch<double, A> const& self, requires_arch<avx512f>) noexcept
        {
            return _mm512_roundscale_round_pd(self, _MM_FROUND_TO_ZERO, _MM_FROUND_CUR_DIRECTION);
        }

        // zip_hi
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A>
        zip_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                assert(false && "not implemented yet");
                return {};
            }
            else if constexpr (sizeof(T) == 2)
            {
                assert(false && "not implemented yet");
                return {};
            }
            else if constexpr (sizeof(T) == 4)
            {
                __m512i idx = _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27,
                                                12, 28, 13, 29, 14, 30, 15, 31);
                return _mm512_permutex2var_epi32(self, idx, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                __m512i idx = _mm512_setr_epi64(4, 12, 5, 13, 6, 14, 7, 15);
                return _mm512_permutex2var_epi64(self, idx, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A>
        zip_hi(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            __m512i idx = _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27,
                                            12, 28, 13, 29, 14, 30, 15, 31);
            return _mm512_permutex2var_ps(self, idx, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A>
        zip_hi(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            __m512i idx = _mm512_setr_epi64(4, 12, 5, 13, 6, 14, 7, 15);
            return _mm512_permutex2var_pd(self, idx, other);
        }

        // zip_lo
        // See zip_hi: one vpermt2{d,q,ps,pd} in place of the unpack+insert128 pile.
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A>
        zip_lo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512f>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                assert(false && "not implemented yet");
                return {};
            }
            else if constexpr (sizeof(T) == 2)
            {
                assert(false && "not implemented yet");
                return {};
            }
            else if constexpr (sizeof(T) == 4)
            {
                __m512i idx = _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19,
                                                4, 20, 5, 21, 6, 22, 7, 23);
                return _mm512_permutex2var_epi32(self, idx, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                __m512i idx = _mm512_setr_epi64(0, 8, 1, 9, 2, 10, 3, 11);
                return _mm512_permutex2var_epi64(self, idx, other);
            }
            else
            {
                assert(false && "unsupported arch/op combination");
                return {};
            }
        }
        template <class A>
        XSIMD_INLINE batch<float, A>
        zip_lo(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512f>) noexcept
        {
            __m512i idx = _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19,
                                            4, 20, 5, 21, 6, 22, 7, 23);
            return _mm512_permutex2var_ps(self, idx, other);
        }
        template <class A>
        XSIMD_INLINE batch<double, A>
        zip_lo(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512f>) noexcept
        {
            __m512i idx = _mm512_setr_epi64(0, 8, 1, 9, 2, 10, 3, 11);
            return _mm512_permutex2var_pd(self, idx, other);
        }

        // widen
        template <class A, class T>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<avx512f>) noexcept
        {
            __m256i x_lo = detail::lower_half(x);
            __m256i x_hi = detail::upper_half(x);
            __m512i lo, hi;
            if constexpr (sizeof(T) == 4)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm512_cvtepi32_epi64(x_lo);
                    hi = _mm512_cvtepi32_epi64(x_hi);
                }
                else
                {
                    lo = _mm512_cvtepu32_epi64(x_lo);
                    hi = _mm512_cvtepu32_epi64(x_hi);
                }
            }
            else if constexpr (sizeof(T) == 2)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    lo = _mm512_cvtepi16_epi32(x_lo);
                    hi = _mm512_cvtepi16_epi32(x_hi);
                }
                else
                {
                    lo = _mm512_cvtepu16_epi32(x_lo);
                    hi = _mm512_cvtepu16_epi32(x_hi);
                }
            }
            else if constexpr (sizeof(T) == 1)
            {
                auto pair_lo = widen(batch<T, avx2>(x_lo), avx2 {});
                auto pair_hi = widen(batch<T, avx2>(x_hi), avx2 {});
                return { detail::merge_avx(pair_lo[0], pair_lo[1]), detail::merge_avx(pair_hi[0], pair_hi[1]) };
            }
            return { lo, hi };
        }
        template <class A>
        XSIMD_INLINE std::array<batch<double, A>, 2> widen(batch<float, A> const& x, requires_arch<avx512f>) noexcept
        {
            __m512d lo = _mm512_cvtps_pd(detail::lower_half(x));
            __m512d hi = _mm512_cvtps_pd(detail::upper_half(x));
            return { lo, hi };
        }

    }
}

#endif
