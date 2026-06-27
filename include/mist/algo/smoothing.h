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

#include <cmath>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

namespace mist::algo
{

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
    if (n == 0)
        return out;

    const auto size = static_cast<std::size_t>(std::distance(first, last));
    if (n > size)
        return out;

    out.reserve(size - n + 1);

    // Initial window: positions [0, n).
    T sum = T(0);
    auto window_end = first;
    for (std::size_t i = 0; i < n; ++i, ++window_end)
        sum += *window_end;

    const T inv_n = T(1) / static_cast<T>(n);
    out.push_back(sum * inv_n);

    // Slide: subtract the element leaving, add the element entering.
    auto window_start = first;
    while (window_end != last)
    {
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
moving_mean(R &&r, std::size_t n)
{
    return moving_mean(std::ranges::begin(r), std::ranges::end(r), n);
}

// ---------------------------------------------------------------------------
// ema — exponential moving average.
//
// y[0] = x[0]; y[i] = alpha * x[i] + (1 - alpha) * y[i-1].
// alpha = 1 is a pass-through; smaller alpha smooths more.
// Requires alpha in (0, 1]. Returns empty for empty input or invalid alpha.
// Output length equals input length.
// ---------------------------------------------------------------------------

template <std::forward_iterator ForwardIt>
    requires std::floating_point<typename std::iterator_traits<ForwardIt>::value_type>
[[nodiscard]] std::vector<typename std::iterator_traits<ForwardIt>::value_type>
ema(ForwardIt first, ForwardIt last, double alpha)
{
    using T = typename std::iterator_traits<ForwardIt>::value_type;
    std::vector<T> out;
    if (first == last || !(alpha > 0.0 && alpha <= 1.0))
        return out;

    out.reserve(static_cast<std::size_t>(std::distance(first, last)));
    const T a = static_cast<T>(alpha);
    const T one_minus_a = T(1) - a;

    T prev = *first;
    out.push_back(prev);
    ++first;
    while (first != last)
    {
        prev = a * (*first) + one_minus_a * prev;
        out.push_back(prev);
        ++first;
    }
    return out;
}

template <std::ranges::forward_range R>
    requires std::floating_point<std::ranges::range_value_t<R>>
[[nodiscard]] auto ema(R &&r, double alpha)
{
    return ema(std::ranges::begin(r), std::ranges::end(r), alpha);
}

// ---------------------------------------------------------------------------
// gaussian_smooth — Gaussian-kernel smoothing.
//
// sigma is in samples. The kernel is truncated at ceil(3*sigma) on each side
// and renormalized per output position so boundary elements are not biased
// toward zero. Returns a vector of the same length as the input.
// Returns empty for empty input or sigma <= 0.
// ---------------------------------------------------------------------------

template <std::forward_iterator ForwardIt>
    requires std::floating_point<typename std::iterator_traits<ForwardIt>::value_type>
[[nodiscard]] std::vector<typename std::iterator_traits<ForwardIt>::value_type>
gaussian_smooth(ForwardIt first, ForwardIt last, double sigma)
{
    using T = typename std::iterator_traits<ForwardIt>::value_type;
    const auto size = static_cast<std::size_t>(std::distance(first, last));
    std::vector<T> out;
    if (size == 0 || !(sigma > 0.0))
        return out;

    const int hw = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<double> kernel(static_cast<std::size_t>(2 * hw + 1));
    for (int i = -hw; i <= hw; ++i)
        kernel[static_cast<std::size_t>(i + hw)] =
            std::exp(-0.5 * static_cast<double>(i * i) / (sigma * sigma));

    const std::vector<T> src(first, last);
    out.resize(size);
    for (std::size_t j = 0; j < size; ++j)
    {
        double wsum = 0.0, wxsum = 0.0;
        for (int k = -hw; k <= hw; ++k)
        {
            const int idx = static_cast<int>(j) + k;
            if (idx < 0 || static_cast<std::size_t>(idx) >= size)
                continue;
            const double w = kernel[static_cast<std::size_t>(k + hw)];
            wsum += w;
            wxsum += w * static_cast<double>(src[static_cast<std::size_t>(idx)]);
        }
        out[j] = wsum > 0.0 ? static_cast<T>(wxsum / wsum) : T(0);
    }
    return out;
}

template <std::ranges::forward_range R>
    requires std::floating_point<std::ranges::range_value_t<R>>
[[nodiscard]] auto gaussian_smooth(R &&r, double sigma)
{
    return gaussian_smooth(std::ranges::begin(r), std::ranges::end(r), sigma);
}

} // namespace mist::algo
