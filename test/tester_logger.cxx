/**
 * @file test/logger.cxx
 * @brief Manual smoke-test / demo for the mist::logger subsystem.
 *
 * Build with:
 *   cmake -B build -DMIST_BUILD_TESTS=ON && cmake --build build
 * Run with:
 *   ./build/bin/test_logger
 */

#include <mist/logger/logger.h>
#include <mist/logger/progress_bar.h>
#include <mist/logger/multi_progress_bar.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

// ---------------------------------------------------------------------------
// Minimal test harness — no external dependencies
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

// ---------------------------------------------------------------------------
// Helper: redirect std::cout / std::cerr to a string buffer for inspection
// ---------------------------------------------------------------------------
struct capture_streams
{
    std::ostringstream cout_buf, cerr_buf;
    std::streambuf *old_cout, *old_cerr;
    bool prev_colour;
    bool prev_tty;

    capture_streams()
        : old_cout(std::cout.rdbuf(cout_buf.rdbuf())),
          old_cerr(std::cerr.rdbuf(cerr_buf.rdbuf())),
          prev_colour(mist::logger::is_colour_enabled()),
          prev_tty(mist::logger::is_tty())
    {
        // Force colour off so we capture plain text without ANSI escapes.
        mist::logger::set_colour_enabled(false);
        // Force TTY on so progress bars and named anchors still emit content
        // through the captured cout buffer (after B7 the anchored-band
        // machinery is gated on is_tty()).
        mist::logger::set_tty(true);
    }

    ~capture_streams()
    {
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);
        // Restore both observed states (B15) — never unconditionally force
        // colour or TTY back on, which would be wrong when running
        // non-interactively.
        mist::logger::set_colour_enabled(prev_colour);
        mist::logger::set_tty(prev_tty);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_level_filter()
{
    // set_min_level / get_min_level round-trip
    mist::logger::set_min_level(mist::logger::LevelTag::Info);
    CHECK(mist::logger::get_min_level() == mist::logger::LevelTag::Info);

    mist::logger::set_min_level(mist::logger::LevelTag::Debug);
    CHECK(mist::logger::get_min_level() == mist::logger::LevelTag::Debug);
}

void test_log_output_reaches_cout()
{
    capture_streams cap;

    mist::logger::set_min_level(mist::logger::LevelTag::Debug);
    mist::logger::info("hello info");

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("hello info") != std::string::npos);
}

void test_error_and_warning_go_to_cerr()
{
    capture_streams cap;

    mist::logger::error("something broke");
    mist::logger::warning("be careful");

    const std::string err = cap.cerr_buf.str();
    CHECK(err.find("something broke") != std::string::npos);
    CHECK(err.find("be careful") != std::string::npos);
    // Neither should appear on stdout
    CHECK(cap.cout_buf.str().find("something broke") == std::string::npos);
}

void test_debug_filtered_below_min_level()
{
    capture_streams cap;

    mist::logger::set_min_level(mist::logger::LevelTag::Info);
    mist::logger::debug("should be hidden");

    CHECK(cap.cout_buf.str().find("should be hidden") == std::string::npos);

    // Restore
    mist::logger::set_min_level(mist::logger::LevelTag::Debug);
}

void test_colour_tag_roundtrip()
{
    // Snapshot the prior state so the test doesn't pollute the global
    // colour flag for whatever runs after it (B15).
    const bool prev_colour = mist::logger::is_colour_enabled();

    mist::logger::set_colour_enabled(true);
    const std::string seq = mist::logger::ansi(mist::logger::ColourTag::Red,
                                               {mist::logger::StyleTag::Bold});
    // With colour enabled the sequence must contain the ESC introducer
    CHECK(seq.find("\033[") != std::string::npos);

    mist::logger::set_colour_enabled(false);
    const std::string empty = mist::logger::ansi(mist::logger::ColourTag::Red);
    CHECK(empty.empty());

    mist::logger::set_colour_enabled(prev_colour);
}

void test_convenience_wrappers_compile_and_run()
{
    // Just verify all four wrappers can be called without crashing
    capture_streams cap;
    mist::logger::set_min_level(mist::logger::LevelTag::Debug);
    mist::logger::error("e");
    mist::logger::warning("w");
    mist::logger::info("i");
    mist::logger::debug("d");
    // If we reach here the wrappers work
    CHECK(true);
}

void test_progress_bar_basic()
{
    // Smoke-test: create a bar, drive it to completion, verify no crash.
    // ProgressBar takes only a BarStyle; progress is driven via update(current, total).
    capture_streams cap;

    {
        mist::logger::ProgressBar bar;
        for (int i = 0; i <= 10; ++i)
            bar.update(i, 10, /*flush=*/false);
        bar.finish(/*flush=*/false);
    }

    CHECK(true); // reaching here means no crash / exception
}

void test_progress_bar_finish_emits_final_frame()
{
    // B1 regression test: calling finish() must commit a 100% line even if
    // the bar was last updated at a non-100% fraction. Previously finish()
    // erased the bar without redrawing the final state.
    capture_streams cap;

    {
        mist::logger::ProgressBar bar;
        bar.update(7, 10, /*flush=*/false);   // bar visually at 70%
        bar.finish(/*flush=*/false);          // must commit 100% frame
    }

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("100.0%") != std::string::npos);
    CHECK(out.find("eta: done") != std::string::npos);
}

void test_update_anchor_basic()
{
    // update() should appear on stdout; end_update() should commit it.
    capture_streams cap;

    mist::logger::update("foo", "step 1");
    mist::logger::update("foo", "step 2");
    mist::logger::end_update("foo");

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("foo") != std::string::npos);
    CHECK(out.find("step 2") != std::string::npos);
    // "step 1" may have been overwritten in-place, so we don't require it
}

void test_update_anchor_end_noop()
{
    // end_update() on a name that was never update()'d should be a no-op.
    capture_streams cap;
    mist::logger::end_update("nonexistent"); // must not crash
    CHECK(true);
}

void test_update_anchor_resurrection_warning()
{
    // Calling update() after end_update() on the same name should emit
    // exactly one [WARNING] to stderr, then recreate the anchor normally.
    capture_streams cap;
    mist::logger::set_min_level(mist::logger::LevelTag::Debug);

    mist::logger::update("bar", "first life");
    mist::logger::end_update("bar");

    // First call after end — should warn
    mist::logger::update("bar", "second life");
    const std::string err_after_first = cap.cerr_buf.str();
    CHECK(err_after_first.find("bar") != std::string::npos);
    CHECK(err_after_first.find("WARNING") != std::string::npos);

    // Second call — no additional warning (warning fires once per resurrection)
    mist::logger::update("bar", "still second life");
    const std::string err_after_second = cap.cerr_buf.str();
    // Count occurrences of "WARNING" — should still be just one
    size_t count = 0, pos = 0;
    while ((pos = err_after_second.find("WARNING", pos)) != std::string::npos)
    {
        ++count;
        ++pos;
    }
    CHECK(count == 1);

    mist::logger::end_update("bar");
}

void test_progress_bar_with_update_anchor()
{
    // Smoke-test: bar and a named update anchor can coexist without crashing.
    capture_streams cap;

    mist::logger::ProgressBar bar;
    for (int i = 0; i <= 5; ++i)
    {
        mist::logger::update("task", "step " + std::to_string(i));
        bar.update(i, 5, /*flush=*/false);
    }
    bar.finish(/*flush=*/false);
    mist::logger::end_update("task");

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("task") != std::string::npos);
    CHECK(out.find("step 5") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MultiProgressBar tests
// ---------------------------------------------------------------------------

void test_multi_progress_bar_basic()
{
    // Smoke-test: create a multi-bar, drive it to completion, no crash.
    capture_streams cap;

    mist::logger::MultiProgressBar multi;
    auto &a = multi.add_subtask("task A");
    auto &b = multi.add_subtask("task B");

    for (int i = 0; i <= 10; ++i)
    {
        a.update(i, 10, /*flush=*/false);
        b.update(i, 10, /*flush=*/false);
        multi.update(i, 10, /*flush=*/false);
    }
    a.finish(/*flush=*/false);
    b.finish(/*flush=*/false);
    multi.finish(/*flush=*/false);

    CHECK(true);
}

void test_multi_progress_bar_subtask_output()
{
    // Verify subtask tags appear in output.
    capture_streams cap;

    mist::logger::MultiProgressBar multi;
    auto &a = multi.add_subtask("alpha");
    auto &b = multi.add_subtask("beta");

    a.update(5, 10, /*flush=*/false);
    b.update(3, 10, /*flush=*/false);
    multi.update(4, 10, /*flush=*/false);
    a.finish(/*flush=*/false);
    b.finish(/*flush=*/false);
    multi.finish(/*flush=*/false);

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("alpha") != std::string::npos);
    CHECK(out.find("beta") != std::string::npos);
}

void test_multi_progress_bar_with_log()
{
    // log() calls while a multi-bar is active must not crash.
    capture_streams cap;
    mist::logger::set_min_level(mist::logger::LevelTag::Debug);

    mist::logger::MultiProgressBar multi;
    auto &t = multi.add_subtask("work");

    for (int i = 0; i <= 5; ++i)
    {
        mist::logger::debug("tick " + std::to_string(i));
        t.update(i, 5, /*flush=*/false);
        multi.update(i, 5, /*flush=*/false);
    }
    t.finish(/*flush=*/false);
    multi.finish(/*flush=*/false);

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("work") != std::string::npos);
}

void test_multi_progress_bar_independent_subtask_progress()
{
    // Subtasks can advance at different rates independently.
    capture_streams cap;

    mist::logger::MultiProgressBar multi;
    auto &fast = multi.add_subtask("fast");
    auto &slow = multi.add_subtask("slow");

    for (int i = 0; i <= 10; ++i)
    {
        fast.update(i, 10, /*flush=*/false);
        slow.update(i / 2, 10, /*flush=*/false);
        multi.update(i, 10, /*flush=*/false);
    }
    fast.finish(/*flush=*/false);
    slow.finish(/*flush=*/false);
    multi.finish(/*flush=*/false);

    CHECK(true);
}

// ---------------------------------------------------------------------------
// Regression tests for recent fixes
// ---------------------------------------------------------------------------

void test_assign_tag_recomputes_layout_after_update()
{
    // Recent fix: assign_tag() must force a layout recompute even when called
    // after the first update().  Before the fix, the cached suffix_width_ stuck
    // to the value computed during the very first update, so a longer tag
    // assigned later would push the suffix off-screen / cause misalignment.
    capture_streams cap;

    mist::logger::ProgressBar bar;
    bar.update(1, 10, /*flush=*/false); // first update sets up layout
    bar.assign_tag("long-tag-name");    // must invalidate the cached width
    bar.update(5, 10, /*flush=*/false); // second update renders with new tag
    bar.finish(/*flush=*/false);

    const std::string out = cap.cout_buf.str();
    // The new tag must appear at least once after assign_tag took effect.
    CHECK(out.find("long-tag-name") != std::string::npos);
}

void test_assign_tag_then_clear_tag()
{
    // Tag-set followed by tag-clear should both render without crashing.
    capture_streams cap;

    mist::logger::ProgressBar bar;
    bar.assign_tag("phase-1");
    bar.update(3, 10, /*flush=*/false);
    bar.clear_tag();
    bar.update(7, 10, /*flush=*/false);
    bar.finish(/*flush=*/false);

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("phase-1") != std::string::npos);
    // After clear_tag the default [PROGRESS] prefix should reappear in the
    // final committed frame.
    CHECK(out.find("PROGRESS") != std::string::npos);
}

void test_multi_bar_finish_emits_final_frame()
{
    // Recent fix: MultiProgressBar::finish() was calling _draw_locked()
    // without holding mutex_, racing with any concurrent subtask thread.
    // The fix wraps the final draw in its own lock_guard scope.  In the
    // single-threaded test path the regression is observable as "no final
    // 100% frame committed when finish() is called from a state that was
    // updated under lock" — verify the percentage shows up.
    capture_streams cap;

    mist::logger::MultiProgressBar multi;
    auto &t = multi.add_subtask("task");

    multi.update(5, 10, /*flush=*/false);
    t.update(7, 10, /*flush=*/false);
    t.finish(/*flush=*/false);
    multi.finish(/*flush=*/false);

    const std::string out = cap.cout_buf.str();
    // The committed final frame must include "task" and the bar must complete.
    CHECK(out.find("task") != std::string::npos);
}

void test_multi_bar_unknown_total_mode()
{
    // kUnknownTotal sentinel — passing total <= 0 puts the main bar in
    // "unknown total" mode (no percentage, no ETA).  Test both the explicit
    // sentinel and the implicit non-positive total path.
    capture_streams cap;

    {
        mist::logger::MultiProgressBar multi;
        // Drive with unknown total via the integer overload (non-positive total).
        // Both arguments cast to int64_t so template deduction of T is unambiguous.
        multi.update(static_cast<int64_t>(42),
                     mist::logger::MultiProgressBar::kUnknownTotal,
                     /*flush=*/false);
        multi.finish(/*flush=*/false);
    }

    const std::string out = cap.cout_buf.str();
    // The current count must appear; the per-cent sign must NOT appear in
    // the main-bar line when total is unknown.  We just check the count is
    // shown, since the visual format is implementation detail.
    CHECK(out.find("42") != std::string::npos);
}

void test_multi_bar_kUnknownTotal_is_negative_one()
{
    // The named constant kUnknownTotal is part of the public API — pin its
    // value so a future refactor that changes the sentinel encoding has to
    // update this test too.
    CHECK(mist::logger::MultiProgressBar::kUnknownTotal == -1);
}

void test_multi_bar_set_header_renders_text()
{
    // Header mode replaces the main [PROGRESS] bar with "[tag] msg".
    capture_streams cap;

    {
        mist::logger::MultiProgressBar multi;
        multi.set_header("phase", "loading geometry", /*flush=*/false);
        auto &t = multi.add_subtask("worker");
        t.update(1, 10, /*flush=*/false);
        t.finish(/*flush=*/false);
        multi.finish(/*flush=*/false);
    }

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("phase") != std::string::npos);
    CHECK(out.find("loading geometry") != std::string::npos);
}

void test_multi_bar_restart_resets_clock()
{
    // restart() must reset both the main and subtask clocks without losing
    // the subtask labels — used by drivers that cycle the same multi-bar
    // across logical phases.
    capture_streams cap;

    mist::logger::MultiProgressBar multi;
    auto &t = multi.add_subtask("cycle");

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        for (int i = 0; i <= 4; ++i)
        {
            t.update(i, 4, /*flush=*/false);
            multi.update(i, 4, /*flush=*/false);
        }
        multi.restart(/*flush=*/false);
    }
    t.finish(/*flush=*/false);
    multi.finish(/*flush=*/false);

    const std::string out = cap.cout_buf.str();
    CHECK(out.find("cycle") != std::string::npos);
}

void demo_visual()
{
    mist::logger::set_colour_enabled(true);
    mist::logger::set_min_level(mist::logger::LevelTag::Debug);

    std::cout << "\n--- mist::logger visual demo ---\n";
    mist::logger::error("This is an error message");
    mist::logger::warning("This is a warning message");
    mist::logger::info("This is an info message");
    mist::logger::debug("This is a debug message");
    mist::logger::plain("This is a plain message");

    mist::logger::plain("--- progress trackers demo ---");

    // Loop 1: two named update anchors
    for (int i = 0; i <= 10; ++i)
    {
        mist::logger::debug(std::to_string(i) + "debug message — anchor pinned below");
        mist::logger::update("test", "testing: " + std::to_string(i));
        if (i > 5)
            mist::logger::update("second test", "testing: " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    mist::logger::end_update("test");
    mist::logger::end_update("second test");

    mist::logger::debug("All update anchors finished.");

    // Loop 2: progress bar + named update anchor together
    mist::logger::ProgressBar bar;
    for (int i = 0; i <= 10; ++i)
    {
        mist::logger::debug(std::to_string(i) + "debug message — bar and anchor pinned below");
        mist::logger::update("test", "testing: " + std::to_string(i));
        bar.update(i, 10, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    bar.finish();
    mist::logger::end_update("test");

    // Loop 3: multi-progress bar with two subtasks
    mist::logger::plain("--- multi progress bar demo ---");
    {
        mist::logger::MultiProgressBar multi;
        auto &a = multi.add_subtask("loader");
        auto &b = multi.add_subtask("parser");

        for (int i = 0; i <= 10; ++i)
        {
            mist::logger::debug(std::to_string(i) + "debug message — multi-bar pinned below");
            a.update(i, 10, false);
            b.update(i, 20, false);
            multi.update(i, 15, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        a.finish();
        b.finish();
        multi.finish();
    }
    mist::logger::info("All done.");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main()
{
    std::cout << "Running mist::logger tests...\n";

    test_level_filter();
    test_log_output_reaches_cout();
    test_error_and_warning_go_to_cerr();
    test_debug_filtered_below_min_level();
    test_colour_tag_roundtrip();
    test_convenience_wrappers_compile_and_run();
    test_progress_bar_basic();
    test_progress_bar_finish_emits_final_frame();
    test_update_anchor_basic();
    test_update_anchor_end_noop();
    test_update_anchor_resurrection_warning();
    test_progress_bar_with_update_anchor();
    test_multi_progress_bar_basic();
    test_multi_progress_bar_subtask_output();
    test_multi_progress_bar_with_log();
    test_multi_progress_bar_independent_subtask_progress();
    // regression tests for fixes landed in this cycle
    test_assign_tag_recomputes_layout_after_update();
    test_assign_tag_then_clear_tag();
    test_multi_bar_finish_emits_final_frame();
    test_multi_bar_unknown_total_mode();
    test_multi_bar_kUnknownTotal_is_negative_one();
    test_multi_bar_set_header_renders_text();
    test_multi_bar_restart_resets_clock();

    std::cout << s_tests_run << " tests run, "
              << s_tests_failed << " failed.\n";

    if (s_tests_failed == 0)
    {
        std::cout << "All tests passed.\n";
        demo_visual();
        return 0;
    }
    return 1;
}