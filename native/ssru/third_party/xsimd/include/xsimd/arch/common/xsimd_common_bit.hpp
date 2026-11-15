/****************************************************************
 * Partial backport of `__cpp_lib_bitops == 201907L` from C++20 *
 ****************************************************************/

#ifndef XSIMD_BIT_HPP
#define XSIMD_BIT_HPP

#include "../../config/xsimd_config.hpp"

#if XSIMD_CPP_VERSION > 202002L

#include <version>

#if __cpp_lib_bitops >= 201907L

#include <bit>

namespace xsimd
{
    namespace detail
    {
        using std::countl_one;
        using std::countl_zero;
        using std::countr_one;
        using std::countr_zero;
        using std::popcount;
    }
}

#endif

#else

#include <climits>
#include <type_traits>

#ifdef __has_builtin
#define XSIMD_HAS_BUILTIN(x) __has_builtin(x)
#else
#define XSIMD_HAS_BUILTIN(x) 0
#endif

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace xsimd
{
    namespace detail
    {
        // FIXME: We could do better by dispatching to the appropriate popcount instruction
        // depending on the arch.

        template <class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE int popcount(T x) noexcept
        {
#if XSIMD_HAS_BUILTIN(__builtin_popcountg)
            return __builtin_popcountg(x);
#else
            if constexpr (sizeof(T) == 1)
            {
#if XSIMD_HAS_BUILTIN(__builtin_popcount)
                return __builtin_popcount(x);
#elif defined(_MSC_VER)
                return __popcnt(x);
#else
                // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSet64
                return ((uint64_t)x * 0x200040008001ULL & 0x111111111111111ULL) % 0xf;
#endif
            }
            else if constexpr (sizeof(T) == 2)
            {
#if XSIMD_HAS_BUILTIN(__builtin_popcount)
                return __builtin_popcount(x);
#elif defined(_MSC_VER)
                return __popcnt16(x);
#else
                // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSet64
                constexpr unsigned long long msb12 = 0x1001001001001ULL;
                constexpr unsigned long long mask5 = 0x84210842108421ULL;

                unsigned int v = (unsigned int)x;

                return ((v & 0xfff) * msb12 & mask5) % 0x1f
                    + (((v & 0xfff000) >> 12) * msb12 & mask5) % 0x1f;
#endif
            }
            else if constexpr (sizeof(T) == 4)
            {
#if XSIMD_HAS_BUILTIN(__builtin_popcount)
                return __builtin_popcount(x);
#elif defined(_MSC_VER)
                return __popcnt(x);
#else
                // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
                x = x - ((x >> 1) & (T) ~(T)0 / 3);
                x = (x & (T) ~(T)0 / 15 * 3) + ((x >> 2) & (T) ~(T)0 / 15 * 3);
                x = (x + (x >> 4)) & (T) ~(T)0 / 255 * 15;
                return (x * ((T) ~(T)0 / 255)) >> (sizeof(T) - 1) * CHAR_BIT;
#endif
            }
            else
            {
                // sizeof(T) == 8
#if XSIMD_HAS_BUILTIN(__builtin_popcountll)
                return __builtin_popcountll(x);
#elif XSIMD_HAS_BUILTIN(__builtin_popcount)
                return __builtin_popcount((unsigned int)x) + __builtin_popcount((unsigned int)(x >> 32));
#elif defined(_MSC_VER)
#ifdef _M_X64
                return (int)__popcnt64(x);
#else
                return (int)(__popcnt((unsigned int)x) + __popcnt((unsigned int)(x >> 32)));
#endif
#else
                // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
                x = x - ((x >> 1) & (T) ~(T)0 / 3);
                x = (x & (T) ~(T)0 / 15 * 3) + ((x >> 2) & (T) ~(T)0 / 15 * 3);
                x = (x + (x >> 4)) & (T) ~(T)0 / 255 * 15;
                return (x * ((T) ~(T)0 / 255)) >> (sizeof(T) - 1) * CHAR_BIT;
#endif
            }
#endif
        }

        template <class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE int countl_zero(T x) noexcept
        {
#if XSIMD_HAS_BUILTIN(__builtin_clzg)
            return __builtin_clzg(x, (int)(sizeof(T) * CHAR_BIT));
#else
            if (x == 0)
                return sizeof(T) * CHAR_BIT;

            if constexpr (sizeof(T) <= 4)
            {
#if XSIMD_HAS_BUILTIN(__builtin_clz)
                return __builtin_clz((unsigned int)x) - (4 - sizeof(T)) * CHAR_BIT;
#elif defined(_MSC_VER)
                unsigned long index;
                _BitScanReverse(&index, (unsigned long)x);
                return sizeof(T) * CHAR_BIT - index - 1;
#else
                x |= x >> 1;
                x |= x >> 2;
                x |= x >> 4;
                if constexpr (sizeof(T) >= 2)
                {
                    x |= x >> 8;
                }
                if constexpr (sizeof(T) >= 4)
                {
                    x |= x >> 16;
                }
                return sizeof(T) * CHAR_BIT - popcount(x);
#endif
            }
            else
            {
                // sizeof(T) == 8
#if XSIMD_HAS_BUILTIN(__builtin_clzll)
                return __builtin_clzll((unsigned long long)x);
#elif defined(_MSC_VER) && defined(_M_X64)
                unsigned long index;
                _BitScanReverse64(&index, (unsigned long long)x);
                return sizeof(T) * CHAR_BIT - index - 1;
#else
                x |= x >> 1;
                x |= x >> 2;
                x |= x >> 4;
                x |= x >> 8;
                x |= x >> 16;
                x |= x >> 32;
                return sizeof(T) * CHAR_BIT - popcount(x);
#endif
            }
#endif
        }

        template <class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE int countl_one(T x) noexcept
        {
            return countl_zero(T(~x));
        }

        template <class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE int countr_zero(T x) noexcept
        {
#if XSIMD_HAS_BUILTIN(__builtin_ctzg)
            return __builtin_ctzg(x, (int)(sizeof(T) * CHAR_BIT));
#else
            if (x == 0)
                return sizeof(T) * CHAR_BIT;

            if constexpr (sizeof(T) <= 4)
            {
#if XSIMD_HAS_BUILTIN(__builtin_ctz)
                return __builtin_ctz((unsigned int)x);
#elif defined(_MSC_VER)
                unsigned long index;
                _BitScanForward(&index, (unsigned long)x);
                return index;
#endif
            }
            else
            {
                // sizeof(T) == 8
#if XSIMD_HAS_BUILTIN(__builtin_ctzll)
                return __builtin_ctzll((unsigned long long)x);
#elif defined(_MSC_VER) && defined(_M_X64)
                unsigned long index;
                _BitScanForward64(&index, (unsigned long long)x);
                return index;
#endif
            }

            // https://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
            return popcount((T)((x & -x) - 1));
#endif
        }

        template <class T, class = std::enable_if_t<std::is_unsigned_v<T>>>
        XSIMD_INLINE int countr_one(T x) noexcept
        {
            return countr_zero(T(~x));
        }

    }
}

#endif
#endif
