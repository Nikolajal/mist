// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file bits.h
 * @brief 32-bit mask manipulation helpers — encode/decode individual bits.
 *
 * ROOT-free, header-only. Upstreamed from a downstream detector framework
 * that used these for per-device participant and dead-channel masks; they
 * are detector-agnostic and belong in the toolkit.
 *
 * Modernised against C++20 `<bit>` (`std::countr_zero`, `std::popcount`) and
 * ranges/concepts for the multi-bit encoder. See @ref mist::bits.
 */

#include <bit>
#include <cassert>
#include <cstdint>
#include <ranges>
#include <vector>

namespace mist::bits
{
/**
     * @brief Encode a single bit into a 32-bit mask.
     * @param index Index of the bit to set (0..31). Out-of-range input
     *              triggers a debug-build assert; release builds return 0.
     * @return Mask with only that bit set, or 0 if @p index >= 32.
     */
[[nodiscard]] constexpr std::uint32_t encode_bit(std::uint8_t index) noexcept
{
    assert(index < 32 && "encode_bit: index must be < 32");
    return (index < 32) ? (std::uint32_t{1} << index) : 0u;
}

/**
     * @brief Encode multiple bit indices into a 32-bit mask.
     *
     * Generalised from the original `std::vector<uint8_t>` overload to any
     * input range of integral indices. Out-of-range indices trigger a
     * debug-build assert and are skipped in release builds.
     *
     * @tparam Range Input range whose value type is integral.
     * @param indices Bit indices to set (0..31).
     * @return Mask with all in-range specified bits set.
     */
template <std::ranges::input_range Range>
    requires std::integral<std::ranges::range_value_t<Range>>
[[nodiscard]] constexpr std::uint32_t encode_bits(Range &&indices) noexcept
{
    std::uint32_t mask = 0;
    for (const auto index : indices)
    {
        assert(index >= 0 && index < 32 && "encode_bits: every index must be in [0, 32)");
        if (index >= 0 && index < 32)
            mask |= (std::uint32_t{1} << static_cast<unsigned>(index));
    }
    return mask;
}

/**
     * @brief Count trailing zeros — index of the least-significant set bit.
     * @param mask 32-bit mask.
     * @return Index of the lowest set bit, or 32 if @p mask is 0.
     * @note Thin wrapper over @c std::countr_zero (constant-time on any
     *       conforming C++20 implementation).
     */
[[nodiscard]] constexpr std::uint8_t count_trailing_zeros(std::uint32_t mask) noexcept
{
    return mask == 0 ? std::uint8_t{32}
                     : static_cast<std::uint8_t>(std::countr_zero(mask));
}

/**
     * @brief Decode a 32-bit mask into the indices of its set bits.
     *
     * Uses the @c mask&=mask-1 (Kernighan) trick to clear the lowest set bit
     * each iteration, so the work is O(popcount(mask)) rather than
     * O(highest-set-bit).
     *
     * @param mask 32-bit mask.
     * @return Ascending indices of the set bits.
     */
[[nodiscard]] inline std::vector<std::uint8_t> decode_bits(std::uint32_t mask)
{
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(std::popcount(mask)));
    while (mask)
    {
        result.push_back(static_cast<std::uint8_t>(std::countr_zero(mask)));
        mask &= mask - 1; // clear lowest set bit
    }
    return result;
}

} // namespace mist::bits
