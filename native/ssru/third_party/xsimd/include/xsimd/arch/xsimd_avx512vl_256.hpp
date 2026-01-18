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

#ifndef XSIMD_AVX512VL_256_HPP
#define XSIMD_AVX512VL_256_HPP

#include "../types/xsimd_avx512vl_register.hpp"
#include "../types/xsimd_batch_constant.hpp"
#include "../utils/bits.hpp"

#include <type_traits>

namespace xsimd
{
    namespace kernel
    {
        using namespace types;

        namespace detail
        {
            // Defined in xsimd_avx512f.hpp. This header is included before it so
            // that avx512f.hpp's masked load/store forwarder can resolve the
            // avx512vl_256 overloads below by ordinary lookup; forward-declare
            // the two helpers it borrows from there.
            XSIMD_INLINE uint32_t morton(uint16_t x, uint16_t y) noexcept;
            template <size_t N>
            XSIMD_INLINE unsigned char tobitset(unsigned char unpacked[N]);

            template <class A, class T, int Cmp>
            XSIMD_INLINE batch_bool<T, A> compare_int_avx512vl_256(batch<T, A> const& self, batch<T, A> const& other) noexcept
            {
                using register_type = typename batch_bool<T, A>::register_type;
                if (std::is_signed_v<T>)
                {
                    if constexpr (sizeof(T) == 1)
                    {
                        // shifting to take sign into account
                        uint64_t mask_low0 = _mm256_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x000000FF)) << 24,
                                                                   (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x000000FF)) << 24,
                                                                   Cmp);
                        uint64_t mask_low1 = _mm256_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x0000FF00)) << 16,
                                                                   (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x0000FF00)) << 16,
                                                                   Cmp);
                        uint64_t mask_high0 = _mm256_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x00FF0000)) << 8,
                                                                    (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x00FF0000)) << 8,
                                                                    Cmp);
                        uint64_t mask_high1 = _mm256_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0xFF000000)),
                                                                    (batch<int32_t, A>(other.data) & batch<int32_t, A>(0xFF000000)),
                                                                    Cmp);
                        uint64_t mask = 0;
                        for (unsigned i = 0; i < 8; ++i)
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
                        uint16_t mask_low = _mm256_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0x0000FFFF)) << 16,
                                                                  (batch<int32_t, A>(other.data) & batch<int32_t, A>(0x0000FFFF)) << 16,
                                                                  Cmp);
                        uint16_t mask_high = _mm256_cmp_epi32_mask((batch<int32_t, A>(self.data) & batch<int32_t, A>(0xFFFF0000)),
                                                                   (batch<int32_t, A>(other.data) & batch<int32_t, A>(0xFFFF0000)),
                                                                   Cmp);
                        return static_cast<register_type>(morton(mask_low, mask_high));
                    }
                    else if constexpr (sizeof(T) == 4)
                    {
                        return (register_type)_mm256_cmp_epi32_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 8)
                    {
                        return (register_type)_mm256_cmp_epi64_mask(self, other, Cmp);
                    }
                }
                else
                {
                    if constexpr (sizeof(T) == 1)
                    {
                        uint64_t mask_low0 = _mm256_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x000000FF)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x000000FF)), Cmp);
                        uint64_t mask_low1 = _mm256_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x0000FF00)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x0000FF00)), Cmp);
                        uint64_t mask_high0 = _mm256_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x00FF0000)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x00FF0000)), Cmp);
                        uint64_t mask_high1 = _mm256_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0xFF000000)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0xFF000000)), Cmp);
                        uint64_t mask = 0;
                        for (unsigned i = 0; i < 8; ++i)
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
                        uint16_t mask_low = _mm256_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0x0000FFFF)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0x0000FFFF)), Cmp);
                        uint16_t mask_high = _mm256_cmp_epu32_mask((batch<uint32_t, A>(self.data) & batch<uint32_t, A>(0xFFFF0000)), (batch<uint32_t, A>(other.data) & batch<uint32_t, A>(0xFFFF0000)), Cmp);
                        return static_cast<register_type>(morton(mask_low, mask_high));
                    }
                    else if constexpr (sizeof(T) == 4)
                    {
                        return (register_type)_mm256_cmp_epu32_mask(self, other, Cmp);
                    }
                    else if constexpr (sizeof(T) == 8)
                    {
                        return (register_type)_mm256_cmp_epu64_mask(self, other, Cmp);
                    }
                }
            }
        }

        // load mask
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            constexpr auto size = batch_bool<T, A>::size;
            constexpr auto chunk_size = size >= 8 ? 8 : 4;
            constexpr auto iter = size / chunk_size;
            static_assert((size % chunk_size) == 0, "incorrect size of bool batch");
            register_type mask = 0;
            for (std::size_t i = 0; i < iter; ++i)
            {
                unsigned char block = detail::tobitset<chunk_size>((unsigned char*)mem + i * chunk_size);
                mask |= (register_type(block) << (i * chunk_size));
            }
            return mask;
        }

        // from bool
        template <class A, class T>
        XSIMD_INLINE batch<T, A> from_bool(batch_bool<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            return select(self, batch<T, A>(1), batch<T, A>(0));
        }

        // from_mask
        template <class T, class A>
        XSIMD_INLINE batch_bool<T, A> from_mask(batch_bool<T, A> const&, uint64_t mask, requires_arch<avx512vl_256>) noexcept
        {
            assert(mask == (mask & ((uint64_t(1) << batch_bool<T, A>::size) - 1)) && "inbound mask");
            return static_cast<typename batch_bool<T, A>::register_type>(mask & ((uint64_t(1) << batch_bool<T, A>::size) - 1));
        }

        // mask
        template <class A, class T>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            return self.data & ((uint64_t(1) << batch_bool<T, A>::size) - 1);
        }

        // batch_bool_cast
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch_bool<T_out, A> batch_bool_cast(batch_bool<T_in, A> const& self, batch_bool<T_out, A> const&, requires_arch<avx512vl_256>) noexcept
        {
            return self.data;
        }

        // set
        template <class A, class T, class... Values>
        XSIMD_INLINE batch_bool<T, A> set(batch_bool<T, A> const&, requires_arch<avx512vl_256>, Values... values) noexcept
        {
            static_assert(sizeof...(Values) == batch_bool<T, A>::size, "consistent init");
            using reg_t = typename batch_bool<T, A>::register_type;
            return ::xsimd::utils::make_bit_mask_from_bools<reg_t>(values...);
        }

        // store
        template <class T, class A>
        XSIMD_INLINE void store(batch_bool<T, A> const& self, bool* mem, requires_arch<avx512vl_256>) noexcept
        {
            constexpr auto size = batch_bool<T, A>::size;
            for (std::size_t i = 0; i < size; ++i)
                mem[i] = (self.data >> i) & 0x1;
        }

        // abs
        template <class A>
        XSIMD_INLINE batch<int64_t, A> abs(batch<int64_t, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_abs_epi64(self);
        }

        // Masked load/store: native 256-bit EVEX predication shared by the
        // constant (batch_bool_constant) and runtime (batch_bool) overloads.
        // Partial ordering picks the avx512vl_256 tag over the avx2 bridges this
        // arch inherits — crucial because the k-register mask cannot feed the
        // avx2 vpmaskmov path. 8/16-bit elements fall back to the common scalar
        // path. Unsigned element types reinterpret to the signed EVEX intrinsic.
        namespace detail
        {
            // One core per native register type; signed and unsigned integrals
            // share an overload (the EVEX intrinsic is sign-agnostic). Mode
            // selects aligned vs unaligned.
            template <class T, class Mode, enable_sized_integral_t<T, 4> = 0>
            XSIMD_INLINE __m256i maskload256(T const* mem, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    return _mm256_maskz_load_epi32((__mmask8)m, mem);
                }
                else
                {
                    return _mm256_maskz_loadu_epi32((__mmask8)m, mem);
                }
            }
            template <class T, class Mode, enable_sized_integral_t<T, 8> = 0>
            XSIMD_INLINE __m256i maskload256(T const* mem, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    return _mm256_maskz_load_epi64((__mmask8)m, mem);
                }
                else
                {
                    return _mm256_maskz_loadu_epi64((__mmask8)m, mem);
                }
            }
            template <class Mode>
            XSIMD_INLINE __m256 maskload256(float const* mem, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    return _mm256_maskz_load_ps((__mmask8)m, mem);
                }
                else
                {
                    return _mm256_maskz_loadu_ps((__mmask8)m, mem);
                }
            }
            template <class Mode>
            XSIMD_INLINE __m256d maskload256(double const* mem, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    return _mm256_maskz_load_pd((__mmask8)m, mem);
                }
                else
                {
                    return _mm256_maskz_loadu_pd((__mmask8)m, mem);
                }
            }

            template <class T, class Mode, enable_sized_integral_t<T, 4> = 0>
            XSIMD_INLINE void maskstore256(T* mem, __m256i src, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    _mm256_mask_store_epi32(mem, (__mmask8)m, src);
                }
                else
                {
                    _mm256_mask_storeu_epi32(mem, (__mmask8)m, src);
                }
            }
            template <class T, class Mode, enable_sized_integral_t<T, 8> = 0>
            XSIMD_INLINE void maskstore256(T* mem, __m256i src, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    _mm256_mask_store_epi64(mem, (__mmask8)m, src);
                }
                else
                {
                    _mm256_mask_storeu_epi64(mem, (__mmask8)m, src);
                }
            }
            template <class Mode>
            XSIMD_INLINE void maskstore256(float* mem, __m256 src, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    _mm256_mask_store_ps(mem, (__mmask8)m, src);
                }
                else
                {
                    _mm256_mask_storeu_ps(mem, (__mmask8)m, src);
                }
            }
            template <class Mode>
            XSIMD_INLINE void maskstore256(double* mem, __m256d src, uint64_t m, Mode) noexcept
            {
                if constexpr (std::is_same_v<Mode, aligned_mode>)
                {
                    _mm256_mask_store_pd(mem, (__mmask8)m, src);
                }
                else
                {
                    _mm256_mask_storeu_pd(mem, (__mmask8)m, src);
                }
            }
        }

        // Constant masks: prefix/suffix shapes lower to plain moves; interior
        // masks keep the EVEX path.
        template <class A, class T, bool... V, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool_constant<T, A, V...> mask, convert<T>, Mode, requires_arch<avx512vl_256>) noexcept
        {
            // all() reaches here only via the avx512f half-split cascade.
            if constexpr (mask.all())
            {
                return batch<T, A>::load(mem, Mode {});
            }
            else if constexpr (detail::lowers_to_plain_moves(mask))
            {
                return detail::plain_move_load<avx>(mem, mask, convert<T> {}, Mode {});
            }
            else
            {
                return detail::maskload256(mem, mask.mask(), Mode {});
            }
        }

        template <class A, class T, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE batch<T, A> load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<avx512vl_256>) noexcept
        {
            return detail::maskload256(mem, mask.mask(), Mode {});
        }

        template <class A, class T, bool... V, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, V...> mask, Mode, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (mask.all())
            {
                src.store(mem, Mode {});
            }
            else if constexpr (detail::lowers_to_plain_moves(mask))
            {
                detail::plain_move_store<avx2>(mem, src, mask, Mode {});
            }
            else
            {
                detail::maskstore256(mem, src, mask.mask(), Mode {});
            }
        }

        template <class A, class T, class Mode,
                  typename = std::enable_if_t<std::is_arithmetic_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>>
        XSIMD_INLINE void store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<avx512vl_256>) noexcept
        {
            detail::maskstore256(mem, src, mask.mask(), Mode {});
        }

        // gather
        template <class T, class A, class U, detail::enable_sized_integral_t<T, 4> = 0, detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_mmask_i32gather_epi32(_mm256_setzero_si256(), (__mmask8)0xFF, index, static_cast<const void*>(src), sizeof(T));
        }

        template <class T, class A, class U, detail::enable_sized_integral_t<T, 8> = 0, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<T, A> gather(batch<T, A> const&, T const* src, batch<U, A> const& index,
                                        kernel::requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_mmask_i64gather_epi64(_mm256_setzero_si256(), (__mmask8)0xF, index, static_cast<const void*>(src), sizeof(T));
        }

        template <class A, class U, detail::enable_sized_integral_t<U, 4> = 0>
        XSIMD_INLINE batch<float, A> gather(batch<float, A> const&, float const* src,
                                            batch<U, A> const& index,
                                            kernel::requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_mmask_i32gather_ps(_mm256_setzero_ps(), (__mmask8)0xFF, index, src, sizeof(float));
        }

        template <class A, class U, detail::enable_sized_integral_t<U, 8> = 0>
        XSIMD_INLINE batch<double, A> gather(batch<double, A> const&, double const* src,
                                             batch<U, A> const& index,
                                             requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_mmask_i64gather_pd(_mm256_setzero_pd(), (__mmask8)0xF, index, src, sizeof(double));
        }

        // scatter
        template <class A, class T,
                  class = std::enable_if_t<std::is_same_v<uint32_t, T> || std::is_same_v<int32_t, T>>>
        XSIMD_INLINE void scatter(batch<T, A> const& src, T* dst,
                                  batch<int32_t, A> const& index,
                                  kernel::requires_arch<avx512vl_256>) noexcept
        {
            _mm256_i32scatter_epi32(dst, index, src, sizeof(T));
        }

        template <class A, class T,
                  class = std::enable_if_t<std::is_same_v<uint64_t, T> || std::is_same_v<int64_t, T>>>
        XSIMD_INLINE void scatter(batch<T, A> const& src, T* dst,
                                  batch<int64_t, A> const& index,
                                  kernel::requires_arch<avx512vl_256>) noexcept
        {
            _mm256_i64scatter_epi64(dst, index, src, sizeof(T));
        }

        template <class A>
        XSIMD_INLINE void scatter(batch<float, A> const& src, float* dst,
                                  batch<int32_t, A> const& index,
                                  kernel::requires_arch<avx512vl_256>) noexcept
        {
            _mm256_i32scatter_ps(dst, index, src, sizeof(float));
        }

        template <class A>
        XSIMD_INLINE void scatter(batch<double, A> const& src, double* dst,
                                  batch<int64_t, A> const& index,
                                  kernel::requires_arch<avx512vl_256>) noexcept
        {
            _mm256_i64scatter_pd(dst, index, src, sizeof(double));
        }

        // max
        template <class A>
        XSIMD_INLINE batch<int64_t, A> max(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_max_epi64(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> max(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_max_epu64(self, other);
        }

        // min
        template <class A>
        XSIMD_INLINE batch<int64_t, A> min(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_min_epi64(self, other);
        }
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> min(batch<uint64_t, A> const& self, batch<uint64_t, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_min_epu64(self, other);
        }

        // swizzle (dynamic version)
        template <class A>
        XSIMD_INLINE batch<float, A> swizzle(batch<float, A> const& self, batch<uint32_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_permutexvar_ps(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch<uint64_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_permutexvar_pd(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<uint64_t, A> swizzle(batch<uint64_t, A> const& self, batch<uint64_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_permutexvar_epi64(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<int64_t, A> swizzle(batch<int64_t, A> const& self, batch<uint64_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return bitwise_cast<int64_t>(swizzle(bitwise_cast<uint64_t>(self), mask, avx512vl_256 {}));
        }

        template <class A>
        XSIMD_INLINE batch<uint32_t, A> swizzle(batch<uint32_t, A> const& self, batch<uint32_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_permutexvar_epi32(mask, self);
        }

        template <class A>
        XSIMD_INLINE batch<int32_t, A> swizzle(batch<int32_t, A> const& self, batch<uint32_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return bitwise_cast<int32_t>(swizzle(bitwise_cast<uint32_t>(self), mask, avx512vl_256 {}));
        }
        template <class A>
        XSIMD_INLINE batch<uint8_t, A> swizzle(batch<uint8_t, A> const& self, batch<uint8_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return swizzle(batch<uint8_t, avx2> { self.data }, batch<uint8_t, avx2> { mask.data }, avx2 {}).data;
        }
        // No EVEX byte/word permute without AVX512_VBMI, so 16-bit dynamic
        // swizzle forwards to avx2 — mirrors the uint8_t base above. Without
        // this base the sized-2 bridge below re-selects itself for uint16_t
        // and recurses forever (always_inline build failure).
        template <class A>
        XSIMD_INLINE batch<uint16_t, A> swizzle(batch<uint16_t, A> const& self, batch<uint16_t, A> mask, requires_arch<avx512vl_256>) noexcept
        {
            return swizzle(batch<uint16_t, avx2> { self.data }, batch<uint16_t, avx2> { mask.data }, avx2 {}).data;
        }
        template <class A, typename T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch<uint8_t, A> const& mask, requires_arch<avx512vl_256> req) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<uint8_t>(self), mask, req));
        }
        template <class A, typename T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch<uint16_t, A> const& mask, requires_arch<avx512vl_256> req) noexcept
        {
            return bitwise_cast<T>(swizzle(bitwise_cast<uint16_t>(self), mask, req));
        }

        // swizzle
        template <class A, uint8_t... Vals, typename T, detail::enable_sized_t<T, 1> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint8_t, A, Vals...> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return swizzle(self, mask, fma3<avx2> {});
        }
        template <class A, uint16_t... Vals, typename T, detail::enable_sized_t<T, 2> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint16_t, A, Vals...> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return swizzle(self, mask, fma3<avx2> {});
        }
        template <class A, uint32_t... Vals, typename T, detail::enable_sized_t<T, 4> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint32_t, A, Vals...> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return swizzle(self, mask, fma3<avx2> {});
        }

        template <class A, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3>
        XSIMD_INLINE batch<double, A> swizzle(batch<double, A> const& self, batch_constant<uint64_t, A, V0, V1, V2, V3>, requires_arch<avx512vl_256>) noexcept
        {
            constexpr auto mask = detail::mod_shuffle(V0, V1, V2, V3);
            return _mm256_permutex_pd(self, mask);
        }
        template <class A, typename T, uint64_t V0, uint64_t V1, uint64_t V2, uint64_t V3, detail::enable_sized_t<T, 8> = 0>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<uint64_t, A, V0, V1, V2, V3>, requires_arch<avx512vl_256>) noexcept
        {
            constexpr auto mask = detail::mod_shuffle(V0, V1, V2, V3);
            return _mm256_permutex_epi64(self, mask);
        }

        // insert
        template <class A, size_t I>
        XSIMD_INLINE batch<float, A> insert(batch<float, A> const& self, float val, index<I>, requires_arch<avx512vl_256>) noexcept
        {

            int32_t tmp = bit_cast<int32_t>(val);
            return _mm256_castsi256_ps(_mm256_mask_set1_epi32(_mm256_castps_si256(self), __mmask8(1 << (I & 7)), tmp));
        }

        template <class A, size_t I>
        XSIMD_INLINE batch<double, A> insert(batch<double, A> const& self, double val, index<I>, requires_arch<avx512vl_256>) noexcept
        {
            int64_t tmp = bit_cast<int64_t>(val);
            return _mm256_castsi256_pd(_mm256_mask_set1_epi64(_mm256_castpd_si256(self), __mmask8(1 << (I & 3)), tmp));
        }

        template <class A, class T, size_t I, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I> pos, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm256_mask_set1_epi32(self, __mmask8(1 << (I & 7)), val);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_mask_set1_epi64(self, __mmask8(1 << (I & 3)), val);
            }
            else
            {
                return insert(self, val, pos, common {});
            }
        }

        // isnan
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isnan(batch<float, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, self, _CMP_UNORD_Q);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isnan(batch<double, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, self, _CMP_UNORD_Q);
        }

        // bitwise_rshift — signed int64 uses the native EVEX arithmetic shift
        // (VPSRAQ / VPSRAVQ, lat 1 / CPI 0.5). Every other width/sign keeps the
        // inherited avx2 codegen (srai/srav for 32-bit, srli for unsigned 64).
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, int32_t other, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (std::is_signed_v<T> && sizeof(T) == 8)
            {
                return _mm256_srai_epi64(self, other);
            }
            else
            {
                return bitwise_rshift(self, other, avx2 {});
            }
        }
        template <size_t shift, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits, "Shift amount must be less than the number of bits in T");
            if constexpr (std::is_signed_v<T> && sizeof(T) == 8)
            {
                return _mm256_srai_epi64(self, shift);
            }
            else
            {
                return bitwise_rshift<shift>(self, avx2 {});
            }
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (std::is_signed_v<T> && sizeof(T) == 8)
            {
                return _mm256_srav_epi64(self, other);
            }
            else
            {
                return bitwise_rshift(self, other, avx2 {});
            }
        }

        // rotl
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm256_rolv_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_rolv_epi64(self, other);
            }
            else
            {
                return rotl(self, other, avx2 {});
            }
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, int32_t other, requires_arch<avx512vl_256>) noexcept
        {
            return rotl(self, batch<T, A>(other), A {});
        }
        template <size_t count, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count must be less than the number of bits in T");
            if constexpr (sizeof(T) == 4)
            {
                return _mm256_rol_epi32(self, count);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_rol_epi64(self, count);
            }
            else
            {
                return rotl<count>(self, avx2 {});
            }
        }

        // rotr
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (sizeof(T) == 4)
            {
                return _mm256_rorv_epi32(self, other);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_rorv_epi64(self, other);
            }
            else
            {
                return rotr(self, other, avx2 {});
            }
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, int32_t other, requires_arch<avx512vl_256>) noexcept
        {
            return rotr(self, batch<T, A>(other), A {});
        }

        template <size_t count, class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count must be less than the number of bits in T");
            if constexpr (sizeof(T) == 4)
            {
                return _mm256_ror_epi32(self, count);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_ror_epi64(self, count);
            }
            else
            {
                return rotr<count>(self, avx2 {});
            }
        }

        // compress — native EVEX VPCOMPRESS{PS,PD,Q,D} for the widths with VL
        // forms. 8/16-bit need AVX512_VBMI2, so they fall through to common{}.
        template <class A>
        XSIMD_INLINE batch<float, A> compress(batch<float, A> const& self, batch_bool<float, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_compress_ps(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> compress(batch<double, A> const& self, batch_bool<double, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_compress_pd(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int32_t, A> compress(batch<int32_t, A> const& self, batch_bool<int32_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_compress_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> compress(batch<uint32_t, A> const& self, batch_bool<uint32_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_compress_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> compress(batch<int64_t, A> const& self, batch_bool<int64_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_compress_epi64(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> compress(batch<uint64_t, A> const& self, batch_bool<uint64_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_compress_epi64(mask.mask(), self);
        }

        // expand
        template <class A>
        XSIMD_INLINE batch<float, A> expand(batch<float, A> const& self, batch_bool<float, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_expand_ps(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> expand(batch<double, A> const& self, batch_bool<double, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_expand_pd(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int32_t, A> expand(batch<int32_t, A> const& self, batch_bool<int32_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_expand_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint32_t, A> expand(batch<uint32_t, A> const& self, batch_bool<uint32_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_expand_epi32(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<int64_t, A> expand(batch<int64_t, A> const& self, batch_bool<int64_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_expand_epi64(mask.mask(), self);
        }
        template <class A>
        XSIMD_INLINE batch<uint64_t, A> expand(batch<uint64_t, A> const& self, batch_bool<uint64_t, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_maskz_expand_epi64(mask.mask(), self);
        }

        // all
        template <class A, class T>
        XSIMD_INLINE bool all(batch_bool<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            constexpr register_type bitmask = (register_type(1) << batch_bool<T, A>::size) - 1;
            return (self.data & bitmask) == bitmask;
        }

        // any
        template <class A, class T>
        XSIMD_INLINE bool any(batch_bool<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            constexpr register_type bitmask = (register_type(1) << batch_bool<T, A>::size) - 1;
            return (self.data & bitmask) != 0;
        }

        // eq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> eq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, other, _CMP_EQ_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> eq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, other, _CMP_EQ_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> eq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return detail::compare_int_avx512vl_256<A, T, _MM_CMPINT_EQ>(self, other);
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> eq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(~self.data ^ other.data);
        }

        // neq
        template <class A>
        XSIMD_INLINE batch_bool<float, A> neq(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, other, _CMP_NEQ_UQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> neq(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, other, _CMP_NEQ_UQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (~(self == other));
        }
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> neq(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data ^ other.data);
        }

        // gt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> gt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, other, _CMP_GT_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> gt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, other, _CMP_GT_OQ);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return detail::compare_int_avx512vl_256<A, T, _MM_CMPINT_GT>(self, other);
        }

        // ge
        template <class A>
        XSIMD_INLINE batch_bool<float, A> ge(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, other, _CMP_GE_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> ge(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, other, _CMP_GE_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return detail::compare_int_avx512vl_256<A, T, _MM_CMPINT_GE>(self, other);
        }

        // lt
        template <class A>
        XSIMD_INLINE batch_bool<float, A> lt(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, other, _CMP_LT_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> lt(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, other, _CMP_LT_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> lt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return detail::compare_int_avx512vl_256<A, T, _MM_CMPINT_LT>(self, other);
        }

        // le
        template <class A>
        XSIMD_INLINE batch_bool<float, A> le(batch<float, A> const& self, batch<float, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<float, A>::register_type)_mm256_cmp_ps_mask(self, other, _CMP_LE_OQ);
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> le(batch<double, A> const& self, batch<double, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return (typename batch_bool<double, A>::register_type)_mm256_cmp_pd_mask(self, other, _CMP_LE_OQ);
        }

        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            return detail::compare_int_avx512vl_256<A, T, _MM_CMPINT_LE>(self, other);
        }

        // select
        template <class A>
        XSIMD_INLINE batch<float, A> select(batch_bool<float, A> const& cond, batch<float, A> const& true_br, batch<float, A> const& false_br, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_mask_blend_ps(cond, false_br, true_br);
        }
        template <class A>
        XSIMD_INLINE batch<double, A> select(batch_bool<double, A> const& cond, batch<double, A> const& true_br, batch<double, A> const& false_br, requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_mask_blend_pd(cond, false_br, true_br);
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> select(batch_bool<T, A> const& cond, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx512vl_256>) noexcept
        {
            if constexpr (sizeof(T) == 1)
            {
                batch_bool<T, avx2> batch_cond = batch_bool<T, avx2>::from_mask(cond.mask());
                return _mm256_blendv_epi8(false_br, true_br, batch_cond);
            }
            else if constexpr (sizeof(T) == 2)
            {
                batch_bool<T, avx2> batch_cond = batch_bool<T, avx2>::from_mask(cond.mask());
                return _mm256_blendv_epi8(false_br, true_br, batch_cond);
            }
            else if constexpr (sizeof(T) == 4)
            {
                return _mm256_mask_blend_epi32(cond, false_br, true_br);
            }
            else if constexpr (sizeof(T) == 8)
            {
                return _mm256_mask_blend_epi64(cond, false_br, true_br);
            }
        }
        template <class A, class T, bool... Values>
        XSIMD_INLINE batch<T, A> select(batch_bool_constant<T, A, Values...> const&, batch<T, A> const& true_br, batch<T, A> const& false_br, requires_arch<avx512vl_256>) noexcept
        {
            return select(batch_bool<T, A> { Values... }, true_br, false_br, avx512vl_256 {});
        }

        // decr_if / incr_if — the inherited avx kernels compute
        // `self ± batch<T>(mask.data)`, which assumes a vector batch_bool whose
        // true lanes are all-ones. Here batch_bool::data is a k-mask bitfield,
        // so that broadcast yields garbage. Delegate to the select-based common
        // implementation instead.
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return decr_if(self, mask, common {});
        }
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, batch_bool<T, A> const& mask, requires_arch<avx512vl_256>) noexcept
        {
            return incr_if(self, mask, common {});
        }

        // reciprocal
        template <class A>
        XSIMD_INLINE batch<float, A>
        reciprocal(batch<float, A> const& self,
                   kernel::requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_rcp14_ps(self);
        }

        template <class A>
        XSIMD_INLINE batch<double, A>
        reciprocal(batch<double, A> const& self,
                   kernel::requires_arch<avx512vl_256>) noexcept
        {
            return _mm256_rcp14_pd(self);
        }

        // bitwise_and
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_and(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data & other.data);
        }

        // bitwise_andnot
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_andnot(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data & ~other.data);
        }

        // bitwise_not
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_not(batch_bool<T, A> const& self, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(~self.data);
        }

        // bitwise_or
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_or(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data | other.data);
        }

        // bitwise_xor
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> bitwise_xor(batch_bool<T, A> const& self, batch_bool<T, A> const& other, requires_arch<avx512vl_256>) noexcept
        {
            using register_type = typename batch_bool<T, A>::register_type;
            return register_type(self.data ^ other.data);
        }

        // sadd
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<avx512vl_256>) noexcept
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

    }
}

#endif
