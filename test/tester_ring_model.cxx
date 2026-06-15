// tester_ring_model.cxx — exercises the ROOT-free Cherenkov ring density.
//
// Confirms:
//   - logistic_window symmetry / amplitude at a centred window
//   - ring_sigma reduces to the baseline with no features
//   - ring_density_polar peaks at R0 and integrates the signal to N_gamma
//   - ring_density_xy agrees with the polar form
//   - ring_contour returns a closed loop on the ring at sigma_level = 0

#include "mist/ring_finding/ring_model.h"

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
        std::printf("  FAIL: %s — got %.6g, want %.6g (tol %.3g)\n",
                    what, got, want, tol);
        ++failures;
    }
}

} // namespace

int main()
{
    std::puts("[tester_ring_model] ring_sigma / logistic_window");
    {
        // No features -> exactly the baseline at any phi.
        check_close(rf::ring_sigma(0.3, 2.0), 2.0, 1e-12, "baseline width, no features");
        check_close(rf::ring_sigma(-1.7, 2.0, {}), 2.0, 1e-12, "baseline width, empty features");

        // A logistic window adds width near its centre, ~nothing far away.
        std::vector<rf::logistic_feature> feats = {{1.0, 0.0, 1.0, 0.05}};
        const double at_center = rf::ring_sigma(0.0, 2.0, feats);
        const double far_away = rf::ring_sigma(3.0, 2.0, feats);
        check(at_center > 2.5, "feature widens at its centre");
        check_close(far_away, 2.0, 1e-3, "feature negligible far away");
    }

    std::puts("[tester_ring_model] ring_density_polar");
    {
        rf::ring_params p;
        p.x0 = 0.0;
        p.y0 = 0.0;
        p.radius = 10.0;
        p.sigma = 0.5;
        p.photons = 4.0;
        p.background = 0.3;

        // Peak in R sits at the ring radius (background symmetric about it).
        const double on_ring = rf::ring_density_polar(10.0, 0.0, p);
        const double off_in = rf::ring_density_polar(8.0, 0.0, p);
        const double off_out = rf::ring_density_polar(12.0, 0.0, p);
        check(on_ring > off_in && on_ring > off_out, "density peaks on the ring");

        // Far from the ring -> only the flat background remains.
        check_close(rf::ring_density_polar(100.0, 0.0, p), 0.3, 1e-9, "far field = background");

        // Radial integral of the (signal-only) Gaussian term is N_gamma/(2 pi R0):
        //   ∫ rho_signal dR = N_gamma/(2 pi R0). Numerically integrate.
        double integral = 0.0;
        const double dr = 1e-3;
        for (double r = 0.0; r < 20.0; r += dr)
            integral += (rf::ring_density_polar(r, 0.0, p) - p.background) * dr;
        const double expected = p.photons / (2.0 * std::numbers::pi * p.radius);
        check_close(integral, expected, 1e-3, "radial signal integral = N/(2 pi R0)");
    }

    std::puts("[tester_ring_model] ring_density_xy");
    {
        rf::ring_params p;
        p.x0 = 1.0;
        p.y0 = -2.0;
        p.radius = 5.0;
        p.sigma = 0.4;
        p.photons = 3.0;
        p.background = 0.1;

        // A point on the +x ray from the centre at distance R should match the
        // polar evaluation at (R, phi=0).
        const double xy = rf::ring_density_xy(p.x0 + 5.0, p.y0, p);
        const double polar = rf::ring_density_polar(5.0, 0.0, p);
        check_close(xy, polar, 1e-9, "xy agrees with polar on +x ray");
    }

    std::puts("[tester_ring_model] ring_contour");
    {
        rf::ring_params p;
        p.x0 = 2.0;
        p.y0 = 3.0;
        p.radius = 7.0;
        p.sigma = 1.0;

        const auto pts = rf::ring_contour(p, 0.0, {}, 200);
        check(pts.size() == 201, "n_points+1 contour points");
        // At sigma_level 0 every point is at radius R0 from the centre.
        bool all_on_ring = true;
        for (const auto &q : pts)
        {
            const double r = std::hypot(q[0] - p.x0, q[1] - p.y0);
            if (std::fabs(r - 7.0) > 1e-9)
            {
                all_on_ring = false;
                break;
            }
        }
        check(all_on_ring, "sigma_level 0 contour lies on R0");
        // Closed loop: first and last point coincide (phi -pi and +pi).
        check_close(pts.front()[0], pts.back()[0], 1e-9, "contour closes in x");
        check_close(pts.front()[1], pts.back()[1], 1e-9, "contour closes in y");

        check(rf::ring_contour(p, 1.0, {}, 0).empty(), "zero points -> empty");
    }

    if (failures)
    {
        std::printf("[tester_ring_model] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_ring_model] OK");
    return EXIT_SUCCESS;
}
