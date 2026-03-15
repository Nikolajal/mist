#include <mist/logger/multi_progress_bar.h>
#include <mist/logger/logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#if defined(__has_include) && __has_include(<sys/ioctl.h>)
#   include <sys/ioctl.h>
#   include <unistd.h>
#   define MIST_MPB_HAS_IOCTL 1
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
    }

    multi_progress_bar::~multi_progress_bar()
    {
        if (active_)
        {
            active_          = false;
            last_line_count_ = 0;
        }
        // Base destructor deregisters from _registry().
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
        if (!active_ || last_line_count_ == 0) return;
        const_cast<multi_progress_bar *>(this)->_draw_locked();
    }

    // =========================================================================
    // Public update / finish / set_header
    // =========================================================================

    void multi_progress_bar::update(double fraction, bool flush)
    {
        _set_main_fraction(
            static_cast<float>(std::clamp(fraction, 0.0, 1.0)), flush);
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
            if (!active_ && last_line_count_ == 0) return;
            main_fraction_ = 1.0f;
            active_        = false;
        }
        anchor_object::erase_all();   // erase the region (uses last_line_count_)
        last_line_count_ = 0;         // safe to zero now
        anchor_object::redraw_all();  // redraws other anchors
        _draw_locked();               // commit final frame as permanent output
        if (flush) std::cout << std::flush;
    }

    // =========================================================================
    // Private — _set_main_fraction (not holding mutex)
    // =========================================================================

    void multi_progress_bar::_set_main_fraction(float frac, bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            _update_state_locked(frac);
        }
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    // =========================================================================
    // Private — _update_state_locked (call with mutex_ held)
    // =========================================================================

    void multi_progress_bar::_update_state_locked(float frac)
    {
        if (!active_)
        {
            active_ = true;
            start_  = clock_t::now();
        }
        main_fraction_ = frac;

        if (tag_col_width_ < 0)
        {
            int max_tag = 0;
            for (auto &s : subtasks_)
                max_tag = std::max(max_tag, static_cast<int>(s->tag_.size()));
            tag_col_width_ = max_tag + 1;
        }

        const int n      = static_cast<int>(subtasks_.size());
        last_line_count_ = 1 + (n > 0 ? 1 + n : 0);
    }

    // =========================================================================
    // Private — _draw_locked (no mutex acquisition, no cursor movement)
    // =========================================================================

    void multi_progress_bar::_draw_locked()
    {
        if (tag_col_width_ < 0)
        {
            int max_tag = 0;
            for (auto &s : subtasks_)
                max_tag = std::max(max_tag, static_cast<int>(s->tag_.size()));
            tag_col_width_ = max_tag + 1;
        }

        const int n      = static_cast<int>(subtasks_.size());
        last_line_count_ = 1 + (n > 0 ? 1 + n : 0);

        std::string out;
        out.reserve(512);
        _render_main(out);

        if (n > 0)
        {
            const int tw = _terminal_width();
            out += "\n";
            _emit_line(out, "\033[2m  \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 subtasks \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\033[0m", tw);
            for (auto &s : subtasks_)
            {
                out += "\n";
                _render_subtask(out, *s);
            }
        }

        std::cout << out << '\n';
    }

    // =========================================================================
    // Private — _render_main
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

        constexpr int prefix_w = 11; // "[PROGRESS] "
        constexpr int brackets =  3;
        const int     tw       = _terminal_width();
        const int     bar_w    = std::max(10, tw - prefix_w - brackets - suffix_width_);
        const int     filled   = static_cast<int>(std::round(main_fraction_ * bar_w));
        const int     empty    = bar_w - filled;

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
    // Private — _render_subtask
    // =========================================================================

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

        const std::string suf_str   = suf.str();
        constexpr int     worst_w   = 21;
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
    // Private — _emit_line / _terminal_width / _format_duration
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
            if (parent_.finished_count_ > 0) --parent_.finished_count_;
        }
        fraction_ = fraction;
        if (current) current_ = *current;
        if (total)   total_   = *total;
        parent_._subtask_updated_locked(this, lk, flush);
    }

    void multi_progress_bar::_subtask_updated_locked(const subtask_progress_bar * /*who*/,
                                                      std::unique_lock<std::mutex> &lk,
                                                      bool flush)
    {
        _update_state_locked(main_fraction_);
        lk.unlock();
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void multi_progress_bar::_subtask_finished_locked(subtask_progress_bar *who,
                                                       std::unique_lock<std::mutex> &lk,
                                                       bool flush)
    {
        who->active_   = false;
        who->fraction_ = 1.0f;
        ++finished_count_;
        _update_state_locked(main_fraction_);
        lk.unlock();
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

} // namespace mist::logger
