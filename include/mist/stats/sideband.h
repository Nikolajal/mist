// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file stats/sideband.h
 * @brief Sideband-subtraction signal estimate for a binned spectrum.
 *
 * Detector-agnostic HEP statistic: estimate the signal yield in a peak window
 * by subtracting a background estimated from equal-width flanking sidebands.
 * ROOT-free — operates on a plain span of bin contents plus a uniform binning
 * description. See @ref mist::stats::sideband_subtract.
 *
 * Upstreamed from a downstream analysis that re-derived this per call site
 * (and hit a normalisation bug doing so); owning it once removes that class
 * of mistake.
 */

#include <cmath>
#include <cstddef>
#include <span>

namespace mist::stats
{
/**
     * @brief Result of a sideband subtraction.
     */
struct sideband_result
{
    double signal = 0.0;     ///< Background-subtracted yield: peak - background.
    double error = 0.0;      ///< Poisson error on @c signal: sqrt(peak + background).
    double peak = 0.0;       ///< Integral over the peak window.
    double background = 0.0; ///< Background under the peak, from the sidebands.
    bool ok = false;         ///< False on invalid input (see the function notes).
};

/**
     * @brief Estimate a peak's signal via sideband subtraction.
     *
     * The peak window is @f$[peak\_lo, peak\_hi]@f$. The background under the
     * peak is estimated from two flanking sidebands of total width equal to the
     * peak window (half on each side): the "outer" window
     * @f$[peak\_lo - w/2,\ peak\_hi + w/2]@f$ with @f$w = peak\_hi - peak\_lo@f$
     * is integrated, and the background is @c outer - @c peak (i.e. the wings).
     * Because the wings carry the same total width as the peak window, the wing
     * integral is itself the background estimate — no width rescaling, which is
     * exactly the normalisation step that is easy to get wrong by hand.
     *
     *   signal     = peak - background
     *   error      = sqrt(peak + background)   (peak and wings are disjoint;
     *                                          Poisson-independent)
     *
     * Bin selection mirrors the usual histogram convention: the bins
     * *containing* the window edges are included (inclusive `FindBin(lo)` ..
     * `FindBin(hi)`), and out-of-range bins are clamped to the valid span.
     *
     * @param bin_contents Uniformly-binned contents (counts).
     * @param x_min        Lower edge of the first bin.
     * @param bin_width    Width of each bin (> 0).
     * @param peak_lo      Lower edge of the peak window.
     * @param peak_hi      Upper edge of the peak window (> @p peak_lo).
     * @return Signal estimate; @c ok == false if the span is empty,
     *         @p bin_width <= 0, or @p peak_hi <= @p peak_lo.
     *
     * @note Errors assume count-like (Poisson) contents. For weighted or
     *       pre-scaled spectra the caller should account for the weights; the
     *       central value (peak - background) is unaffected by a uniform scale.
     */
[[nodiscard]] inline sideband_result
sideband_subtract(std::span<const double> bin_contents,
                  double x_min, double bin_width,
                  double peak_lo, double peak_hi)
{
    sideband_result result;
    const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(bin_contents.size());
    if (n == 0 || bin_width <= 0.0 || !(peak_hi > peak_lo))
        return result;

    const auto bin_of = [&](double x) -> std::ptrdiff_t
    {
        return static_cast<std::ptrdiff_t>(std::floor((x - x_min) / bin_width));
    };
    const auto integrate = [&](double lo, double hi) -> double
    {
        std::ptrdiff_t i0 = bin_of(lo);
        std::ptrdiff_t i1 = bin_of(hi);
        if (i0 < 0)
            i0 = 0;
        if (i1 > n - 1)
            i1 = n - 1;
        double sum = 0.0;
        for (std::ptrdiff_t i = i0; i <= i1; ++i)
            sum += bin_contents[static_cast<std::size_t>(i)];
        return sum;
    };

    const double half = 0.5 * (peak_hi - peak_lo);
    result.peak = integrate(peak_lo, peak_hi);
    const double outer = integrate(peak_lo - half, peak_hi + half);
    result.background = outer - result.peak; // the flanking wings
    result.signal = result.peak - result.background;
    result.error = std::sqrt(std::fabs(result.peak) + std::fabs(result.background));
    result.ok = true;
    return result;
}

} // namespace mist::stats
