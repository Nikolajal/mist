/**
 * @file test/tester_rnd.cxx
 * @brief Unit tests for the @ref mist::Rnd random-number-generator wrapper.
 *
 * Build with:
 *   cmake -B build -DMIST_BUILD_TESTS=ON && cmake --build build
 * Run with:
 *   ./build/bin/test_rnd       (Unix)
 *   build/bin/<Config>/test_rnd.exe   (Windows)
 *
 * The statistical tests use fixed seeds and large sample counts so the
 * tolerance windows are wide enough to never produce spurious failures on
 * any platform.  Mean / variance / proportion checks use 5-sigma envelopes
 * for the asymptotic distributions; the probability of a false positive
 * per check is < 6e-7.
 */

#include <mist/rnd.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Minimal test harness — kept symmetrical with tester_logger.cxx so the test
// binaries can be assembled into one CTest dashboard.
// ---------------------------------------------------------------------------

static int s_tests_run = 0;
static int s_tests_failed = 0;

#define CHECK(expr)                                                \
    do                                                             \
    {                                                              \
        ++s_tests_run;                                             \
        if (!(expr))                                               \
        {                                                          \
            ++s_tests_failed;                                      \
            std::cerr << "  FAIL  " << __FILE__ << ":" << __LINE__ \
                      << "  " << #expr << "\n";                    \
        }                                                          \
    } while (false)

#define CHECK_NEAR(value, target, tol)                                       \
    do                                                                       \
    {                                                                        \
        ++s_tests_run;                                                       \
        const double _v = static_cast<double>(value);                        \
        const double _t = static_cast<double>(target);                       \
        if (!(std::fabs(_v - _t) <= (tol)))                                  \
        {                                                                    \
            ++s_tests_failed;                                                \
            std::cerr << "  FAIL  " << __FILE__ << ":" << __LINE__           \
                      << "  |" << #value << " - " << _t << "| > " << (tol)   \
                      << "  (got " << _v << ")\n";                           \
        }                                                                    \
    } while (false)

// ---------------------------------------------------------------------------
// Deterministic seeding — same seed must yield the same sequence.
// ---------------------------------------------------------------------------

void test_deterministic_seed_reproducibility()
{
    mist::Rnd a(12345);
    mist::Rnd b(12345);
    for (int i = 0; i < 100; ++i)
    {
        const double xa = a.uniform(0.0, 1.0);
        const double xb = b.uniform(0.0, 1.0);
        CHECK(xa == xb);
    }
}

void test_reseed_resets_sequence()
{
    mist::Rnd r(42);
    const double first = r.uniform(0.0, 1.0);
    // burn a few samples
    for (int i = 0; i < 10; ++i)
        (void)r.uniform(0.0, 1.0);
    r.reseed(42);
    const double after_reseed = r.uniform(0.0, 1.0);
    CHECK(first == after_reseed);
}

void test_different_seeds_diverge()
{
    mist::Rnd a(1);
    mist::Rnd b(2);
    int same = 0;
    for (int i = 0; i < 100; ++i)
    {
        if (a.uniform(0.0, 1.0) == b.uniform(0.0, 1.0))
            ++same;
    }
    // Could theoretically collide once, but should not for 100 draws.
    CHECK(same == 0);
}

// ---------------------------------------------------------------------------
// uniform — bounds and statistical moments.
// ---------------------------------------------------------------------------

void test_uniform_bounds_respected()
{
    mist::Rnd r(7);
    for (int i = 0; i < 1000; ++i)
    {
        const double x = r.uniform(2.0, 5.0);
        CHECK(x >= 2.0);
        CHECK(x < 5.0); // half-open interval per std::uniform_real_distribution
    }
}

void test_uniform_default_is_zero_to_one()
{
    mist::Rnd r(11);
    for (int i = 0; i < 200; ++i)
    {
        const double x = r.uniform<double>();
        CHECK(x >= 0.0);
        CHECK(x < 1.0);
    }
}

void test_uniform_swaps_inverted_bounds()
{
    mist::Rnd r(3);
    // Calling with a > b should not throw; result must still be in [b, a).
    const double x = r.uniform(10.0, 0.0);
    CHECK(x >= 0.0);
    CHECK(x < 10.0);
}

void test_uniform_mean_close_to_half()
{
    mist::Rnd r(2024);
    constexpr int N = 200'000;
    double sum = 0.0;
    for (int i = 0; i < N; ++i)
        sum += r.uniform(0.0, 1.0);
    const double mean = sum / N;
    // sigma of sample mean of U[0,1] over N samples = 1/sqrt(12*N) ≈ 6.5e-4 for N=2e5.
    // 5-sigma window ≈ 3.3e-3.  Use 5e-3 for safety.
    CHECK_NEAR(mean, 0.5, 5e-3);
}

void test_uniform_float_does_not_throw()
{
    // The SFINAE alias must accept float, double, and long double.
    mist::Rnd r(13);
    const float  f = r.uniform(0.f, 1.f);
    const double d = r.uniform(0.0, 1.0);
    CHECK(f >= 0.f && f < 1.f);
    CHECK(d >= 0.0 && d < 1.0);
}

// ---------------------------------------------------------------------------
// normal — mean / variance moments.
// ---------------------------------------------------------------------------

void test_normal_moments()
{
    mist::Rnd r(4242);
    constexpr int N = 200'000;
    constexpr double mu = 3.0;
    constexpr double sig = 2.0;

    double sum = 0.0, sumsq = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double x = r.normal(mu, sig);
        sum += x;
        sumsq += x * x;
    }
    const double mean = sum / N;
    const double var  = sumsq / N - mean * mean;

    // sigma of sample mean = sig / sqrt(N) ≈ 4.5e-3.   5-sigma ≈ 2.3e-2.
    CHECK_NEAR(mean, mu, 3e-2);
    // sigma of sample variance ≈ sig^2 * sqrt(2/N).   5-sigma ≈ 6.3e-2.
    CHECK_NEAR(var, sig * sig, 1e-1);
}

// ---------------------------------------------------------------------------
// Poisson — mean ≈ lambda; reject lambda <= 0.
// ---------------------------------------------------------------------------

void test_poisson_mean()
{
    mist::Rnd r(99);
    constexpr int N = 100'000;
    constexpr double lambda = 3.7;

    double sum = 0.0;
    for (int i = 0; i < N; ++i)
        sum += r.poisson(lambda);
    const double mean = sum / N;
    // sigma of sample mean = sqrt(lambda/N) ≈ 6.1e-3 here.   5-sigma ≈ 3e-2.
    CHECK_NEAR(mean, lambda, 5e-2);
}

void test_poisson_rejects_invalid_lambda()
{
    mist::Rnd r(1);
    bool threw_zero = false, threw_neg = false;
    try { (void)r.poisson(0.0); } catch (const std::invalid_argument &) { threw_zero = true; }
    try { (void)r.poisson(-1.0); } catch (const std::invalid_argument &) { threw_neg = true; }
    CHECK(threw_zero);
    CHECK(threw_neg);
}

// ---------------------------------------------------------------------------
// generate_phi — sample lies in [-π, π).
// ---------------------------------------------------------------------------

void test_generate_phi_range()
{
    mist::Rnd r(77);
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < 1000; ++i)
    {
        const double phi = r.generate_phi();
        CHECK(phi >= -pi);
        CHECK(phi < pi);
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main()
{
    std::cout << "Running mist::Rnd tests...\n";

    test_deterministic_seed_reproducibility();
    test_reseed_resets_sequence();
    test_different_seeds_diverge();
    test_uniform_bounds_respected();
    test_uniform_default_is_zero_to_one();
    test_uniform_swaps_inverted_bounds();
    test_uniform_mean_close_to_half();
    test_uniform_float_does_not_throw();
    test_normal_moments();
    test_poisson_mean();
    test_poisson_rejects_invalid_lambda();
    test_generate_phi_range();

    std::cout << s_tests_run << " tests run, "
              << s_tests_failed << " failed.\n";

    if (s_tests_failed == 0)
    {
        std::cout << "All Rnd tests passed.\n";
        return 0;
    }
    return 1;
}
