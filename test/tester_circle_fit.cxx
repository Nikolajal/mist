// SPDX-License-Identifier: MIT
//
// tester_circle_fit.cxx — exercises mist::ring_finding::circle_fit.

#include <mist/ring_finding/circle_fit.h>
#include <mist/ring_finding/hough_transform.h> // for the Hit POD (Point2 interop)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
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
        std::printf("  FAIL: %s — got %.6g, want %.6g\n", what, got, want);
        ++failures;
    }
}

struct P
{
    double x, y;
};

// Sample n points on the circle (cx, cy, r), optionally over a partial arc.
std::vector<P> ring(double cx, double cy, double r, int n,
                    double a0 = 0.0, double a1 = 2.0 * std::numbers::pi)
{
    std::vector<P> pts;
    for (int i = 0; i < n; ++i)
    {
        const double a = a0 + (a1 - a0) * i / n;
        pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return pts;
}

} // namespace

int main()
{
    std::puts("[tester_circle_fit] exact circle");
    {
        const auto pts = ring(3.0, -2.0, 5.0, 64);
        const auto fit = rf::circle_fit(pts);
        check(fit.ok, "full ring: ok");
        check(fit.n_points == 64, "full ring: n_points");
        check_close(fit.x0, 3.0, 1e-9, "full ring: x0");
        check_close(fit.y0, -2.0, 1e-9, "full ring: y0");
        check_close(fit.radius, 5.0, 1e-9, "full ring: radius");
        check(fit.rms_residual < 1e-9, "full ring: ~zero residual");
    }

    std::puts("[tester_circle_fit] partial arc");
    {
        // A 90-degree arc still determines the circle exactly (noise-free).
        const auto pts = ring(0.0, 0.0, 10.0, 30, 0.0, std::numbers::pi / 2.0);
        const auto fit = rf::circle_fit(pts);
        check(fit.ok, "arc: ok");
        check_close(fit.x0, 0.0, 1e-6, "arc: x0");
        check_close(fit.y0, 0.0, 1e-6, "arc: y0");
        check_close(fit.radius, 10.0, 1e-6, "arc: radius");
    }

    std::puts("[tester_circle_fit] minimal + degenerate");
    {
        // Exactly three non-collinear points: unique circle.
        std::vector<P> tri = {{1, 0}, {0, 1}, {-1, 0}}; // unit circle, centre 0
        const auto fit = rf::circle_fit(tri);
        check(fit.ok, "3 points: ok");
        check_close(fit.radius, 1.0, 1e-9, "3 points: radius 1");
        check_close(fit.x0, 0.0, 1e-9, "3 points: x0 0");
    }
    {
        std::vector<P> two = {{0, 0}, {1, 1}};
        check(!rf::circle_fit(two).ok, "2 points -> not ok");
    }
    {
        std::vector<P> empty;
        check(!rf::circle_fit(empty).ok, "empty -> not ok");
    }
    {
        // Collinear points: singular normal system.
        std::vector<P> line = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
        check(!rf::circle_fit(line).ok, "collinear -> not ok");
    }

    std::puts("[tester_circle_fit] noisy fit is close");
    {
        // Perturb a ring slightly and check the recovered params are near.
        auto pts = ring(2.0, 5.0, 8.0, 200);
        // deterministic small wobble, no RNG dependency
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const double e = (static_cast<int>(i % 5) - 2) * 0.01; // in [-0.02, 0.02]
            pts[i].x += e;
            pts[i].y -= e;
        }
        const auto fit = rf::circle_fit(pts);
        check(fit.ok, "noisy: ok");
        check_close(fit.x0, 2.0, 0.05, "noisy: x0 near");
        check_close(fit.y0, 5.0, 0.05, "noisy: y0 near");
        check_close(fit.radius, 8.0, 0.05, "noisy: radius near");
        check(fit.rms_residual < 0.1, "noisy: small residual");
    }

    // Works with the ring_finding::Hit POD too (Point2 concept).
    std::puts("[tester_circle_fit] Hit interop");
    {
        std::vector<rf::Hit> hits;
        for (const auto &p : ring(1.0, 1.0, 4.0, 20))
        {
            rf::Hit h{};
            h.x = static_cast<float>(p.x);
            h.y = static_cast<float>(p.y);
            hits.push_back(h);
        }
        const auto fit = rf::circle_fit(hits);
        check(fit.ok, "Hit range: ok");
        check_close(fit.radius, 4.0, 1e-3, "Hit range: radius (float precision)");
    }

    // Taubin and Pratt must recover an exact circle just as Kåsa does — this
    // is a strong check on the characteristic-polynomial coefficients (a wrong
    // coefficient would not reproduce the exact centre/radius).
    std::puts("[tester_circle_fit] taubin / pratt exact recovery");
    for (auto method : {rf::circle_method::taubin, rf::circle_method::pratt})
    {
        const char *nm = (method == rf::circle_method::taubin) ? "taubin" : "pratt";
        const auto pts = ring(-4.0, 7.0, 12.0, 50);
        const auto fit = rf::circle_fit(pts, method);
        check(fit.ok, nm);
        check_close(fit.x0, -4.0, 1e-7, "exact x0");
        check_close(fit.y0, 7.0, 1e-7, "exact y0");
        check_close(fit.radius, 12.0, 1e-7, "exact radius");
        check(fit.rms_residual < 1e-7, "exact ~zero residual");
        // partial arc, exact
        const auto arc = ring(1.0, 1.0, 6.0, 24, 0.2, 1.4);
        const auto af = rf::circle_fit(arc, method);
        check(af.ok && std::fabs(af.radius - 6.0) < 1e-5, "exact arc radius");
        // degenerate
        std::vector<P> line = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
        check(!rf::circle_fit(line, method).ok, "collinear -> not ok");
    }

    // All three methods agree closely on a clean (near-exact) full ring.
    std::puts("[tester_circle_fit] methods agree on clean data");
    {
        const auto pts = ring(2.5, -1.5, 9.0, 120);
        const auto k = rf::circle_fit(pts, rf::circle_method::kasa);
        const auto t = rf::circle_fit(pts, rf::circle_method::taubin);
        const auto p = rf::circle_fit(pts, rf::circle_method::pratt);
        check(std::fabs(k.radius - t.radius) < 1e-6, "kasa ~ taubin radius");
        check(std::fabs(t.radius - p.radius) < 1e-6, "taubin ~ pratt radius");
    }

    if (failures)
    {
        std::printf("[tester_circle_fit] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_circle_fit] OK");
    return EXIT_SUCCESS;
}
