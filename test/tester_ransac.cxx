// SPDX-License-Identifier: MIT
//
// tester_ransac.cxx — exercises mist::ring_finding::find_rings_ransac.
//
// The headline test mimics the dRICH beam-test failure mode: a BRIGHT, far-off-
// centre Cherenkov arc (centre well outside the sensor) sitting on a uniform DCR
// noise floor that *outnumbers* the arc hits. A single least-squares circle
// collapses to the noise centroid (≈ origin); RANSAC must instead recover the
// true far centre, exactly as the eye does from the hitmap.

#include <mist/ring_finding/ransac_ring_finder.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <random>
#include <vector>

namespace rf = mist::ring_finding;

namespace
{
int failures = 0;

void check(bool cond, const char *what)
{
    if (!cond)
    {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

void check_close(double got, double want, double tol, const char *what)
{
    if (std::fabs(got - want) > tol)
    {
        std::printf("  FAIL: %s — got %.4g, want %.4g (tol %.3g)\n", what, got, want, tol);
        ++failures;
    }
}

rf::Hit hit(double x, double y) { return rf::Hit{static_cast<float>(x), static_cast<float>(y), 0.f, 0}; }

// std::uniform_real_distribution / std::normal_distribution are NOT portable:
// the same seeded engine yields different sequences on libstdc++, libc++ and
// MSVC, which would make this test's point cloud platform-dependent (and the
// RANSAC result with it).  Generate the cloud with explicit portable transforms
// so every platform exercises the identical hits — matching the finder's own
// cross-platform-deterministic sampler.
double uniform(std::mt19937 &rng, double lo, double hi)
{
    // rng() ∈ [0, 2^32) → [0, 1) → [lo, hi).
    const double u = static_cast<double>(rng()) * (1.0 / 4294967296.0);
    return lo + (hi - lo) * u;
}
double gaussian(std::mt19937 &rng, double mu, double sigma)
{
    // Box–Muller from two portable uniforms.
    double u1 = uniform(rng, 0.0, 1.0);
    const double u2 = uniform(rng, 0.0, 1.0);
    if (u1 < 1e-12)
        u1 = 1e-12; // guard log(0)
    return mu + sigma * std::sqrt(-2.0 * std::log(u1)) *
                    std::cos(2.0 * std::numbers::pi * u2);
}
} // namespace

int main()
{
    constexpr double pi = std::numbers::pi;
    std::mt19937 rng(20260614u);

    // 1) Far-off-centre BRIGHT arc + uniform-noise majority — the real case.
    std::puts("[tester_ransac] far arc recovered under noise majority");
    {
        const double cx = -250.0, cy = 0.0, R = 260.0;
        std::vector<rf::Hit> hits;

        // The arc: the part of the circle that crosses the ±85 mm sensor — i.e.
        // the apex region near x≈+10, bowing to x≈−4 at y=±85.  ~400 bright hits
        // with a few-mm radial smear (a real ring has finite width).
        for (int i = 0; i < 400; ++i)
        {
            const double a = uniform(rng, -0.35, 0.35); // ~±20° about apex
            const double rr = R + gaussian(rng, 0.0, 1.5);
            hits.push_back(hit(cx + rr * std::cos(a), cy + rr * std::sin(a)));
        }

        // Uniform DCR noise over the sensor — MORE hits than the arc (3000).
        for (int i = 0; i < 3000; ++i)
            hits.push_back(hit(uniform(rng, -85.0, 85.0), uniform(rng, -85.0, 85.0)));

        rf::RansacOptions opt;
        opt.max_rings = 1;
        opt.iterations = 1500;
        opt.inlier_band = 6.0;
        opt.min_inliers = 50;
        opt.r_min = 50.0;
        opt.r_max = 1000.0;

        const auto rings = rf::find_rings_ransac(hits, opt);
        check(!rings.empty(), "found a ring");
        if (!rings.empty())
        {
            // The whole point: a FAR off-centre ring on the correct side, where a
            // plain least-squares circle collapses to the noise centroid near the
            // origin.  A ~36° on-sensor arc has a long radial lever arm, so the
            // centre/radius are genuinely under-determined (verified: across RNG
            // realisations the recovered centre spans roughly ±150 mm about the
            // truth) — we therefore assert the robust, realisation-independent
            // claim (far, correct side, large), not an exact centre.  The
            // downstream Taubin fit refines the seed for physics use.
            check(rings[0].cx < -150.0, "centre far off-sensor, correct side (not the origin)");
            check_close(rings[0].cy, cy, 40.0, "centre y near 0");
            check(rings[0].radius > 180.0 && rings[0].radius < 400.0,
                  "radius large (far arc, not a noise clump)");
        }
    }

    // 1b) SPARSE far arc (per-event photon yield ~15) under a noise majority —
    //     the regime where a RAW inlier-excess score fails: the arc shows only
    //     ~10 % of its circumference, so its few hits would lose to any
    //     fully-visible small circle.  Only the completeness correction
    //     (excess / visible-fraction) recovers the far centre here.
    std::puts("[tester_ransac] SPARSE far arc recovered (completeness correction)");
    {
        const double cx = -250.0, cy = 0.0, R = 260.0;
        std::vector<rf::Hit> hits;
        for (int i = 0; i < 15; ++i) // ~15 photons — sparse, like a real event
        {
            const double a = uniform(rng, -0.35, 0.35), rr = R + gaussian(rng, 0.0, 1.5);
            hits.push_back(hit(cx + rr * std::cos(a), cy + rr * std::sin(a)));
        }
        for (int i = 0; i < 40; ++i) // noise outnumbers the visible arc slice
            hits.push_back(hit(uniform(rng, -85.0, 85.0), uniform(rng, -85.0, 85.0)));

        rf::RansacOptions opt;
        opt.max_rings = 1;
        opt.iterations = 4000;
        opt.inlier_band = 6.0;
        opt.min_inliers = 8;
        opt.r_min = 50.0;
        opt.r_max = 1000.0;

        const auto rings = rf::find_rings_ransac(hits, opt);
        check(!rings.empty(), "found a sparse far ring");
        if (!rings.empty())
        {
            // Sparse (~15-hit) far arc: even less constrained than the dense case,
            // so assert only the robust claim the completeness correction buys —
            // a FAR, LARGE ring on the correct side rather than the small
            // near-origin circle a raw inlier-count score would prefer.
            check(rings[0].cx < -150.0, "sparse centre far off-sensor, correct side");
            check_close(rings[0].cy, cy, 40.0, "sparse centre y near 0");
            check(rings[0].radius > 180.0 && rings[0].radius < 450.0,
                  "sparse radius large (arc, not clump)");
        }
    }

    // 1c) PRODUCTION regime: a sparse far arc with only a few scattered noise
    //     hits — the per-event reality where the hit bounding box is NOT the
    //     sensor.  The completeness correction therefore needs the sensor
    //     fiducial passed explicitly; without it the score has no geometric
    //     reference and collapses to a small near-cluster circle.
    std::puts("[tester_ransac] sparse far arc with explicit sensor fiducial");
    {
        const double cx = -250.0, cy = 0.0, R = 260.0;
        std::vector<rf::Hit> hits;
        for (int i = 0; i < 12; ++i) // ~12 photons
        {
            const double a = uniform(rng, -0.35, 0.35), rr = R + gaussian(rng, 0.0, 1.5);
            hits.push_back(hit(cx + rr * std::cos(a), cy + rr * std::sin(a)));
        }
        for (int i = 0; i < 6; ++i) // only a few DCR hits this event
            hits.push_back(hit(uniform(rng, -99.0, 99.0), uniform(rng, -99.0, 99.0)));

        rf::RansacOptions opt;
        opt.max_rings = 1;
        opt.iterations = 4000;
        opt.inlier_band = 6.0;
        opt.min_inliers = 6;
        opt.min_significance = 2.0; // sparse event, modest floor
        opt.r_min = 50.0;
        opt.r_max = 1000.0;
        opt.fiducial_xmin = -99.0; // the KNOWN sensor window
        opt.fiducial_xmax = 99.0;
        opt.fiducial_ymin = -99.0;
        opt.fiducial_ymax = 99.0;

        const auto rings = rf::find_rings_ransac(hits, opt);
        check(!rings.empty(), "found a sparse far ring (fiducial)");
        if (!rings.empty())
        {
            // A 36° arc with mm-scale smear cannot pin the centre tightly (the
            // radial lever arm is long), so the hard assertions are only that
            // it is a FAR, LARGE ring in the right direction — not the small
            // near-origin circle the raw-count score would have picked.  The
            // downstream Taubin fit refines from this seed.
            check(rings[0].cx < -150.0, "fiducial centre far off-sensor, correct side");
            check_close(rings[0].cy, cy, 80.0, "fiducial centre y near 0");
            check(rings[0].radius > 180.0, "fiducial radius large (arc, not clump)");
        }
    }

    // 2) Determinism: same seed ⇒ identical result.
    std::puts("[tester_ransac] deterministic");
    {
        std::vector<rf::Hit> hits;
        for (int i = 0; i < 120; ++i)
        {
            const double a = 2.0 * pi * i / 120.0;
            hits.push_back(hit(30.0 + 50.0 * std::cos(a), -20.0 + 50.0 * std::sin(a)));
        }
        const auto a = rf::find_rings_ransac(hits, {});
        const auto b = rf::find_rings_ransac(hits, {});
        check(a.size() == b.size() && !a.empty(), "same ring count");
        if (!a.empty() && a.size() == b.size())
            check(a[0].cx == b[0].cx && a[0].cy == b[0].cy && a[0].radius == b[0].radius,
                  "identical geometry across calls");
    }

    // 3) Two concentric-ish rings (two radiators) — remove-and-repeat.
    std::puts("[tester_ransac] two rings separated");
    {
        std::vector<rf::Hit> hits;
        for (int i = 0; i < 200; ++i)
        {
            const double a = 2.0 * pi * i / 200.0;
            hits.push_back(hit(40.0 * std::cos(a), 40.0 * std::sin(a))); // R=40
            hits.push_back(hit(90.0 * std::cos(a), 90.0 * std::sin(a))); // R=90
        }
        rf::RansacOptions opt;
        opt.max_rings = 2;
        opt.inlier_band = 4.0;
        opt.min_inliers = 30;
        const auto rings = rf::find_rings_ransac(hits, opt);
        check(rings.size() == 2, "found two rings");
        if (rings.size() == 2)
        {
            const double r0 = rings[0].radius, r1 = rings[1].radius;
            const double lo = std::min(r0, r1), hi = std::max(r0, r1);
            check_close(lo, 40.0, 3.0, "inner radius ≈ 40");
            check_close(hi, 90.0, 3.0, "outer radius ≈ 90");
        }
    }

    // 4) Pure noise ⇒ no spurious high-inlier ring.
    std::puts("[tester_ransac] pure noise yields no strong ring");
    {
        std::vector<rf::Hit> hits;
        for (int i = 0; i < 2000; ++i)
            hits.push_back(hit(uniform(rng, -85.0, 85.0), uniform(rng, -85.0, 85.0)));
        rf::RansacOptions opt;
        opt.max_rings = 1;
        opt.min_inliers = 200; // a real arc clears this; flat noise should not
        opt.inlier_band = 4.0;
        const auto rings = rf::find_rings_ransac(hits, opt);
        check(rings.empty(), "no ring with >=200 inliers in pure noise");
    }

    if (failures)
    {
        std::printf("[tester_ransac] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_ransac] OK");
    return EXIT_SUCCESS;
}
