// SPDX-License-Identifier: MIT
//
// mist/algo/smoothing.h — sliding-window reductions over a numeric sequence.
// One output value per valid window position (windows advance by one
// element, not by the window size).
//
// Output length: distance(first, last) - n + 1 for input length >= n.
//
// Header-only. ROOT-free. The TGraph-typed counterpart in
// mist::hep::graph::moving_average is a thin adapter around this primitive.
//
// Behaviour:
//   - n == 0 or n > distance(first, last)  -> returns an empty vector
//
// Constraints:
//   - The iterator's value type must be floating-point.
//
#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

namespace mist::algo {

// ---------------------------------------------------------------------------
// moving_mean — sliding window of size `n` advancing by one element.
//
// Implementation: incremental running sum. The first window is computed
// directly; each subsequent window adjusts the sum by subtracting the
// element leaving the window and adding the new entrant. O(N) total work
// regardless of n (vs O(N*n) for a naive recompute per window).
// ---------------------------------------------------------------------------

template <std::forward_iterator ForwardIt>
    requires std::floating_point<typename std::iterator_traits<ForwardIt>::value_type>
[[nodiscard]] std::vector<typename std::iterator_traits<ForwardIt>::value_type>
moving_mean(ForwardIt first, ForwardIt last, std::size_t n)
{
    using T = typename std::iterator_traits<ForwardIt>::value_type;
    std::vector<T> out;
    if (n == 0) return out;

    const auto size = static_cast<std::size_t>(std::distance(first, last));
    if (n > size) return out;

    out.reserve(size - n + 1);

    // Initial window: positions [0, n).
    T sum = T(0);
    auto window_end = first;
    for (std::size_t i = 0; i < n; ++i, ++window_end) sum += *window_end;

    const T inv_n = T(1) / static_cast<T>(n);
    out.push_back(sum * inv_n);

    // Slide: subtract the element leaving, add the element entering.
    auto window_start = first;
    while (window_end != last) {
        sum += *window_end - *window_start;
        ++window_end;
        ++window_start;
        out.push_back(sum * inv_n);
    }
    return out;
}

template <std::ranges::forward_range R>
    requires std::floating_point<std::ranges::range_value_t<R>>
[[nodiscard]] auto
moving_mean(R&& r, std::size_t n)
{
    return moving_mean(std::ranges::begin(r), std::ranges::end(r), n);
}

}  // namespace mist::algo
