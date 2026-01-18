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

#ifndef XSIMD_COMMON_CAST_HPP
#define XSIMD_COMMON_CAST_HPP

#include "../../config/xsimd_macros.hpp"
#include "../../utils/xsimd_type_traits.hpp"

#include <array>

namespace xsimd
{
    namespace kernel
    {
        template <class A, class T>
        XSIMD_INLINE std::array<batch<widen_t<T>, A>, 2> widen(batch<T, A> const& x, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) T buffer[batch<T, A>::size];
            x.store_aligned(&buffer[0]);

            using T_out = widen_t<T>;
            alignas(A::alignment()) T_out out_buffer[batch<T, A>::size];
            for (size_t i = 0; i < batch<T, A>::size; ++i)
                out_buffer[i] = static_cast<T_out>(buffer[i]);

            return { batch<T_out, A>::load_aligned(&out_buffer[0]),
                     batch<T_out, A>::load_aligned(&out_buffer[batch<T_out, A>::size]) };
        }

    }

}

#endif
