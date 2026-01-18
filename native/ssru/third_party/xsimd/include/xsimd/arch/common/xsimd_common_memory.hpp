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

#ifndef XSIMD_COMMON_MEMORY_HPP
#define XSIMD_COMMON_MEMORY_HPP

#include "../../types/xsimd_batch_constant.hpp"
#include "../../utils/xsimd_type_traits.hpp"
#include "./xsimd_common_details.hpp"

#include <algorithm>
#include <array>
#include <complex>

namespace xsimd
{
    template <typename T, class A, T... Values>
    struct batch_constant;

    template <typename T, class A, bool... Values>
    struct batch_bool_constant;

    namespace kernel
    {

        using namespace types;

        // broadcast
        namespace detail
        {
            template <class T, class A>
            struct broadcaster
            {
                using return_type = batch<T, A>;

                static XSIMD_INLINE return_type run(T v) noexcept
                {
                    return return_type::broadcast(v);
                }
            };

            template <class A>
            struct broadcaster<bool, A>
            {
                using return_type = batch_bool<xsimd::as_unsigned_integer_t<bool>, A>;

                static XSIMD_INLINE return_type run(bool b) noexcept
                {
                    return return_type(b);
                }
            };
        }

        // compress
        namespace detail
        {
            template <class IT, class A, class I, size_t... Is>
            XSIMD_INLINE batch<IT, A> create_compress_swizzle_mask(I bitmask, std::index_sequence<Is...>)
            {
                batch<IT, A> swizzle_mask(IT(0));
                alignas(A::alignment()) IT mask_buffer[batch<IT, A>::size] = { Is... };
                size_t inserted = 0;
                for (size_t i = 0; i < sizeof...(Is); ++i)
                    if ((bitmask >> i) & 1u)
                        std::swap(mask_buffer[inserted++], mask_buffer[i]);
                return batch<IT, A>::load_aligned(&mask_buffer[0]);
            }
        }

        template <typename A, typename T>
        XSIMD_INLINE batch<T, A>
        compress(batch<T, A> const& x, batch_bool<T, A> const& mask,
                 kernel::requires_arch<common>) noexcept
        {
            using IT = as_unsigned_integer_t<T>;
            constexpr std::size_t size = batch_bool<T, A>::size;
            auto bitmask = mask.mask();
            auto z = select(mask, x, batch<T, A>((T)0));
            auto compress_mask = detail::create_compress_swizzle_mask<IT, A>(bitmask, std::make_index_sequence<size>());
            return swizzle(z, compress_mask);
        }

        // expand
        template <typename A, typename T>
        XSIMD_INLINE batch<T, A>
        expand(batch<T, A> const& x, batch_bool<T, A> const& mask,
               kernel::requires_arch<common>) noexcept
        {
            constexpr auto size = batch<T, A>::size;
            alignas(A::alignment()) T x_in[size], x_out[size] = { T() };
            x.store_aligned(x_in);
            int i = 0, j = 0;
            for (auto bitmask = mask.mask(); bitmask; bitmask >>= 1, ++i)
            {
                if (bitmask & 1)
                    x_out[i] = x_in[j++];
            }
            return xsimd::batch<T, A>::load_aligned(x_out);
        }

        // extract_pair
        template <class A, class T>
        XSIMD_INLINE batch<T, A> extract_pair(batch<T, A> const& self, batch<T, A> const& other, std::size_t i, requires_arch<common>) noexcept
        {
            constexpr std::size_t size = batch<T, A>::size;
            assert(i < size && "index in bounds");

            alignas(A::alignment()) T self_buffer[size];
            self.store_aligned(self_buffer);

            alignas(A::alignment()) T other_buffer[size];
            other.store_aligned(other_buffer);

            alignas(A::alignment()) T concat_buffer[size];

            for (std::size_t j = 0; j < (size - i); ++j)
            {
                concat_buffer[j] = other_buffer[i + j];
                if (j < i)
                {
                    concat_buffer[size - 1 - j] = self_buffer[i - 1 - j];
                }
            }
            return batch<T, A>::load_aligned(concat_buffer);
        }

        // gather
        namespace detail
        {
            // Not using XSIMD_INLINE here as it makes msvc hand got ever on avx512
            template <size_t N, typename T, typename A, typename U, typename V, std::enable_if_t<N == 0, int> = 0>
            inline batch<T, A> gather(U const* src, batch<V, A> const& index,
                                      ::xsimd::index<N> I) noexcept
            {
                return insert(batch<T, A> {}, static_cast<T>(src[index.get(I)]), I);
            }

            template <size_t N, typename T, typename A, typename U, typename V, std::enable_if_t<N != 0, int> = 0>
            inline batch<T, A>
            gather(U const* src, batch<V, A> const& index, ::xsimd::index<N> I) noexcept
            {
                static_assert(N <= batch<V, A>::size, "Incorrect value in recursion!");

                const auto test = gather<N - 1, T, A>(src, index, {});
                return insert(test, static_cast<T>(src[index.get(I)]), I);
            }
        } // namespace detail

        template <typename T, typename A, typename V>
        XSIMD_INLINE batch<T, A>
        gather(batch<T, A> const&, T const* src, batch<V, A> const& index,
               kernel::requires_arch<common>) noexcept
        {
            static_assert(batch<T, A>::size == batch<V, A>::size,
                          "Index and destination sizes must match");

            return detail::gather<batch<V, A>::size - 1, T, A>(src, index, {});
        }

        // Gather with runtime indexes and mismatched strides.
        template <typename T, typename A, typename U, typename V>
        XSIMD_INLINE detail::sizes_mismatch_t<T, U, batch<T, A>>
        gather(batch<T, A> const&, U const* src, batch<V, A> const& index,
               kernel::requires_arch<common>) noexcept
        {
            static_assert(batch<T, A>::size == batch<V, A>::size,
                          "Index and destination sizes must match");

            return detail::gather<batch<V, A>::size - 1, T, A>(src, index, {});
        }

        // Gather with runtime indexes and matching strides.
        template <typename T, typename A, typename U, typename V>
        XSIMD_INLINE detail::stride_match_t<T, U, batch<T, A>>
        gather(batch<T, A> const&, U const* src, batch<V, A> const& index,
               kernel::requires_arch<common>) noexcept
        {
            static_assert(batch<T, A>::size == batch<V, A>::size,
                          "Index and destination sizes must match");

            return batch_cast<T>(kernel::gather(batch<U, A> {}, src, index, A {}));
        }

        // insert
        template <class A, class T, size_t I>
        XSIMD_INLINE batch<T, A> insert(batch<T, A> const& self, T val, index<I>, requires_arch<common>) noexcept
        {
            struct index_mask
            {
                static constexpr bool get(size_t index, size_t /* size*/)
                {
                    return index != I;
                }
            };
            batch<T, A> tmp(val);
            return select(make_batch_bool_constant<T, index_mask, A>(), self, tmp);
        }

        // get
        template <class A, size_t I, class T>
        XSIMD_INLINE T get(batch<T, A> const& self, ::xsimd::index<I>, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) T buffer[batch<T, A>::size];
            self.store_aligned(&buffer[0]);
            return buffer[I];
        }

        template <class A, size_t I, class T>
        XSIMD_INLINE T get(batch_bool<T, A> const& self, ::xsimd::index<I>, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) T buffer[batch_bool<T, A>::size];
            self.store_aligned(&buffer[0]);
            return buffer[I];
        }

        template <class A, size_t I, class T>
        XSIMD_INLINE typename batch<std::complex<T>, A>::value_type get(batch<std::complex<T>, A> const& self, ::xsimd::index<I>, requires_arch<common>) noexcept
        {
            using value_type = typename batch<std::complex<T>, A>::value_type;
            alignas(A::alignment()) value_type buffer[batch<std::complex<T>, A>::size];
            self.store_aligned(&buffer[0]);
            return buffer[I];
        }

        template <class A, class T>
        XSIMD_INLINE T get(batch<T, A> const& self, std::size_t i, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) T buffer[batch<T, A>::size];
            self.store_aligned(&buffer[0]);
            return buffer[i];
        }

        template <class A, class T>
        XSIMD_INLINE T get(batch_bool<T, A> const& self, std::size_t i, requires_arch<common>) noexcept
        {
            alignas(A::alignment()) bool buffer[batch_bool<T, A>::size];
            self.store_aligned(&buffer[0]);
            return buffer[i];
        }

        template <class A, class T>
        XSIMD_INLINE typename batch<std::complex<T>, A>::value_type get(batch<std::complex<T>, A> const& self, std::size_t i, requires_arch<common>) noexcept
        {
            using T2 = typename batch<std::complex<T>, A>::value_type;
            alignas(A::alignment()) T2 buffer[batch<std::complex<T>, A>::size];
            self.store_aligned(&buffer[0]);
            return buffer[i];
        }

        // first
        template <class A, class T>
        XSIMD_INLINE T first(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            return get(self, 0, common {});
        }

        template <class A, class T>
        XSIMD_INLINE T first(batch_bool<T, A> const& self, requires_arch<common>) noexcept
        {
            return first(batch<T, A>(self), A {});
        }

        template <class A, class T>
        XSIMD_INLINE typename batch<std::complex<T>, A>::value_type first(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return { first(self.real(), A {}), first(self.imag(), A {}) };
        }

        // load
        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> load_unaligned(bool const* mem, batch_bool<T, A>, requires_arch<common>) noexcept
        {
            using batch_type = batch<T, A>;
            batch_type ref(0);
            constexpr auto size = batch_bool<T, A>::size;
            alignas(A::alignment()) T buffer[size];
            for (std::size_t i = 0; i < size; ++i)
                buffer[i] = mem[i] ? 1 : 0;
            return ref != batch_type::load_aligned(&buffer[0]);
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> load_aligned(bool const* mem, batch_bool<T, A> b, requires_arch<common>) noexcept
        {
            return load_unaligned(mem, b, A {});
        }

        template <class A, class T>
        XSIMD_INLINE batch_bool<T, A> load_stream(bool const* mem, batch_bool<T, A> b, requires_arch<common>) noexcept
        {
            return load_aligned(mem, b, A {});
        }

        // load_aligned
        namespace detail
        {
            template <class A, class T_in, class T_out>
            XSIMD_INLINE batch<T_out, A> load_aligned(T_in const* mem, convert<T_out>, requires_arch<common>, with_fast_conversion) noexcept
            {
                using batch_type_in = batch<T_in, A>;
                using batch_type_out = batch<T_out, A>;
                return fast_cast(batch_type_in::load_aligned(mem), batch_type_out(), A {});
            }
            template <class A, class T_in, class T_out>
            XSIMD_INLINE batch<T_out, A> load_aligned(T_in const* mem, convert<T_out>, requires_arch<common>, with_slow_conversion) noexcept
            {
                static_assert(!std::is_same_v<T_in, T_out>, "there should be a direct load for this type combination");
                using batch_type_out = batch<T_out, A>;
                alignas(A::alignment()) T_out buffer[batch_type_out::size];
                std::copy(mem, mem + batch_type_out::size, std::begin(buffer));
                return batch_type_out::load_aligned(buffer);
            }
        }
        template <class A, class T_in, class T_out>
        XSIMD_INLINE batch<T_out, A> load_aligned(T_in const* mem, convert<T_out> cvt, requires_arch<common>) noexcept
        {
            return detail::load_aligned<A>(mem, cvt, A {}, detail::conversion_type<A, T_in, T_out> {});
        }

        // load_unaligned
        namespace detail
        {
            template <class A, class T_in, class T_out>
            XSIMD_INLINE batch<T_out, A> load_unaligned(T_in const* mem, convert<T_out>, requires_arch<common>, with_fast_conversion) noexcept
            {
                using batch_type_in = batch<T_in, A>;
                using batch_type_out = batch<T_out, A>;
                return fast_cast(batch_type_in::load_unaligned(mem), batch_type_out(), A {});
            }

            template <class A, class T_in, class T_out>
            XSIMD_INLINE batch<T_out, A> load_unaligned(T_in const* mem, convert<T_out> cvt, requires_arch<common>, with_slow_conversion) noexcept
            {
                static_assert(!std::is_same_v<T_in, T_out>, "there should be a direct load for this type combination");
                return load_aligned<A>(mem, cvt, common {}, with_slow_conversion {});
            }
        }
        template <class A, class T_in, class T_out>
        XSIMD_INLINE batch<T_out, A> load_unaligned(T_in const* mem, convert<T_out> cvt, requires_arch<common>) noexcept
        {
            return detail::load_unaligned<A>(mem, cvt, common {}, detail::conversion_type<A, T_in, T_out> {});
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> load(T const* mem, aligned_mode, requires_arch<A>) noexcept
        {
            return load_aligned<A>(mem, convert<T> {}, A {});
        }

        template <class A, class T>
        XSIMD_INLINE batch<T, A> load(T const* mem, unaligned_mode, requires_arch<A>) noexcept
        {
            return load_unaligned<A>(mem, convert<T> {}, A {});
        }

        // Masked-memory dispatch idiom. To give an arch a native masked path, add a
        // `requires_arch<that-arch>` overload in its arch file; conversion ranking makes
        // it beat the inherited one. Keep this base layer arch-agnostic:
        //  (a) specialize via a concrete `requires_arch<arch>` overload -- no register
        //      tag, no `enable_if` on `A`;
        //  (b) base overloads use the `requires_arch<common>` tag only; a generic
        //      `requires_arch<A>` here ties with an arch's own overload (gcc-10 ambiguity);
        //  (c) capability decisions go through arch-agnostic traits (see below).
        namespace detail
        {
            // True when an integer access can borrow the same-width float `vmaskmov*` path
            // (integral type, same-size float exists, arch has that float register);
            // otherwise the scalar-buffer fallback is used. Names no architecture.
            template <class A, class T_in, class T_out>
            using masked_memory_uses_fp_bitcast = std::integral_constant<bool,
                                                                         std::is_same_v<T_in, T_out>
                                                                             && std::is_integral_v<T_out>
                                                                             && !std::is_void_v<sized_fp_t<sizeof(T_out)>>
                                                                             && types::has_simd_register_v<sized_fp_t<sizeof(T_out)>, A>>;

            // Scalar-buffer fallback: materialize masked-off lanes as zero, then load.
            template <class A, class T_in, class T_out, bool... Values, class alignment>
            XSIMD_INLINE batch<T_out, A>
            load_masked_common(T_in const* mem, batch_bool_constant<T_out, A, Values...>, convert<T_out>, alignment, std::false_type /* uses_fp_bitcast */) noexcept
            {
                constexpr std::size_t size = batch<T_out, A>::size;
                alignas(A::alignment()) std::array<T_out, size> buffer {};
                constexpr bool mask[size] = { Values... };

                for (std::size_t i = 0; i < size; ++i)
                    buffer[i] = mask[i] ? static_cast<T_out>(mem[i]) : T_out(0);

                return batch<T_out, A>::load(buffer.data(), aligned_mode {});
            }

            // Integer-via-float path: reinterpret to the same-width float type, reuse the
            // floating-point masked load (e.g. `vmaskmovps`), then bitcast the result back.
            template <class A, class T, bool... Values, class Mode>
            XSIMD_INLINE batch<T, A>
            load_masked_common(T const* mem, batch_bool_constant<T, A, Values...>, convert<T>, Mode, std::true_type /* uses_fp_bitcast */) noexcept
            {
                using fp_t = sized_fp_t<sizeof(T)>;
                const auto f = ::xsimd::kernel::load_masked<A>(reinterpret_cast<const fp_t*>(mem), batch_bool_constant<fp_t, A, Values...> {}, convert<fp_t> {}, Mode {}, A {});
                return bitwise_cast<T>(f);
            }

            template <class A, class T_in, class T_out, bool... Values, class alignment>
            XSIMD_INLINE void
            store_masked_common(T_out* mem, batch<T_in, A> const& src, batch_bool_constant<T_in, A, Values...>, alignment, std::false_type /* uses_fp_bitcast */) noexcept
            {
                constexpr std::size_t size = batch<T_in, A>::size;
                constexpr bool mask[size] = { Values... };

                for (std::size_t i = 0; i < size; ++i)
                    if (mask[i])
                    {
                        mem[i] = static_cast<T_out>(src.get(i));
                    }
            }

            template <class A, class T, bool... Values, class Mode>
            XSIMD_INLINE void
            store_masked_common(T* mem, batch<T, A> const& src, batch_bool_constant<T, A, Values...>, Mode, std::true_type /* uses_fp_bitcast */) noexcept
            {
                using fp_t = sized_fp_t<sizeof(T)>;
                ::xsimd::kernel::store_masked<A>(reinterpret_cast<fp_t*>(mem), bitwise_cast<fp_t>(src), batch_bool_constant<fp_t, A, Values...> {}, Mode {}, A {});
            }
        }

        template <class A, class T_in, class T_out, bool... Values, class alignment>
        XSIMD_INLINE batch<T_out, A>
        load_masked(T_in const* mem, batch_bool_constant<T_out, A, Values...> mask, convert<T_out> cvt, alignment mode, requires_arch<common>) noexcept
        {
            return detail::load_masked_common(mem, mask, cvt, mode, detail::masked_memory_uses_fp_bitcast<A, T_in, T_out> {});
        }

        template <class A, class T, class Mode>
        XSIMD_INLINE batch<T, A>
        load_masked(T const* mem, batch_bool<T, A> mask, convert<T>, Mode, requires_arch<common>) noexcept
        {
            // Scalar fallback: only active lanes are touched. Arches with
            // hardware predicated loads should override this.
            constexpr std::size_t size = batch<T, A>::size;
            alignas(A::alignment()) std::array<T, size> buffer;
            for (std::size_t i = 0; i < size; ++i)
                buffer[i] = mask.get(i) ? mem[i] : T(0);
            return batch<T, A>::load_aligned(buffer.data());
        }

        template <class A, class T_in, class T_out, bool... Values, class alignment>
        XSIMD_INLINE void
        store_masked(T_out* mem, batch<T_in, A> const& src, batch_bool_constant<T_in, A, Values...> mask, alignment mode, requires_arch<common>) noexcept
        {
            detail::store_masked_common(mem, src, mask, mode, detail::masked_memory_uses_fp_bitcast<A, T_in, T_out> {});
        }

        template <class A, class T, class Mode>
        XSIMD_INLINE void
        store_masked(T* mem, batch<T, A> const& src, batch_bool<T, A> mask, Mode, requires_arch<common>) noexcept
        {
            // Scalar fallback: only active lanes are touched. Arches with
            // hardware predicated stores should override this.
            constexpr std::size_t size = batch<T, A>::size;
            alignas(A::alignment()) std::array<T, size> src_buf;
            src.store_aligned(src_buf.data());
            for (std::size_t i = 0; i < size; ++i)
                if (mask.get(i))
                    mem[i] = src_buf[i];
        }

        template <class A, class T_in, class T_out>
        XSIMD_INLINE batch<T_out, A> load_stream(T_in const* mem, convert<T_out> cvt, requires_arch<common>) noexcept
        {
            return load_aligned<A>(mem, cvt, A {});
        }

        // rotate_right
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> rotate_right(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            struct rotate_generator
            {
                static constexpr size_t get(size_t index, size_t size)
                {
                    return (index - N) % size;
                }
            };

            return swizzle(self, make_batch_constant<as_unsigned_integer_t<T>, rotate_generator, A>());
        }

        template <size_t N, class A, class T>
        XSIMD_INLINE batch<std::complex<T>, A> rotate_right(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return { rotate_right<N>(self.real()), rotate_right<N>(self.imag()) };
        }

        // rotate_left
        template <size_t N, class A, class T>
        XSIMD_INLINE batch<T, A> rotate_left(batch<T, A> const& self, requires_arch<common>) noexcept
        {
            struct rotate_generator
            {
                static constexpr size_t get(size_t index, size_t size)
                {
                    return (index + N) % size;
                }
            };

            return swizzle(self, make_batch_constant<as_unsigned_integer_t<T>, rotate_generator, A>());
        }

        template <size_t N, class A, class T>
        XSIMD_INLINE batch<std::complex<T>, A> rotate_left(batch<std::complex<T>, A> const& self, requires_arch<common>) noexcept
        {
            return { rotate_left<N>(self.real()), rotate_left<N>(self.imag()) };
        }

        // Scatter with runtime indexes.
        namespace detail
        {
            template <size_t N, typename T, typename A, typename U, typename V, std::enable_if_t<N == 0, int> = 0>
            XSIMD_INLINE void scatter(batch<T, A> const& src, U* dst,
                                      batch<V, A> const& index,
                                      ::xsimd::index<N> I) noexcept
            {
                dst[index.get(I)] = static_cast<U>(src.get(I));
            }

            template <size_t N, typename T, typename A, typename U, typename V, std::enable_if_t<N != 0, int> = 0>
            XSIMD_INLINE void
            scatter(batch<T, A> const& src, U* dst, batch<V, A> const& index,
                    ::xsimd::index<N> I) noexcept
            {
                static_assert(N <= batch<V, A>::size, "Incorrect value in recursion!");

                kernel::detail::scatter<N - 1, T, A, U, V>(
                    src, dst, index, {});
                dst[index.get(I)] = static_cast<U>(src.get(I));
            }
        } // namespace detail

        template <typename A, typename T, typename V>
        XSIMD_INLINE void
        scatter(batch<T, A> const& src, T* dst,
                batch<V, A> const& index,
                kernel::requires_arch<common>) noexcept
        {
            static_assert(batch<T, A>::size == batch<V, A>::size,
                          "Source and index sizes must match");
            kernel::detail::scatter<batch<V, A>::size - 1, T, A, T, V>(
                src, dst, index, {});
        }

        template <typename A, typename T, typename U, typename V>
        XSIMD_INLINE detail::sizes_mismatch_t<T, U, void>
        scatter(batch<T, A> const& src, U* dst,
                batch<V, A> const& index,
                kernel::requires_arch<common>) noexcept
        {
            static_assert(batch<T, A>::size == batch<V, A>::size,
                          "Source and index sizes must match");
            kernel::detail::scatter<batch<V, A>::size - 1, T, A, U, V>(
                src, dst, index, {});
        }

        template <typename A, typename T, typename U, typename V>
        XSIMD_INLINE detail::stride_match_t<T, U, void>
        scatter(batch<T, A> const& src, U* dst,
                batch<V, A> const& index,
                kernel::requires_arch<common>) noexcept
        {
            static_assert(batch<T, A>::size == batch<V, A>::size,
                          "Source and index sizes must match");
            const auto tmp = batch_cast<U>(src);
            kernel::scatter<A>(tmp, dst, index, A {});
        }

        // shuffle
        namespace detail
        {
            constexpr bool is_swizzle_fst(size_t)
            {
                return true;
            }
            template <typename ITy, typename... ITys>
            constexpr bool is_swizzle_fst(size_t bsize, ITy index, ITys... indices)
            {
                return index < bsize && is_swizzle_fst(bsize, indices...);
            }
            constexpr bool is_swizzle_snd(size_t)
            {
                return true;
            }
            template <typename ITy, typename... ITys>
            constexpr bool is_swizzle_snd(size_t bsize, ITy index, ITys... indices)
            {
                return index >= bsize && is_swizzle_snd(bsize, indices...);
            }

            constexpr bool is_zip_lo(size_t)
            {
                return true;
            }

            template <typename ITy>
            constexpr bool is_zip_lo(size_t, ITy)
            {
                return false;
            }

            template <typename ITy0, typename ITy1, typename... ITys>
            constexpr bool is_zip_lo(size_t bsize, ITy0 index0, ITy1 index1, ITys... indices)
            {
                return index0 == (bsize - (sizeof...(indices) + 2)) && index1 == (2 * bsize - (sizeof...(indices) + 2)) && is_zip_lo(bsize, indices...);
            }

            constexpr bool is_zip_hi(size_t)
            {
                return true;
            }

            template <typename ITy>
            constexpr bool is_zip_hi(size_t, ITy)
            {
                return false;
            }

            template <typename ITy0, typename ITy1, typename... ITys>
            constexpr bool is_zip_hi(size_t bsize, ITy0 index0, ITy1 index1, ITys... indices)
            {
                return index0 == (bsize / 2 + bsize - (sizeof...(indices) + 2)) && index1 == (bsize / 2 + 2 * bsize - (sizeof...(indices) + 2)) && is_zip_hi(bsize, indices...);
            }

            constexpr bool is_select(size_t)
            {
                return true;
            }

            template <typename ITy, typename... ITys>
            constexpr bool is_select(size_t bsize, ITy index, ITys... indices)
            {
                return (index < bsize ? index : index - bsize) == (bsize - sizeof...(ITys)) && is_select(bsize, indices...);
            }

        }

        template <class A, typename T, typename ITy, ITy... Indices>
        XSIMD_INLINE batch<T, A> shuffle(batch<T, A> const& x, batch<T, A> const& y, batch_constant<ITy, A, Indices...>, requires_arch<common>) noexcept
        {
            constexpr size_t bsize = sizeof...(Indices);
            static_assert(bsize == batch<T, A>::size, "valid shuffle");

            // Detect common patterns
            if constexpr (detail::is_swizzle_fst(bsize, Indices...))
            {
                return swizzle(x, batch_constant<ITy, A, ((Indices >= bsize) ? 0 /* never happens */ : Indices)...>());
            }

            if constexpr (detail::is_swizzle_snd(bsize, Indices...))
            {
                return swizzle(y, batch_constant<ITy, A, ((Indices >= bsize) ? (Indices - bsize) : 0 /* never happens */)...>());
            }

            if constexpr (detail::is_zip_lo(bsize, Indices...))
            {
                return zip_lo(x, y);
            }

            if constexpr (detail::is_zip_hi(bsize, Indices...))
            {
                return zip_hi(x, y);
            }

            if constexpr (detail::is_select(bsize, Indices...))
            {
                return select(batch_bool_constant<T, A, (Indices < bsize)...>(), x, y);
            }

#if defined(__has_builtin) && !defined(XSIMD_WITH_EMULATED)
#if __has_builtin(__builtin_shufflevector)
#define builtin_shuffle __builtin_shufflevector
#endif
#endif

#if defined(builtin_shuffle)
            typedef T vty __attribute__((__vector_size__(sizeof(batch<T, A>))));
            return (typename batch<T, A>::register_type)builtin_shuffle((vty)x.data, (vty)y.data, Indices...);

// FIXME: my experiments show that GCC only correctly optimizes this builtin
// starting at GCC 13, where it already has __builtin_shuffle_vector
//
// #elif __has_builtin(__builtin_shuffle) || GCC >= 6
//            typedef ITy integer_vector_type __attribute__((vector_size(sizeof(batch<ITy, A>))));
//            return __builtin_shuffle(x.data, y.data, integer_vector_type{Indices...});
#else
            // Use a common_pattern. It is suboptimal but clang optimizes this
            // pretty well.
            batch<T, A> x_lane = swizzle(x, batch_constant<ITy, A, ((Indices >= bsize) ? (Indices - bsize) : Indices)...>());
            batch<T, A> y_lane = swizzle(y, batch_constant<ITy, A, ((Indices >= bsize) ? (Indices - bsize) : Indices)...>());
            batch_bool_constant<T, A, (Indices < bsize)...> select_x_lane;
            return select(select_x_lane, x_lane, y_lane);
#endif
        }

        // store
        template <class A, class T>
        XSIMD_INLINE void store(batch_bool<T, A> const& self, bool* mem, requires_arch<common>) noexcept
        {
            using batch_type = batch<T, A>;
            constexpr auto size = batch_bool<T, A>::size;
            alignas(A::alignment()) T buffer[size];
            kernel::store_aligned<A>(&buffer[0], batch_type(self), A {});
            for (std::size_t i = 0; i < size; ++i)
                mem[i] = bool(buffer[i]);
        }

        template <class A, class T>
        XSIMD_INLINE void store_stream(batch_bool<T, A> const& self, bool* mem, requires_arch<common>) noexcept
        {
            store(self, mem, A {});
        }

        // store_aligned
        template <class A, class T_in, class T_out>
        XSIMD_INLINE void store_aligned(T_out* mem, batch<T_in, A> const& self, requires_arch<common>) noexcept
        {
            static_assert(!std::is_same_v<T_in, T_out>, "there should be a direct store for this type combination");
            alignas(A::alignment()) T_in buffer[batch<T_in, A>::size];
            store_aligned(&buffer[0], self);
            std::copy(std::begin(buffer), std::end(buffer), mem);
        }

        // store_unaligned
        template <class A, class T_in, class T_out>
        XSIMD_INLINE void store_unaligned(T_out* mem, batch<T_in, A> const& self, requires_arch<common>) noexcept
        {
            static_assert(!std::is_same_v<T_in, T_out>, "there should be a direct store for this type combination");
            return store_aligned<A>(mem, self, common {});
        }

        template <class A, class T_in, class T_out>
        XSIMD_INLINE void store_stream(T_out* mem, batch<T_in, A> const& self, requires_arch<common>) noexcept
        {
            store_aligned<A>(mem, self, A {});
        }

        // swizzle
        template <class A, class T, class ITy, ITy... Vs>
        XSIMD_INLINE batch<std::complex<T>, A> swizzle(batch<std::complex<T>, A> const& self, batch_constant<ITy, A, Vs...> mask, requires_arch<common>) noexcept
        {
            return { swizzle(self.real(), mask), swizzle(self.imag(), mask) };
        }

        template <class A, class T, class ITy>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch<ITy, A> mask, requires_arch<common>) noexcept
        {
            constexpr size_t size = batch<T, A>::size;
            alignas(A::alignment()) T self_buffer[size];
            store_aligned(&self_buffer[0], self);

            alignas(A::alignment()) ITy mask_buffer[size];
            store_aligned(&mask_buffer[0], mask);

            alignas(A::alignment()) T out_buffer[size];
            for (size_t i = 0; i < size; ++i)
                out_buffer[i] = self_buffer[mask_buffer[i]];
            return batch<T, A>::load_aligned(out_buffer);
        }

        template <class A, class T, class ITy, ITy... Is>
        XSIMD_INLINE batch<T, A> swizzle(batch<T, A> const& self, batch_constant<ITy, A, Is...>, requires_arch<common>) noexcept
        {
            constexpr size_t size = batch<T, A>::size;
            alignas(A::alignment()) T self_buffer[size];
            store_aligned(&self_buffer[0], self);
            return { self_buffer[Is]... };
        }

        template <class A, class T, class ITy>
        XSIMD_INLINE batch<std::complex<T>, A> swizzle(batch<std::complex<T>, A> const& self, batch<ITy, A> mask, requires_arch<common>) noexcept
        {
            return { swizzle(self.real(), mask), swizzle(self.imag(), mask) };
        }

        // load_complex_aligned
        namespace detail
        {
            template <class A, class T>
            XSIMD_INLINE batch<std::complex<T>, A> load_complex(batch<T, A> const& /*hi*/, batch<T, A> const& /*lo*/, requires_arch<common>) noexcept
            {
                static_assert(std::is_same_v<T, void>, "load_complex not implemented for the required architecture");
            }

            template <class A, class T>
            XSIMD_INLINE batch<T, A> complex_high(batch<std::complex<T>, A> const& /*src*/, requires_arch<common>) noexcept
            {
                static_assert(std::is_same_v<T, void>, "complex_high not implemented for the required architecture");
            }

            template <class A, class T>
            XSIMD_INLINE batch<T, A> complex_low(batch<std::complex<T>, A> const& /*src*/, requires_arch<common>) noexcept
            {
                static_assert(std::is_same_v<T, void>, "complex_low not implemented for the required architecture");
            }
        }

        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch<std::complex<T_out>, A> load_complex_aligned(std::complex<T_in> const* mem, convert<std::complex<T_out>>, requires_arch<common>) noexcept
        {
            using real_batch = batch<T_out, A>;
            T_in const* buffer = reinterpret_cast<T_in const*>(mem);
            real_batch hi = real_batch::load_aligned(buffer),
                       lo = real_batch::load_aligned(buffer + real_batch::size);
            return detail::load_complex(hi, lo, A {});
        }

        // load_complex_unaligned
        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch<std::complex<T_out>, A> load_complex_unaligned(std::complex<T_in> const* mem, convert<std::complex<T_out>>, requires_arch<common>) noexcept
        {
            using real_batch = batch<T_out, A>;
            T_in const* buffer = reinterpret_cast<T_in const*>(mem);
            real_batch hi = real_batch::load_unaligned(buffer),
                       lo = real_batch::load_unaligned(buffer + real_batch::size);
            return detail::load_complex(hi, lo, A {});
        }

        template <class A, class T_out, class T_in>
        XSIMD_INLINE batch<std::complex<T_out>, A> load_complex_stream(std::complex<T_in> const* mem, convert<std::complex<T_out>>, requires_arch<common>) noexcept
        {
            return load_complex_aligned<A>(mem, kernel::convert<std::complex<T_out>> {}, A {});
        }

        // store_complex_aligned
        template <class A, class T_out, class T_in>
        XSIMD_INLINE void store_complex_aligned(std::complex<T_out>* dst, batch<std::complex<T_in>, A> const& src, requires_arch<common>) noexcept
        {
            using real_batch = batch<T_in, A>;
            real_batch hi = detail::complex_high(src, A {});
            real_batch lo = detail::complex_low(src, A {});
            T_out* buffer = reinterpret_cast<T_out*>(dst);
            lo.store_aligned(buffer);
            hi.store_aligned(buffer + real_batch::size);
        }

        // store_complex_unaligned
        template <class A, class T_out, class T_in>
        XSIMD_INLINE void store_complex_unaligned(std::complex<T_out>* dst, batch<std::complex<T_in>, A> const& src, requires_arch<common>) noexcept
        {
            using real_batch = batch<T_in, A>;
            real_batch hi = detail::complex_high(src, A {});
            real_batch lo = detail::complex_low(src, A {});
            T_out* buffer = reinterpret_cast<T_out*>(dst);
            lo.store_unaligned(buffer);
            hi.store_unaligned(buffer + real_batch::size);
        }

        template <class A, class T_out, class T_in>
        XSIMD_INLINE void store_complex_stream(std::complex<T_out>* dst, batch<std::complex<T_in>, A> const& src, requires_arch<common>) noexcept
        {
            store_complex_aligned<A>(dst, src, A {});
        }

        // transpose
        template <class A, class T>
        XSIMD_INLINE void transpose(batch<T, A>* matrix_begin, batch<T, A>* matrix_end, requires_arch<common>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<T, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            alignas(A::alignment()) T scratch_buffer[batch<T, A>::size * batch<T, A>::size];
            for (size_t i = 0; i < batch<T, A>::size; ++i)
            {
                matrix_begin[i].store_aligned(&scratch_buffer[i * batch<T, A>::size]);
            }
            // FIXME: this is super naive we can probably do better.
            for (size_t i = 0; i < batch<T, A>::size; ++i)
            {
                for (size_t j = 0; j < i; ++j)
                {
                    std::swap(scratch_buffer[i * batch<T, A>::size + j],
                              scratch_buffer[j * batch<T, A>::size + i]);
                }
            }
            for (size_t i = 0; i < batch<T, A>::size; ++i)
            {
                matrix_begin[i] = batch<T, A>::load_aligned(&scratch_buffer[i * batch<T, A>::size]);
            }
        }

        // transpose
        template <class A, class = std::enable_if_t<batch<int16_t, A>::size == 8>>
        XSIMD_INLINE void transpose(batch<int16_t, A>* matrix_begin, batch<int16_t, A>* matrix_end, requires_arch<common>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<int16_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto l0 = zip_lo(matrix_begin[0], matrix_begin[1]);
            auto l1 = zip_lo(matrix_begin[2], matrix_begin[3]);
            auto l2 = zip_lo(matrix_begin[4], matrix_begin[5]);
            auto l3 = zip_lo(matrix_begin[6], matrix_begin[7]);

            auto l4 = zip_lo(bit_cast<batch<int32_t, A>>(l0), bit_cast<batch<int32_t, A>>(l1));
            auto l5 = zip_lo(bit_cast<batch<int32_t, A>>(l2), bit_cast<batch<int32_t, A>>(l3));

            auto l6 = zip_hi(bit_cast<batch<int32_t, A>>(l0), bit_cast<batch<int32_t, A>>(l1));
            auto l7 = zip_hi(bit_cast<batch<int32_t, A>>(l2), bit_cast<batch<int32_t, A>>(l3));

            auto h0 = zip_hi(matrix_begin[0], matrix_begin[1]);
            auto h1 = zip_hi(matrix_begin[2], matrix_begin[3]);
            auto h2 = zip_hi(matrix_begin[4], matrix_begin[5]);
            auto h3 = zip_hi(matrix_begin[6], matrix_begin[7]);

            auto h4 = zip_lo(bit_cast<batch<int32_t, A>>(h0), bit_cast<batch<int32_t, A>>(h1));
            auto h5 = zip_lo(bit_cast<batch<int32_t, A>>(h2), bit_cast<batch<int32_t, A>>(h3));

            auto h6 = zip_hi(bit_cast<batch<int32_t, A>>(h0), bit_cast<batch<int32_t, A>>(h1));
            auto h7 = zip_hi(bit_cast<batch<int32_t, A>>(h2), bit_cast<batch<int32_t, A>>(h3));

            matrix_begin[0] = bit_cast<batch<int16_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(l4), bit_cast<batch<int64_t, A>>(l5)));
            matrix_begin[1] = bit_cast<batch<int16_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(l4), bit_cast<batch<int64_t, A>>(l5)));
            matrix_begin[2] = bit_cast<batch<int16_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(l6), bit_cast<batch<int64_t, A>>(l7)));
            matrix_begin[3] = bit_cast<batch<int16_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(l6), bit_cast<batch<int64_t, A>>(l7)));

            matrix_begin[4] = bit_cast<batch<int16_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(h4), bit_cast<batch<int64_t, A>>(h5)));
            matrix_begin[5] = bit_cast<batch<int16_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(h4), bit_cast<batch<int64_t, A>>(h5)));
            matrix_begin[6] = bit_cast<batch<int16_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(h6), bit_cast<batch<int64_t, A>>(h7)));
            matrix_begin[7] = bit_cast<batch<int16_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(h6), bit_cast<batch<int64_t, A>>(h7)));
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint16_t, A>* matrix_begin, batch<uint16_t, A>* matrix_end, requires_arch<common>) noexcept
        {
            transpose(reinterpret_cast<batch<int16_t, A>*>(matrix_begin), reinterpret_cast<batch<int16_t, A>*>(matrix_end), A {});
        }

        template <class A, class = std::enable_if_t<batch<int8_t, A>::size == 16>>
        XSIMD_INLINE void transpose(batch<int8_t, A>* matrix_begin, batch<int8_t, A>* matrix_end, requires_arch<common>) noexcept
        {
            assert((matrix_end - matrix_begin == batch<int8_t, A>::size) && "correctly sized matrix");
            (void)matrix_end;
            auto l0 = zip_lo(matrix_begin[0], matrix_begin[1]);
            auto l1 = zip_lo(matrix_begin[2], matrix_begin[3]);
            auto l2 = zip_lo(matrix_begin[4], matrix_begin[5]);
            auto l3 = zip_lo(matrix_begin[6], matrix_begin[7]);
            auto l4 = zip_lo(matrix_begin[8], matrix_begin[9]);
            auto l5 = zip_lo(matrix_begin[10], matrix_begin[11]);
            auto l6 = zip_lo(matrix_begin[12], matrix_begin[13]);
            auto l7 = zip_lo(matrix_begin[14], matrix_begin[15]);

            auto h0 = zip_hi(matrix_begin[0], matrix_begin[1]);
            auto h1 = zip_hi(matrix_begin[2], matrix_begin[3]);
            auto h2 = zip_hi(matrix_begin[4], matrix_begin[5]);
            auto h3 = zip_hi(matrix_begin[6], matrix_begin[7]);
            auto h4 = zip_hi(matrix_begin[8], matrix_begin[9]);
            auto h5 = zip_hi(matrix_begin[10], matrix_begin[11]);
            auto h6 = zip_hi(matrix_begin[12], matrix_begin[13]);
            auto h7 = zip_hi(matrix_begin[14], matrix_begin[15]);

            auto L0 = zip_lo(bit_cast<batch<int16_t, A>>(l0), bit_cast<batch<int16_t, A>>(l1));
            auto L1 = zip_lo(bit_cast<batch<int16_t, A>>(l2), bit_cast<batch<int16_t, A>>(l3));
            auto L2 = zip_lo(bit_cast<batch<int16_t, A>>(l4), bit_cast<batch<int16_t, A>>(l5));
            auto L3 = zip_lo(bit_cast<batch<int16_t, A>>(l6), bit_cast<batch<int16_t, A>>(l7));

            auto m0 = zip_lo(bit_cast<batch<int32_t, A>>(L0), bit_cast<batch<int32_t, A>>(L1));
            auto m1 = zip_lo(bit_cast<batch<int32_t, A>>(L2), bit_cast<batch<int32_t, A>>(L3));
            auto m2 = zip_hi(bit_cast<batch<int32_t, A>>(L0), bit_cast<batch<int32_t, A>>(L1));
            auto m3 = zip_hi(bit_cast<batch<int32_t, A>>(L2), bit_cast<batch<int32_t, A>>(L3));

            matrix_begin[0] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(m0), bit_cast<batch<int64_t, A>>(m1)));
            matrix_begin[1] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(m0), bit_cast<batch<int64_t, A>>(m1)));
            matrix_begin[2] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(m2), bit_cast<batch<int64_t, A>>(m3)));
            matrix_begin[3] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(m2), bit_cast<batch<int64_t, A>>(m3)));

            auto L4 = zip_hi(bit_cast<batch<int16_t, A>>(l0), bit_cast<batch<int16_t, A>>(l1));
            auto L5 = zip_hi(bit_cast<batch<int16_t, A>>(l2), bit_cast<batch<int16_t, A>>(l3));
            auto L6 = zip_hi(bit_cast<batch<int16_t, A>>(l4), bit_cast<batch<int16_t, A>>(l5));
            auto L7 = zip_hi(bit_cast<batch<int16_t, A>>(l6), bit_cast<batch<int16_t, A>>(l7));

            auto m4 = zip_lo(bit_cast<batch<int32_t, A>>(L4), bit_cast<batch<int32_t, A>>(L5));
            auto m5 = zip_lo(bit_cast<batch<int32_t, A>>(L6), bit_cast<batch<int32_t, A>>(L7));
            auto m6 = zip_hi(bit_cast<batch<int32_t, A>>(L4), bit_cast<batch<int32_t, A>>(L5));
            auto m7 = zip_hi(bit_cast<batch<int32_t, A>>(L6), bit_cast<batch<int32_t, A>>(L7));

            matrix_begin[4] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(m4), bit_cast<batch<int64_t, A>>(m5)));
            matrix_begin[5] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(m4), bit_cast<batch<int64_t, A>>(m5)));
            matrix_begin[6] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(m6), bit_cast<batch<int64_t, A>>(m7)));
            matrix_begin[7] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(m6), bit_cast<batch<int64_t, A>>(m7)));

            auto H0 = zip_lo(bit_cast<batch<int16_t, A>>(h0), bit_cast<batch<int16_t, A>>(h1));
            auto H1 = zip_lo(bit_cast<batch<int16_t, A>>(h2), bit_cast<batch<int16_t, A>>(h3));
            auto H2 = zip_lo(bit_cast<batch<int16_t, A>>(h4), bit_cast<batch<int16_t, A>>(h5));
            auto H3 = zip_lo(bit_cast<batch<int16_t, A>>(h6), bit_cast<batch<int16_t, A>>(h7));

            auto M0 = zip_lo(bit_cast<batch<int32_t, A>>(H0), bit_cast<batch<int32_t, A>>(H1));
            auto M1 = zip_lo(bit_cast<batch<int32_t, A>>(H2), bit_cast<batch<int32_t, A>>(H3));
            auto M2 = zip_hi(bit_cast<batch<int32_t, A>>(H0), bit_cast<batch<int32_t, A>>(H1));
            auto M3 = zip_hi(bit_cast<batch<int32_t, A>>(H2), bit_cast<batch<int32_t, A>>(H3));

            matrix_begin[8] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(M0), bit_cast<batch<int64_t, A>>(M1)));
            matrix_begin[9] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(M0), bit_cast<batch<int64_t, A>>(M1)));
            matrix_begin[10] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(M2), bit_cast<batch<int64_t, A>>(M3)));
            matrix_begin[11] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(M2), bit_cast<batch<int64_t, A>>(M3)));

            auto H4 = zip_hi(bit_cast<batch<int16_t, A>>(h0), bit_cast<batch<int16_t, A>>(h1));
            auto H5 = zip_hi(bit_cast<batch<int16_t, A>>(h2), bit_cast<batch<int16_t, A>>(h3));
            auto H6 = zip_hi(bit_cast<batch<int16_t, A>>(h4), bit_cast<batch<int16_t, A>>(h5));
            auto H7 = zip_hi(bit_cast<batch<int16_t, A>>(h6), bit_cast<batch<int16_t, A>>(h7));

            auto M4 = zip_lo(bit_cast<batch<int32_t, A>>(H4), bit_cast<batch<int32_t, A>>(H5));
            auto M5 = zip_lo(bit_cast<batch<int32_t, A>>(H6), bit_cast<batch<int32_t, A>>(H7));
            auto M6 = zip_hi(bit_cast<batch<int32_t, A>>(H4), bit_cast<batch<int32_t, A>>(H5));
            auto M7 = zip_hi(bit_cast<batch<int32_t, A>>(H6), bit_cast<batch<int32_t, A>>(H7));

            matrix_begin[12] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(M4), bit_cast<batch<int64_t, A>>(M5)));
            matrix_begin[13] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(M4), bit_cast<batch<int64_t, A>>(M5)));
            matrix_begin[14] = bit_cast<batch<int8_t, A>>(zip_lo(bit_cast<batch<int64_t, A>>(M6), bit_cast<batch<int64_t, A>>(M7)));
            matrix_begin[15] = bit_cast<batch<int8_t, A>>(zip_hi(bit_cast<batch<int64_t, A>>(M6), bit_cast<batch<int64_t, A>>(M7)));
        }

        template <class A>
        XSIMD_INLINE void transpose(batch<uint8_t, A>* matrix_begin, batch<uint8_t, A>* matrix_end, requires_arch<common>) noexcept
        {
            transpose(reinterpret_cast<batch<int8_t, A>*>(matrix_begin), reinterpret_cast<batch<int8_t, A>*>(matrix_end), A {});
        }

    }

}

#endif
