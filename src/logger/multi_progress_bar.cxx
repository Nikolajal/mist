#include <mist/logger/multi_progress_bar.h>
#include <mist/logger/logger.h>
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
    // Internal helpers (file-local)
    // =========================================================================
    namespace
    {
        std::string si(int64_t n)
        {
            constexpr std::array<std::pair<int64_t, const char *>, 4> tiers = {{
                {1'000'000'000LL, "G"},
                {1'000'000LL, "M"},
                {1'000LL, "K"},
                {1LL, ""},
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
    } // anonymous namespace

    // =========================================================================
    // multi_progress_bar — ctor / dtor
    // =========================================================================

    multi_progress_bar::multi_progress_bar(bar_style style)
        : style_(style), start_(clock_t::now())
    {
    }

    multi_progress_bar::~multi_progress_bar()
    {
        if (active_)
            detail::progress_bar_registry::instance().unregister_bar(this);
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
    // Public update / finish
    // =========================================================================

    void multi_progress_bar::update(double fraction, bool flush)
    {
        _set_main_fraction(static_cast<float>(std::clamp(fraction, 0.0, 1.0)),
                           flush);
    }

    void multi_progress_bar::finish(bool flush)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!active_ && last_line_count_ == 0)
            return;

        main_fraction_ = 1.0f;
        _render_all_locked(false, /*skip_erase=*/false);

        std::cout << '\n';
        if (flush)
            std::cout << std::flush;

        active_ = false;
        last_line_count_ = 0;
        detail::progress_bar_registry::instance().unregister_bar(this);
    }

    // =========================================================================
    // Registry callbacks (called with registry mutex held, NOT our mutex_)
    // =========================================================================

    void multi_progress_bar::render_unlocked(bool flush)
    {
        // Registry has already erased the bar region before calling here,
        // so we must NOT erase again — pass skip_erase=true.
        _render_all_locked(flush, /*skip_erase=*/true);
    }

    // =========================================================================
    // Private helpers
    // =========================================================================

    void multi_progress_bar::_set_main_fraction(float frac, bool flush)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        main_fraction_ = frac;
        _render_all_locked(flush, /*skip_erase=*/false);
    }

    void multi_progress_bar::_subtask_updated_locked(
        const subtask_progress_bar * /*who*/, bool flush)
    {
        _render_all_locked(flush, /*skip_erase=*/false);
    }

    void multi_progress_bar::_subtask_finished_locked(
        subtask_progress_bar *who, bool flush)
    {
        if (!who->active_) // already finished, ignore
            return;
        who->active_ = false;
        ++finished_count_;

        _render_all_locked(flush, /*skip_erase=*/false);
    }

    // -------------------------------------------------------------------------
    // _render_all_locked
    // -------------------------------------------------------------------------
    void multi_progress_bar::_render_all_locked(bool flush, bool skip_erase)
    {
        // --- Throttle: don't redraw more than ~10 times/sec ---
        const bool first_render = (last_line_count_ == 0);
        if (!first_render && !flush)
        {
            auto now = clock_t::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_render_).count();
            if (ms < 100)
                return;
        }
        last_render_ = clock_t::now();

        // --- Register on first render ---
        if (!active_)
        {
            active_ = true;
            start_ = clock_t::now();
            detail::progress_bar_registry::instance().register_bar(this);
        }

        // --- Count active subtasks ---
        int active_count = 0;
        for (auto &s : subtasks_)
            if (s->active_)
                ++active_count;

        // --- Pre-compute stable tag column width ---
        if (tag_col_width_ < 0)
        {
            int max_tag = 0;
            for (auto &s : subtasks_)
                max_tag = std::max(max_tag, static_cast<int>(s->tag_.size()));
            tag_col_width_ = max_tag + 1;
        }

        // --- Erase previous render (skip when called from render_unlocked) ---
        if (last_line_count_ > 0 && !skip_erase)
        {
            std::string up;
            up.reserve(last_line_count_ * 12);
            up += "\r\033[2K";
            for (int i = 1; i < last_line_count_; ++i)
                up += "\033[1A\r\033[2K";
            std::cout << up;
        }

        // --- Build output ---
        std::string out;
        out.reserve(512);

        _render_main(out);

        const int total_subtasks_rendered = static_cast<int>(subtasks_.size());
        if (total_subtasks_rendered > 0)
        {
            const int tw = _terminal_width();
            out += first_render ? "\n" : "\033[1B\r";
            _emit_line(out, "\033[2m  ─── subtasks ───\033[0m", tw);

            for (auto &s : subtasks_)
            {
                out += first_render ? "\n" : "\033[1B\r";
                _render_subtask(out, *s); // render all — inactive shown at 100% dimmed
            }
        }

        std::cout << out;
        if (flush || first_render)
            std::cout << std::flush;

        last_line_count_ = 1 + (total_subtasks_rendered > 0 ? 1 + total_subtasks_rendered : 0);
    }

    // -------------------------------------------------------------------------
    // _render_main
    // -------------------------------------------------------------------------
    void multi_progress_bar::_render_main(std::string &out) const
    {
        const double elapsed =
            std::chrono::duration<double>(clock_t::now() - start_).count();

        std::ostringstream suf;
        suf << " " << std::fixed << std::setprecision(1)
            << (main_fraction_ * 100.0f) << "%";

        if (total_subtasks_ > 0)
            suf << "  " << finished_count_ << "/" << total_subtasks_ << " tasks";

        suf << "  elapsed: " << _format_duration(elapsed);

        if (main_fraction_ > 0.001f && main_fraction_ < 1.0f)
        {
            const double eta = (elapsed / main_fraction_) * (1.0 - main_fraction_);
            suf << "  eta: " << _format_duration(eta);
        }
        else if (main_fraction_ >= 1.0f)
            suf << "  eta: done";

        const std::string suf_str = suf.str();

        if (suffix_width_ < 0)
        {
            std::ostringstream worst;
            worst << " 100.0%";
            if (total_subtasks_ > 0)
                worst << "  " << total_subtasks_ << "/" << total_subtasks_ << " tasks";
            worst << "  elapsed: 99h 59m 59s  eta: 99h 59m 59s";
            suffix_width_ = static_cast<int>(worst.str().size());
        }

        constexpr int prefix_w = 11;
        constexpr int brackets = 3;
        const int tw = _terminal_width();
        const int bar_w = std::max(10, tw - prefix_w - brackets - suffix_width_);
        const int filled = static_cast<int>(std::round(main_fraction_ * bar_w));
        const int empty = bar_w - filled;

        std::string padded = suf_str;
        if (static_cast<int>(padded.size()) < suffix_width_)
            padded += std::string(suffix_width_ - padded.size(), ' ');

        std::string fill_s, tip_s, empty_s;
        if (style_ == bar_style::BLOCK)
        {
            for (int i = 0; i < filled; ++i)
                fill_s += "\xe2\x96\x88";
            for (int i = 0; i < empty; ++i)
                empty_s += "\xe2\x96\x91";
        }
        else
        {
            if (filled > 0)
            {
                fill_s = std::string(filled - 1, '=');
                tip_s = (main_fraction_ < 1.0f) ? ">" : "=";
            }
            empty_s = std::string(empty, ' ');
        }

        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE});
        out += "[PROGRESS]";
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE});
        out += " [";
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD});
        out += fill_s;
        out += tip_s;
        out += ansi(colour_tag::WHITE, {style_tag::DIM});
        out += empty_s;
        out += ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE});
        out += "]";
        out += ansi();
        out += padded;
    }

    // -------------------------------------------------------------------------
    // _render_subtask
    // -------------------------------------------------------------------------
    void multi_progress_bar::_render_subtask(std::string &out,
                                             const subtask_progress_bar &s) const
    {
        const int tw = _terminal_width();

        std::string tag_col = "  " + s.tag_;
        if (static_cast<int>(s.tag_.size()) < tag_col_width_)
            tag_col += std::string(tag_col_width_ - s.tag_.size(), ' ');

        std::ostringstream suf;
        suf << " " << std::fixed << std::setprecision(1)
            << (s.fraction_ * 100.0f) << "%";
        if (s.total_ > 0)
            suf << "  " << si(s.current_) << "/" << si(s.total_);

        const std::string suf_str = suf.str();
        constexpr int worst_suf_w = 21;

        const int prefix_w = static_cast<int>(tag_col.size());
        constexpr int brackets = 3;
        const int bar_w = std::max(6, tw - prefix_w - brackets - worst_suf_w);
        const int filled = static_cast<int>(std::round(s.fraction_ * bar_w));
        const int empty = bar_w - filled;

        std::string padded = suf_str;
        if (static_cast<int>(padded.size()) < worst_suf_w)
            padded += std::string(worst_suf_w - padded.size(), ' ');

        std::string fill_s, tip_s, empty_s;
        if (style_ == bar_style::BLOCK)
        {
            for (int i = 0; i < filled; ++i)
                fill_s += "\xe2\x96\x88";
            for (int i = 0; i < empty; ++i)
                empty_s += "\xe2\x96\x91";
        }
        else
        {
            if (filled > 0)
            {
                fill_s = std::string(filled - 1, '=');
                tip_s = (s.fraction_ < 1.0f) ? ">" : "=";
            }
            empty_s = std::string(empty, ' ');
        }

        out += ansi(colour_tag::CYAN, {style_tag::NONE});
        out += tag_col;
        out += "[";
        out += ansi(colour_tag::CYAN, {style_tag::BOLD});
        out += fill_s;
        out += tip_s;
        out += ansi(colour_tag::WHITE, {style_tag::DIM});
        out += empty_s;
        out += ansi(colour_tag::CYAN, {style_tag::NONE});
        out += "]";
        out += ansi();
        out += padded;
    }

    // -------------------------------------------------------------------------
    // _emit_line
    // -------------------------------------------------------------------------
    void multi_progress_bar::_emit_line(std::string &out,
                                        const std::string &line,
                                        int /*term_width*/)
    {
        out += line;
    }

    // -------------------------------------------------------------------------
    // _terminal_width
    // -------------------------------------------------------------------------
    int multi_progress_bar::_terminal_width()
    {
#if defined(MIST_MPB_HAS_IOCTL)
        struct winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
            return static_cast<int>(w.ws_col);
#endif
        return 80;
    }

    // -------------------------------------------------------------------------
    // _format_duration
    // -------------------------------------------------------------------------
    std::string multi_progress_bar::_format_duration(double seconds)
    {
        const int h = static_cast<int>(seconds) / 3600;
        const int m = (static_cast<int>(seconds) % 3600) / 60;
        const int s = static_cast<int>(seconds) % 60;

        std::ostringstream o;
        if (h > 0)
            o << h << "h ";
        if (h > 0 || m > 0)
            o << m << "m ";
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
        std::lock_guard<std::mutex> lk(parent_.mutex_);
        parent_._subtask_finished_locked(this, flush);
    }

    // Fix 2: reactivate on update
    void subtask_progress_bar::_update_impl(float fraction,
                                            std::optional<int64_t> current,
                                            std::optional<int64_t> total,
                                            bool flush)
    {
        std::lock_guard<std::mutex> lk(parent_.mutex_);
        // Reactivate if previously finished (e.g. spill reset)
        if (!active_)
        {
            active_ = true;
            if (parent_.finished_count_ > 0)
                --parent_.finished_count_;
        }
        fraction_ = fraction;
        if (current)
            current_ = *current;
        if (total)
            total_ = *total;
        parent_._subtask_updated_locked(this, flush);
    }

} // namespace mist::logger