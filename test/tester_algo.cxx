// SPDX-License-Identifier: MIT
//
// tester_algo.cxx — exercises mist::algo::block_mean, block_rms, moving_mean.
//
// Verification strategy: hand-computed expected outputs against small
// fixed inputs. Edge cases (empty input, n == 0, n > size, drop_partial
// flag) are checked explicitly.

#include <mist/algo/binning.h>
#include <mist/algo/smoothing.h>
#include <mist/algo/util.h>
#include <mist/algo/edges.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

void check_vec_close(const std::vector<double> &got,
                     const std::vector<double> &want,
                     double tol,
                     const char *what)
{
    if (got.size() != want.size())
    {
        std::printf("  FAIL: %s — size %zu vs %zu\n",
                    what, got.size(), want.size());
        ++failures;
        return;
    }
    for (std::size_t i = 0; i < got.size(); ++i)
    {
        if (std::fabs(got[i] - want[i]) > tol)
        {
            std::printf("  FAIL: %s[%zu] — got %.6g, want %.6g\n",
                        what, i, got[i], want[i]);
            ++failures;
        }
    }
}

} // namespace

int main()
{
    // ------------------------------------------------------------------
    std::puts("[tester_algo] block_mean");

    // Aligned input: 10 points, n=2 -> 5 blocks of 2.
    {
        std::vector<double> in = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        const auto got = a::block_mean(in, 2);
        check_vec_close(got, {1.5, 3.5, 5.5, 7.5, 9.5}, 1e-12, "aligned n=2");
    }

    // Misaligned input + default drop_partial=false: final partial block kept.
    // 11 points, n=3 -> 3 full blocks (mean 2,5,8) + 1 partial block of 2
    // (mean 9.5).
    {
        std::vector<double> in = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        const auto got = a::block_mean(in, 3);
        check_vec_close(got, {2.0, 5.0, 8.0, 10.5}, 1e-12,
                        "misaligned n=3, drop_partial=false");
    }

    // drop_partial=true: matches AAU off-by-one behaviour.
    {
        std::vector<double> in = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        const auto got = a::block_mean(in, 3, /*drop_partial=*/true);
        check_vec_close(got, {2.0, 5.0, 8.0}, 1e-12,
                        "misaligned n=3, drop_partial=true");
    }

    // Iterator-pair form.
    {
        std::vector<double> in = {2, 4, 6, 8};
        const auto got = a::block_mean(in.begin(), in.end(), 2);
        check_vec_close(got, {3.0, 7.0}, 1e-12, "iterator-pair form");
    }

    // float instantiation (concept check at the type level).
    {
        std::vector<float> in = {1.f, 3.f, 5.f, 7.f};
        const auto got = a::block_mean(in, 2);
        check(got.size() == 2, "float instantiation size");
        check_close(static_cast<double>(got[0]), 2.0, 1e-6, "float [0]");
        check_close(static_cast<double>(got[1]), 6.0, 1e-6, "float [1]");
    }

    // Edge: empty input -> empty output.
    {
        std::vector<double> empty;
        check(a::block_mean(empty, 2).empty(), "empty input -> empty output");
    }

    // Edge: n == 0 -> empty output.
    {
        std::vector<double> in = {1, 2, 3};
        check(a::block_mean(in, 0).empty(), "n=0 -> empty output");
    }

    // Edge: n > size -> empty output.
    {
        std::vector<double> in = {1, 2};
        check(a::block_mean(in, 10).empty(), "n>size -> empty output");
    }

    // ------------------------------------------------------------------
    std::puts("[tester_algo] block_rms");

    // Aligned: 4 points {1,1,3,3}, n=2 -> two blocks. Each block has
    // mean 1 / 3, deviations ±0, ±0 within the block — but here each
    // block is {1,1} or {3,3}, so RMS within each block is 0.
    {
        std::vector<double> in = {1, 1, 3, 3};
        const auto got = a::block_rms(in, 2);
        check_vec_close(got, {0.0, 0.0}, 1e-12, "constant-within-block");
    }

    // Block {0, 2}: mean 1, deviations ±1, variance = 1, RMS = 1.
    // Block {3, 7}: mean 5, deviations ±2, variance = 4, RMS = 2.
    {
        std::vector<double> in = {0, 2, 3, 7};
        const auto got = a::block_rms(in, 2);
        check_vec_close(got, {1.0, 2.0}, 1e-12, "two-block variance");
    }

    // Misaligned + drop_partial=true: discard the final partial block.
    {
        std::vector<double> in = {0, 2, 3, 7, 99};
        const auto got = a::block_rms(in, 2, /*drop_partial=*/true);
        check_vec_close(got, {1.0, 2.0}, 1e-12, "drop_partial=true skips tail");
    }

    // Edge: empty / n==0 / n>size all empty.
    {
        std::vector<double> empty;
        check(a::block_rms(empty, 2).empty(), "empty -> empty");
        std::vector<double> in = {1, 2};
        check(a::block_rms(in, 0).empty(), "n=0 -> empty");
        check(a::block_rms(in, 10).empty(), "n>size -> empty");
    }

    // ------------------------------------------------------------------
    std::puts("[tester_algo] moving_mean");

    // 5 points, n=3 -> 3 output windows.
    // {1,2,3,4,5}: windows (1,2,3)=2, (2,3,4)=3, (3,4,5)=4.
    {
        std::vector<double> in = {1, 2, 3, 4, 5};
        const auto got = a::moving_mean(in, 3);
        check_vec_close(got, {2.0, 3.0, 4.0}, 1e-12, "n=3 over 5 points");
    }

    // n == size -> single output (the global mean).
    {
        std::vector<double> in = {1, 2, 3, 4};
        const auto got = a::moving_mean(in, 4);
        check(got.size() == 1, "n=size produces 1 output");
        check_close(got.front(), 2.5, 1e-12, "n=size value");
    }

    // n == 1 -> output equals input.
    {
        std::vector<double> in = {7, 3, 9, 1};
        const auto got = a::moving_mean(in, 1);
        check_vec_close(got, {7.0, 3.0, 9.0, 1.0}, 1e-12, "n=1 identity");
    }

    // Edge: empty / n==0 / n>size all empty.
    {
        std::vector<double> empty;
        check(a::moving_mean(empty, 2).empty(), "empty -> empty");
        std::vector<double> in = {1, 2};
        check(a::moving_mean(in, 0).empty(), "n=0 -> empty");
        check(a::moving_mean(in, 10).empty(), "n>size -> empty");
    }

    // ------------------------------------------------------------------
    std::puts("[tester_algo] sign");

    check(a::sign(5) == 1, "positive int -> +1");
    check(a::sign(-3) == -1, "negative int -> -1");
    check(a::sign(0) == 0, "zero int -> 0");
    check(a::sign(2.5) == 1, "positive double -> +1");
    check(a::sign(-0.001) == -1, "small negative double -> -1");
    check(a::sign(0.0) == 0, "zero double -> 0");
    check(a::sign(-0.0) == 0, "negative zero -> 0");
    check(a::sign<float>(1.0f) == 1, "positive float -> +1");
    static_assert(a::sign(42) == 1, "sign is constexpr");
    static_assert(a::sign(-7) == -1, "sign is constexpr (negative)");

    // ------------------------------------------------------------------
    std::puts("[tester_algo] log_binning");
    {
        const auto e = a::log_binning(3, 0.1, 100.0);
        check(e.size() == 4, "log_binning: n+1 edges");
        check_close(e[0], 0.1, 1e-12, "log_binning: low pinned");
        check_close(e[1], 1.0, 1e-9, "log_binning: decade 1");
        check_close(e[2], 10.0, 1e-9, "log_binning: decade 2");
        check_close(e[3], 100.0, 1e-12, "log_binning: high pinned");
        check_close(e[1] / e[0], e[2] / e[1], 1e-9, "log_binning: constant ratio");
    }
    check(a::log_binning(0, 0.1, 10.0).empty(), "log_binning: n=0 -> empty");
    check(a::log_binning(5, 0.0, 10.0).empty(), "log_binning: x_min=0 -> empty");
    check(a::log_binning(5, -1.0, 10.0).empty(), "log_binning: x_min<0 -> empty");
    check(a::log_binning(5, 10.0, 1.0).empty(), "log_binning: x_max<=x_min -> empty");

    // ------------------------------------------------------------------
    std::puts("[tester_algo] linspace");
    {
        const auto e = a::linspace(4, 0.0, 1.0);
        check(e.size() == 5, "linspace: n+1 edges");
        check_close(e[0], 0.0, 1e-15, "linspace: front pinned");
        check_close(e[4], 1.0, 1e-15, "linspace: back pinned");
        check_close(e[1], 0.25, 1e-12, "linspace: step 1");
        check_close(e[2], 0.50, 1e-12, "linspace: step 2");
        check_close(e[3], 0.75, 1e-12, "linspace: step 3");
        // constant difference between adjacent edges
        check_close(e[1] - e[0], e[2] - e[1], 1e-12, "linspace: uniform spacing");
    }
    check(a::linspace(0, 0.0, 1.0).empty(), "linspace: n=0 -> empty");
    check(a::linspace(5, 1.0, 0.0).empty(), "linspace: x_max<=x_min -> empty");

    // ------------------------------------------------------------------
    std::puts("[tester_algo] weighted_block_mean");
    {
        // Equal weights -> same as block_mean.
        std::vector<double> in = {1, 2, 3, 4};
        std::vector<double> w = {1, 1, 1, 1};
        const auto got = a::weighted_block_mean(in, w, 2);
        check_vec_close(got, {1.5, 3.5}, 1e-12, "equal weights == block_mean");
    }
    {
        // Block {0,4} with weights {1,3}: (0*1 + 4*3)/(1+3) = 3.0.
        // Block {0,4} with weights {3,1}: (0*3 + 4*1)/(3+1) = 1.0.
        std::vector<double> in = {0, 4, 0, 4};
        std::vector<double> w = {1, 3, 3, 1};
        const auto got = a::weighted_block_mean(in, w, 2);
        check_vec_close(got, {3.0, 1.0}, 1e-12, "non-uniform weights");
    }
    {
        // Zero-weight block -> emits 0, not NaN.
        std::vector<double> in = {5, 7};
        std::vector<double> w = {0, 0};
        const auto got = a::weighted_block_mean(in, w, 2);
        check(got.size() == 1, "zero-weight block size");
        check_close(got[0], 0.0, 1e-15, "zero-weight block -> 0");
    }
    {
        // drop_partial=true discards the trailing partial block.
        std::vector<double> in = {1, 2, 3};
        std::vector<double> w = {1, 1, 1};
        const auto got = a::weighted_block_mean(in, w, 2, true);
        check(got.size() == 1, "drop_partial discards tail");
        check_close(got[0], 1.5, 1e-12, "drop_partial value");
    }

    // ------------------------------------------------------------------
    std::puts("[tester_algo] ema");
    {
        // alpha=1 is a pass-through.
        std::vector<double> in = {3, 1, 4, 1, 5};
        const auto got = a::ema(in, 1.0);
        check_vec_close(got, {3, 1, 4, 1, 5}, 1e-12, "alpha=1 pass-through");
    }
    {
        // alpha=0.5, input all-1s -> output all-1s (stationary).
        std::vector<double> in(10, 1.0);
        const auto got = a::ema(in, 0.5);
        check(got.size() == 10, "ema output length");
        check_close(got.back(), 1.0, 1e-9, "stationary all-1 EMA");
    }
    {
        // Step input: [0,0,...,0,1,1,...,1] -> output converges toward 1.
        std::vector<double> in(20, 0.0);
        in[10] = 1.0;
        in[11] = 1.0;
        in[12] = 1.0;
        in[13] = 1.0;
        in[14] = 1.0;
        in[15] = 1.0;
        in[16] = 1.0;
        in[17] = 1.0;
        in[18] = 1.0;
        in[19] = 1.0;
        const auto got = a::ema(in, 0.5);
        // After the step the EMA must be > 0 and < 1.
        check(got[10] > 0.0 && got[10] < 1.0, "ema responds to step");
        // And it must be monotonically increasing in the second half.
        for (int i = 11; i < 20; ++i)
            check(got[static_cast<std::size_t>(i)] >= got[static_cast<std::size_t>(i - 1)],
                  "ema monotone after step");
    }
    check(a::ema(std::vector<double>{}, 0.5).empty(), "ema: empty -> empty");
    check(a::ema(std::vector<double>{1, 2}, 0.0).empty(), "ema: alpha=0 -> empty");
    check(a::ema(std::vector<double>{1, 2}, 1.1).empty(), "ema: alpha>1 -> empty");

    // ------------------------------------------------------------------
    std::puts("[tester_algo] gaussian_smooth");
    {
        // Constant input -> output equals input (kernel sums to 1 by design).
        std::vector<double> in(20, 3.0);
        const auto got = a::gaussian_smooth(in, 2.0);
        check(got.size() == 20, "gaussian_smooth output length");
        for (std::size_t i = 0; i < got.size(); ++i)
            check_close(got[i], 3.0, 1e-12, "constant input preserved");
    }
    {
        // Impulse at centre: output must be symmetric and peak at centre.
        std::vector<double> in(21, 0.0);
        in[10] = 1.0;
        const auto got = a::gaussian_smooth(in, 1.5);
        check(got.size() == 21, "impulse output length");
        // Peak at position 10.
        check(got[10] >= got[9] && got[10] >= got[11], "impulse peak at centre");
        // Symmetric around centre (indices equidistant from 10).
        check_close(got[8], got[12], 1e-12, "impulse symmetry");
        check_close(got[7], got[13], 1e-12, "impulse symmetry 2");
    }
    check(a::gaussian_smooth(std::vector<double>{}, 1.0).empty(), "gaussian_smooth: empty -> empty");
    check(a::gaussian_smooth(std::vector<double>{1.0}, 0.0).empty(), "gaussian_smooth: sigma=0 -> empty");

    // ------------------------------------------------------------------
    if (failures)
    {
        std::printf("[tester_algo] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_algo] OK");
    return EXIT_SUCCESS;
}
