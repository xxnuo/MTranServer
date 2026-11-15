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

#ifndef XSIMD_COMMON_COMPLEX_HPP
#define XSIMD_COMMON_COMPLEX_HPP

#include "./xsimd_common_details.hpp"

#include <complex>

namespace xsimd
{

    namespace kernel
    {

        using namespace types;

        // real
        template <class A, class T>
        XSIMD_INLINE batch<T, A> real(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return self;
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> real(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return self.real();
        }

        // imag
        template <class A, class T>
        XSIMD_INLINE batch<T, A> imag(batch<T, A> const& /*self*/, requires_arch<common>) noexcept
        {
            return batch<T, A>(T(0));
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> imag(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return self.imag();
        }

        // arg
        template <class A, class T>
        XSIMD_INLINE real_batch_type_t<batch<T, A>> arg(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return atan2(imag(self), real(self));
        }

        // conj
        template <class A, class T>
        XSIMD_INLINE complex_batch_type_t<batch<T, A>> conj(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return { real(self), -imag(self) };
        }

        // norm
        template <class A, class T>
        XSIMD_INLINE real_batch_type_t<batch<T, A>> norm(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return { fma(real(self), real(self), imag(self) * imag(self)) };
        }

        // proj
        template <class A, class T>
        XSIMD_INLINE complex_batch_type_t<batch<T, A>> proj(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            using batch_type = complex_batch_type_t<batch<T, A>>;
            using real_batch = typename batch_type::real_batch;
            using real_value_type = typename real_batch::value_type;
#ifdef __FAST_MATH__
            return { self };
#else
            auto cond = xsimd::isinf(real(self)) || xsimd::isinf(imag(self));
            return select(cond,
                          batch_type(constants::infinity<real_batch>(),
                                     copysign(real_batch(real_value_type(0)), imag(self))),
                          batch_type(self));
#endif
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> isnan(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return batch_bool<T, A>(isnan(self.real()) || isnan(self.imag()));
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> isinf(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return batch_bool<T, A>(isinf(self.real()) || isinf(self.imag()));
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> isfinite(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return batch_bool<T, A>(isfinite(self.real()) && isfinite(self.imag()));
        }
    }
}

#endif
