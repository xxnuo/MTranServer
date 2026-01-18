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

#ifndef XSIMD_COMMON_ARITHMETIC_HPP
#define XSIMD_COMMON_ARITHMETIC_HPP

#include "../../types/xsimd_batch_constant.hpp"
#include "./xsimd_common_details.hpp"

#include <complex>
#include <limits>
#include <type_traits>
#include <utility>

namespace xsimd
{

    namespace kernel
    {

        using namespace types;

        // bitwise_lshift
        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept
                                 { return x << y; },
                                 self, other);
        }
        template <size_t shift, class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> bitwise_lshift(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits, "Shift must be less than the number of bits in T");
            return bitwise_lshift(self, shift, A {});
        }

        // bitwise_rshift
        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept
                                 { return x >> y; },
                                 self, other);
        }
        template <size_t shift, class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> bitwise_rshift(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(shift < bits, "Shift must be less than the number of bits in T");
            return bitwise_rshift(self, shift, A {});
        }

        // decr
        template <class A, class T>
        XSIMD_INLINE batch<T, A> decr(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return self - T(1);
        }

        // decr_if
        template <class A, class T, class Mask>
        XSIMD_INLINE batch<T, A> decr_if(batch<T, A> const& self, Mask const& mask, requires_arch<common>) noexcept
        {
            return select(mask, decr(self), self);
        }

        // div
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch<T, A> div(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept -> T
                                 { return x / y; },
                                 self, other);
        }

        // fma
        template <class A, class T>
        XSIMD_INLINE batch<T, A> fma(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<common>) noexcept
        {
            return x * y + z;
        }

        template <class A, class T>
        XSIMD_INLINE batch<std::complex<T>, A> fma(batch<std::complex<T>, A> const& x, batch<std::complex<T>, A> const& y, batch<std::complex<T>, A> const& z, requires_arch<common>) noexcept
        {
            auto res_r = fms(x.real(), y.real(), fms(x.imag(), y.imag(), z.real()));
            auto res_i = fma(x.real(), y.imag(), fma(x.imag(), y.real(), z.imag()));
            return { res_r, res_i };
        }

        // fms
        template <class A, class T>
        XSIMD_INLINE batch<T, A> fms(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<common>) noexcept
        {
            return x * y - z;
        }

        template <class A, class T>
        XSIMD_INLINE batch<std::complex<T>, A> fms(batch<std::complex<T>, A> const& x, batch<std::complex<T>, A> const& y, batch<std::complex<T>, A> const& z, requires_arch<common>) noexcept
        {
            auto res_r = fms(x.real(), y.real(), fma(x.imag(), y.imag(), z.real()));
            auto res_i = fma(x.real(), y.imag(), fms(x.imag(), y.real(), z.imag()));
            return { res_r, res_i };
        }

        // fnma
        template <class A, class T>
        XSIMD_INLINE batch<T, A> fnma(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<common>) noexcept
        {
            return -x * y + z;
        }

        template <class A, class T>
        XSIMD_INLINE batch<std::complex<T>, A> fnma(batch<std::complex<T>, A> const& x, batch<std::complex<T>, A> const& y, batch<std::complex<T>, A> const& z, requires_arch<common>) noexcept
        {
            auto res_r = -fms(x.real(), y.real(), fma(x.imag(), y.imag(), z.real()));
            auto res_i = -fma(x.real(), y.imag(), fms(x.imag(), y.real(), z.imag()));
            return { res_r, res_i };
        }

        // fnms
        template <class A, class T>
        XSIMD_INLINE batch<T, A> fnms(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<common>) noexcept
        {
            return -x * y - z;
        }

        template <class A, class T>
        XSIMD_INLINE batch<std::complex<T>, A> fnms(batch<std::complex<T>, A> const& x, batch<std::complex<T>, A> const& y, batch<std::complex<T>, A> const& z, requires_arch<common>) noexcept
        {
            auto res_r = -fms(x.real(), y.real(), fms(x.imag(), y.imag(), z.real()));
            auto res_i = -fma(x.real(), y.imag(), fma(x.imag(), y.real(), z.imag()));
            return { res_r, res_i };
        }

        // fmas
        template <class A, class T>
        XSIMD_INLINE batch<T, A> fmas(batch<T, A> const& x, batch<T, A> const& y, batch<T, A> const& z, requires_arch<common>) noexcept
        {
            struct even_lane
            {
                static constexpr bool get(unsigned const i, unsigned) noexcept { return (i & 1u) == 0; }
            };
            const auto mask = make_batch_bool_constant<T, even_lane, A>();
            return fma(x, y, select(mask, neg(z), z));
        }

        // incr
        template <class A, class T>
        XSIMD_INLINE batch<T, A> incr(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return self + T(1);
        }

        // incr_if
        template <class A, class T, class Mask>
        XSIMD_INLINE batch<T, A> incr_if(batch<T, A> const& self, Mask const& mask, requires_arch<common>) noexcept
        {
            return select(mask, incr(self), self);
        }

        // mul
        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> mul(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept -> T
                                 { return x * y; },
                                 self, other);
        }

        // mul_hi
        namespace detail
        {
            template <class T>
            struct mulhi_helper
            {
                using wider = std::conditional_t<
                    std::is_signed_v<T>,
                    std::conditional_t<sizeof(T) == 1, int16_t,
                                       std::conditional_t<sizeof(T) == 2, int32_t, int64_t>>,
                    std::conditional_t<sizeof(T) == 1, uint16_t,
                                       std::conditional_t<sizeof(T) == 2, uint32_t, uint64_t>>>;

                static XSIMD_INLINE T compute(T x, T y) noexcept
                {
                    constexpr int shift = 8 * sizeof(T);
                    return static_cast<T>((static_cast<wider>(x) * static_cast<wider>(y)) >> shift);
                }
            };

            // 64-bit unsigned software mul_hi via 32-bit splits
            XSIMD_INLINE uint64_t mulhi_u64(uint64_t x, uint64_t y) noexcept
            {
#if defined(__SIZEOF_INT128__)
                return static_cast<uint64_t>((static_cast<unsigned __int128>(x) * static_cast<unsigned __int128>(y)) >> 64);
#else
                uint64_t xl = x & 0xffffffffULL;
                uint64_t xh = x >> 32;
                uint64_t yl = y & 0xffffffffULL;
                uint64_t yh = y >> 32;
                uint64_t ll = xl * yl;
                uint64_t lh = xl * yh;
                uint64_t hl = xh * yl;
                uint64_t hh = xh * yh;
                uint64_t mid = (ll >> 32) + (lh & 0xffffffffULL) + (hl & 0xffffffffULL);
                return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
#endif
            }

            XSIMD_INLINE int64_t mulhi_i64(int64_t x, int64_t y) noexcept
            {
#if defined(__SIZEOF_INT128__)
                return static_cast<int64_t>((static_cast<__int128>(x) * static_cast<__int128>(y)) >> 64);
#else
                uint64_t uhi = mulhi_u64(static_cast<uint64_t>(x), static_cast<uint64_t>(y));
                if (x < 0)
                    uhi -= static_cast<uint64_t>(y);
                if (y < 0)
                    uhi -= static_cast<uint64_t>(x);
                return static_cast<int64_t>(uhi);
#endif
            }

            template <>
            struct mulhi_helper<uint64_t>
            {
                static XSIMD_INLINE uint64_t compute(uint64_t x, uint64_t y) noexcept { return mulhi_u64(x, y); }
            };

            template <>
            struct mulhi_helper<int64_t>
            {
                static XSIMD_INLINE int64_t compute(int64_t x, int64_t y) noexcept { return mulhi_i64(x, y); }
            };

            // 64x64 unsigned mul_hi via 32x32->64 widening mul (WMul wraps _mm*_mul_epu32).
            template <class A, class WMul>
            XSIMD_INLINE batch<uint64_t, A> mulhi_u64_core(batch<uint64_t, A> const& x,
                                                           batch<uint64_t, A> const& y,
                                                           WMul mul_epu32) noexcept
            {
                using B = batch<uint64_t, A>;
                const B mask(uint64_t(0xffffffffULL));
                // mul_epu32 uses only the low 32 bits, so low operands need no mask.
                B xh = x >> 32;
                B yh = y >> 32;
                B ll = mul_epu32(x, y);
                B lh = mul_epu32(x, yh);
                B hl = mul_epu32(xh, y);
                B hh = mul_epu32(xh, yh);
                B mid = (ll >> 32) + (lh & mask) + (hl & mask);
                return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
            }

            // Signed variant: unsigned core + sign fixup via arithmetic shift-by-63.
            template <class A, class WMul>
            XSIMD_INLINE batch<int64_t, A> mulhi_i64_core(batch<int64_t, A> const& x,
                                                          batch<int64_t, A> const& y,
                                                          WMul mul_epu32) noexcept
            {
                auto ux = ::xsimd::bitwise_cast<uint64_t>(x);
                auto uy = ::xsimd::bitwise_cast<uint64_t>(y);
                auto uhi = mulhi_u64_core<A>(ux, uy, mul_epu32);
                auto sa = ::xsimd::bitwise_cast<uint64_t>(x >> 63);
                auto sb = ::xsimd::bitwise_cast<uint64_t>(y >> 63);
                return ::xsimd::bitwise_cast<int64_t>(uhi - (uy & sa) - (ux & sb));
            }

            // Fused mul_hilo: both halves from one set of partials. Returns { hi, lo }.
            template <class A, class WMul>
            XSIMD_INLINE std::pair<batch<uint64_t, A>, batch<uint64_t, A>>
            mulhilo_u64_core(batch<uint64_t, A> const& x,
                             batch<uint64_t, A> const& y,
                             WMul mul_epu32) noexcept
            {
                using B = batch<uint64_t, A>;
                const B mask(uint64_t(0xffffffffULL));
                B xh = x >> 32;
                B yh = y >> 32;
                B ll = mul_epu32(x, y);
                B lh = mul_epu32(x, yh);
                B hl = mul_epu32(xh, y);
                B hh = mul_epu32(xh, yh);
                B mid = (ll >> 32) + (lh & mask) + (hl & mask);
                B hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
                B lo = (ll & mask) | (mid << 32);
                return { hi, lo };
            }
        }

        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> mul_hi(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept -> T
                                 { return detail::mulhi_helper<T>::compute(x, y); },
                                 self, other);
        }

        // mul_hilo
        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE std::pair<batch<T, A>, batch<T, A>>
        mul_hilo(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return std::pair<batch<T, A>, batch<T, A>> { mul_hi<A>(self, other, A {}), self * other };
        }

        // Signed 64-bit mul_hilo: unsigned path + sign fixup on hi (lo is sign-invariant).
        template <class A>
        XSIMD_INLINE std::pair<batch<int64_t, A>, batch<int64_t, A>>
        mul_hilo(batch<int64_t, A> const& self, batch<int64_t, A> const& other, requires_arch<common>) noexcept
        {
            auto ux = ::xsimd::bitwise_cast<uint64_t>(self);
            auto uy = ::xsimd::bitwise_cast<uint64_t>(other);
            auto hilo = mul_hilo<A>(ux, uy, A {});
            auto sa = ::xsimd::bitwise_cast<uint64_t>(self >> 63);
            auto sb = ::xsimd::bitwise_cast<uint64_t>(other >> 63);
            auto hi = hilo.first - (uy & sa) - (ux & sb);
            return { ::xsimd::bitwise_cast<int64_t>(hi),
                     ::xsimd::bitwise_cast<int64_t>(hilo.second) };
        }

        // rotl
        template <class A, class T, class STy>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, STy other, requires_arch<common>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            return (self << other) | (self >> (bits - other));
        }
        template <size_t count, class A, class T>
        XSIMD_INLINE batch<T, A> rotl(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count amount must be less than the number of bits in T");
            return bitwise_lshift<count>(self) | bitwise_rshift<bits - count>(self);
        }

        // rotr
        template <class A, class T, class STy>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, STy other, requires_arch<common>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            return (self >> other) | (self << (bits - other));
        }
        template <size_t count, class A, class T>
        XSIMD_INLINE batch<T, A> rotr(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr auto bits = std::numeric_limits<T>::digits + std::numeric_limits<T>::is_signed;
            static_assert(count < bits, "Count must be less than the number of bits in T");
            return bitwise_rshift<count>(self) | bitwise_lshift<bits - count>(self);
        }

        // sadd
        template <class A>
        XSIMD_INLINE batch<float, A> sadd(batch<float, A> const& self, batch<float, A> const& other, requires_arch<common>) noexcept
        {
            return add(self, other); // no saturated arithmetic on floating point numbers
        }
        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> sadd(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                auto self_pos_branch = min(std::numeric_limits<T>::max() - other, self);
                auto self_neg_branch = max(std::numeric_limits<T>::min() - other, self);
                return other + select(other >= 0, self_pos_branch, self_neg_branch);
            }
            else
            {
                const auto diffmax = std::numeric_limits<T>::max() - self;
                const auto mindiff = min(diffmax, other);
                return self + mindiff;
            }
        }
        template <class A>
        XSIMD_INLINE batch<double, A> sadd(batch<double, A> const& self, batch<double, A> const& other, requires_arch<common>) noexcept
        {
            return add(self, other); // no saturated arithmetic on floating point numbers
        }

        // ssub
        template <class A>
        XSIMD_INLINE batch<float, A> ssub(batch<float, A> const& self, batch<float, A> const& other, requires_arch<common>) noexcept
        {
            return sub(self, other); // no saturated arithmetic on floating point numbers
        }
        template <class A, class T, class /*=std::enable_if_t<std::is_integral_v<T>>*/>
        XSIMD_INLINE batch<T, A> ssub(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            if (std::is_signed_v<T>)
            {
                // Saturating self - other, mirroring the signed sadd above.
                // sadd(self, -other) is wrong when other == numeric_limits<T>::min(),
                // since -other is not representable.
                auto self_underflow_branch = max(std::numeric_limits<T>::min() + other, self);
                auto self_overflow_branch = min(std::numeric_limits<T>::max() + other, self);
                return select(other >= 0, self_underflow_branch, self_overflow_branch) - other;
            }
            else
            {
                const auto diff = min(self, other);
                return self - diff;
            }
        }
        template <class A>
        XSIMD_INLINE batch<double, A> ssub(batch<double, A> const& self, batch<double, A> const& other, requires_arch<common>) noexcept
        {
            return sub(self, other); // no saturated arithmetic on floating point numbers
        }

    }

}

#endif
