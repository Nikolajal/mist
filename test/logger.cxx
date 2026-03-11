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

    capture_streams()
        : old_cout(std::cout.rdbuf(cout_buf.rdbuf())),
          old_cerr(std::cerr.rdbuf(cerr_buf.rdbuf()))
    {
        // Force colour off so we capture plain text without ANSI escapes
        mist::logger::set_colour_enabled(false);
    }

    ~capture_streams()
    {
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);
        mist::logger::set_colour_enabled(true);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_level_filter()
{
    // set_min_level / get_min_level round-trip
    mist::logger::set_min_level(mist::logger::level_tag::INFO);
    CHECK(mist::logger::get_min_level() == mist::logger::level_tag::INFO);

    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);
    CHECK(mist::logger::get_min_level() == mist::logger::level_tag::DEBUG);
}

void test_log_output_reaches_cout()
{
    capture_streams cap;

    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);
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

    mist::logger::set_min_level(mist::logger::level_tag::INFO);
    mist::logger::debug("should be hidden");

    CHECK(cap.cout_buf.str().find("should be hidden") == std::string::npos);

    // Restore
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);
}

void test_colour_tag_roundtrip()
{
    mist::logger::set_colour_enabled(true);
    const std::string seq = mist::logger::ansi(mist::logger::colour_tag::RED,
                                               {mist::logger::style_tag::BOLD});
    // With colour enabled the sequence must contain the ESC introducer
    CHECK(seq.find("\033[") != std::string::npos);

    mist::logger::set_colour_enabled(false);
    const std::string empty = mist::logger::ansi(mist::logger::colour_tag::RED);
    CHECK(empty.empty());

    mist::logger::set_colour_enabled(true);
}

void test_convenience_wrappers_compile_and_run()
{
    // Just verify all four wrappers can be called without crashing
    capture_streams cap;
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);
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
    // progress_bar takes only a bar_style; progress is driven via update(current, total).
    capture_streams cap;

    {
        mist::logger::progress_bar bar;
        for (int i = 0; i <= 10; ++i)
            bar.update(i, 10, /*flush=*/false);
        bar.finish(/*flush=*/false);
    }

    CHECK(true); // reaching here means no crash / exception
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
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);

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

    mist::logger::progress_bar bar;
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
// multi_progress_bar tests
// ---------------------------------------------------------------------------

void test_multi_progress_bar_basic()
{
    // Smoke-test: create a multi-bar, drive it to completion, no crash.
    capture_streams cap;

    mist::logger::multi_progress_bar multi;
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

    mist::logger::multi_progress_bar multi;
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
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);

    mist::logger::multi_progress_bar multi;
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

    mist::logger::multi_progress_bar multi;
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

void demo_visual()
{
    mist::logger::set_colour_enabled(true);
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);

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
        mist::logger::debug("debug message — anchor pinned below");
        mist::logger::update("test", "testing: " + std::to_string(i));
        if (i > 5)
            mist::logger::update("second test", "testing: " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    mist::logger::end_update("test");
    mist::logger::end_update("second test");

    mist::logger::debug("All update anchors finished.");

    // Loop 2: progress bar + named update anchor together
    mist::logger::progress_bar bar;
    for (int i = 0; i <= 10; ++i)
    {
        mist::logger::debug("debug message — bar and anchor pinned below");
        mist::logger::update("test", "testing: " + std::to_string(i));
        bar.update(i, 10, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    bar.finish();
    mist::logger::end_update("test");

    // Loop 3: multi-progress bar with two subtasks
    mist::logger::plain("--- multi progress bar demo ---");
    {
        mist::logger::multi_progress_bar multi;
        auto &a = multi.add_subtask("loader");
        auto &b = multi.add_subtask("parser");

        for (int i = 0; i <= 10; ++i)
        {
            mist::logger::debug("debug message — multi-bar pinned below");
            a.update(i, 10, false);
            b.update(i, 20, false);
            multi.update(i, 10, false);
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
    test_update_anchor_basic();
    test_update_anchor_end_noop();
    test_update_anchor_resurrection_warning();
    test_progress_bar_with_update_anchor();
    test_multi_progress_bar_basic();
    test_multi_progress_bar_subtask_output();
    test_multi_progress_bar_with_log();
    test_multi_progress_bar_independent_subtask_progress();

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