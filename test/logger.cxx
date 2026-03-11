/**
 * @file test/logger.cxx
 * @brief Manual smoke-test / demo for the mist::logger subsystem.
 *
 * This file serves two purposes:
 *   1. TESTS  — automated checks that run silently and return exit code 1
 *               on any failure (suitable for CI).
 *   2. DEMO   — a visual terminal walkthrough that runs only when all
 *               tests pass, so you can see the logger and progress bar
 *               working in a real TTY.
 *
 * There is intentionally no external test framework (no Catch2, no GTest).
 * The entire harness is ~10 lines of macros and counters, keeping the
 * build dependency-free.
 *
 * Build with:
 *   cmake -B build -DMIST_BUILD_TESTS=ON && cmake --build build
 * Run with:
 *   ./build/bin/test_logger
 */

// ============================================================================
// Includes
// ============================================================================

// --- mist library headers (the code under test) ---
#include <mist/logger/logger.h>               // log(), info(), error(), …, set_min_level()
#include <mist/logger/progress_bar.h>         // progress_bar class
#include <mist/logger/progress_bar_registry.h>// singleton registry that ties bars to log output

// --- Standard library ---
#include <cassert>    // not actually used by CHECK, but available if needed
#include <chrono>     // std::chrono::milliseconds — used in the visual demo
#include <iostream>   // std::cout, std::cerr
#include <sstream>    // std::ostringstream — used by capture_streams
#include <thread>     // std::this_thread::sleep_for — used in the visual demo

// ============================================================================
// Minimal test harness
// ============================================================================
//
// Two file-scope counters track how many assertions have been attempted and
// how many have failed. They are `static` so they are private to this
// translation unit (no risk of name collision if other .cxx files are added).

static int s_tests_run    = 0;  // incremented on every CHECK call
static int s_tests_failed = 0;  // incremented only when an assertion fails

/**
 * CHECK(expr) — the one assertion macro.
 *
 * Usage:
 *   CHECK(some_condition);
 *   CHECK(get_value() == expected);
 *
 * On success: increments s_tests_run and does nothing else.
 * On failure: increments both counters and prints a diagnostic to stderr
 *             that includes the file name, line number, and the exact
 *             source text of the failing expression.
 *
 * Implementation notes:
 *
 *   do { } while (false)
 *     Wraps the body in a single compound statement so the macro is safe
 *     to use after `if`, `else`, etc. without needing extra braces at the
 *     call site.  The trailing semicolon at the call site is consumed by
 *     `while (false);`.
 *
 *   #expr  (stringification)
 *     The preprocessor # operator converts the token sequence passed as
 *     `expr` into a string literal at compile time, e.g.
 *       CHECK(x == 3)  →  prints  "x == 3"
 *     This makes failure messages self-documenting.
 *
 *   __FILE__ and __LINE__
 *     Predefined macros replaced by the preprocessor with the source file
 *     path and line number *at the call site*.  A regular function could
 *     not do this — it would always report its own location instead.
 */
#define CHECK(expr)                                                          \
    do {                                                                     \
        ++s_tests_run;                                                       \
        if (!(expr)) {                                                       \
            ++s_tests_failed;                                                \
            std::cerr << "  FAIL  " << __FILE__ << ":" << __LINE__          \
                      << "  " << #expr << "\n";                             \
        }                                                                    \
    } while (false)

// ============================================================================
// capture_streams — RAII helper for redirecting stdout/stderr
// ============================================================================
//
// Several tests need to inspect what the logger actually wrote.  The standard
// way to do this without forking a process is to swap the stream buffers
// (rdbuf) for in-memory string buffers, run the code under test, then read
// back the captured text.
//
// This struct uses RAII (Resource Acquisition Is Initialisation): the
// redirection happens in the constructor and is automatically undone in the
// destructor, even if the test throws.  This guarantees the real terminal
// streams are always restored.

struct capture_streams
{
    // In-memory buffers that receive output during the capture window
    std::ostringstream cout_buf, cerr_buf;

    // Pointers to the *original* buffers — saved so we can restore them
    std::streambuf *old_cout, *old_cerr;

    // Constructor: install the in-memory buffers and disable ANSI colour.
    //
    // std::cout.rdbuf(new_buf) atomically installs new_buf as the active
    // buffer *and* returns a pointer to the previous buffer, which we save
    // for restoration.  Same for std::cerr.
    //
    // Colour is disabled because ANSI escape sequences (\033[31m etc.) would
    // clutter the captured strings and make substring searches unreliable.
    capture_streams()
        : old_cout(std::cout.rdbuf(cout_buf.rdbuf())),  // redirect cout → cout_buf
          old_cerr(std::cerr.rdbuf(cerr_buf.rdbuf()))   // redirect cerr → cerr_buf
    {
        mist::logger::set_colour_enabled(false); // plain text only while capturing
    }

    // Destructor: restore the real streams and re-enable colour.
    // Called automatically when the capture_streams object goes out of scope.
    ~capture_streams()
    {
        std::cout.rdbuf(old_cout); // restore real stdout
        std::cerr.rdbuf(old_cerr); // restore real stderr
        mist::logger::set_colour_enabled(true);
    }
};

// ============================================================================
// Individual test functions
// ============================================================================

// ----------------------------------------------------------------------------
// test_level_filter
//
// Concept: the logger has a global minimum level.  Only messages whose level
// is <= the minimum are actually printed.  Levels in order:
//   ERROR(0) < WARNING(1) < INFO(2) < DEBUG(3) < PROGRESS(4) < PLAIN(5)
//
// This test only checks the getter/setter round-trip — not the filtering
// behaviour itself (that is covered by test_debug_filtered_below_min_level).
// ----------------------------------------------------------------------------
void test_level_filter()
{
    // Set the minimum to INFO and verify the getter reflects the change
    mist::logger::set_min_level(mist::logger::level_tag::INFO);
    CHECK(mist::logger::get_min_level() == mist::logger::level_tag::INFO);

    // Set it back to DEBUG (most verbose) and verify again
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);
    CHECK(mist::logger::get_min_level() == mist::logger::level_tag::DEBUG);
}

// ----------------------------------------------------------------------------
// test_log_output_reaches_cout
//
// Verifies that info() (and by implication all non-error levels) writes to
// stdout, not to some other stream or to nowhere.
// ----------------------------------------------------------------------------
void test_log_output_reaches_cout()
{
    capture_streams cap; // start capturing; ANSI colour disabled inside

    mist::logger::set_min_level(mist::logger::level_tag::DEBUG); // allow all levels
    mist::logger::info("hello info");

    // cap.cout_buf.str() returns everything written to cout since capture started
    const std::string out = cap.cout_buf.str();

    // The literal payload must appear somewhere in the output.
    // We use find() rather than == because the logger wraps the message in
    // prefix labels and (when enabled) ANSI escapes.
    CHECK(out.find("hello info") != std::string::npos);
} // cap destructor restores streams here

// ----------------------------------------------------------------------------
// test_error_and_warning_go_to_cerr
//
// Errors and warnings are conventionally written to stderr (std::cerr) so
// they are not mixed into pipeline stdout.  This test asserts that routing.
// It also checks the negative — that they do NOT appear on stdout — to catch
// a hypothetical bug where a message is accidentally written to both.
// ----------------------------------------------------------------------------
void test_error_and_warning_go_to_cerr()
{
    capture_streams cap;

    mist::logger::error("something broke");
    mist::logger::warning("be careful");

    const std::string err = cap.cerr_buf.str(); // everything on stderr

    // Both messages must be on stderr
    CHECK(err.find("something broke") != std::string::npos);
    CHECK(err.find("be careful")      != std::string::npos);

    // And must NOT appear on stdout
    CHECK(cap.cout_buf.str().find("something broke") == std::string::npos);
}

// ----------------------------------------------------------------------------
// test_debug_filtered_below_min_level
//
// When the minimum level is INFO, a DEBUG message (which is more verbose /
// lower priority) must be silently dropped — nothing should be written.
// ----------------------------------------------------------------------------
void test_debug_filtered_below_min_level()
{
    capture_streams cap;

    mist::logger::set_min_level(mist::logger::level_tag::INFO); // only INFO and above
    mist::logger::debug("should be hidden");                     // DEBUG < INFO → filtered

    // stdout must be empty — the message was swallowed before printing
    CHECK(cap.cout_buf.str().find("should be hidden") == std::string::npos);

    // Restore to DEBUG so subsequent tests are not affected by this state change
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);
}

// ----------------------------------------------------------------------------
// test_colour_tag_roundtrip
//
// The ansi() helper builds ANSI SGR escape sequences.  When colour is enabled
// the result must contain "\033[" (ESC + '[', the standard SGR introducer).
// When colour is disabled it must return an empty string so log output piped
// to a file is never polluted with raw escape codes.
// ----------------------------------------------------------------------------
void test_colour_tag_roundtrip()
{
    // --- Colour ON ---
    mist::logger::set_colour_enabled(true);
    const std::string seq = mist::logger::ansi(mist::logger::colour_tag::RED,
                                               {mist::logger::style_tag::BOLD});
    // \033 is the octal escape for the ESC character (ASCII 27).
    // All ANSI CSI sequences start with ESC followed by '['.
    CHECK(seq.find("\033[") != std::string::npos);

    // --- Colour OFF ---
    mist::logger::set_colour_enabled(false);
    const std::string empty = mist::logger::ansi(mist::logger::colour_tag::RED);
    CHECK(empty.empty()); // must produce nothing when colour is disabled

    // Restore for the tests that follow
    mist::logger::set_colour_enabled(true);
}

// ----------------------------------------------------------------------------
// test_convenience_wrappers_compile_and_run
//
// error(), warning(), info(), and debug() are inline wrappers around log().
// This test is intentionally trivial: its only goal is to prove that all four
// wrappers compile and execute without crashing.  If any wrapper had a
// signature mismatch or called an undefined function the build itself would
// fail, or the test would crash at runtime.
//
// CHECK(true) at the end is a sentinel — it contributes one entry to the
// "tests run" counter so this function is always visible in the summary, even
// though there is nothing to assert beyond "we got here".
// ----------------------------------------------------------------------------
void test_convenience_wrappers_compile_and_run()
{
    capture_streams cap; // suppress output so it doesn't clutter the terminal
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG);

    mist::logger::error("e");    // routes to stderr
    mist::logger::warning("w");  // routes to stderr
    mist::logger::info("i");     // routes to stdout
    mist::logger::debug("d");    // routes to stdout

    CHECK(true); // if we reach this line, all four wrappers worked
}

// ----------------------------------------------------------------------------
// test_progress_bar_basic
//
// Smoke-test for progress_bar.  The class has no trivial boolean state we can
// inspect from outside, so the test only verifies that:
//   1. Construction, update(), and finish() do not crash or throw.
//   2. The destructor (end of inner scope) also runs cleanly.
//
// The bar is driven inside a capture_streams scope so its terminal escape
// sequences go to the in-memory buffer rather than the real terminal.
//
// flush=false is passed to update() and finish() to avoid blocking on I/O
// inside the captured buffer — in a non-TTY context flush has no meaningful
// effect, but it avoids any potential timing issues.
// ----------------------------------------------------------------------------
void test_progress_bar_basic()
{
    capture_streams cap;

    {
        mist::logger::progress_bar bar; // default style (BLOCK)

        // Drive from 0/10 to 10/10.  The integral template overload computes
        // fraction = current / total internally, clamped to [0, 1].
        for (int i = 0; i <= 10; ++i)
            bar.update(i, 10, /*flush=*/false);

        bar.finish(/*flush=*/false); // commit the bar, print a newline, unregister
    } // bar destructor runs here — safe because finish() was already called

    CHECK(true); // sentinel: reaching here confirms no crash or exception
}

// ============================================================================
// Visual demo
// ============================================================================
//
// This runs only when every test passes (see main()).  Unlike the tests above,
// it deliberately writes to the real terminal so you can see the styled output
// and the animated progress bar.  It is not a test — it has no assertions.

void demo_visual()
{
    mist::logger::set_colour_enabled(true);
    mist::logger::set_min_level(mist::logger::level_tag::DEBUG); // show all levels

    // Show one line per level so the colours and labels are all visible
    std::cout << "\n--- mist::logger visual demo ---\n";
    mist::logger::error("This is an error message");
    mist::logger::warning("This is a warning message");
    mist::logger::info("This is an info message");
    mist::logger::debug("This is a debug message");

    // Animate a progress bar at ~100 fps with 10ms sleep per step.
    // flush=false during the loop avoids a system-call per tick;
    // finish() does a final flush when the bar commits.
    std::cout << "\n--- progress bar demo (100 steps) ---\n";
    {
        mist::logger::progress_bar bar;
        for (int i = 0; i <= 100; ++i)
        {
            bar.update(i, 100, /*flush=*/false);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        bar.finish(); // final flush + newline
    }
    std::cout << '\n';
}

// ============================================================================
// Entry point
// ============================================================================

int main()
{
    std::cout << "Running mist::logger tests...\n";

    // Run every test function in sequence.  Each one calls CHECK one or more
    // times, accumulating results in s_tests_run / s_tests_failed.
    test_level_filter();
    test_log_output_reaches_cout();
    test_error_and_warning_go_to_cerr();
    test_debug_filtered_below_min_level();
    test_colour_tag_roundtrip();
    test_convenience_wrappers_compile_and_run();
    test_progress_bar_basic();

    // Print the final tally
    std::cout << s_tests_run    << " tests run, "
              << s_tests_failed << " failed.\n";

    if (s_tests_failed == 0)
    {
        std::cout << "All tests passed.\n";
        demo_visual(); // only reached when everything is green
        return 0;      // exit code 0 = success (important for CI / install script)
    }
    return 1; // exit code 1 = at least one assertion failed
}