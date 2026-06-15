// SPDX-License-Identifier: MIT
#pragma once
#include <mist/logger/logger_types.h>
#include <format>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
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
 * by a single internal recursive mutex (@ref mist::logger::AnchorObject::registry_lock).
 * Free functions in this header acquire it around the erase → print → redraw
 * sequence so logging from multiple threads is safe.  Lock order across the
 * codebase is registry → bar; @ref mist::logger::AnchorObject::render_line
 * overrides may acquire their own bar-internal mutex while the registry mutex
 * is already held.
 *
 * ### TTY-awareness
 * When stdout/stderr are not attached to a TTY (file redirection or pipe),
 * @ref mist::logger::AnchorObject::erase_all / @ref mist::logger::AnchorObject::redraw_all
 * become no-ops and progress bars suppress all cursor-control output, keeping
 * log files clean.  See @ref mist::logger::is_tty in @c logger_types.h.
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
inline void error(std::string_view msg, bool flush = true) { log(LevelTag::Error, msg, flush); }
/// @brief Log at WARNING severity (→ stderr).
inline void warning(std::string_view msg, bool flush = true) { log(LevelTag::Warning, msg, flush); }
/// @brief Log at INFO severity (→ stdout).
inline void info(std::string_view msg, bool flush = true) { log(LevelTag::Info, msg, flush); }
/// @brief Log at DEBUG severity (→ stdout).
inline void debug(std::string_view msg, bool flush = true) { log(LevelTag::Debug, msg, flush); }
/// @brief Log a plain unstyled message — bypasses the level filter.
inline void plain(std::string_view msg, bool flush = true) { log(LevelTag::Plain, msg, flush); }

// ------------------------------------------------------------------
// std::format convenience overloads (C++20)
// ------------------------------------------------------------------
//
// Enable `info("fitted {} spectra", n)` and friends. Each requires at
// least one format argument, so a no-argument call (`info("plain text")`)
// unambiguously selects the (std::string_view, bool) overload above, and
// a call with non-bool arguments prefers the format overload (the plain
// overload would need a bool second argument).
//
// Deliberate ambiguity resolution: a call whose sole extra argument is a
// `bool` — e.g. `info("x", true)` — matches both the flush overload and a
// one-`bool` format overload equally, so it is rejected at compile time.
// That preserves the historical `flush` meaning for the common
// `info(text, flush)` form; to format a lone bool, pass it through an
// explicit `std::format(...)` or add a non-bool argument. The format
// overloads always flush.

template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void log(LevelTag tag, std::format_string<Args...> fmt, Args &&...args)
{
    log(tag, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void error(std::format_string<Args...> fmt, Args &&...args)
{
    log(LevelTag::Error, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void warning(std::format_string<Args...> fmt, Args &&...args)
{
    log(LevelTag::Warning, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void info(std::format_string<Args...> fmt, Args &&...args)
{
    log(LevelTag::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void debug(std::format_string<Args...> fmt, Args &&...args)
{
    log(LevelTag::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void plain(std::format_string<Args...> fmt, Args &&...args)
{
    log(LevelTag::Plain, std::format(fmt, std::forward<Args>(args)...));
}

// ------------------------------------------------------------------
// Completion output
// ------------------------------------------------------------------

/**
     * @brief Log a success/completion line — always shown, green and bold.
     *
     * Routes through the free-colour @ref log(std::string_view,ColourTag,std::initializer_list<StyleTag>)
     * overload, which bypasses the level filter. A completion confirmation is
     * user-facing output that should always appear and does not belong on the
     * Error→Debug severity ladder, so it intentionally does *not* introduce a
     * new @ref LevelTag; it reuses the free-colour path with a ✓ prefix.
     */
inline void done(std::string_view msg)
{
    log(std::string("✓ ") + std::string(msg),
        ColourTag::BrightGreen, {StyleTag::Bold});
}

/// @brief @c std::format overload of @ref done (requires >= 1 argument).
template <class... Args>
    requires(sizeof...(Args) >= 1)
inline void done(std::format_string<Args...> fmt, Args &&...args)
{
    done(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
}

// ------------------------------------------------------------------
// Third-party stdout capture
// ------------------------------------------------------------------

/**
     * @brief RAII guard routing @c std::cout (and @c std::cerr) through the
     *        logger for its lifetime.
     *
     * Third-party code that writes directly to @c std::cout (ROOT's minimiser,
     * @c TFit chatter, …) would otherwise scribble over the anchored progress
     * band. While a @c ScopedCoutToMist is alive, each completed line written
     * to @c std::cout / @c std::cerr is emitted through @ref plain, i.e. via
     * the erase → print → redraw anchor protocol, so it cooperates with
     * progress bars. The original stream buffers are restored on destruction.
     *
     * No-op when output is not a TTY (see @ref is_tty): there is no anchored
     * band to protect and log files should stay free of reformatting, so the
     * streams are left untouched.
     *
     * @note Not thread-safe — install one on the thread doing the noisy work.
     *       Non-copyable.
     *
     * @code{.cpp}
     * {
     *     mist::logger::ScopedCoutToMist redirect;
     *     graph->Fit(f.get(), "RMQ");   // ROOT chatter -> logger
     * }
     * @endcode
     */
class ScopedCoutToMist
{
public:
    ScopedCoutToMist()
    {
        if (!is_tty())
            return; // nothing to protect; leave the streams alone
        real_cout_ = std::cout.rdbuf();
        real_cerr_ = std::cerr.rdbuf();
        buf_.set_real(real_cout_);
        std::cout.rdbuf(&buf_);
        std::cerr.rdbuf(&buf_);
        active_ = true;
    }

    ~ScopedCoutToMist()
    {
        if (!active_)
            return;
        buf_.flush_remaining();
        std::cout.rdbuf(real_cout_);
        std::cerr.rdbuf(real_cerr_);
    }

    ScopedCoutToMist(const ScopedCoutToMist &) = delete;
    ScopedCoutToMist &operator=(const ScopedCoutToMist &) = delete;

private:
    /// Line-buffering streambuf that emits each completed line via @ref plain.
    class routing_buf : public std::streambuf
    {
    public:
        void set_real(std::streambuf *real) { real_ = real; }
        void flush_remaining()
        {
            if (!line_.empty())
                emit();
        }

    protected:
        int overflow(int ch) override
        {
            if (ch == traits_type::eof())
                return ch;
            const char c = static_cast<char>(ch);
            if (c == '\n')
                emit();
            else
                line_.push_back(c);
            return ch;
        }

        std::streamsize xsputn(const char *s, std::streamsize n) override
        {
            for (std::streamsize i = 0; i < n; ++i)
                overflow(static_cast<unsigned char>(s[i]));
            return n;
        }

    private:
        void emit()
        {
            // Restore the real terminal buffer while the logger prints, so
            // its own writes to std::cout do not feed back into us.
            std::streambuf *mine = std::cout.rdbuf(real_);
            plain(line_);
            std::cout.rdbuf(mine);
            line_.clear();
        }

        std::string line_;
        std::streambuf *real_ = nullptr;
    };

    routing_buf buf_;
    std::streambuf *real_cout_ = nullptr;
    std::streambuf *real_cerr_ = nullptr;
    bool active_ = false;
};

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
