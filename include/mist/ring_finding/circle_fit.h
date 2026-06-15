// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file ring_finding/circle_fit.h
 * @brief Closed-form least-squares circle fits (Kåsa / Taubin / Pratt).
 *
 * Complements @ref mist::ring_finding::HoughTransform — the Hough transform
 * *finds* rings on a coarse accumulator grid; these *refine* a set of points
 * already assigned to a ring into a continuous centre and radius.
 *
 * ROOT-free and dependency-free. All three methods are algebraic (no iterative
 * geometric minimiser): they share one set of centroid-centred moments, and
 * differ only in a scalar "eta" found from a characteristic polynomial:
 *   - @c kasa   : eta = 0 (minimises algebraic distance directly; simplest,
 *                 but has a small-arc / large-noise bias).
 *   - @c taubin : eta = root of a cubic; the de-facto accurate algebraic fit,
 *                 much less biased on partial arcs. Recommended default for
 *                 refining Hough candidates.
 *   - @c pratt  : eta = root of a quartic; also bias-reduced, robust when the
 *                 points nearly form a line.
 *
 * The Newton solves for Taubin/Pratt use only scalar arithmetic — no linear
 * algebra library. (The reference Compass library covers ~22 algorithms via
 * Eigen; the geometric/iterative ones that genuinely need an eigensolver are
 * intentionally not pulled into the ROOT-free, dependency-free core.)
 *
 * See @ref mist::ring_finding::circle_fit().
 */

#include <cmath>
#include <concepts>
#include <cstddef>
#include <ranges>

namespace mist::ring_finding
{
/**
     * @brief A 2-D point usable by @ref mist::ring_finding::circle_fit() —
     *        anything exposing @c x and @c y members convertible to @c double
     *        (e.g. @ref mist::ring_finding::Hit).
     */
template <typename P>
concept Point2 = requires(const P &p) {
    { p.x } -> std::convertible_to<double>;
    { p.y } -> std::convertible_to<double>;
};

/**
     * @brief Algebraic circle-fit method. See the file overview for the
     *        accuracy/robustness trade-offs.
     */
enum class circle_method
{
    kasa,   ///< Algebraic least squares (eta = 0). Simplest, mild bias.
    taubin, ///< Taubin fit (cubic). Recommended; least small-arc bias.
    pratt   ///< Pratt fit (quartic). Bias-reduced, robust near-collinear.
};

/**
     * @brief Result of a circle fit.
     *
     * @c ok is false (and the geometry fields are left at 0) when the fit
     * cannot be formed: fewer than three points, or (near-)collinear points
     * that leave the system singular.
     */
struct CircleFitResult
{
    double x0 = 0.0;           ///< Fitted centre x.
    double y0 = 0.0;           ///< Fitted centre y.
    double radius = 0.0;       ///< Fitted radius.
    double rms_residual = 0.0; ///< RMS of geometric residuals sqrt<(d_i - R)^2>.
    std::size_t n_points = 0;  ///< Number of points used.
    bool ok = false;           ///< True if a fit was produced.
};

namespace detail
{
/// Centroid-centred normalised moments shared by every algebraic method.
struct circle_moments
{
    double Mxx, Myy, Mxy, Mxz, Myz, Mzz, Mz, cov_xy;
};

/// Newton root of the Taubin characteristic cubic (started at 0).
[[nodiscard]] inline double taubin_eta(const circle_moments &m)
{
    const double var_z = m.Mzz - m.Mz * m.Mz;
    const double A3 = 4.0 * m.Mz;
    const double A2 = -3.0 * m.Mz * m.Mz - m.Mzz;
    const double A1 = var_z * m.Mz + 4.0 * m.cov_xy * m.Mz - m.Mxz * m.Mxz - m.Myz * m.Myz;
    const double A0 = m.Mxz * (m.Mxz * m.Myy - m.Myz * m.Mxy) +
                      m.Myz * (m.Myz * m.Mxx - m.Mxz * m.Mxy) - var_z * m.cov_xy;
    const double A22 = A2 + A2;
    const double A33 = A3 + A3 + A3;

    double x = 0.0, y = 1e300;
    for (int i = 0; i < 50; ++i)
    {
        const double yold = y;
        y = A0 + x * (A1 + x * (A2 + x * A3));
        if (std::fabs(y) > std::fabs(yold))
            break;
        const double dy = A1 + x * (A22 + x * A33);
        if (dy == 0.0)
            break;
        const double xold = x;
        x = xold - y / dy;
        if (std::fabs((x - xold) / (x != 0.0 ? x : 1.0)) < 1e-12)
            break;
        if (x < 0.0)
            x = 0.0;
    }
    return x;
}

/// Newton root of the Pratt characteristic quartic (started at 0).
[[nodiscard]] inline double pratt_eta(const circle_moments &m)
{
    const double var_z = m.Mzz - m.Mz * m.Mz;
    const double A2 = 4.0 * m.cov_xy - 3.0 * m.Mz * m.Mz - m.Mzz;
    const double A1 = var_z * m.Mz + 4.0 * m.cov_xy * m.Mz - m.Mxz * m.Mxz - m.Myz * m.Myz;
    const double A0 = m.Mxz * (m.Mxz * m.Myy - m.Myz * m.Mxy) +
                      m.Myz * (m.Myz * m.Mxx - m.Mxz * m.Mxy) - var_z * m.cov_xy;
    const double A22 = A2 + A2;

    double x = 0.0, y = 1e300;
    for (int i = 0; i < 50; ++i)
    {
        const double yold = y;
        y = A0 + x * (A1 + x * (A2 + 4.0 * x * x));
        if (std::fabs(y) > std::fabs(yold))
            break;
        const double dy = A1 + x * (A22 + 16.0 * x * x);
        if (dy == 0.0)
            break;
        const double xold = x;
        x = xold - y / dy;
        if (std::fabs((x - xold) / (x != 0.0 ? x : 1.0)) < 1e-12)
            break;
        if (x < 0.0)
            x = 0.0;
    }
    return x;
}
} // namespace detail

/**
     * @brief Fit a circle to a range of 2-D points.
     *
     * @tparam Range  A forward range (iterated more than once) of @ref Point2.
     * @param  points Input points.
     * @param  method Algebraic method (default @ref circle_method::kasa).
     * @return Fit result; @c ok == false on degenerate input (see
     *         @ref CircleFitResult).
     *
     * @note Requires a forward range because the points are traversed three
     *       times (centroid, moments, residuals). Pass a @c std::vector,
     *       @c std::span, etc. — not a single-pass input range.
     */
template <std::ranges::forward_range Range>
    requires Point2<std::ranges::range_value_t<Range>>
[[nodiscard]] CircleFitResult circle_fit(const Range &points,
                                         circle_method method = circle_method::kasa)
{
    CircleFitResult result;

    std::size_t n = 0;
    double mean_x = 0.0, mean_y = 0.0;
    for (const auto &p : points)
    {
        mean_x += static_cast<double>(p.x);
        mean_y += static_cast<double>(p.y);
        ++n;
    }
    result.n_points = n;
    if (n < 3)
        return result;
    mean_x /= static_cast<double>(n);
    mean_y /= static_cast<double>(n);

    // Centroid-centred normalised moments (z = u^2 + v^2).
    detail::circle_moments m{};
    for (const auto &p : points)
    {
        const double u = static_cast<double>(p.x) - mean_x;
        const double v = static_cast<double>(p.y) - mean_y;
        const double z = u * u + v * v;
        m.Mxx += u * u;
        m.Myy += v * v;
        m.Mxy += u * v;
        m.Mxz += u * z;
        m.Myz += v * z;
        m.Mzz += z * z;
    }
    const double inv_n = 1.0 / static_cast<double>(n);
    m.Mxx *= inv_n;
    m.Myy *= inv_n;
    m.Mxy *= inv_n;
    m.Mxz *= inv_n;
    m.Myz *= inv_n;
    m.Mzz *= inv_n;
    m.Mz = m.Mxx + m.Myy;
    m.cov_xy = m.Mxx * m.Myy - m.Mxy * m.Mxy;

    // eta selects the method; the centre extraction below is shared.
    double eta = 0.0;
    switch (method)
    {
    case circle_method::taubin:
        eta = detail::taubin_eta(m);
        break;
    case circle_method::pratt:
        eta = detail::pratt_eta(m);
        break;
    case circle_method::kasa:
        eta = 0.0;
        break;
    }
    if (!std::isfinite(eta))
        return result;

    const double det = eta * eta - eta * m.Mz + m.cov_xy;
    const double scale = m.Mz; // ~ R^2; moments are O(R^2)
    if (!(std::fabs(det) > 1e-12 * scale * scale) || scale == 0.0)
        return result;

    const double uc = (m.Mxz * (m.Myy - eta) - m.Myz * m.Mxy) / (2.0 * det);
    const double vc = (m.Myz * (m.Mxx - eta) - m.Mxz * m.Mxy) / (2.0 * det);

    const double radius2 = uc * uc + vc * vc + m.Mz;
    if (!(radius2 > 0.0))
        return result;

    result.x0 = uc + mean_x;
    result.y0 = vc + mean_y;
    result.radius = std::sqrt(radius2);

    double sse = 0.0;
    for (const auto &p : points)
    {
        const double dx = static_cast<double>(p.x) - result.x0;
        const double dy = static_cast<double>(p.y) - result.y0;
        const double d = std::sqrt(dx * dx + dy * dy) - result.radius;
        sse += d * d;
    }
    result.rms_residual = std::sqrt(sse * inv_n);
    result.ok = true;
    return result;
}

} // namespace mist::ring_finding
