// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file algo/intersect.h
 * @brief Closed-form intersections of fitted lines, with error propagation.
 *
 * Generic, ROOT-free primitives for a recurring analysis need: given two
 * straight-line fits (slope + intercept, each with an uncertainty), where do
 * they cross, and where does a single line cross @f$y = 0@f$ — and with what
 * uncertainty? The canonical use is breakdown-voltage extraction (intersect a
 * sub-threshold baseline with an above-threshold ramp), but the maths is
 * detector-agnostic.
 *
 * Errors are propagated in first order assuming the four line parameters are
 * **independent** (no covariance term). This matches the common case where the
 * two lines come from *separate* fits; when both slopes/intercepts come from a
 * single fit their covariance is ignored, so treat the returned error as a
 * first-order estimate in that case.
 *
 * See @ref mist::algo::intersect_lines and @ref mist::algo::line_zero_crossing.
 */

#include <cmath>

namespace mist::algo
{
    /**
     * @brief Intersection of two lines with propagated uncertainties.
     */
    struct intersection
    {
        double x = 0.0;     ///< Intersection abscissa.
        double x_err = 0.0; ///< 1σ uncertainty on @c x.
        double y = 0.0;     ///< Intersection ordinate.
        double y_err = 0.0; ///< 1σ uncertainty on @c y.
        bool ok = false;    ///< False if the lines are (near-)parallel.
    };

    /**
     * @brief A scalar value with a 1σ uncertainty.
     */
    struct value_with_error
    {
        double value = 0.0; ///< Central value.
        double error = 0.0; ///< 1σ uncertainty.
        bool ok = false;    ///< False if the result is undefined (see notes).
    };

    /**
     * @brief Intersection of two lines @f$y = m_1 x + q_1@f$ and
     *        @f$y = m_2 x + q_2@f$.
     *
     * Solves @f$x^\* = (q_2 - q_1)/(m_1 - m_2)@f$ and
     * @f$y^\* = (m_1 q_2 - m_2 q_1)/(m_1 - m_2)@f$, propagating the four
     * parameter errors in first order (assumed independent).
     *
     * @param m1      Slope of line 1.
     * @param m1_err  1σ uncertainty on @p m1.
     * @param q1      Intercept of line 1.
     * @param q1_err  1σ uncertainty on @p q1.
     * @param m2      Slope of line 2.
     * @param m2_err  1σ uncertainty on @p m2.
     * @param q2      Intercept of line 2.
     * @param q2_err  1σ uncertainty on @p q2.
     * @return @ref intersection; @c ok is false (and the geometry left at 0) if
     *         the slopes are equal (parallel lines, @f$m_1 = m_2@f$).
     */
    [[nodiscard]] inline intersection
    intersect_lines(double m1, double m1_err, double q1, double q1_err,
                    double m2, double m2_err, double q2, double q2_err)
    {
        intersection result;
        const double d = m1 - m2;
        if (d == 0.0 || !std::isfinite(d))
            return result; // parallel / degenerate

        const double x = (q2 - q1) / d;
        const double y = (m1 * q2 - m2 * q1) / d;

        // x = (q2 - q1) / (m1 - m2)
        const double dx_dq1 = -1.0 / d;
        const double dx_dq2 = 1.0 / d;
        const double dx_dm1 = -x / d; // = -(q2-q1)/d^2
        const double dx_dm2 = x / d;  // =  (q2-q1)/d^2

        // y = (m1 q2 - m2 q1) / (m1 - m2)
        const double dy_dq1 = -m2 / d;
        const double dy_dq2 = m1 / d;
        const double dy_dm1 = m2 * (q1 - q2) / (d * d);
        const double dy_dm2 = m1 * (q2 - q1) / (d * d);

        const auto sq = [](double v) { return v * v; };
        const double vx = sq(dx_dq1 * q1_err) + sq(dx_dq2 * q2_err) +
                          sq(dx_dm1 * m1_err) + sq(dx_dm2 * m2_err);
        const double vy = sq(dy_dq1 * q1_err) + sq(dy_dq2 * q2_err) +
                          sq(dy_dm1 * m1_err) + sq(dy_dm2 * m2_err);

        result.x = x;
        result.y = y;
        result.x_err = std::sqrt(vx);
        result.y_err = std::sqrt(vy);
        result.ok = true;
        return result;
    }

    /**
     * @brief Abscissa where the line @f$y = m x + q@f$ crosses @f$y = 0@f$.
     *
     * Solves @f$x = -q/m@f$ with first-order error propagation from the
     * (independent) slope and intercept uncertainties.
     *
     * @param m      Slope.
     * @param m_err  1σ uncertainty on @p m.
     * @param q      Intercept.
     * @param q_err  1σ uncertainty on @p q.
     * @return @ref value_with_error holding the crossing abscissa; @c ok is
     *         false if @p m is zero (horizontal line, no crossing).
     */
    [[nodiscard]] inline value_with_error
    line_zero_crossing(double m, double m_err, double q, double q_err)
    {
        value_with_error result;
        if (m == 0.0 || !std::isfinite(m))
            return result; // horizontal line: no finite crossing

        const double x = -q / m;
        // dx/dq = -1/m ; dx/dm = q/m^2
        const double dx_dq = -1.0 / m;
        const double dx_dm = q / (m * m);
        result.value = x;
        result.error = std::sqrt((dx_dq * q_err) * (dx_dq * q_err) +
                                 (dx_dm * m_err) * (dx_dm * m_err));
        result.ok = true;
        return result;
    }

} // namespace mist::algo
