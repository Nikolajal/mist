// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file ring_finding/ring_model.h
 * @brief Parametric Cherenkov-ring signal density (ROOT-free).
 *
 * Complements @ref mist::ring_finding::circle_fit() — circle fits recover a
 * ring's centre and radius from points; this models the *expected density* of
 * a Cherenkov ring as a Gaussian-in-radius signal of a given photon yield on a
 * flat background, with an azimuthally-varying width built from optional
 * logistic features (used to model acceptance gaps, e.g. photodetector-unit
 * boundaries).
 *
 * ROOT-free and dependency-free: @c TMath::Pi() / @c TMath::Gaus are replaced
 * by @c std::numbers::pi and an inline normalised Gaussian. The histogram fit
 * that consumes this model (a @c TH2 chi-squared minimisation) stays in the
 * ROOT-coupled consumer; only the analytic model and contour generation are
 * upstreamed here.
 *
 * Upstreamed from the ePIC dRICH beam-test analysis (`utility/ring_model.h`),
 * where the density and contour math were tangled with the ROOT fitter.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace mist::ring_finding
{
    /**
     * @brief A logistic acceptance feature on the azimuthal-width profile.
     *
     * Models a localised dip/bump in the ring width centred at @c center,
     * spanning @c width in azimuth, with logistic edge sharpness @c slope and
     * peak contribution @c amplitude. Used to imprint acceptance-gap structure
     * (e.g. PDU boundaries) onto the otherwise constant ring width.
     */
    struct logistic_feature
    {
        double amplitude = 0.0; ///< Peak contribution to the width.
        double center = 0.0;    ///< Azimuthal centre of the feature [rad].
        double width = 0.0;     ///< Azimuthal extent of the feature [rad].
        double slope = 1.0;     ///< Logistic edge width (smaller ⇒ sharper).
    };

    /**
     * @brief Parameters of the ring density model.
     *
     * Mirrors the six-parameter beam-test model
     * @f${x_0, y_0, R_0, \sigma_R, N_\gamma, b}@f$.
     */
    struct ring_params
    {
        double x0 = 0.0;         ///< Ring centre x.
        double y0 = 0.0;         ///< Ring centre y.
        double radius = 0.0;     ///< Ring radius @f$R_0@f$.
        double sigma = 1.0;      ///< Baseline radial width @f$\sigma_R@f$.
        double photons = 1.0;    ///< Integrated photon yield @f$N_\gamma@f$.
        double background = 0.0; ///< Flat background level @f$b@f$.
    };

    /**
     * @brief Difference of two logistics — a smooth "window" in @p phi.
     *
     * @f[
     *   A\left(\frac{1}{1+e^{-(\phi-c_1)/s_1}}
     *        - \frac{1}{1+e^{-(\phi-c_2)/s_2}}\right)
     * @f]
     *
     * @param phi        Independent variable (e.g. azimuthal angle [rad]).
     * @param amplitude  Peak amplitude @f$A@f$.
     * @param center_1   Centre of the rising logistic @f$c_1@f$.
     * @param sigma_1    Width of the rising logistic @f$s_1@f$.
     * @param center_2   Centre of the falling logistic @f$c_2@f$.
     * @param sigma_2    Width of the falling logistic @f$s_2@f$.
     * @return Window value at @p phi.
     */
    [[nodiscard]] inline double
    logistic_window(double phi, double amplitude,
                    double center_1, double sigma_1,
                    double center_2, double sigma_2)
    {
        return amplitude *
               (1.0 / (1.0 + std::exp(-(phi - center_1) / sigma_1)) -
                1.0 / (1.0 + std::exp(-(phi - center_2) / sigma_2)));
    }

    /**
     * @brief Azimuthally-varying ring width @f$\sigma_R(\phi)@f$.
     *
     * A constant baseline plus one difference-of-logistic window per feature:
     * each @ref logistic_feature contributes a window centred at
     * @c center, with rising/falling logistics half a @c width apart and shared
     * edge sharpness @c slope.
     *
     * @param phi             Azimuthal angle [rad].
     * @param baseline_sigma  Constant width baseline.
     * @param features        Optional logistic acceptance features.
     * @return Effective ring width at @p phi.
     */
    [[nodiscard]] inline double
    ring_sigma(double phi, double baseline_sigma,
               const std::vector<logistic_feature> &features = {})
    {
        double result = baseline_sigma;
        for (const auto &f : features)
            result += logistic_window(phi, f.amplitude,
                                      f.center - 0.5 * f.width, f.slope,
                                      f.center + 0.5 * f.width, f.slope);
        return result;
    }

    /**
     * @brief Ring signal + flat background density in polar @f$(R, \phi)@f$.
     *
     * @f[
     *   \rho(R,\phi) = \frac{N_\gamma}{2\pi R_0}\,
     *                  \mathcal{N}\!\big(R;\,R_0,\,\sigma_R(\phi)\big) + b
     * @f]
     * with @f$\mathcal{N}@f$ a normalised Gaussian (unit integral over @f$R@f$).
     *
     * @param r         Radial coordinate.
     * @param phi       Azimuthal coordinate [rad].
     * @param p         Ring parameters.
     * @param features  Optional azimuthal-gap features.
     * @return Expected density at @f$(R, \phi)@f$.
     */
    [[nodiscard]] inline double
    ring_density_polar(double r, double phi, const ring_params &p,
                       const std::vector<logistic_feature> &features = {})
    {
        const double sigma = ring_sigma(phi, p.sigma, features);
        const double norm = 1.0 / (std::numbers::sqrt2 *
                                   std::sqrt(std::numbers::pi) * sigma);
        const double z = (r - p.radius) / sigma;
        const double gauss = norm * std::exp(-0.5 * z * z);
        const double signal = p.photons * (1.0 / (2.0 * std::numbers::pi * p.radius)) * gauss;
        return signal + p.background;
    }

    /**
     * @brief Ring density in Cartesian @f$(x, y)@f$.
     *
     * Converts @f$(x, y)@f$ to ring-centred polar coordinates and evaluates
     * @ref ring_density_polar.
     *
     * @param x         Cartesian x.
     * @param y         Cartesian y.
     * @param p         Ring parameters.
     * @param features  Optional azimuthal-gap features.
     * @return Expected density at @f$(x, y)@f$.
     */
    [[nodiscard]] inline double
    ring_density_xy(double x, double y, const ring_params &p,
                    const std::vector<logistic_feature> &features = {})
    {
        const double dx = x - p.x0;
        const double dy = y - p.y0;
        const double r = std::hypot(dx, dy);
        const double phi = std::atan2(dy, dx);
        return ring_density_polar(r, phi, p, features);
    }

    /**
     * @brief Cartesian contour points of the ring at a given σ level.
     *
     * Traces @f$R_0 + k\,\sigma_R(\phi)@f$ around @f$\phi \in [-\pi, \pi]@f$ at
     * @c sigma_level multiples @c k of the (azimuthally-varying) width. ROOT-free
     * replacement for the beam-test `plot_ring_integral`, which returned ROOT
     * @c TGraph polygons; here the caller decides how to draw the points.
     *
     * @param p            Ring parameters (centre, radius, baseline width).
     * @param sigma_level  Width multiplier @f$k@f$ (0 ⇒ the ring itself).
     * @param features     Optional azimuthal-gap features.
     * @param n_points     Number of sampled points (default 500); the returned
     *                     polyline has @c n_points + 1 entries (closed loop).
     * @return @c {x, y} contour points, empty if @p n_points is 0.
     */
    [[nodiscard]] inline std::vector<std::array<double, 2>>
    ring_contour(const ring_params &p, double sigma_level,
                 const std::vector<logistic_feature> &features = {},
                 std::size_t n_points = 500)
    {
        std::vector<std::array<double, 2>> points;
        if (n_points == 0)
            return points;
        points.reserve(n_points + 1);
        constexpr double pi = std::numbers::pi;
        for (std::size_t i = 0; i <= n_points; ++i)
        {
            const double phi = -pi + 2.0 * pi * (static_cast<double>(i) /
                                                 static_cast<double>(n_points));
            const double rr = p.radius +
                              sigma_level * ring_sigma(phi, p.sigma, features);
            points.push_back({p.x0 + rr * std::cos(phi),
                              p.y0 + rr * std::sin(phi)});
        }
        return points;
    }

} // namespace mist::ring_finding
