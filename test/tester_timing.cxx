// tester_timing.cxx — exercises the ROOT-free timing helpers.
//
// Confirms:
//   - triangle_acceptance: 1 at dt=0, linear falloff, floored, 0 outside ±L
//   - poisson_rate_mle: recovers 1/mean, handles empty / degenerate input

#include "mist/stats/timing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace s = mist::stats;

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
    std::puts("[tester_timing] triangle_acceptance");
    {
        const double L = 100.0;
        check_close(s::triangle_acceptance(0.0, L), 1.0, 1e-12, "dt=0 -> 1");
        check_close(s::triangle_acceptance(50.0, L), 0.5, 1e-12, "dt=L/2 -> 0.5");
        check_close(s::triangle_acceptance(-50.0, L), 0.5, 1e-12, "symmetric in |dt|");
        // Outside the support: exactly zero.
        check(s::triangle_acceptance(100.0, L) == 0.0, "dt=L -> 0");
        check(s::triangle_acceptance(150.0, L) == 0.0, "dt>L -> 0");
        // Near the edge the value is floored (default 0.01).
        check_close(s::triangle_acceptance(99.999, L), 0.01, 1e-9, "edge floored to 0.01");
        check_close(s::triangle_acceptance(99.999, L, 0.05), 0.05, 1e-9, "custom floor honoured");
        // Invalid frame length.
        check(s::triangle_acceptance(1.0, 0.0) == 0.0, "L<=0 -> 0");
    }

    std::puts("[tester_timing] poisson_rate_mle");
    {
        // Mean interval 4 -> rate 0.25.
        std::vector<double> dts = {2.0, 4.0, 6.0, 4.0};
        const auto r = s::poisson_rate_mle(dts);
        check(r.ok, "valid input -> ok");
        check_close(r.rate, 0.25, 1e-12, "rate = 1/mean = 0.25");

        // Empty -> not ok, zero rate.
        std::vector<double> empty;
        const auto re = s::poisson_rate_mle(empty);
        check(!re.ok && re.rate == 0.0, "empty -> not ok");

        // All-zero intervals -> mean 0 -> not ok (no division blow-up).
        std::vector<double> zeros = {0.0, 0.0, 0.0};
        const auto rz = s::poisson_rate_mle(zeros);
        check(!rz.ok && rz.rate == 0.0, "zero mean -> not ok");
    }

    if (failures)
    {
        std::printf("[tester_timing] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_timing] OK");
    return EXIT_SUCCESS;
}
