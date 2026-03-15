#pragma once
#include <mist/logger/logger_types.h>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file logger.h
 * @brief Free-function logging interface for the mist::logger subsystem.
 *
 * ### Anchored objects
 * Any object that occupies a fixed band of terminal lines (progress bars,
 * multi-bars, status panels) inherits from anchor_object and registers itself
 * in a global list.  log() uses that list to know how many lines to erase
 * before printing and redraw after, keeping log output above the anchored
 * region at all times.
 *
 * ### cout redirect
 * ScopedCoutToMist is an RAII guard that temporarily hijacks std::cout,
 * buffering everything written to it and forwarding each non-empty line to
 * mist::logger::plain on destruction.  Use it to absorb third-party output
 * (e.g. ROOT's minimiser printout) without corrupting the anchor system.
 */
namespace mist::logger
{
    // ------------------------------------------------------------------
    // Anchored-object registry
    // ------------------------------------------------------------------

    /**
     * @brief Base class for objects that "anchor" a fixed block of terminal
     *        lines (progress bars, multi-bars, status panels, …).
     *
     * Subclasses register themselves on construction and deregister on
     * destruction.  log() calls erase_all() before printing and redraw_all()
     * after, so log output always appears above the anchored region.
     */
    class anchor_object
    {
    public:
        anchor_object();
        virtual ~anchor_object();

        anchor_object(const anchor_object &) = delete;
        anchor_object &operator=(const anchor_object &) = delete;
        anchor_object(anchor_object &&) = delete;
        anchor_object &operator=(anchor_object &&) = delete;

        /** @brief How many terminal lines this anchor currently occupies. */
        [[nodiscard]] virtual int rendered_line_count() const = 0;

        /**
         * @brief Re-emit all anchor lines at the current cursor position.
         *
         * Called by redraw_all() after erase_all() has already positioned the
         * cursor.  Must emit exactly rendered_line_count() lines (each ending
         * with '\n') and must NOT move the cursor further.
         */
        virtual void render_line() const = 0;

        /** @brief Total lines currently occupying the terminal. */
        static int total_anchored_lines();

        /**
         * @brief Move the cursor up past all anchor lines and erase them.
         *
         * Leaves the cursor at the start of the line where the topmost anchor
         * began, ready for new output or a redraw_all() call.
         */
        static void erase_all();

        /**
         * @brief Redraw all registered anchors from the current cursor position.
         *
         * Calls render_line() on each anchor in registration order.  The cursor
         * ends up one line below the last anchor's last line.
         */
        static void redraw_all();

    private:
        static std::vector<anchor_object *> &_registry();
    };

    // ------------------------------------------------------------------
    // Level filter
    // ------------------------------------------------------------------

    void      set_min_level(level_tag level);
    level_tag get_min_level();
    bool      check_level(level_tag level);

    // ------------------------------------------------------------------
    // Core log functions
    // ------------------------------------------------------------------

    /**
     * @brief Log a message at the given level.
     *
     * Routes ERROR and WARNING to std::cerr, everything else to std::cout.
     * Automatically calls anchor_object::erase_all() before printing and
     * anchor_object::redraw_all() after, so in-flight progress bars and
     * multi-bars are not corrupted.
     */
    void log(level_tag tag, std::string_view msg, bool flush = true);

    /// Log with explicit colour and style — always goes to std::cout.
    void log(std::string_view                  msg,
             colour_tag                        c = colour_tag::RESET,
             std::initializer_list<style_tag>  s = {style_tag::NONE});

    // ------------------------------------------------------------------
    // Convenience wrappers
    // ------------------------------------------------------------------

    inline void error  (std::string_view msg, bool flush = true) { log(level_tag::ERROR,   msg, flush); }
    inline void warning(std::string_view msg, bool flush = true) { log(level_tag::WARNING,  msg, flush); }
    inline void info   (std::string_view msg, bool flush = true) { log(level_tag::INFO,     msg, flush); }
    inline void debug  (std::string_view msg, bool flush = true) { log(level_tag::DEBUG,    msg, flush); }
    inline void plain  (std::string_view msg, bool flush = true) { log(level_tag::PLAIN,    msg, flush); }

    // ------------------------------------------------------------------
    // Named in-place update anchors
    // ------------------------------------------------------------------

    /**
     * @brief Update a named in-place line (creates the anchor on first call).
     *
     * Each unique @p update_name gets its own anchored line.  Subsequent calls
     * with the same name overwrite that line in place.  Call end_update() to
     * commit the final state as a permanent scrolling line and remove the anchor.
     */
    void update(std::string update_name, std::string_view msg, bool flush = true);

    /**
     * @brief Commit and remove a named update anchor.
     *
     * The last message is printed as a permanent scrolling line.  After this
     * call the name can be reused for a fresh anchor.
     */
    void end_update(std::string update_name, bool flush = true);

    // ------------------------------------------------------------------
    // ScopedCoutToMist — RAII cout → mist::logger::plain redirector
    // ------------------------------------------------------------------

    /**
     * @brief RAII guard that redirects std::cout to mist::logger::plain.
     *
     * On construction, replaces std::cout's streambuf with an internal
     * ostringstream.  On destruction:
     *   1. The original streambuf is restored (so plain() can safely write
     *      to cout again).
     *   2. Every non-empty line captured in the buffer is forwarded to
     *      mist::logger::plain(), which in turn respects the anchor system
     *      (erase_all / redraw_all), keeping progress bars intact.
     *
     * Typical use — absorb ROOT's minimiser printout during a Gaussian fit:
     * @code{.cpp}
     * {
     *     mist::logger::ScopedCoutToMist _redirect;
     *     h->Fit("gaus", "Q0", "", lo, hi);
     * }  // lines emitted by ROOT are forwarded here
     * @endcode
     *
     * The type is also exported as `mist::logger::scoped_cout_to_mist` for
     * users who prefer the snake_case convention.
     */
    class ScopedCoutToMist
    {
    public:
        ScopedCoutToMist() : orig_(std::cout.rdbuf(buf_.rdbuf())) {}

        ~ScopedCoutToMist()
        {
            // Restore first so plain() can safely write to cout.
            std::cout.rdbuf(orig_);
            std::string line;
            std::istringstream ss(buf_.str());
            while (std::getline(ss, line))
                if (!line.empty())
                    plain(line);
        }

        // Non-copyable, non-movable — streambuf pointer must stay stable.
        ScopedCoutToMist(const ScopedCoutToMist &) = delete;
        ScopedCoutToMist &operator=(const ScopedCoutToMist &) = delete;
        ScopedCoutToMist(ScopedCoutToMist &&) = delete;
        ScopedCoutToMist &operator=(ScopedCoutToMist &&) = delete;

    private:
        std::ostringstream buf_;
        std::streambuf    *orig_;
    };

    /// Snake_case alias for ScopedCoutToMist.
    using scoped_cout_to_mist = ScopedCoutToMist;

} // namespace mist::logger

// Cascade includes — logger carries progress_bar and multi_progress_bar.
#include <mist/logger/progress_bar.h>
#include <mist/logger/multi_progress_bar.h>
