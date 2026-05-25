#pragma once
#include <mist/logger/logger_types.h>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file logger.h
 * @brief Free-function logging interface for the mist::logger subsystem.
 *
 * ### Anchored objects
 * Any object that occupies a fixed band of terminal lines (progress bars,
 * multi-bars, status panels) inherits from AnchorObject and registers itself
 * in a global list.  log() uses that list to know how many lines to erase
 * before printing and redraw after, keeping log output above the anchored
 * region at all times.
 *
 * ### Thread safety
 * The registry, the named-update map, and the level filter are all guarded
 * by a single internal recursive mutex (@ref AnchorObject::registry_lock).
 * Free functions in this header acquire it around the erase → print → redraw
 * sequence so logging from multiple threads is safe.  Lock order across the
 * codebase is registry → bar; @ref render_line overrides may acquire their
 * own bar-internal mutex while the registry mutex is already held.
 *
 * ### TTY-awareness
 * When stdout/stderr are not attached to a TTY (file redirection or pipe),
 * @ref AnchorObject::erase_all / @ref redraw_all become no-ops and progress
 * bars suppress all cursor-control output, keeping log files clean.  See
 * @ref is_tty in @c logger_types.h.
 */
namespace mist::logger
{
    // ------------------------------------------------------------------
    // Anchored-object registry
    // ------------------------------------------------------------------

    /**
     * @brief Base class for any object occupying a fixed band at the bottom
     *        of the terminal (progress bars, multi-bars, named update lines).
     *
     * Derived classes register themselves on construction and deregister on
     * destruction.  The static methods @ref erase_all and @ref redraw_all are
     * used by @ref log to clear the anchored region before printing scrolling
     * text and to repaint it afterwards.
     *
     * @note Move and copy are deleted: anchors hold a stable address registered
     *       in a global vector, so moving would invalidate that registration.
     *
     * @par Thread safety
     * The registry, the level filter, and the named-update map are all guarded
     * by a single internal recursive mutex (@ref registry_lock).  Bar update
     * paths acquire the bar's own internal mutex first (briefly, to mutate
     * state), release it, then call @ref erase_all / @ref redraw_all which
     * acquire the registry mutex.  @ref render_line overrides that need to
     * read their own state safely should acquire the bar's mutex inside the
     * call — the lock order is therefore registry → bar, consistent across all
     * code paths.
     */
    class AnchorObject
    {
    public:
        /// @brief Register this anchor with the global registry.
        AnchorObject();

        /// @brief Deregister this anchor from the global registry.
        virtual ~AnchorObject();

        AnchorObject(const AnchorObject &) = delete;
        AnchorObject &operator=(const AnchorObject &) = delete;
        AnchorObject(AnchorObject &&) = delete;
        AnchorObject &operator=(AnchorObject &&) = delete;

        /// @brief Number of terminal lines this anchor currently occupies.
        /// Return 0 before the first render so erase_all() skips it.
        [[nodiscard]] virtual int rendered_line_count() const = 0;

        /// @brief Emit this anchor's lines at the current cursor position.
        /// Implementations must not move the cursor — that is owned by
        /// @ref erase_all and @ref redraw_all.
        virtual void render_line() const = 0;

        /// @brief Sum of rendered_line_count() across all registered anchors.
        static int total_anchored_lines();

        /// @brief Move the cursor up over the entire anchored band, erasing
        ///        each line, leaving the cursor at the top of the band.
        static void erase_all();

        /// @brief Ask every registered anchor to redraw itself in order.
        /// Cursor must already be positioned at the band's top line.
        static void redraw_all();

        /**
         * @brief Acquire the global registry mutex.
         *
         * Returns a @c std::unique_lock so the caller controls when the lock is
         * released.  Used by @ref log and the named-update functions to guard
         * the erase-print-redraw sequence as one atomic operation.  The mutex
         * is recursive so callers already holding it (e.g. inside
         * @ref render_line dispatched by @ref redraw_all) may call back into
         * registry-protected helpers without deadlocking.
         */
        static std::unique_lock<std::recursive_mutex> registry_lock();

    private:
        static std::vector<AnchorObject *> &_registry();
        static std::recursive_mutex &_registry_mutex();
    };

    // ------------------------------------------------------------------
    // Level filter
    // ------------------------------------------------------------------

    /// @brief Set the minimum severity that @ref log will emit (DEBUG by default).
    /// @c PLAIN bypasses the filter and is always emitted.  Thread-safe.
    void set_min_level(LevelTag level);

    /// @brief Current minimum-severity threshold for @ref log.
    LevelTag get_min_level();

    /// @brief Returns @c true if a message tagged @p level would be emitted.
    bool check_level(LevelTag level);

    // ------------------------------------------------------------------
    // Core log functions
    // ------------------------------------------------------------------

    /**
     * @brief Print one scrolling log line at the given severity.
     *
     * The call is wrapped in an internal RAII guard that erases the anchored
     * band, prints the line, then repaints the anchors, so log output never
     * collides with progress bars or named update lines.
     *
     * @param tag    Severity classifying the message (filters via @ref set_min_level).
     * @param msg    Message text — printed verbatim after a colour-coded tag prefix.
     * @param flush  If @c true (default), flush the underlying stream after printing.
     * @note         ERROR and WARNING are written to @c std::cerr; everything else
     *               goes to @c std::cout.
     */
    void log(LevelTag tag, std::string_view msg, bool flush = true);

    /**
     * @brief Print one scrolling line styled with a free colour/style choice.
     *
     * Bypasses the level filter — intended for one-off coloured output that
     * does not fit any predefined severity.
     *
     * @param msg  Message text printed verbatim.
     * @param c    Foreground colour to apply.
     * @param s    Brace-enclosed list of style modifiers (default: @c NONE).
     */
    void log(std::string_view msg,
             ColourTag c,
             std::initializer_list<StyleTag> s = {StyleTag::None});

    // ------------------------------------------------------------------
    // Convenience wrappers
    // ------------------------------------------------------------------

    /// @brief Log at ERROR severity (→ stderr).
    inline void error  (std::string_view msg, bool flush = true) { log(LevelTag::Error,   msg, flush); }
    /// @brief Log at WARNING severity (→ stderr).
    inline void warning(std::string_view msg, bool flush = true) { log(LevelTag::Warning,  msg, flush); }
    /// @brief Log at INFO severity (→ stdout).
    inline void info   (std::string_view msg, bool flush = true) { log(LevelTag::Info,     msg, flush); }
    /// @brief Log at DEBUG severity (→ stdout).
    inline void debug  (std::string_view msg, bool flush = true) { log(LevelTag::Debug,    msg, flush); }
    /// @brief Log a plain unstyled message — bypasses the level filter.
    inline void plain  (std::string_view msg, bool flush = true) { log(LevelTag::Plain,    msg, flush); }

    // ------------------------------------------------------------------
    // In-place update line
    // ------------------------------------------------------------------

    /**
     * @brief Print or refresh a named single-line status anchor.
     *
     * The first call creates the anchor; subsequent calls overwrite the
     * message in place without scrolling.  Multiple distinct names can be
     * active simultaneously — each occupies one line of the anchored band.
     *
     * @param update_name  Unique key identifying the anchor.  Re-used names
     *                     update the existing line; a name resurrected after
     *                     @ref end_update triggers a one-shot warning.
     * @param msg          New message to display.
     * @param flush        If @c true, flush stdout after the redraw.
     */
    void update(std::string update_name, std::string_view msg, bool flush = true);

    /**
     * @brief Finalise a named anchor and commit it as a scrolling line.
     *
     * Removes the anchor from the band, writes its last message as a
     * permanent line above the remaining anchors, and redraws what is left.
     * A no-op if @p update_name was never used.
     *
     * @param update_name  Anchor key previously passed to @ref update.
     * @param flush        If @c true, flush stdout after committing.
     */
    void end_update(std::string update_name, bool flush = true);

} // namespace mist::logger
