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

#ifndef XSIMD_COMMON_LOGICAL_HPP
#define XSIMD_COMMON_LOGICAL_HPP

#include "./xsimd_common_bit.hpp"
#include "./xsimd_common_details.hpp"

#include <climits>

namespace xsimd
{

    namespace kernel
    {

        using namespace types;

        // count
        template <class A, class T>
        XSIMD_INLINE size_t count(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            return xsimd::detail::popcount(self.mask());
        }

        template <class A, class T>
        XSIMD_INLINE size_t countl_zero(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr size_t unused_bits = 64 - batch_bool<T, A>::size;
            constexpr uint64_t lower_mask = batch_bool<T, A>::size < 64 ? ((uint64_t)1 << (batch_bool<T, A>::size % 64)) - 1 : (uint64_t)-1;
            return xsimd::detail::countl_zero(self.mask() & lower_mask) - unused_bits;
        }

        template <class A, class T>
        XSIMD_INLINE size_t countl_one(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr size_t unused_bits = 64 - batch_bool<T, A>::size;
            constexpr uint64_t upper_mask = batch_bool<T, A>::size < 64 ? ~(((uint64_t)1 << (batch_bool<T, A>::size % 64)) - 1) : (uint64_t)0;
            return xsimd::detail::countl_one(self.mask() | upper_mask) - unused_bits;
        }

        template <class A, class T>
        XSIMD_INLINE size_t countr_zero(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr uint64_t stop = batch_bool<T, A>::size < 64 ? (uint64_t)1 << (batch_bool<T, A>::size % 64) : 0;
            return xsimd::detail::countr_zero(self.mask() | stop);
        }

        template <class A, class T>
        XSIMD_INLINE size_t countr_one(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            constexpr uint64_t stop = batch_bool<T, A>::size < 64 ? ~((uint64_t)1 << (batch_bool<T, A>::size % 64)) : (uint64_t)-1;
            return xsimd::detail::countr_one(self.mask() & stop);
        }

        // from  mask
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> from_mask(batch_bool<T, A> const&, uint64_t mask, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) bool buffer[batch_bool<T, A>::size];
            // This is inefficient and should never be called.
            for (size_t i = 0; i < batch_bool<T, A>::size; ++i)
                buffer[i] = mask & (1ull << i);
            return batch_bool<T, A>::load_aligned(buffer);
        }

        // ge
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> ge(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return other <= self;
        }

        // gt
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> gt(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return other < self;
        }

        // is_even
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> is_even(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return is_flint(self * T(0.5));
        }

        // is_flint
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> is_flint(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            auto frac = select(isnan(self - self), constants::nan<batch<T, A>>(), self - trunc(self));
            return frac == T(0.);
        }

        // is_odd
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> is_odd(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return is_even(self - T(1.));
        }

        // isinf
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> isinf(batch<T, A> const&, requires_arch<common>) noexcept
        {
            return batch_bool<T, A>(false);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isinf(batch<float, A> const& self, requires_arch<common>) noexcept
        {
#ifdef __FAST_MATH__
            (void)self;
            return { false };
#else
            return abs(self) == std::numeric_limits<float>::infinity();
#endif
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isinf(batch<double, A> const& self, requires_arch<common>) noexcept
        {
#ifdef __FAST_MATH__
            (void)self;
            return { false };
#else
            return abs(self) == std::numeric_limits<double>::infinity();
#endif
        }

        // isfinite
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> isfinite(batch<T, A> const&, requires_arch<common>) noexcept
        {
            return batch_bool<T, A>(true);
        }
        template <class A>
        XSIMD_INLINE batch_bool<float, A> isfinite(batch<float, A> const& self, requires_arch<common>) noexcept
        {
            return (self - self) == 0.f;
        }
        template <class A>
        XSIMD_INLINE batch_bool<double, A> isfinite(batch<double, A> const& self, requires_arch<common>) noexcept
        {
            return (self - self) == 0.;
        }

        // isnan
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> isnan(batch<T, A> const&, requires_arch<common>) noexcept
        {
            return batch_bool<T, A>(false);
        }

        // le
        template <class A, class T, class = std::enable_if_t<std::is_integral_v<T>>>
        XSIMD_INLINE batch_bool<T, A> le(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return (self < other) || (self == other);
        }

        // neq
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> neq(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return !(other == self);
        }

        // logical_and
        template <class A, class T>
        XSIMD_INLINE batch<T, A> logical_and(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept
                                 { return x && y; },
                                 self, other);
        }

        // logical_or
        template <class A, class T>
        XSIMD_INLINE batch<T, A> logical_or(batch<T, A> const& self, batch<T, A> const& other, requires_arch<common>) noexcept
        {
            return detail::apply([](T x, T y) noexcept
                                 { return x || y; },
                                 self, other);
        }

        // mask
        template <class A, class T>
        XSIMD_INLINE uint64_t mask(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) bool buffer[batch_bool<T, A>::size];
            self.store_aligned(buffer);
            // This is inefficient and should never be called.
            uint64_t res = 0;
            for (size_t i = 0; i < batch_bool<T, A>::size; ++i)
                if (buffer[i])
                    res |= 1ul << i;
            return res;
        }

        // select
        namespace detail
        {
            template <typename T, typename A>
            constexpr bool is_batch_bool_register_same_v = std::is_same_v<typename batch_bool<T, A>::register_type, typename batch<T, A>::register_type>;
        }

        template <class A, class T, std::enable_if_t<detail::is_batch_bool_register_same_v<T, A>, int> = 3>
        XSIMD_INLINE batch_bool<T, A> select(batch_bool<T, A> const& cond, batch_bool<T, A> const& true_br, batch_bool<T, A> const& false_br, requires_arch<common>)
        {
            using register_type = typename batch_bool<T, A>::register_type;
            // Do not cast, but rather reinterpret the masks as batches.
            const auto true_v = batch<T, A> { static_cast<register_type>(true_br) };
            const auto false_v = batch<T, A> { static_cast<register_type>(false_br) };
            return batch_bool<T, A> { select(cond, true_v, false_v) };
        }

        template <class A, class T, std::enable_if_t<!detail::is_batch_bool_register_same_v<T, A>, int> = 3>
        XSIMD_INLINE batch_bool<T, A> select(batch_bool<T, A> const& cond, batch_bool<T, A> const& true_br, batch_bool<T, A> const& false_br, requires_arch<common>)
        {
            return (true_br & cond) | (bitwise_andnot(false_br, cond));
        }

        template <class A, class T, bool... Values>
        XSIMD_INLINE batch_bool<T, A> select(batch_bool_constant<T, A, Values...> const& cond, batch_bool<T, A> const& true_br, batch_bool<T, A> const& false_br, requires_arch<common>)
        {
            return (true_br & cond) | (false_br & ~cond);
        }
    }
}

#endif
