/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ***************************************************************************/

#ifndef XSIMD_CPUID_UTILS_HPP
#define XSIMD_CPUID_UTILS_HPP

#include <cassert>
#include <type_traits>

namespace xsimd
{
    namespace utils
    {
        template <typename... Args>
        constexpr auto make_bit_mask(Args... bits)
        {
            using out_type = std::common_type_t<unsigned short, std::make_unsigned_t<Args>...>;
            [[maybe_unused]] constexpr auto bit_count = static_cast<out_type>(8 * sizeof(out_type));
            assert((((static_cast<out_type>(bits) < bit_count) && ...)));
            return static_cast<out_type>((0u | ... | (1u << bits)));
        }

        /**
         * Return a mask whose bit `i` holds the truth value of the `i`th argument.
         */
        template <typename I, typename... Bools>
        constexpr I make_bit_mask_from_bools(Bools... bools) noexcept
        {
            static_assert(std::is_unsigned_v<I>, "Bit operations must be done on unsigned integers");
            static_assert(sizeof...(Bools) <= 8 * sizeof(I), "Not enough bits to hold all the values");
            // GCC's -Wsequence-point does not model fold sequencing and rejects `shift++` inside
            // the fold operand, hence the comma to separate read from increment.
            I mask = 0;
            unsigned shift = 0;
            ((mask |= static_cast<I>(static_cast<I>(bools ? 1 : 0) << shift), ++shift), ...);
            return mask;
        }

        template <auto... Bits, typename I>
        constexpr bool all_bits_set(I value)
        {
            static_assert(std::is_unsigned_v<I>, "Bit operations must be done on unsigned integers");
            constexpr I mask = make_bit_mask<I>(static_cast<I>(Bits)...);
            return (value & mask) == mask;
        }

        template <auto Bit, typename I>
        constexpr I set_bit(I value)
        {
            static_assert(std::is_unsigned_v<I>, "Bit operations must be done on unsigned integers");
            constexpr I mask = make_bit_mask<I>(static_cast<I>(Bit));
            return value | mask;
        }

        /**
         * Return a mask with the `width` lowest bits set.
         */
        template <typename I>
        constexpr I make_low_mask(I width) noexcept
        {
            static_assert(std::is_unsigned_v<I>, "Bit operations must be done on unsigned integers");
            assert(width <= static_cast<I>(8 * sizeof(I)));
            if (width == static_cast<I>(8 * sizeof(I)))
            {
                return ~I { 0 };
            }
            return (I { 1 } << width) - I { 1 };
        }

        /* A bitset over an unsigned integer type, indexed by an enum key type. */
        template <typename K, typename U>
        struct uint_bitset
        {
            /* The underlying unsigned integer type storing the bits. */
            using storage_type = U;
            /* The enum type whose values name individual bits. */
            using key_type = K;

            /* Construct from a raw bit pattern. */
            constexpr explicit uint_bitset(storage_type bitset = {}) noexcept
                : m_bitset(bitset)
            {
            }

            /* Return true if every bit named by the template arguments is set. */
            template <key_type... bits>
            constexpr bool all_bits_set() const noexcept
            {
                return utils::all_bits_set<static_cast<storage_type>(bits)...>(m_bitset);
            }

            /* Return true if the bit is set. */
            template <key_type bit>
            constexpr bool bit_is_set() const noexcept
            {
                return all_bits_set<bit>();
            }

            /* Set the corresponding bit to true in the bitfield. */
            template <key_type bit>
            constexpr void set_bit() noexcept
            {
                m_bitset = utils::set_bit<static_cast<storage_type>(bit)>(m_bitset);
            }

            /* Extract the bits in [start, end[, shifted down to start at bit 0. */
            template <key_type start, key_type end>
            constexpr storage_type get_range() const noexcept
            {
                constexpr storage_type start_bit = static_cast<storage_type>(start);
                constexpr storage_type end_bit = static_cast<storage_type>(end);
                constexpr storage_type width = end_bit - start_bit;
                constexpr storage_type mask = make_low_mask<storage_type>(width);
                return (m_bitset >> start_bit) & mask;
            }

        private:
            storage_type m_bitset = { 0 };
        };
    }
}

#endif
