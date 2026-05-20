/**
 * @file multi_progress_bar.cxx
 * @brief Implementation of @ref mist::logger::multi_progress_bar and the
 *        associated @ref mist::logger::subtask_progress_bar.
 *
 * Locking model:
 *  - One @c std::mutex per multi-bar guards the main fraction, the subtask
 *    vector, finished_count_, the cached widths, and every subtask's per-line
 *    state.
 *  - The global @ref anchor_object registry mutex is the outer lock; the
 *    multi-bar's @c mutex_ is the inner lock.  Subtask update paths take the
 *    multi-bar mutex first, mutate state, RELEASE, then call the registry-
 *    locked anchor operations.  @ref render_line acquires the multi-bar
 *    mutex briefly while the registry mutex is already held by the caller.
 *  - On a non-TTY destination the anchored region is suppressed entirely
 *    (no cursor escapes emitted).
 */

#include <mist/logger/multi_progress_bar.h>
#include <mist/logger/logger.h>
#include <array>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#if defined(__has_include) && __has_include(<sys/ioctl.h>)
#include <sys/ioctl.h>
#include <unistd.h>
#define MIST_MPB_HAS_IOCTL 1
#endif

namespace mist::logger
{
    // =========================================================================
    // Internal helpers
    // =========================================================================
    namespace
    {
        std::string si(int64_t n)
        {
            constexpr std::array<std::pair<int64_t, const char *>, 4> tiers = {{
                {1'000'000'000LL, "G"},
                {1'000'000LL,     "M"},
                {1'000LL,         "K"},
                {1LL,             ""},
            }};
            for (auto [thresh, suf] : tiers)
            {
                if (n >= thresh)
                {
                    std::ostringstream o;
                    o << std::fixed << std::setprecision(2)
                      << (static_cast<double>(n) / thresh) << suf;
                    return o.str();
                }
            }
            return std::to_string(n);
        }
    }

    // =========================================================================
    // multi_progress_bar — ctor / dtor
    // =========================================================================

    multi_progress_bar::multi_progress_bar(bar_style style)
        : style_(style), start_(clock_t::now())
    {
        // anchor_object base constructor registers this in _registry().
        // rendered_line_count() returns 0 until active_ is set on first update.
    }

    multi_progress_bar::~multi_progress_bar()
    {
        if (active_)
        {
            active_          = false;
            last_line_count_ = 0;
            // Base destructor deregisters from _registry().
        }
    }

    // =========================================================================
    // subtask management
    // =========================================================================

    subtask_progress_bar &multi_progress_bar::add_subtask(std::string tag)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        subtasks_.push_back(
            std::unique_ptr<subtask_progress_bar>(
                new subtask_progress_bar(std::move(tag), *this)));
        ++total_subtasks_;
        tag_col_width_ = -1;
        return *subtasks_.back();
    }

    // =========================================================================
    // anchor_object interface
    // =========================================================================

    void multi_progress_bar::render_line() const
    {
        // Called by redraw_all() — cursor already positioned by erase_all().
        // The registry mutex is held by the caller; we add this bar's own
        // mutex briefly to read state consistently (B4 fix: previously
        // _draw_locked was reached with no mutex_ held, racing with subtask
        // updates).
        std::lock_guard<std::mutex> lk(mutex_);
        if (!active_ || last_line_count_ == 0)
            return;
        const_cast<multi_progress_bar *>(this)->_draw_locked();
    }

    // =========================================================================
    // Public update / finish
    // =========================================================================

    void multi_progress_bar::update(double fraction, bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            _update_state_locked(
                static_cast<float>(std::clamp(fraction, 0.0, 1.0)));
        }
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void multi_progress_bar::set_header(std::string tag, std::string_view msg, bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            header_mode_ = !tag.empty();
            header_tag_  = std::move(tag);
            header_msg_  = std::string(msg);
            _update_state_locked(main_fraction_);
        }
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void multi_progress_bar::finish(bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!active_ && last_line_count_ == 0)
                return;
            main_fraction_ = 1.0f;
            active_        = false;
            // Keep last_line_count_ intact — erase_all() needs it to know
            // how many lines to move up. We clear it after erasing.
        }
        anchor_object::erase_all();   // uses last_line_count_ via rendered_line_count()
        last_line_count_ = 0;         // now safe to zero — erase is done
        anchor_object::redraw_all();  // redraws other anchors only (active_=false skips us)
        _draw_locked();               // commit final frame as permanent scrolling output
        if (flush) std::cout << std::flush;
    }

    // =========================================================================
    // Private — state update (call with mutex_ held)
    // =========================================================================

    void multi_progress_bar::_update_state_locked(float frac)
    {
        // Activate on first call.
        if (!active_)
        {
            active_ = true;
            start_  = clock_t::now();
        }

        main_fraction_ = frac;

        // Pre-compute stable tag column width once all subtasks are known.
        if (tag_col_width_ < 0)
        {
            int max_tag = 0;
            for (auto &s : subtasks_)
                max_tag = std::max(max_tag, static_cast<int>(s->tag_.size()));
            tag_col_width_ = max_tag + 1;
        }

        // Update line count so rendered_line_count() is accurate before draw.
        const int n = static_cast<int>(subtasks_.size());
        last_line_count_ = 1 + (n > 0 ? 1 + n : 0);
    }

    // =========================================================================
    // Private — pure draw (no cursor movement, no mutex acquisition)
    // =========================================================================

    void multi_progress_bar::_draw_locked()
    {
        // On a non-TTY destination, suppress the entire multi-bar block so
        // log files / piped output stay free of cursor-control escapes and
        // ANSI sequences (B7 fix).
        if (!is_tty())
            return;

        // Ensure suffix_width_ and tag_col_width_ are initialised even if
        // _draw_locked is called from finish() before any update().
        if (tag_col_width_ < 0)
        {
            int max_tag = 0;
            for (auto &s : subtasks_)
                max_tag = std::max(max_tag, static_cast<int>(s->tag_.size()));
            tag_col_width_ = max_tag + 1;
        }

        // Set line count before drawing so rendered_line_count() is always
        // accurate — erase_all() may be called at any point after this.
        const int n = static_cast<int>(subtasks_.size());
        last_line_count_ = 1 + (n > 0 ? 1 + n : 0);

        std::string out;
        out.reserve(512);
        _render_main(out);

        if (n > 0)
        {
            const int tw = _terminal_width();
            out += "\n";
            _emit_line(out, "\033[2m  ─── subtasks ───\033[0m", tw);
            for (auto &s : subtasks_)
            {
                out += "\n";
                _render_subtask(out, *s);
            }
        }
        out += "\n";  // cursor must end on a fresh line below the anchor region

        std::cout << out;
    }

    // =========================================================================
    // Private — subtask callbacks (called with mutex_ held by subtask methods)
    // =========================================================================

    // ─────────────────────────────────────────────────────────────────────────
    // Subtask callback notes (B5):
    //
    // The anchor operations (erase_all / redraw_all) acquire the global
    // registry mutex.  Lock order across the codebase is registry → bar; the
    // subtask paths enter with the bar mutex held, so we must RELEASE the bar
    // mutex before acquiring the registry mutex, otherwise the ordering would
    // be bar → registry and could deadlock with a concurrent log() that goes
    // registry → bar (via redraw_all → render_line).
    //
    // Releasing the bar mutex means another thread could mutate state during
    // the anchor calls.  In practice the multi-bar is driven from a single
    // thread (the writer's main loop), and worker threads only call log() —
    // which acquires the registry mutex (synchronising it with our anchor
    // calls) but never the bar mutex.  Therefore no state changes are
    // possible during the unlocked window in the supported use pattern.
    //
    // If you support concurrent updates from multiple threads in the future,
    // wrap state mutations in a small "needs another pass" flag that is
    // re-checked after re-acquiring the lock.
    // ─────────────────────────────────────────────────────────────────────────

    void multi_progress_bar::_subtask_updated_locked(
        const subtask_progress_bar * /*who*/, std::unique_lock<std::mutex> &lk, bool flush)
    {
        _update_state_locked(main_fraction_);
        lk.unlock();  // see "Subtask callback notes" above
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
        lk.lock();
    }

    void multi_progress_bar::_subtask_finished_locked(
        subtask_progress_bar *who, std::unique_lock<std::mutex> &lk, bool flush)
    {
        if (!who->active_)
            return;
        who->active_   = false;
        who->fraction_ = 1.0f;
        // Freeze the elapsed display so subsequent redraws (triggered by
        // OTHER subtasks updating) don't show this finished subtask still
        // ticking.  If the subtask was never updated start_set_ stays false
        // and the elapsed column simply remains blank.
        if (who->start_set_)
        {
            who->frozen_elapsed_seconds_ =
                std::chrono::duration<double>(clock_t::now() - who->start_).count();
        }
        ++finished_count_;
        _update_state_locked(main_fraction_);
        lk.unlock();  // see "Subtask callback notes" above
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
        lk.lock();
    }

    void multi_progress_bar::_set_main_fraction(float frac, bool flush)
    {
        std::unique_lock<std::mutex> lk(mutex_);
        _update_state_locked(frac);
        lk.unlock();
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void multi_progress_bar::_set_main_progress(float frac, int64_t current,
                                                int64_t total, bool flush)
    {
        {
            std::unique_lock<std::mutex> lk(mutex_);
            main_current_ = current;
            main_total_   = total;
            _update_state_locked(frac);
        }
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void multi_progress_bar::set_unit(std::string unit, bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            main_unit_ = std::move(unit);
            suffix_width_ = -1; // force recompute on next render
        }
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void multi_progress_bar::restart(bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            // Reset cycle-level state. We keep `active_` true and the
            // subtask list intact — only timing and fraction are recycled.
            start_         = clock_t::now();
            main_fraction_ = 0.0f;
            // Do NOT touch main_current_ / main_total_ — the caller typically
            // sets these explicitly after restart() via update().
            suffix_width_  = -1; // force recompute (legend may have shifted)
            finished_count_ = 0;

            // Cascade restart into every subtask (under the parent lock so
            // updates inside subtask state are atomic w.r.t. the redraw).
            for (auto &s : subtasks_)
            {
                s->fraction_  = 0.0f;
                s->current_   = 0;
                s->total_     = 0;
                s->active_    = true;
                s->start_     = clock_t::now();
                s->start_set_ = true;
            }
        }
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    // =========================================================================
    // _render_main
    // =========================================================================
    void multi_progress_bar::_render_main(std::string &out) const
    {
        if (header_mode_)
        {
            out += "\033[2K\r";
            out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE});
            out += "[" + header_tag_ + "]";
            out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE});
            out += " " + header_msg_;
            out += ansi();
            return;
        }

        const double elapsed =
            std::chrono::duration<double>(clock_t::now() - start_).count();

        const bool unknown_total = (main_total_ <= 0);

        std::ostringstream suf;

        if (unknown_total)
        {
            // No fraction available — show counter and elapsed only.
            if (!main_unit_.empty())
                suf << " " << si(main_current_) << " " << main_unit_;
            suf << "  elapsed: " << _format_duration(elapsed);
        }
        else
        {
            suf << " " << std::fixed << std::setprecision(1)
                << (main_fraction_ * 100.0f) << "%";

            if (!main_unit_.empty())
                suf << "  " << si(main_current_) << "/" << si(main_total_)
                    << " " << main_unit_;

            suf << "  elapsed: " << _format_duration(elapsed);

            if (main_fraction_ > 0.001f && main_fraction_ < 1.0f)
            {
                const double eta = (elapsed / main_fraction_) * (1.0 - main_fraction_);
                suf << "  eta: " << _format_duration(eta);
            }
            else if (main_fraction_ >= 1.0f)
                suf << "  eta: done";
        }

        const std::string suf_str = suf.str();

        if (suffix_width_ < 0)
        {
            // Worst-case width matches the longest possible suffix string
            // for the current mode, so the padded line never reflows.
            std::ostringstream worst;
            if (unknown_total)
            {
                if (!main_unit_.empty())
                    worst << " 1.00G " << main_unit_;
                worst << "  elapsed: 99h 59m 59s";
            }
            else
            {
                worst << " 100.0%";
                if (!main_unit_.empty())
                    worst << "  1.00G/1.00G " << main_unit_;
                worst << "  elapsed: 99h 59m 59s  eta: 99h 59m 59s";
            }
            suffix_width_ = static_cast<int>(worst.str().size());
        }

        constexpr int prefix_w = 11;
        constexpr int brackets = 3;
        const int tw     = _terminal_width();
        const int bar_w  = std::max(10, tw - prefix_w - brackets - suffix_width_);
        const int filled = static_cast<int>(std::round(main_fraction_ * bar_w));
        const int empty  = bar_w - filled;

        std::string padded = suf_str;
        if (static_cast<int>(padded.size()) < suffix_width_)
            padded += std::string(suffix_width_ - padded.size(), ' ');

        std::string fill_s, tip_s, empty_s;
        if (style_ == bar_style::BLOCK)
        {
            for (int i = 0; i < filled; ++i) fill_s  += "\xe2\x96\x88";
            for (int i = 0; i < empty;  ++i) empty_s += "\xe2\x96\x91";
        }
        else
        {
            if (filled > 0)
            {
                fill_s = std::string(filled - 1, '=');
                tip_s  = (main_fraction_ < 1.0f) ? ">" : "=";
            }
            empty_s = std::string(empty, ' ');
        }

        out += "\033[2K\r";
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE});
        out += "[PROGRESS]";
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE});
        out += " [";
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD});
        out += fill_s + tip_s;
        out += ansi(colour_tag::WHITE, {style_tag::DIM});
        out += empty_s;
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE});
        out += "]";
        out += ansi();
        out += padded;
    }

    // =========================================================================
    // _render_subtask
    // =========================================================================
    void multi_progress_bar::_render_subtask(std::string &out,
                                              const subtask_progress_bar &s) const
    {
        const int tw = _terminal_width();

        std::string tag_col = "  " + s.tag_;
        if (static_cast<int>(s.tag_.size()) < tag_col_width_)
            tag_col += std::string(tag_col_width_ - s.tag_.size(), ' ');

        // Per-subtask elapsed time — only set if the subtask has been
        // updated at least once since (re)activation.  The display drops the
        // "elapsed: " prefix to keep the subtask line compact: anyone reading
        // the column knows what the trailing duration is.
        //
        // When the subtask is finished we use the frozen duration captured at
        // finish() time so the figure does not keep ticking under us.
        std::string elapsed_str;
        if (s.start_set_)
        {
            const double elapsed = s.active_
                ? std::chrono::duration<double>(clock_t::now() - s.start_).count()
                : s.frozen_elapsed_seconds_;
            elapsed_str = "  " + _format_duration(elapsed);
        }

        std::ostringstream suf;
        suf << " " << std::fixed << std::setprecision(1)
            << (s.fraction_ * 100.0f) << "%";
        if (s.total_ > 0)
            suf << "  " << si(s.current_) << "/" << si(s.total_);
        suf << elapsed_str;

        const std::string suf_str   = suf.str();
        // Worst case includes per-subtask elapsed "  99h 59m 59s" (13 chars).
        constexpr int     worst_w   = 21 + 14;
        const int         prefix_w  = static_cast<int>(tag_col.size());
        constexpr int     brackets  = 3;
        const int         bar_w     = std::max(6, tw - prefix_w - brackets - worst_w);
        const int         filled    = static_cast<int>(std::round(s.fraction_ * bar_w));
        const int         empty     = bar_w - filled;

        std::string padded = suf_str;
        if (static_cast<int>(padded.size()) < worst_w)
            padded += std::string(worst_w - padded.size(), ' ');

        std::string fill_s, tip_s, empty_s;
        if (style_ == bar_style::BLOCK)
        {
            for (int i = 0; i < filled; ++i) fill_s  += "\xe2\x96\x88";
            for (int i = 0; i < empty;  ++i) empty_s += "\xe2\x96\x91";
        }
        else
        {
            if (filled > 0)
            {
                fill_s = std::string(filled - 1, '=');
                tip_s  = (s.fraction_ < 1.0f) ? ">" : "=";
            }
            empty_s = std::string(empty, ' ');
        }

        out += "\033[2K\r";
        out += ansi(colour_tag::CYAN, {style_tag::NONE});
        out += tag_col + "[";
        out += ansi(colour_tag::CYAN, {style_tag::BOLD});
        out += fill_s + tip_s;
        out += ansi(colour_tag::WHITE, {style_tag::DIM});
        out += empty_s;
        out += ansi(colour_tag::CYAN, {style_tag::NONE});
        out += "]";
        out += ansi();
        out += padded;
    }

    // =========================================================================
    // _emit_line / _terminal_width / _format_duration
    // =========================================================================

    void multi_progress_bar::_emit_line(std::string &out,
                                        const std::string &line,
                                        int /*term_width*/)
    {
        out += "\033[2K\r";
        out += line;
    }

    int multi_progress_bar::_terminal_width()
    {
#if defined(MIST_MPB_HAS_IOCTL)
        struct winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
            return static_cast<int>(w.ws_col);
#endif
        return 80;
    }

    std::string multi_progress_bar::_format_duration(double seconds)
    {
        const int h = static_cast<int>(seconds) / 3600;
        const int m = (static_cast<int>(seconds) % 3600) / 60;
        const int s = static_cast<int>(seconds) % 60;

        std::ostringstream o;
        if (h > 0) o << h << "h ";
        if (h > 0 || m > 0) o << m << "m ";
        o << s << "s";
        return o.str();
    }

    // =========================================================================
    // subtask_progress_bar — public methods
    // =========================================================================

    void subtask_progress_bar::update(double fraction, bool flush)
    {
        _update_impl(static_cast<float>(std::clamp(fraction, 0.0, 1.0)),
                     std::nullopt, std::nullopt, flush);
    }

    void subtask_progress_bar::finish(bool flush)
    {
        std::unique_lock<std::mutex> lk(parent_.mutex_);
        parent_._subtask_finished_locked(this, lk, flush);
    }

    void subtask_progress_bar::_update_impl(float fraction,
                                            std::optional<int64_t> current,
                                            std::optional<int64_t> total,
                                            bool flush)
    {
        std::unique_lock<std::mutex> lk(parent_.mutex_);
        if (!active_)
        {
            active_ = true;
            if (parent_.finished_count_ > 0)
                --parent_.finished_count_;
        }
        // Start the per-subtask clock on the first update after activation.
        // Subsequent updates leave start_ alone so elapsed continues to grow
        // until the next finish() or restart().
        if (!start_set_)
        {
            start_     = std::chrono::steady_clock::now();
            start_set_ = true;
        }
        fraction_ = fraction;
        if (current) current_ = *current;
        if (total)   total_   = *total;
        parent_._subtask_updated_locked(this, lk, flush);
    }

    void subtask_progress_bar::restart(bool flush)
    {
        std::unique_lock<std::mutex> lk(parent_.mutex_);
        // Resurrection bookkeeping: if this subtask was previously finished,
        // bring the parent's counter back to a consistent state.
        if (!active_ && parent_.finished_count_ > 0)
            --parent_.finished_count_;
        active_    = true;
        fraction_  = 0.0f;
        current_   = 0;
        total_     = 0;
        start_     = std::chrono::steady_clock::now();
        start_set_ = true;
        // Re-render via the same path as a normal update so the parent
        // and the anchor band see the reset immediately.
        parent_._subtask_updated_locked(this, lk, flush);
    }

} // namespace mist::logger