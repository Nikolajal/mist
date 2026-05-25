/**
 * @file test/tester_hough.cxx
 * @brief Unit tests for @ref mist::ring_finding::HoughTransform.
 *
 * Build with:
 *   cmake -B build -DMIST_BUILD_TESTS=ON && cmake --build build
 * Run with:
 *   ./build/bin/test_hough        (Unix)
 *   build/bin/<Config>/test_hough.exe   (Windows)
 *
 * The tests inject synthetic hits drawn from one or two circles into the
 * ring-finder and verify the recovered (cx, cy, R) lie within one cell of
 * the truth.  Acceptance tolerances are derived from the accumulator cell
 * size, never tuned empirically — they remain valid across platforms and
 * compiler versions.
 */

#include <mist/ring_finding/hough_transform.h>
#include <mist/logger/logger.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
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
// Geometry helpers
// ---------------------------------------------------------------------------

namespace
{
    constexpr float pi = 3.14159265358979323846f;

    /// Build a regular square grid geometry of @p side × @p side pixels with
    /// @p pitch mm spacing.  Used as the LUT key → (x,y) map.
    std::map<int, std::array<float, 2>>
    make_grid_geometry(int side, float pitch)
    {
        std::map<int, std::array<float, 2>> geom;
        for (int iy = 0; iy < side; ++iy)
            for (int ix = 0; ix < side; ++ix)
                geom[iy * side + ix] = {ix * pitch, iy * pitch};
        return geom;
    }

    /// Build @p n_hits hits uniformly distributed along the arc of a circle
    /// centred at (@p cx, @p cy) with radius @p R.  Each Hit is snapped to
    /// the nearest grid point so the lut_key is valid.
    std::vector<mist::ring_finding::Hit>
    make_ring_hits(float cx, float cy, float R, int n_hits,
                   int side, float pitch, float time = 0.f)
    {
        std::vector<mist::ring_finding::Hit> hits;
        hits.reserve(n_hits);
        for (int i = 0; i < n_hits; ++i)
        {
            const float angle = 2.f * pi * i / n_hits;
            const float x = cx + R * std::cos(angle);
            const float y = cy + R * std::sin(angle);

            const int ix = static_cast<int>(std::round(x / pitch));
            const int iy = static_cast<int>(std::round(y / pitch));
            if (ix < 0 || iy < 0 || ix >= side || iy >= side)
                continue;

            mist::ring_finding::Hit h;
            h.x = ix * pitch;
            h.y = iy * pitch;
            h.time = time;
            h.lut_key = iy * side + ix;
            hits.push_back(h);
        }
        return hits;
    }
} // anonymous

// ---------------------------------------------------------------------------
// LUT readiness — the early-return branch in find_rings
// ---------------------------------------------------------------------------

void test_find_rings_before_build_returns_empty()
{
    // Suppress the [ERROR] line that find_rings emits when the LUT is empty.
    const auto prev_level = mist::logger::get_min_level();
    mist::logger::set_min_level(mist::logger::LevelTag::Plain);

    mist::ring_finding::HoughTransform ht;
    CHECK(!ht.is_lut_ready());

    std::vector<mist::ring_finding::Hit> hits;
    const auto rings = ht.find_rings(hits, 0.3f, 5, 5);
    CHECK(rings.empty());

    mist::logger::set_min_level(prev_level);
}

void test_build_lut_ready_after_build()
{
    auto geom = make_grid_geometry(20, 3.f);
    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, 3.2f);
    CHECK(ht.is_lut_ready());
    CHECK(ht.get_nx() > 0);
    CHECK(ht.get_ny() > 0);
    CHECK(ht.get_r_bins().size() > 0);
    CHECK_NEAR(ht.get_cell_size(), 3.2, 1e-6);
}

// ---------------------------------------------------------------------------
// Convenience constructor — builds the LUT in one shot
// ---------------------------------------------------------------------------

void test_convenience_ctor_builds_lut()
{
    auto geom = make_grid_geometry(16, 3.f);
    mist::ring_finding::HoughTransform ht(geom, 10.f, 25.f, 1.f, 3.2f);
    CHECK(ht.is_lut_ready());
}

// ---------------------------------------------------------------------------
// Single ring recovery
// ---------------------------------------------------------------------------

void test_find_rings_recovers_single_ring()
{
    // 30 × 30 grid, 3 mm pitch → 87 × 87 mm active area.
    constexpr int side = 30;
    constexpr float pitch = 3.f;
    constexpr float cell_size = 3.2f;
    auto geom = make_grid_geometry(side, pitch);

    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, cell_size);

    // Truth: circle centred well inside the grid.
    const float cx_true = 45.f;
    const float cy_true = 45.f;
    const float R_true  = 18.f;

    auto hits = make_ring_hits(cx_true, cy_true, R_true, 36, side, pitch);
    CHECK(hits.size() >= 20); // grid quantisation may drop a few

    const auto rings = ht.find_rings(hits, 0.3f, 5, 5);
    CHECK(rings.size() == 1);
    if (rings.empty())
        return;

    // Recovered centre / radius should land within the accumulator cell size.
    CHECK_NEAR(rings[0].cx,     cx_true, cell_size);
    CHECK_NEAR(rings[0].cy,     cy_true, cell_size);
    // Radius is sampled in 1 mm bins, so the recovered R must be within ±1 mm.
    CHECK_NEAR(rings[0].radius, R_true,  1.0f);
    // Most of the injected hits should be associated.
    CHECK(static_cast<int>(rings[0].hit_indices.size()) >= 15);
}

// ---------------------------------------------------------------------------
// Mean-time computation
// ---------------------------------------------------------------------------

void test_mean_time_is_average_of_assigned_hits()
{
    constexpr int side = 30;
    constexpr float pitch = 3.f;
    auto geom = make_grid_geometry(side, pitch);

    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, 3.2f);

    auto hits = make_ring_hits(45.f, 45.f, 18.f, 36, side, pitch, /*time=*/7.5f);
    const auto rings = ht.find_rings(hits, 0.3f, 5, 5);
    CHECK(rings.size() == 1);
    if (!rings.empty())
        CHECK_NEAR(rings[0].mean_time, 7.5f, 1e-3);
}

// ---------------------------------------------------------------------------
// Dual ring extraction
// ---------------------------------------------------------------------------

void test_find_rings_recovers_two_disjoint_rings()
{
    constexpr int side = 40;
    constexpr float pitch = 3.f;
    constexpr float cell_size = 3.2f;
    auto geom = make_grid_geometry(side, pitch);

    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, cell_size);

    // Two well-separated circles.
    auto hits1 = make_ring_hits(30.f, 30.f, 15.f, 36, side, pitch);
    auto hits2 = make_ring_hits(85.f, 85.f, 20.f, 36, side, pitch);

    std::vector<mist::ring_finding::Hit> hits;
    hits.insert(hits.end(), hits1.begin(), hits1.end());
    hits.insert(hits.end(), hits2.begin(), hits2.end());

    const auto rings = ht.find_rings(hits, 0.2f, 5, 5, /*max_rings=*/2);
    CHECK(rings.size() == 2);
    if (rings.size() != 2)
        return;

    // rings[] is sorted by descending vote count.  Identify each by proximity.
    auto match_truth = [](float cx, float cy, float R,
                          const mist::ring_finding::RingResult &r)
    {
        return std::fabs(r.cx - cx) <= 4.f &&
               std::fabs(r.cy - cy) <= 4.f &&
               std::fabs(r.radius - R) <= 1.5f;
    };

    const bool matched_1 = match_truth(30.f, 30.f, 15.f, rings[0]) ||
                           match_truth(30.f, 30.f, 15.f, rings[1]);
    const bool matched_2 = match_truth(85.f, 85.f, 20.f, rings[0]) ||
                           match_truth(85.f, 85.f, 20.f, rings[1]);
    CHECK(matched_1);
    CHECK(matched_2);
}

// ---------------------------------------------------------------------------
// Returned rings are sorted by descending peak_votes (B12 regression)
// ---------------------------------------------------------------------------

void test_rings_sorted_by_descending_votes()
{
    constexpr int side = 40;
    constexpr float pitch = 3.f;
    auto geom = make_grid_geometry(side, pitch);

    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, 3.2f);

    // Two rings — one much denser than the other.
    auto hits_dense  = make_ring_hits(30.f, 30.f, 15.f, 72, side, pitch);
    auto hits_sparse = make_ring_hits(85.f, 85.f, 20.f, 24, side, pitch);

    std::vector<mist::ring_finding::Hit> hits;
    hits.insert(hits.end(), hits_dense.begin(),  hits_dense.end());
    hits.insert(hits.end(), hits_sparse.begin(), hits_sparse.end());

    const auto rings = ht.find_rings(hits, 0.1f, 5, 5, /*max_rings=*/2);
    CHECK(rings.size() == 2);
    if (rings.size() < 2)
        return;
    CHECK(rings[0].peak_votes >= rings[1].peak_votes);
}

// ---------------------------------------------------------------------------
// max_rings cap is honoured
// ---------------------------------------------------------------------------

void test_max_rings_cap()
{
    constexpr int side = 40;
    constexpr float pitch = 3.f;
    auto geom = make_grid_geometry(side, pitch);

    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, 3.2f);

    auto hits1 = make_ring_hits(30.f, 30.f, 15.f, 36, side, pitch);
    auto hits2 = make_ring_hits(85.f, 85.f, 20.f, 36, side, pitch);

    std::vector<mist::ring_finding::Hit> hits;
    hits.insert(hits.end(), hits1.begin(), hits1.end());
    hits.insert(hits.end(), hits2.begin(), hits2.end());

    // Even though two rings are present, ask for at most one.
    const auto rings = ht.find_rings(hits, 0.2f, 5, 5, /*max_rings=*/1);
    CHECK(rings.size() <= 1);
}

// ---------------------------------------------------------------------------
// Empty hits — should return cleanly, no rings
// ---------------------------------------------------------------------------

void test_empty_hits_returns_no_rings()
{
    auto geom = make_grid_geometry(20, 3.f);
    mist::ring_finding::HoughTransform ht(geom, 10.f, 25.f, 1.f, 3.2f);

    std::vector<mist::ring_finding::Hit> empty;
    const auto rings = ht.find_rings(empty, 0.3f, 5, 5);
    CHECK(rings.empty());
}

// ---------------------------------------------------------------------------
// Unknown lut_key — those hits are dropped from voting
// ---------------------------------------------------------------------------

void test_unknown_lut_keys_are_ignored()
{
    constexpr int side = 30;
    constexpr float pitch = 3.f;
    auto geom = make_grid_geometry(side, pitch);

    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, 3.2f);

    auto hits = make_ring_hits(45.f, 45.f, 18.f, 36, side, pitch);

    // Inject a few hits with bogus keys — they must be silently dropped.
    for (int i = 0; i < 10; ++i)
    {
        mist::ring_finding::Hit h;
        h.x = 0.f; h.y = 0.f; h.time = 0.f;
        h.lut_key = 99999; // not in geometry
        hits.push_back(h);
    }

    const auto rings = ht.find_rings(hits, 0.3f, 5, 5);
    CHECK(rings.size() == 1);
}

// ---------------------------------------------------------------------------
// Accumulator size matches geometry × r_bins
// ---------------------------------------------------------------------------

void test_accumulator_shape_is_consistent()
{
    auto geom = make_grid_geometry(20, 3.f);
    mist::ring_finding::HoughTransform ht;
    ht.build_lut(geom, 10.f, 25.f, 1.f, 3.2f);

    const auto &accum = ht.get_accumulator();
    const std::size_t expected = ht.get_r_bins().size() *
                                 static_cast<std::size_t>(ht.get_nx()) *
                                 static_cast<std::size_t>(ht.get_ny());
    CHECK(accum.size() == expected);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main()
{
    std::cout << "Running mist::ring_finding::HoughTransform tests...\n";

    test_find_rings_before_build_returns_empty();
    test_build_lut_ready_after_build();
    test_convenience_ctor_builds_lut();
    test_find_rings_recovers_single_ring();
    test_mean_time_is_average_of_assigned_hits();
    test_find_rings_recovers_two_disjoint_rings();
    test_rings_sorted_by_descending_votes();
    test_max_rings_cap();
    test_empty_hits_returns_no_rings();
    test_unknown_lut_keys_are_ignored();
    test_accumulator_shape_is_consistent();

    std::cout << s_tests_run << " tests run, "
              << s_tests_failed << " failed.\n";

    if (s_tests_failed == 0)
    {
        std::cout << "All HoughTransform tests passed.\n";
        return 0;
    }
    return 1;
}
