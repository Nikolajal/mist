// SPDX-License-Identifier: MIT
//
// mist/algo/binning.h — non-overlapping block reductions over a numeric
// sequence. Each block of `n` consecutive input values produces one output
// value: the mean (block_mean) or the population standard deviation about
// the block mean (block_rms).
//
// Header-only. ROOT-free. The TGraph-typed counterparts in
// mist::hep::graph::block_average / block_rms are thin adapters around
// these primitives.
//
// Behaviour:
//   - n == 0 or n > distance(first, last)  -> returns an empty vector
//   - drop_partial == false (default)      -> the final block, even if it
//                                             contains fewer than n values,
//                                             is included in the output
//   - drop_partial == true                 -> the final partial block is
//                                             discarded (matches the AAU
//                                             original off-by-one behaviour
//                                             for bit-compatibility)
//
// Constraints:
//   - The iterator's value type must be floating-point (compile-time check
//     via std::floating_point concept). Integer inputs are rejected at
//     declaration time with a clear concept-failure diagnostic.
//
#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

namespace mist::algo {

// ---------------------------------------------------------------------------
// block_mean
// ---------------------------------------------------------------------------

template <std::input_iterator InputIt>
    requires std::floating_point<typename std::iterator_traits<InputIt>::value_type>
[[nodiscard]] std::vector<typename std::iterator_traits<InputIt>::value_type>
block_mean(InputIt first, InputIt last,
           std::size_t n,
           bool drop_partial = false)
{
    using T = typename std::iterator_traits<InputIt>::value_type;
    std::vector<T> out;
    if (n == 0) return out;

    const auto size = static_cast<std::size_t>(std::distance(first, last));
    if (n > size) return out;

    out.reserve(size / n + (drop_partial ? 0 : 1));

    auto it = first;
    while (it != last) {
        const auto remaining = static_cast<std::size_t>(std::distance(it, last));
        const std::size_t block = std::min(n, remaining);
        if (block < n && drop_partial) break;

        T acc = T(0);
        for (std::size_t i = 0; i < block; ++i, ++it) acc += *it;
        out.push_back(acc / static_cast<T>(block));
    }
    return out;
}

template <std::ranges::input_range R>
    requires std::floating_point<std::ranges::range_value_t<R>>
[[nodiscard]] auto
block_mean(R&& r, std::size_t n, bool drop_partial = false)
{
    return block_mean(std::ranges::begin(r), std::ranges::end(r), n, drop_partial);
}

// ---------------------------------------------------------------------------
// block_rms
//
// Population standard deviation (divisor = block size) about the block
// mean. Matches the AAU original's RMS convention; documented explicitly
// because the "sample" vs "population" choice is not universal.
// ---------------------------------------------------------------------------

template <std::input_iterator InputIt>
    requires std::floating_point<typename std::iterator_traits<InputIt>::value_type>
[[nodiscard]] std::vector<typename std::iterator_traits<InputIt>::value_type>
block_rms(InputIt first, InputIt last,
          std::size_t n,
          bool drop_partial = false)
{
    using T = typename std::iterator_traits<InputIt>::value_type;
    std::vector<T> out;
    if (n == 0) return out;

    const auto size = static_cast<std::size_t>(std::distance(first, last));
    if (n > size) return out;

    out.reserve(size / n + (drop_partial ? 0 : 1));

    auto it = first;
    while (it != last) {
        const auto remaining = static_cast<std::size_t>(std::distance(it, last));
        const std::size_t block = std::min(n, remaining);
        if (block < n && drop_partial) break;

        // Two-pass: mean, then variance about the mean.
        auto block_first = it;
        T mean_acc = T(0);
        for (std::size_t i = 0; i < block; ++i, ++it) mean_acc += *it;
        const T mean = mean_acc / static_cast<T>(block);

        T var_acc = T(0);
        for (std::size_t i = 0; i < block; ++i, ++block_first) {
            const T d = *block_first - mean;
            var_acc += d * d;
        }
        out.push_back(std::sqrt(var_acc / static_cast<T>(block)));
    }
    return out;
}

template <std::ranges::input_range R>
    requires std::floating_point<std::ranges::range_value_t<R>>
[[nodiscard]] auto
block_rms(R&& r, std::size_t n, bool drop_partial = false)
{
    return block_rms(std::ranges::begin(r), std::ranges::end(r), n, drop_partial);
}

}  // namespace mist::algo
