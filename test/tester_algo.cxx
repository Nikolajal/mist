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

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

void check_close(double got, double want, double tol, const char* what) {
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL: %s — got %.6g, want %.6g (tol %.3g)\n",
                    what, got, want, tol);
        ++failures;
    }
}

void check_vec_close(const std::vector<double>& got,
                     const std::vector<double>& want,
                     double tol,
                     const char* what) {
    if (got.size() != want.size()) {
        std::printf("  FAIL: %s — size %zu vs %zu\n",
                    what, got.size(), want.size());
        ++failures;
        return;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (std::fabs(got[i] - want[i]) > tol) {
            std::printf("  FAIL: %s[%zu] — got %.6g, want %.6g\n",
                        what, i, got[i], want[i]);
            ++failures;
        }
    }
}

}  // namespace

int main() {
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
    if (failures) {
        std::printf("[tester_algo] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_algo] OK");
    return EXIT_SUCCESS;
}
