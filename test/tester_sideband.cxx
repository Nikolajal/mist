// SPDX-License-Identifier: MIT
//
// tester_sideband.cxx — exercises mist::stats::sideband_subtract.

#include <mist/stats/sideband.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

namespace st = mist::stats;

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

} // namespace

int main()
{
    // 20 bins, x_min=0, bin_width=1. Peak window [9, 10.5] selects bins 9,10;
    // half = 0.75 -> outer [8.25, 11.25] selects bins 8..11; wings = bins 8,11.

    std::puts("[tester_sideband] pure signal, no background");
    {
        std::vector<double> c(20, 0.0);
        c[9] = 50.0;
        c[10] = 50.0;
        const auto r = st::sideband_subtract(c, 0.0, 1.0, 9.0, 10.5);
        check(r.ok, "ok");
        check_close(r.peak, 100.0, 1e-9, "peak = 100");
        check_close(r.background, 0.0, 1e-9, "background = 0");
        check_close(r.signal, 100.0, 1e-9, "signal = 100");
        check_close(r.error, 10.0, 1e-9, "error = sqrt(100) = 10");
    }

    std::puts("[tester_sideband] flat background, no signal");
    {
        std::vector<double> c(20, 4.0); // flat
        const auto r = st::sideband_subtract(c, 0.0, 1.0, 9.0, 10.5);
        // peak bins 9,10 = 8; outer bins 8..11 = 16; background = 8; signal = 0.
        check_close(r.peak, 8.0, 1e-9, "peak = 8");
        check_close(r.background, 8.0, 1e-9, "background = 8");
        check_close(r.signal, 0.0, 1e-9, "flat bg -> signal 0");
        check_close(r.error, 4.0, 1e-9, "error = sqrt(8+8) = 4");
    }

    std::puts("[tester_sideband] signal over flat background");
    {
        std::vector<double> c(20, 4.0);
        c[9] += 20.0;
        c[10] += 20.0; // inject 40 of signal
        const auto r = st::sideband_subtract(c, 0.0, 1.0, 9.0, 10.5);
        // peak = 24+24 = 48; outer = 48 + 4 + 4 = 56; background = 8; signal = 40.
        check_close(r.peak, 48.0, 1e-9, "peak = 48");
        check_close(r.background, 8.0, 1e-9, "background = 8 (wings)");
        check_close(r.signal, 40.0, 1e-9, "recovers injected signal 40");
        check_close(r.error, std::sqrt(56.0), 1e-9, "error = sqrt(peak+bg)");
    }

    std::puts("[tester_sideband] clamping + std::array span");
    {
        // Peak near the right edge; outer window clamps to the last bin.
        std::array<double, 5> c{1.0, 1.0, 1.0, 1.0, 9.0};
        const auto r = st::sideband_subtract(c, 0.0, 1.0, 4.0, 4.5);
        check(r.ok, "edge: ok");
        check(r.peak >= 9.0, "edge: peak includes last bin");
    }

    std::puts("[tester_sideband] invalid input");
    {
        std::vector<double> empty;
        check(!st::sideband_subtract(empty, 0.0, 1.0, 1.0, 2.0).ok, "empty -> not ok");
        std::vector<double> c(10, 1.0);
        check(!st::sideband_subtract(c, 0.0, 0.0, 1.0, 2.0).ok, "bin_width 0 -> not ok");
        check(!st::sideband_subtract(c, 0.0, 1.0, 5.0, 5.0).ok, "empty window -> not ok");
        check(!st::sideband_subtract(c, 0.0, 1.0, 6.0, 2.0).ok, "inverted window -> not ok");
    }

    if (failures)
    {
        std::printf("[tester_sideband] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_sideband] OK");
    return EXIT_SUCCESS;
}
