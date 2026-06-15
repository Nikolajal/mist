// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file ring_finding/ransac_ring_finder.h
 * @brief Grid-free RANSAC ring finder.
 *
 * A Hough accumulator votes every hit into a fixed (cx, cy, R) grid — which
 * forces a memory/range trade-off and a least-squares refit that a
 * uniform-noise majority can drag toward the hit centroid.  RANSAC instead
 * works directly on the points:
 *
 *   1. sample 3 hits → the unique circle through them (closed form),
 *   2. count *inliers* (hits within @c inlier_band of that circle),
 *   3. keep the circle with the most inliers,
 *   4. refine it with a Taubin @ref circle_fit on its inliers,
 *   5. remove those hits and repeat for the next ring.
 *
 * Consequences that matter for far-off-centre arcs in a flat noise floor:
 *   - **No accumulator** → the centre/radius range is unbounded for free; a
 *     circle through three arc points lands at the true centre however far
 *     outside the sensor it is.
 *   - **Completeness-corrected score (the crux)** → a candidate is ranked by its
 *     inlier excess *divided by the fraction of its circumference that lands on
 *     the sensor*, not by the raw seen count.  A far 36° arc shows only ~10 % of
 *     its ring, so its handful of hits would always lose to a fully-visible
 *     small ring on raw count — dividing by the visible fraction extrapolates
 *     each candidate to the full ring it implies, making the score agnostic to
 *     where the centre sits.  A bright on-sensor noise clump and a faint far arc
 *     are then judged on the same physical quantity (implied photon yield).
 *   - **Robust to a noise majority** → a significance gate (seen excess over the
 *     Poisson √background of the *visible* arc) rejects pure noise; the
 *     completeness correction only chooses among candidates that already clear
 *     it, so it can never promote an insignificant one.
 *   - **Sub-mm** centre/radius from the Taubin refine — no cell quantisation.
 *
 * Optional per-hit @c weights let callers down-weight high-occupancy channels
 * (e.g. by `1/m_c`) so a *faint* arc can still beat a bright background; with
 * no weights every hit counts 1 (the right choice for a bright arc).
 *
 * Deterministic *and portable*: the sampler is seeded (@ref RansacOptions::seed)
 * and draws indices with a Lemire multiply-shift over the engine's raw 32-bit
 * output rather than @c std::uniform_int_distribution (whose mapping is not
 * specified to match across standard libraries) — so the same hits give
 * identical rings on every call AND on every platform (libstdc++/libc++/MSVC).
 *
 * ROOT-free and dependency-free. Reuses @ref Hit / @ref RingResult from
 * @ref hough_transform.h and the Taubin fit from @ref circle_fit.h.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <unordered_set>
#include <vector>

#include <mist/ring_finding/circle_fit.h>
#include <mist/ring_finding/hough_transform.h> // Hit, RingResult (shared types)

namespace mist::ring_finding
{
/// @brief Tuning knobs for @ref find_rings_ransac.
struct RansacOptions
{
    int max_rings = 2;                  ///< Max rings to extract (two radiators).
    int iterations = 600;               ///< RANSAC samples per ring.
    double inlier_band = 6.0;           ///< |dist − R| < this ⇒ inlier [mm].
    int min_inliers = 8;                ///< Absolute floor on a ring's inlier count.
    double min_significance = 5.0;      ///< Accept only if the inlier EXCESS over the
                                        ///< expected uniform background exceeds this
                                        ///< many σ (Poisson). This is what lets a
                                        ///< concentrated arc beat a small circle that
                                        ///< merely grazes lots of uniform noise.
    double r_min = 10.0;                ///< Reject candidate circles below this R [mm].
    double r_max = 2000.0;              ///< …and above this R [mm].
    double min_visible_arc_frac = 0.10; ///< Floor on the visible arc length,
                                        ///< as a fraction of the sensor
                                        ///< diagonal, used in the
                                        ///< linear-density score — guards a
                                        ///< tiny on-sensor sliver from being
                                        ///< read as an arbitrarily dense ring.
    // Sensor fiducial window — the KNOWN measurement region, used as the
    // acceptance reference for the visible-arc / background terms.  This is
    // essential for sparse events: with only a handful of clustered hits the
    // hit bounding box is NOT the sensor, so the completeness correction has
    // no geometric reference and collapses to a raw count.  Pass the real
    // sensor extent here.  Left as max<=min ⇒ fall back to the hit bbox
    // (fine when hits already fill the sensor, e.g. a noise-dominated frame).
    double fiducial_xmin = 0.0;
    double fiducial_xmax = 0.0;
    double fiducial_ymin = 0.0;
    double fiducial_ymax = 0.0;
    unsigned seed = 0x9e3779b9u; ///< RNG seed (deterministic results).
};

namespace detail
{
/// Unique circle through three points; false if (near-)collinear.
[[nodiscard]] inline bool circle_through_3(const Hit &a, const Hit &b,
                                           const Hit &c, double &cx,
                                           double &cy, double &R)
{
    const double ax = a.x, ay = a.y, bx = b.x, by = b.y, dx = c.x, dy = c.y;
    const double d = 2.0 * (ax * (by - dy) + bx * (dy - ay) + dx * (ay - by));
    if (std::fabs(d) < 1e-9)
        return false;
    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = dx * dx + dy * dy;
    cx = (a2 * (by - dy) + b2 * (dy - ay) + c2 * (ay - by)) / d;
    cy = (a2 * (dx - bx) + b2 * (ax - dx) + c2 * (bx - ax)) / d;
    R = std::hypot(ax - cx, ay - cy);
    return std::isfinite(R);
}
} // namespace detail

/**
     * @brief Find up to @c opts.max_rings rings in @p hits via RANSAC + Taubin.
     *
     * @param hits    All candidate hits (signal + noise).
     * @param opts    Tuning knobs.
     * @param weights Optional per-hit weights, same length as @p hits; an
     *                inlier contributes its weight to the consensus score
     *                instead of 1 (down-weight high-occupancy channels to find
     *                a faint arc). Empty ⇒ uniform weight 1.
     * @return Rings sorted by descending inlier count (@ref RingResult::peak_votes
     *         carries the inlier count; @ref RingResult::hit_indices the inliers).
     */
[[nodiscard]] inline std::vector<RingResult>
find_rings_ransac(const std::vector<Hit> &hits, const RansacOptions &opts = {},
                  const std::vector<float> &weights = {})
{
    std::vector<RingResult> out;
    const bool use_w = weights.size() == hits.size();

    std::vector<int> active;
    active.reserve(hits.size());
    for (int i = 0; i < static_cast<int>(hits.size()); ++i)
        active.push_back(i);
    if (hits.empty())
        return out;

    std::mt19937 rng(opts.seed);
    const double band = opts.inlier_band;

    // Hit bounding box + uniform background density, for the
    // excess-over-background score.  A candidate circle's expected noise
    // inliers ≈ ρ · L · 2·band, where L is the candidate's arc-length that
    // falls inside the hit region — so a small full circle that sweeps lots
    // of uniform noise gets a large *expected* count and thus near-zero
    // EXCESS, while a concentrated real arc keeps its excess.
    double xmin, xmax, ymin, ymax;
    if (opts.fiducial_xmax > opts.fiducial_xmin &&
        opts.fiducial_ymax > opts.fiducial_ymin)
    {
        // Use the caller-supplied sensor fiducial — the correct acceptance
        // reference for sparse events (see RansacOptions::fiducial_*).
        xmin = opts.fiducial_xmin;
        xmax = opts.fiducial_xmax;
        ymin = opts.fiducial_ymin;
        ymax = opts.fiducial_ymax;
    }
    else
    {
        // Fall back to the hit bounding box.
        xmin = hits[0].x, xmax = hits[0].x, ymin = hits[0].y, ymax = hits[0].y;
        for (const auto &h : hits)
        {
            xmin = std::min<double>(xmin, h.x);
            xmax = std::max<double>(xmax, h.x);
            ymin = std::min<double>(ymin, h.y);
            ymax = std::max<double>(ymax, h.y);
        }
    }
    const double bbox_area = std::max(1.0, (xmax - xmin) * (ymax - ymin));
    // Weighted or unweighted total "mass" for the density estimate.
    double total_mass = 0.0;
    if (use_w)
        for (const float w : weights)
            total_mass += static_cast<double>(w);
    else
        total_mass = static_cast<double>(hits.size());
    const double rho_bg = total_mass / bbox_area; // background mass per mm²
    const double bbox_diag = std::hypot(xmax - xmin, ymax - ymin);
    // Visible arc shorter than this is treated as a degenerate sliver — a
    // local clump indistinguishable from a line — and cannot win on density.
    const double l_floor =
        std::max(2.0 * band, opts.min_visible_arc_frac * bbox_diag);

    // Geometric acceptance of a candidate circle: the fraction of its
    // circumference that lands inside the sensor (hit bbox), and the
    // resulting expected background mass in its ±band annulus.  This is the
    // crux of an *unbiased* score: a far-off-centre arc shows only a small
    // on-sensor slice, so both its expected background AND the signal it can
    // possibly collect scale with this fraction — we must divide the excess
    // by it (below) to compare a 10%-visible arc against a fully-visible
    // small ring on equal footing.
    struct VisStats
    {
        double e_bg;  ///< expected background mass in the ±band annulus
        double l_vis; ///< arc length of the circumference inside the sensor [mm]
    };
    const auto visible_stats = [&](double cx, double cy, double R) -> VisStats
    {
        const int n_s = 72;
        int inside = 0;
        for (int s = 0; s < n_s; ++s)
        {
            const double a = (2.0 * std::numbers::pi * s) / n_s;
            const double px = cx + R * std::cos(a);
            const double py = cy + R * std::sin(a);
            if (px >= xmin && px <= xmax && py >= ymin && py <= ymax)
                ++inside;
        }
        const double l_vis =
            (static_cast<double>(inside) / n_s) * 2.0 * std::numbers::pi * R;
        return {rho_bg * l_vis * (2.0 * band), l_vis};
    };

    for (int ring = 0; ring < opts.max_rings; ++ring)
    {
        if (static_cast<int>(active.size()) < opts.min_inliers || active.size() < 3)
            break;

        // Portable index draw: std::uniform_int_distribution is NOT specified
        // to map an engine's output identically across standard libraries, so
        // using it would make the sampled triplets — and thus the rings —
        // platform-dependent despite the fixed seed.  A Lemire multiply-shift
        // over the engine's 32-bit output reduces to [0, n) deterministically
        // and identically on libstdc++/libc++/MSVC (negligible, fixed bias).
        const std::uint32_t n_active = static_cast<std::uint32_t>(active.size());
        const auto pick = [&]() -> int
        { return static_cast<int>((static_cast<std::uint64_t>(rng()) * n_active) >> 32); };
        double best_score = -1e300, best_excess = 0.0, best_ebg = 0.0;
        int best_inliers = 0;
        double bcx = 0.0, bcy = 0.0, bR = 0.0;

        for (int it = 0; it < opts.iterations; ++it)
        {
            const int i = pick(), j = pick(), k = pick();
            if (i == j || j == k || i == k)
                continue;
            double cx, cy, R;
            if (!detail::circle_through_3(hits[active[i]], hits[active[j]],
                                          hits[active[k]], cx, cy, R))
                continue;
            if (R < opts.r_min || R > opts.r_max)
                continue;

            double mass = 0.0;
            int n_in = 0;
            for (const int idx : active)
                if (std::fabs(std::hypot(hits[idx].x - cx, hits[idx].y - cy) - R) < band)
                {
                    mass += use_w ? static_cast<double>(weights[idx]) : 1.0;
                    ++n_in;
                }
            if (n_in < opts.min_inliers)
                continue;

            const VisStats vs = visible_stats(cx, cy, R);
            const double excess = mass - vs.e_bg;

            //  LINEAR-DENSITY score — this is what makes the finder agnostic
            //  to where the centre sits.  `excess` is only the *seen* signal;
            //  dividing by the visible arc LENGTH gives the photon density
            //  per mm of on-sensor circumference, a quantity a real ring
            //  carries uniformly regardless of how much of it is visible.
            //  So a far-off-centre arc showing 10 % of its ring competes on
            //  the same footing as a fully-visible small ring — without this
            //  the small ring always wins simply because more of it is on
            //  the sensor.  (Dividing by the visible *fraction* instead would
            //  reintroduce a 2πR factor and bias the score toward ever-larger
            //  radii; density has no such runaway.)
            const double score = excess / std::max(vs.l_vis, l_floor);
            if (score > best_score)
            {
                best_score = score;
                best_excess = excess;
                best_ebg = vs.e_bg;
                best_inliers = n_in;
                bcx = cx;
                bcy = cy;
                bR = R;
            }
        }

        //  Significance gate: accept only if the *seen* excess is many σ
        //  above the background fluctuation (Poisson √expected) over the
        //  visible arc.  Rejects pure noise; the completeness correction
        //  above only decides *which* significant candidate wins, never
        //  promotes an insignificant one.
        if (best_inliers < opts.min_inliers)
            break;
        const double sigma = std::sqrt(std::max(1.0, best_ebg));
        if (best_excess < opts.min_significance * sigma)
            break;

        // Refine: Taubin fit on the best circle's inliers.
        std::vector<Hit> inlier_pts;
        for (const int idx : active)
            if (std::fabs(std::hypot(hits[idx].x - bcx, hits[idx].y - bcy) - bR) < band)
                inlier_pts.push_back(hits[idx]);

        RingResult r{};
        const auto fit = circle_fit(inlier_pts, circle_method::taubin);
        //  Accept the Taubin refinement only if it stays in the physical
        //  radius window.  A short / near-collinear arc can make the
        //  algebraic fit blow the radius far past r_max (the [r_min,r_max]
        //  gate above only constrained the 3-point CANDIDATE, not the
        //  refined circle) — fall back to the in-range RANSAC candidate
        //  rather than emit a runaway ring with R ~ thousands of mm.
        if (fit.ok && std::isfinite(fit.radius) && fit.radius >= opts.r_min &&
            fit.radius <= opts.r_max)
        {
            r.cx = static_cast<float>(fit.x0);
            r.cy = static_cast<float>(fit.y0);
            r.radius = static_cast<float>(fit.radius);
        }
        else
        {
            r.cx = static_cast<float>(bcx);
            r.cy = static_cast<float>(bcy);
            r.radius = static_cast<float>(bR);
        }

        // Re-collect inliers around the refined circle; gather time + votes.
        std::vector<int> final_in;
        double tsum = 0.0;
        for (const int idx : active)
            if (std::fabs(std::hypot(hits[idx].x - r.cx, hits[idx].y - r.cy) - r.radius) < band)
            {
                final_in.push_back(idx);
                tsum += hits[idx].time;
            }

        if (static_cast<int>(final_in.size()) < opts.min_inliers)
            break; // refined ring lost too many inliers — stop

        r.peak_votes = static_cast<int>(final_in.size());
        r.mean_time = final_in.empty()
                          ? 0.f
                          : static_cast<float>(tsum / static_cast<double>(final_in.size()));
        r.hit_indices = final_in;
        out.push_back(std::move(r));

        // Remove this ring's hits from the active pool.
        const std::unordered_set<int> claimed(final_in.begin(), final_in.end());
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [&claimed](int x)
                                    { return claimed.count(x) > 0; }),
                     active.end());
    }

    std::sort(out.begin(), out.end(),
              [](const RingResult &a, const RingResult &b)
              { return a.peak_votes > b.peak_votes; });
    return out;
}

} // namespace mist::ring_finding
