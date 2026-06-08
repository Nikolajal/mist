// SPDX-License-Identifier: MIT
//
// mist/algo/edges.h — bin-edge generators.
//
// Header-only, ROOT-free. Produces the edge array for a binning, as consumed
// by histogram constructors and any code that needs explicit boundaries.
//
#pragma once

#include <cmath>
#include <vector>

namespace mist::algo {

// ---------------------------------------------------------------------------
// log_binning: n_bins + 1 edges spanning [x_min, x_max], spaced evenly in
// log10 (a constant ratio between adjacent edges). Common for pT / energy
// spectra where a fixed *ratio* per bin is wanted rather than a fixed width.
//
// Requires n_bins > 0 and 0 < x_min < x_max (log is undefined at and below
// zero); otherwise an empty vector is returned. The endpoints are pinned
// exactly to x_min / x_max to absorb pow/log10 round-off.
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::vector<double>
log_binning(std::size_t n_bins, double x_min, double x_max)
{
    std::vector<double> edges;
    if (n_bins == 0 || !(x_min > 0.0) || !(x_max > x_min)) return edges;

    const double log_lo = std::log10(x_min);
    const double log_hi = std::log10(x_max);
    const double step = (log_hi - log_lo) / static_cast<double>(n_bins);

    edges.reserve(n_bins + 1);
    for (std::size_t i = 0; i <= n_bins; ++i)
        edges.push_back(std::pow(10.0, log_lo + static_cast<double>(i) * step));

    edges.front() = x_min;
    edges.back() = x_max;
    return edges;
}

}  // namespace mist::algo
