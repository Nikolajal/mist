// tester_intersect.cxx — line intersection + zero-crossing with errors.
//
// Confirms:
//   - intersect_lines recovers a known crossing (value + symmetry)
//   - parallel lines report ok == false
//   - error propagation matches a hand-computed case
//   - line_zero_crossing solves -q/m with propagated error
//   - horizontal line (m == 0) reports ok == false

#include "mist/algo/intersect.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace a = mist::algo;

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
    std::puts("[tester_intersect] intersect_lines");
    {
        // y = x  and  y = -x + 2  cross at (1, 1).
        const auto r = a::intersect_lines(1.0, 0.0, 0.0, 0.0,
                                          -1.0, 0.0, 2.0, 0.0);
        check(r.ok, "non-parallel -> ok");
        check_close(r.x, 1.0, 1e-12, "x = 1");
        check_close(r.y, 1.0, 1e-12, "y = 1");
        check_close(r.x_err, 0.0, 1e-12, "no input error -> x_err 0");
        check_close(r.y_err, 0.0, 1e-12, "no input error -> y_err 0");
    }
    {
        // Parallel lines (equal slope) -> not ok.
        const auto r = a::intersect_lines(2.0, 0.1, 1.0, 0.1,
                                          2.0, 0.1, 5.0, 0.1);
        check(!r.ok, "parallel -> not ok");
    }
    {
        // Error propagation, hand-checked.
        // Line 1: y = 0*x + q1, q1 = 0 ± 0.3   (horizontal at 0)
        // Line 2: y = 1*x + 0                  (y = x, exact)
        // Cross: m1-m2 = -1, x = (q2-q1)/d = (0-0)/(-1) = 0, y = 0.
        // dx/dq1 = -1/d = 1 -> x_err = |1| * 0.3 = 0.3
        const auto r = a::intersect_lines(0.0, 0.0, 0.0, 0.3,
                                          1.0, 0.0, 0.0, 0.0);
        check(r.ok, "ok");
        check_close(r.x, 0.0, 1e-12, "x = 0");
        check_close(r.x_err, 0.3, 1e-12, "x_err = 0.3 (hand-checked)");
    }
    {
        // Symmetry: swapping the two lines leaves the point + errors unchanged.
        const auto r1 = a::intersect_lines(0.5, 0.05, 1.0, 0.1,
                                           -0.5, 0.05, 3.0, 0.1);
        const auto r2 = a::intersect_lines(-0.5, 0.05, 3.0, 0.1,
                                           0.5, 0.05, 1.0, 0.1);
        check(r1.ok && r2.ok, "both ok");
        check_close(r1.x, r2.x, 1e-12, "x invariant under line swap");
        check_close(r1.y, r2.y, 1e-12, "y invariant under line swap");
        check_close(r1.x_err, r2.x_err, 1e-12, "x_err invariant under swap");
        check_close(r1.y_err, r2.y_err, 1e-12, "y_err invariant under swap");
        // y = 0.5x + 1 = -0.5x + 3 -> x = 2, y = 2.
        check_close(r1.x, 2.0, 1e-12, "x = 2");
        check_close(r1.y, 2.0, 1e-12, "y = 2");
    }

    std::puts("[tester_intersect] line_zero_crossing");
    {
        // 2x - 4 = 0 -> x = 2.
        const auto r = a::line_zero_crossing(2.0, 0.0, -4.0, 0.0);
        check(r.ok, "ok");
        check_close(r.value, 2.0, 1e-12, "x = 2");
        check_close(r.error, 0.0, 1e-12, "no input error -> 0");
    }
    {
        // x = -q/m = -(-4)/2 = 2; dx/dq = -1/m = -0.5 -> err = 0.5 * q_err.
        const auto r = a::line_zero_crossing(2.0, 0.0, -4.0, 0.2);
        check_close(r.error, 0.1, 1e-12, "q_err only: 0.5 * 0.2 = 0.1");
    }
    {
        // Horizontal line: no crossing.
        const auto r = a::line_zero_crossing(0.0, 0.1, 3.0, 0.1);
        check(!r.ok, "m == 0 -> not ok");
    }

    if (failures)
    {
        std::printf("[tester_intersect] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_intersect] OK");
    return EXIT_SUCCESS;
}
