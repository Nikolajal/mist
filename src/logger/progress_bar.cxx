/**
 * @file ProgressBar.cxx
 * @brief Implementation of @ref mist::logger::ProgressBar.
 *
 * State accessed concurrently from update / finish / render_line is protected
 * by the per-instance @c mutex_.  Bars cooperate with the global anchor
 * registry: state-mutating paths acquire the bar mutex briefly, release it,
 * and then call @ref mist::logger::AnchorObject::erase_all and @ref redraw_all
 * (which acquire the registry mutex).  This keeps the lock order
 * @c registry → @c bar consistent across all code paths.
 */

#include <mist/logger/progress_bar.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <array>
#include <utility>

namespace mist::logger
{
    // -------------------------------------------------------------------------
    // SI formatter
    // -------------------------------------------------------------------------

    static std::string format_si(int64_t n)
    {
        if (n < 1'000LL)
            return std::to_string(n);

        constexpr std::array<std::pair<int64_t, const char *>, 3> tiers = {
            {{1'000'000'000LL, "G"}, {1'000'000LL, "M"}, {1'000LL, "K"}}};

        for (auto [threshold, suffix] : tiers)
        {
            if (n >= threshold)
            {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2)
                    << (static_cast<double>(n) / threshold) << suffix;
                return oss.str();
            }
        }
        return std::to_string(n);
    }

    // -------------------------------------------------------------------------
    // Construction / destruction
    // -------------------------------------------------------------------------

    ProgressBar::ProgressBar(BarStyle style)
        : style_(style)
    {
        // AnchorObject base constructor registers this in _registry().
        // rendered_line_count() returns 0 until the first update() call
        // sets active_, so erase_all() won't move up for unrendered bars.
    }

    ProgressBar::ProgressBar(std::string tag, BarStyle style)
        : tag_(std::move(tag)), style_(style)
    {
    }

    void ProgressBar::assign_tag(std::string tag)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        tag_ = std::move(tag);
        suffix_width_ = -1;  // force layout recompute on next render so the
                             // new tag width is reflected regardless of when
                             // assign_tag() is called relative to update().
    }

    void ProgressBar::clear_tag()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        tag_.clear();
    }

    ProgressBar::~ProgressBar()
    {
        // Safety net: if finish() was never called, commit whatever state we
        // have so the bar line isn't left dangling on screen.  We must hold
        // the mutex while reading active_ and the cached suffix — another
        // thread may still be inside update() during destruction (programmer
        // error, but cheap to defend against).
        std::lock_guard<std::mutex> lk(mutex_);
        if (active_)
        {
            active_ = false;
            suffix_width_ = -1;
            _draw();
        }
        // AnchorObject base destructor deregisters from _registry().
    }

    // -------------------------------------------------------------------------
    // AnchorObject interface
    // -------------------------------------------------------------------------

    int ProgressBar::rendered_line_count() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return (suffix_width_ >= 0 && active_) ? 1 : 0;
    }

    void ProgressBar::render_line() const
    {
        // Called by redraw_all() after erase_all() has already positioned the
        // cursor.  redraw_all() holds the registry mutex; we add the bar's
        // own mutex briefly to read state consistently (lock order
        // registry → bar, in line with the documented model).
        std::lock_guard<std::mutex> lk(mutex_);
        if (suffix_width_ < 0 || !active_)
            return;
        _draw();
    }

    // -------------------------------------------------------------------------
    // Public interface
    // -------------------------------------------------------------------------

    bool ProgressBar::is_active() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return active_;
    }

    void ProgressBar::update(double fraction, bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            _update_state(static_cast<float>(std::clamp(fraction, 0.0, 1.0)),
                          std::nullopt, std::nullopt);
        }
        // Delegate all cursor management to AnchorObject — hold the
        // registry lock across BOTH erase and redraw so concurrent
        // updates from other threads can't interleave between them
        // (see log_print_guard for the same RAII pattern).
        {
            auto reg_lk = AnchorObject::registry_lock();
            AnchorObject::erase_all();
            AnchorObject::redraw_all();
        }
        if (flush)
            std::cout << std::flush;
    }

    void ProgressBar::finish(bool flush)
    {
        // 1. Update state to 100% under the mutex (B2 fix: previously called
        //    _update_state unlocked, violating its own "mutex_ must be held"
        //    contract).
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!active_)
                return;
            _update_state(1.0f, 1, 1);
        }

        //  Steps 2–5 must be atomic against concurrent update() calls from
        //  other threads.  Hold the registry lock across the whole
        //  erase → draw → deactivate → redraw sequence (recursive, so
        //  re-acquiring it inside erase_all/redraw_all is fine).  Without
        //  this bracket, a concurrent thread's update() can slip an
        //  erase between our steps and walk the cursor into scroll history.
        auto reg_lk = AnchorObject::registry_lock();

        // 2. Erase the anchored band — this bar is still active and counts
        //    among the lines to erase. We hold no bar mutex here, only the
        //    registry mutex (now held by us).
        AnchorObject::erase_all();

        // 3. Emit the final 100% frame as a permanent scrolling line (B1 fix:
        //    previously the bar was deactivated before any draw, so render_line
        //    and _draw both early-returned and nothing was committed). Locking
        //    around _draw keeps it consistent with the contract that the cached
        //    suffix is only read with the mutex held.
        {
            std::lock_guard<std::mutex> lk(mutex_);
            _draw();
        }

        // 4. Now deactivate, so subsequent erase_all/redraw_all calls ignore
        //    this bar, and the line just drawn scrolls naturally.
        {
            std::lock_guard<std::mutex> lk(mutex_);
            active_ = false;
            suffix_width_ = -1;
        }

        // 5. Redraw the remaining anchors below the committed line.
        AnchorObject::redraw_all();

        if (flush)
            std::cout << std::flush;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    int ProgressBar::terminal_width()
    {
#if defined(MIST_HAS_IOCTL)
        struct winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
            return static_cast<int>(w.ws_col);
#endif
        return 80;
    }

    std::string ProgressBar::format_duration(double seconds)
    {
        const int h = static_cast<int>(seconds) / 3600;
        const int m = (static_cast<int>(seconds) % 3600) / 60;
        const int s = static_cast<int>(seconds) % 60;

        std::ostringstream oss;
        if (h > 0)
            oss << h << "h ";
        if (h > 0 || m > 0)
            oss << m << "m ";
        oss << s << "s";
        return oss.str();
    }

    void ProgressBar::_update_state(float fraction,
                                     std::optional<int64_t> current,
                                     std::optional<int64_t> total)
    {
        // Must be called with mutex_ held.
        if (!active_)
        {
            start_ = clock_t::now();
            active_ = true;
            suffix_width_ = -1;
        }

        const double elapsed =
            std::chrono::duration<double>(clock_t::now() - start_).count();

        std::ostringstream suffix;
        suffix << " " << std::fixed << std::setprecision(1)
               << (fraction * 100.0f) << "%";
        if (current && total)
            suffix << "  " << format_si(*current) << "/" << format_si(*total);
        suffix << "  elapsed: " << format_duration(elapsed);
        if (fraction > 0.001f && fraction < 1.0f)
        {
            const double eta = (elapsed / fraction) * (1.0f - fraction);
            suffix << "  eta: " << format_duration(eta);
        }
        else if (fraction >= 1.0f)
            suffix << "  eta: done";

        last_suffix_ = suffix.str();
        last_fraction_ = fraction;
        last_current_ = current;
        last_total_ = total;

        if (suffix_width_ < 0)
        {
            std::ostringstream worst;
            worst << " 100.0%";
            if (current && total)
                worst << "  1.00G/1.00G";
            worst << "  elapsed: 99h 59m 59s  eta: 99h 59m 59s";
            suffix_width_ = static_cast<int>(worst.str().size());
        }
    }

    void ProgressBar::_draw() const
    {
        // Pure draw at the current cursor position — no cursor movement.
        if (suffix_width_ < 0)
            return;
        // On a non-TTY destination the anchored band is not maintained at all
        // — emit nothing so log files stay free of cursor-control escapes.
        if (!is_tty())
            return;

        std::string padded = last_suffix_;
        if (static_cast<int>(padded.size()) < suffix_width_)
            padded += std::string(suffix_width_ - padded.size(), ' ');

        // Build the prefix string and its display width.
        // If a tag is set: "[tag] " — dynamic width.
        // Otherwise fall back to the default "[PROGRESS] " — width 11.
        const bool has_tag = !tag_.empty();
        const int prefix_w = has_tag ? static_cast<int>(tag_.size()) + 3 // "[" + tag + "] "
                                     : 11;                               // "[PROGRESS] "
        constexpr int brackets = 3;                                      // " [" ... "]"  around the fill bar
        const int term_w = terminal_width();
        const int bar_w = std::max(10, term_w - prefix_w - brackets - suffix_width_);
        const int filled = static_cast<int>(std::round(last_fraction_ * bar_w));
        const int empty = bar_w - filled;

        std::string filled_str, tip_str, empty_str;
        if (style_ == BarStyle::Block)
        {
            for (int i = 0; i < filled; ++i)
                filled_str += "\xe2\x96\x88";
            for (int i = 0; i < empty; ++i)
                empty_str += "\xe2\x96\x91";
        }
        else
        {
            if (filled > 0)
            {
                filled_str = std::string(filled - 1, '=');
                tip_str = (last_fraction_ < 1.0f) ? ">" : "=";
            }
            empty_str = std::string(empty, ' ');
        }

        std::cout << "\033[2K\r"
                  << ansi(ColourTag::BrightGreen, {StyleTag::Bold, StyleTag::Underline});

        if (has_tag)
            std::cout << "[" << tag_ << "]";
        else
            std::cout << "[PROGRESS]";

        std::cout << ansi(ColourTag::BrightGreen, {StyleTag::None})
                  << " ["
                  << ansi(ColourTag::BrightGreen, {StyleTag::Bold})
                  << filled_str << tip_str
                  << ansi(ColourTag::White, {StyleTag::Dim})
                  << empty_str
                  << ansi(ColourTag::BrightGreen, {StyleTag::None})
                  << "]"
                  << ansi()
                  << padded
                  << '\n';
    }

} // namespace mist::logger