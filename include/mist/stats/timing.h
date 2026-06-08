// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file stats/timing.h
 * @brief Timing-distribution corrections and rate estimators (ROOT-free).
 *
 * Two detector-agnostic primitives for same-frame hit timing:
 *
 *  - @ref mist::stats::triangle_acceptance — the geometric acceptance for the
 *    time *difference* of two points drawn uniformly within a frame. Dividing a
 *    Δt distribution by it flattens the triangular pair-acceptance, so an
 *    uncorrelated (flat-in-time) background reads as flat in Δt.
 *  - @ref mist::stats::poisson_rate_mle — the maximum-likelihood firing rate of
 *    a Poisson process from its consecutive-interval samples (the intervals are
 *    exponentially distributed; @f$\hat\lambda = 1/\overline{\Delta t}@f$).
 *
 * ROOT-free: the consumer (a beam-test writer) previously did the triangle
 * correction bin-by-bin inline, and estimated the rate by fitting ROOT's `expo`
 * to a projected histogram. Both are reduced here to closed forms on plain
 * numbers / spans.
 */

#include <cmath>
#include <cstddef>
#include <span>

namespace mist::stats
{
    /**
     * @brief Geometric acceptance of a same-frame time difference.
     *
     * For two times drawn uniformly within a frame of length @p frame_length,
     * the difference @f$\Delta t@f$ has a triangular density on
     * @f$[-L, +L]@f$ proportional to @f$(L - |\Delta t|)@f$. The per-Δt
     * acceptance (normalised to 1 at @f$\Delta t = 0@f$) is therefore
     * @f$(L - |\Delta t|)/L@f$. Divide a measured Δt distribution by this to
     * undo the triangular pair-acceptance.
     *
     * Outside @f$|\Delta t| \ge L@f$ the acceptance is exactly 0 (no pair within
     * one frame can reach such a separation). Inside, the value is floored at
     * @p floor so a division stays finite near the edges.
     *
     * @param dt            Time difference.
     * @param frame_length  Frame length @f$L@f$ (> 0).
     * @param floor         Minimum returned acceptance inside the support
     *                      (default 0.01); ignored for the out-of-support 0.
     * @return Acceptance in @f$[0, 1]@f$: 0 for @f$|dt| \ge L@f$ or invalid
     *         @p frame_length, else @f$\max(\text{floor}, (L-|dt|)/L)@f$.
     */
    [[nodiscard]] inline double
    triangle_acceptance(double dt, double frame_length, double floor = 0.01)
    {
        if (!(frame_length > 0.0))
            return 0.0;
        const double abs_dt = std::fabs(dt);
        if (abs_dt >= frame_length)
            return 0.0;
        const double accept = (frame_length - abs_dt) / frame_length;
        return accept > floor ? accept : floor;
    }

    /**
     * @brief Result of a Poisson-rate estimate.
     */
    struct rate_result
    {
        double rate = 0.0; ///< Estimated rate @f$\hat\lambda@f$ (inverse Δt units).
        bool ok = false;   ///< False on empty input or non-positive mean interval.
    };

    /**
     * @brief Maximum-likelihood Poisson rate from consecutive-interval samples.
     *
     * The Δt between consecutive firings of a Poisson process of rate
     * @f$\lambda@f$ is exponentially distributed, @f$p(\Delta t)=\lambda
     * e^{-\lambda \Delta t}@f$, whose MLE is @f$\hat\lambda = 1/\overline{\Delta
     * t}@f$. This is the closed-form, ROOT-free counterpart to fitting `expo`
     * to a Δt histogram and reading off the decay slope.
     *
     * @param intervals Consecutive Δt samples (each ≥ 0; units arbitrary).
     * @return @c {rate, ok}: @c ok is false (and @c rate 0) if @p intervals is
     *         empty or its mean is not positive.
     *
     * @note To convert to Hz when intervals are in nanoseconds, multiply the
     *       returned rate by @f$10^9@f$.
     */
    [[nodiscard]] inline rate_result
    poisson_rate_mle(std::span<const double> intervals)
    {
        rate_result result;
        if (intervals.empty())
            return result;
        double sum = 0.0;
        for (double dt : intervals)
            sum += dt;
        const double mean = sum / static_cast<double>(intervals.size());
        if (!(mean > 0.0) || !std::isfinite(mean))
            return result;
        result.rate = 1.0 / mean;
        result.ok = true;
        return result;
    }

} // namespace mist::stats
