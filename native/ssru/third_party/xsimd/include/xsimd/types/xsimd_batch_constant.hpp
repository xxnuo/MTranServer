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

#ifndef XSIMD_BATCH_CONSTANT_HPP
#define XSIMD_BATCH_CONSTANT_HPP

#include "../config/xsimd_config.hpp"
#include "../utils/bits.hpp"
#include "./xsimd_batch.hpp"
#include "./xsimd_utils.hpp"

#include <cstddef>
#include <functional>
#include <utility>

namespace xsimd
{
    /**
     * @brief batch of boolean constant
     *
     * Abstract representation of a batch of boolean constants.
     *
     * @tparam batch_type the type of the associated batch values.
     * @tparam Values boolean constant represented by this batch
     **/
    template <typename T, class A, bool... Values>
    struct batch_bool_constant
    {
        using batch_type = batch_bool<T, A>;
        static constexpr std::size_t size = sizeof...(Values);
        using value_type = bool;
        using operand_type = T;
        static_assert(sizeof...(Values) == batch_type::size, "consistent batch size");

    public:
        /**
         * @brief Generate a batch of @p batch_type from this @p batch_bool_constant
         */
        constexpr batch_type as_batch_bool() const noexcept { return { Values... }; }

        /**
         * @brief Generate a batch of @p integers from this @p batch_bool_constant
         */
        constexpr batch<as_integer_t<T>, A> as_batch() const noexcept { return { -as_integer_t<T>(Values)... }; } // the minus is important!
        /**
         * @brief Generate a batch of @p batch_type from this @p batch_bool_constant
         */
        constexpr operator batch_type() const noexcept { return as_batch_bool(); }

        constexpr bool get(std::size_t i) const noexcept
        {
            return std::array<value_type, size> { { Values... } }[i];
        }

        static constexpr int mask() noexcept
        {
            return static_cast<int>(bits());
        }

        static constexpr bool none() noexcept
        {
            return truncated_mask() == 0u;
        }

        static constexpr bool any() noexcept
        {
            return !none();
        }

        static constexpr bool all() noexcept
        {
            return truncated_mask() == low_mask(size);
        }

        static constexpr std::size_t countr_zero() noexcept
        {
            return countr_zero_impl(truncated_mask(), size);
        }

        static constexpr std::size_t countl_zero() noexcept
        {
            return countl_zero_impl(truncated_mask(), size);
        }

        static constexpr std::size_t countr_one() noexcept
        {
            return countr_one_impl(truncated_mask(), size);
        }

        static constexpr std::size_t countl_one() noexcept
        {
            return countl_one_impl(truncated_mask(), size);
        }

        // true when the set lanes form one contiguous run starting at lane 0
        // (the empty and full masks are prefixes)
        static constexpr bool is_prefix() noexcept
        {
            return (truncated_mask() & (truncated_mask() + 1u)) == 0u;
        }

        // true when the set lanes form one contiguous run ending at the last
        // lane (the empty and full masks are suffixes)
        static constexpr bool is_suffix() noexcept
        {
            return ((truncated_mask() ^ low_mask(size)) & ((truncated_mask() ^ low_mask(size)) + 1u)) == 0u;
        }

        // length of the set run when the mask is a pure prefix (first k lanes
        // set, rest clear), else size + 1 so `prefix() == k` stays exact
        static constexpr std::size_t prefix() noexcept
        {
            return is_prefix() ? countr_one() : size + 1;
        }

        // length of the set run when the mask is a pure suffix (last k lanes
        // set, rest clear), else size + 1 so `suffix() == k` stays exact
        static constexpr std::size_t suffix() noexcept
        {
            return is_suffix() ? countl_one() : size + 1;
        }

    private:
        struct logical_or
        {
            constexpr bool operator()(bool x, bool y) const { return x || y; }
        };
        struct logical_and
        {
            constexpr bool operator()(bool x, bool y) const { return x && y; }
        };
        struct logical_xor
        {
            constexpr bool operator()(bool x, bool y) const { return x ^ y; }
        };

        template <class F, class SelfPack, class OtherPack, std::size_t... Indices>
        static constexpr batch_bool_constant<T, A, F()(std::tuple_element_t<Indices, SelfPack>::value, std::tuple_element_t<Indices, OtherPack>::value)...>
        apply(std::index_sequence<Indices...>)
        {
            return {};
        }

        template <class F, bool... OtherValues>
        static constexpr auto apply(batch_bool_constant<T, A, Values...>, batch_bool_constant<T, A, OtherValues...>)
        {
            static_assert(sizeof...(Values) == sizeof...(OtherValues), "compatible constant batches");
            return apply<F, std::tuple<std::integral_constant<bool, Values>...>, std::tuple<std::integral_constant<bool, OtherValues>...>>(std::make_index_sequence<sizeof...(Values)>());
        }

    public:
#define MAKE_BINARY_OP(OP, NAME)                                                      \
    template <bool... OtherValues>                                                    \
    constexpr auto operator OP(batch_bool_constant<T, A, OtherValues...> other) const \
    {                                                                                 \
        return apply<NAME>(*this, other);                                             \
    }

        MAKE_BINARY_OP(|, logical_or)
        MAKE_BINARY_OP(||, logical_or)
        MAKE_BINARY_OP(&, logical_and)
        MAKE_BINARY_OP(&&, logical_and)
        MAKE_BINARY_OP(^, logical_xor)

#undef MAKE_BINARY_OP

        constexpr batch_bool_constant<T, A, !Values...> operator!() const
        {
            return {};
        }

        constexpr batch_bool_constant<T, A, !Values...> operator~() const
        {
            return {};
        }

    private:
        // Build a 64-bit mask from Values... (LSB = index 0)
        static constexpr uint64_t bits() noexcept
        {
            return utils::make_bit_mask_from_bools<uint64_t>(Values...);
        }
        static constexpr uint64_t low_mask(std::size_t k) noexcept
        {
            return (k >= 64u) ? ~uint64_t(0) : ((uint64_t(1) << k) - 1u);
        }
        static constexpr uint64_t truncated_mask() noexcept
        {
            return bits() & low_mask(size);
        }
        static constexpr std::size_t countr_zero_impl(uint64_t v, std::size_t n) noexcept
        {
            return (n == 0 || (v & 1u) != 0u) ? 0u : (1u + countr_zero_impl(v >> 1, n - 1));
        }
        static constexpr std::size_t countr_one_impl(uint64_t v, std::size_t n) noexcept
        {
            return (n == 0 || (v & 1u) == 0u) ? 0u : (1u + countr_one_impl(v >> 1, n - 1));
        }
        static constexpr std::size_t countl_zero_impl(uint64_t v, std::size_t n) noexcept
        {
            return (n == 0) ? 0u : ((((v >> (n - 1)) & 1u) != 0u) ? 0u : (1u + countl_zero_impl(v, n - 1)));
        }
        static constexpr std::size_t countl_one_impl(uint64_t v, std::size_t n) noexcept
        {
            return (n == 0) ? 0u : ((((v >> (n - 1)) & 1u) == 0u) ? 0u : (1u + countl_one_impl(v, n - 1)));
        }
    };

    namespace detail
    {
        template <class A2, std::size_t BeginIdx, typename T, class A, bool... Values, std::size_t... Is>
        XSIMD_INLINE constexpr batch_bool_constant<
            T, A2,
            std::tuple_element_t<BeginIdx + Is,
                                 std::tuple<std::integral_constant<bool, Values>...>>::value...>
        splice_impl(std::index_sequence<Is...>) noexcept
        {
            return {};
        }

        template <class A2, std::size_t Begin, std::size_t End,
                  typename T, class A, bool... Values,
                  std::size_t N = (End >= Begin ? (End - Begin) : 0)>
        XSIMD_INLINE constexpr auto splice(batch_bool_constant<T, A, Values...>) noexcept
        {
            static_assert(Begin <= End, "splice: Begin must be <= End");
            static_assert(End <= sizeof...(Values), "splice: End must be <= size");
            static_assert(N == batch_bool<T, A2>::size, "splice: target arch size must match submask length");
            return splice_impl<A2, Begin, T, A, Values...>(std::make_index_sequence<N>());
        }

        template <class A2, typename T, class A, bool... Values>
        XSIMD_INLINE constexpr auto lower_half(batch_bool_constant<T, A, Values...>) noexcept
        {
            static_assert(sizeof...(Values) % 2 == 0, "lower_half requires even size");
            static_assert(batch_bool<T, A2>::size == sizeof...(Values) / 2,
                          "lower_half: target arch size must match submask length");
            return splice_impl<A2, 0, T, A, Values...>(std::make_index_sequence<sizeof...(Values) / 2>());
        }

        template <class A2, typename T, class A, bool... Values>
        XSIMD_INLINE constexpr auto upper_half(batch_bool_constant<T, A, Values...>) noexcept
        {
            static_assert(sizeof...(Values) % 2 == 0, "upper_half requires even size");
            static_assert(batch_bool<T, A2>::size == sizeof...(Values) / 2,
                          "upper_half: target arch size must match submask length");
            return splice_impl<A2, sizeof...(Values) / 2, T, A, Values...>(std::make_index_sequence<sizeof...(Values) / 2>());
        }
    } // namespace detail

    /**
     * @brief batch of integral constants
     *
     * Abstract representation of a batch of integral constants.
     *
     * @tparam batch_type the type of the associated batch values.
     * @tparam Values constants represented by this batch
     **/
    template <typename T, class A, T... Values>
    struct batch_constant
    {
        static constexpr std::size_t size = sizeof...(Values);
        using batch_type = batch<T, A>;
        using value_type = typename batch_type::value_type;
        static_assert(sizeof...(Values) == batch_type::size, "consistent batch size");

        /**
         * @brief Generate a batch of @p batch_type from this @p batch_constant
         */
        XSIMD_INLINE batch_type as_batch() const noexcept { return { Values... }; }

        /**
         * @brief Generate a batch of @p batch_type from this @p batch_constant
         */
        XSIMD_INLINE operator batch_type() const noexcept { return as_batch(); }

        /**
         * @brief Get the @p i th element of this @p batch_constant
         */
        constexpr T get(std::size_t i) const noexcept
        {
            return get(i, std::array<T, size> { Values... });
        }

    private:
        constexpr T get(std::size_t i, std::array<T, size> const& values) const noexcept
        {
            return values[i];
        }

        template <typename = void>
        struct binary_rshift
        {
            constexpr T operator()(T x, T y) const { return x >> y; }
        };
        template <typename = void>
        struct binary_lshift
        {
            constexpr T operator()(T x, T y) const { return x << y; }
        };

        template <class F, class SelfPack, class OtherPack, std::size_t... Indices>
        static constexpr batch_constant<T, A, F()(std::tuple_element_t<Indices, SelfPack>::value, std::tuple_element_t<Indices, OtherPack>::value)...>
        apply(std::index_sequence<Indices...>)
        {
            return {};
        }

        template <class F, T... OtherValues>
        static constexpr auto apply(batch_constant<T, A, Values...>, batch_constant<T, A, OtherValues...>)
        {
            static_assert(sizeof...(Values) == sizeof...(OtherValues), "compatible constant batches");
            return apply<F, std::tuple<std::integral_constant<T, Values>...>, std::tuple<std::integral_constant<T, OtherValues>...>>(std::make_index_sequence<sizeof...(Values)>());
        }

    public:
#define MAKE_BINARY_OP(OP, NAME)                                                                                       \
    template <T... OtherValues>                                                                                        \
    constexpr auto operator OP(batch_constant<T, A, OtherValues...> other) const                                       \
    {                                                                                                                  \
        return apply<NAME<void>>(*this, other);                                                                        \
    }                                                                                                                  \
    template <T OtherValue>                                                                                            \
    constexpr batch_constant<T, A, (Values OP OtherValue)...> operator OP(std::integral_constant<T, OtherValue>) const \
    {                                                                                                                  \
        return {};                                                                                                     \
    }

        MAKE_BINARY_OP(+, std::plus)
        MAKE_BINARY_OP(-, std::minus)
        MAKE_BINARY_OP(*, std::multiplies)
        MAKE_BINARY_OP(/, std::divides)
        MAKE_BINARY_OP(%, std::modulus)
        MAKE_BINARY_OP(&, std::bit_and)
        MAKE_BINARY_OP(|, std::bit_or)
        MAKE_BINARY_OP(^, std::bit_xor)
        MAKE_BINARY_OP(<<, binary_lshift)
        MAKE_BINARY_OP(>>, binary_rshift)

#undef MAKE_BINARY_OP

        template <class F, class SelfPack, class OtherPack, std::size_t... Indices>
        static constexpr batch_bool_constant<T, A, F()(std::tuple_element_t<Indices, SelfPack>::value, std::tuple_element_t<Indices, OtherPack>::value)...>
        apply_bool(std::index_sequence<Indices...>)
        {
            return {};
        }

        template <class F, T... OtherValues>
        static constexpr auto apply_bool(batch_constant<T, A, Values...>, batch_constant<T, A, OtherValues...>)
        {
            static_assert(sizeof...(Values) == sizeof...(OtherValues), "compatible constant batches");
            return apply_bool<F, std::tuple<std::integral_constant<T, Values>...>, std::tuple<std::integral_constant<T, OtherValues>...>>(std::make_index_sequence<sizeof...(Values)>());
        }

#define MAKE_BINARY_BOOL_OP(OP, NAME)                                                                                       \
    template <T... OtherValues>                                                                                             \
    constexpr auto operator OP(batch_constant<T, A, OtherValues...> other) const                                            \
    {                                                                                                                       \
        return apply_bool<NAME<void>>(*this, other);                                                                        \
    }                                                                                                                       \
    template <T OtherValue>                                                                                                 \
    constexpr batch_bool_constant<T, A, (Values OP OtherValue)...> operator OP(std::integral_constant<T, OtherValue>) const \
    {                                                                                                                       \
        return {};                                                                                                          \
    }

        MAKE_BINARY_BOOL_OP(==, std::equal_to)
        MAKE_BINARY_BOOL_OP(!=, std::not_equal_to)
        MAKE_BINARY_BOOL_OP(<, std::less)
        MAKE_BINARY_BOOL_OP(<=, std::less_equal)
        MAKE_BINARY_BOOL_OP(>, std::greater)
        MAKE_BINARY_BOOL_OP(>=, std::greater_equal)

#undef MAKE_BINARY_BOOL_OP

        constexpr batch_constant<T, A, (T)-Values...> operator-() const
        {
            return {};
        }

        constexpr batch_constant<T, A, (T) + Values...> operator+() const
        {
            return {};
        }

        constexpr batch_constant<T, A, (T)~Values...> operator~() const
        {
            return {};
        }
    };

    namespace detail
    {
        template <typename T, class G, class A, std::size_t... Is>
        XSIMD_INLINE constexpr batch_constant<T, A, (T)G::get(Is, sizeof...(Is))...>
        make_batch_constant(std::index_sequence<Is...>) noexcept
        {
            return {};
        }

        template <typename T, T Val, class A, std::size_t... Is>
        XSIMD_INLINE constexpr batch_constant<T, A, (static_cast<T>(0 * Is) + Val)...>
        make_batch_constant(std::index_sequence<Is...>) noexcept
        {
            return {};
        }

#if XSIMD_CPP_VERSION >= 202002L
        template <std::array Arr, class A, std::size_t... Is>
        XSIMD_INLINE constexpr batch_constant<typename decltype(Arr)::value_type, A, Arr[Is]...>
        make_batch_constant(std::index_sequence<Is...>) noexcept
        {
            return {};
        }
#endif

        template <typename T, class G, class A, std::size_t... Is>
        XSIMD_INLINE constexpr batch_bool_constant<T, A, G::get(Is, sizeof...(Is))...>
        make_batch_bool_constant(std::index_sequence<Is...>) noexcept
        {
            return {};
        }

        template <typename T, bool Val, class A, std::size_t... Is>
        XSIMD_INLINE constexpr batch_bool_constant<T, A, ((static_cast<bool>(Is) | true) & Val)...>
        make_batch_bool_constant(std::index_sequence<Is...>) noexcept
        {
            return {};
        }

#if XSIMD_CPP_VERSION >= 202002L
        template <typename T, std::array Arr, class A, std::size_t... Is>
        XSIMD_INLINE constexpr batch_bool_constant<T, A, Arr[Is]...>
        make_batch_bool_constant(std::index_sequence<Is...>) noexcept
        {
            return {};
        }
#endif

    } // namespace detail

    /**
     * @brief Build a @c batch_constant out of a generator function
     *
     * @tparam batch_type type of the (non-constant) batch to build
     * @tparam G type used to generate that batch. That type must have a static
     * member @c get that's used to generate the batch constant. Conversely, the
     * generated batch_constant has value `{G::get(0, batch_size), ... , G::get(batch_size - 1, batch_size)}`
     *
     * The following generator produces a batch of `(n - 1, 0, 1, ... n-2)`
     *
     * @code
     * struct Rot
     * {
     *     static constexpr unsigned get(unsigned i, unsigned n)
     *     {
     *         return (i + n - 1) % n;
     *     }
     * };
     * @endcode
     */
    template <typename T, class G, class A = default_arch>
    XSIMD_INLINE constexpr decltype(detail::make_batch_constant<T, G, A>(std::make_index_sequence<batch<T, A>::size>()))
    make_batch_constant() noexcept
    {
        return {};
    }

    /**
     * @brief Build a @c batch_bool_constant out of a generator function
     *
     * Similar to @c make_batch_constant for @c batch_bool_constant
     */
    template <typename T, class G, class A = default_arch>
    XSIMD_INLINE constexpr decltype(detail::make_batch_bool_constant<T, G, A>(std::make_index_sequence<batch<T, A>::size>()))
    make_batch_bool_constant() noexcept
    {
        return {};
    }

// FIXME: Skipping those for doxygen because of bad interaction with breathe.
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    /**
     * @brief Build a @c batch_constant with a single repeated value.
     *
     * @tparam T type of the data held in the batch.
     * @tparam Val The value to repeat.
     * @tparam A Architecture that will be used when converting to a regular batch.
     */
    template <typename T, T Val, class A = default_arch>
    XSIMD_INLINE constexpr decltype(detail::make_batch_constant<T, Val, A>(std::make_index_sequence<batch<T, A>::size>()))
    make_batch_constant() noexcept
    {
        return {};
    }

#if XSIMD_CPP_VERSION >= 202002L
    /**
     * @brief Build a @c batch_constant from a std::array (C++20)
     *
     * @tparam Arr The std::array containing the values (non type template argument).
     * @tparam A Architecture that will be used when converting to a regular batch.
     */
    template <std::array Arr, class A = default_arch>
        requires(Arr.size() == batch<typename decltype(Arr)::value_type, A>::size)
    XSIMD_INLINE constexpr auto make_batch_constant() noexcept
    {
        return detail::make_batch_constant<Arr, A>(std::make_index_sequence<Arr.size()>());
    }
#endif

    /*
     * @brief Build a @c batch_bool_constant with a single repeated value.
     *
     * Similar to @c make_batch_constant for @c batch_bool_constant
     */
    template <typename T, bool Val, class A = default_arch>
    XSIMD_INLINE constexpr decltype(detail::make_batch_bool_constant<T, Val, A>(std::make_index_sequence<batch<T, A>::size>()))
    make_batch_bool_constant() noexcept
    {
        return {};
    }

#if XSIMD_CPP_VERSION >= 202002L
    /**
     * @brief Build a @c batch_constant from a std::array of boolean (C++20)
     *
     * @tparam Arr The std::array containing the boolean values (non type template argument).
     * @tparam A Architecture that will be used when converting to a regular batch.
     */
    template <typename T, std::array Arr, class A = default_arch>
        requires(
            (Arr.size() == batch_bool<T, A>::size)
            && std::is_same_v<typename decltype(Arr)::value_type, bool>)
    XSIMD_INLINE constexpr auto make_batch_bool_constant() noexcept
    {
        return detail::make_batch_bool_constant<T, Arr, A>(std::make_index_sequence<Arr.size()>());
    }
#endif

#endif

    namespace generator
    {
        template <class T>
        struct iota
        {
            static constexpr T get(size_t index, size_t)
            {
                return static_cast<T>(index);
            }
        };
    }
    /**
     * @brief Build a @c batch_constant as an enumerated range
     *
     * @tparam T type of the data held in the batch.
     * @tparam A Architecture that will be used when converting to a regular batch.
     */
    template <typename T, class A = default_arch>
    XSIMD_INLINE constexpr auto make_iota_batch_constant() noexcept
    {
        return make_batch_constant<T, generator::iota<T>, A>();
    }

} // namespace xsimd

#endif
