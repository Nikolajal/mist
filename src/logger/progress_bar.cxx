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

    progress_bar::progress_bar(bar_style style)
        : style_(style)
    {
        // anchor_object base constructor registers this in _registry().
        // rendered_line_count() returns 0 until the first update() call
        // sets active_, so erase_all() won't move up for unrendered bars.
    }

    progress_bar::~progress_bar()
    {
        // Safety net: if finish() was never called, commit whatever state we
        // have so the bar line isn't left dangling on screen.
        if (active_)
        {
            active_       = false;
            suffix_width_ = -1;
            _draw();
        }
        // anchor_object base destructor deregisters from _registry().
    }

    // -------------------------------------------------------------------------
    // anchor_object interface
    // -------------------------------------------------------------------------

    int progress_bar::rendered_line_count() const
    {
        return (suffix_width_ >= 0 && active_) ? 1 : 0;
    }

    void progress_bar::render_line() const
    {
        // Called by redraw_all() after erase_all() has already positioned the
        // cursor. Just draw in-place — no cursor movement here.
        if (suffix_width_ < 0 || !active_)
            return;
        _draw();
    }

    // -------------------------------------------------------------------------
    // Public interface
    // -------------------------------------------------------------------------

    bool progress_bar::is_active() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return active_;
    }

    void progress_bar::update(double fraction, bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            _update_state(static_cast<float>(std::clamp(fraction, 0.0, 1.0)),
                          std::nullopt, std::nullopt);
        }
        // Delegate all cursor management to anchor_object.
        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void progress_bar::finish(bool flush)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!active_) return;
            active_       = false;
            suffix_width_ = -1;
        }
        // rendered_line_count() now returns 0, so erase_all() skips this bar.
        // redraw_all() redraws any remaining anchors. Then _draw() commits the
        // final bar state as a permanent scrolling line.
        anchor_object::erase_all();
        anchor_object::redraw_all();
        _draw();
        if (flush) std::cout << std::flush;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    int progress_bar::terminal_width()
    {
#if defined(MIST_HAS_IOCTL)
        struct winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
            return static_cast<int>(w.ws_col);
#endif
        return 80;
    }

    std::string progress_bar::format_duration(double seconds)
    {
        const int h = static_cast<int>(seconds) / 3600;
        const int m = (static_cast<int>(seconds) % 3600) / 60;
        const int s = static_cast<int>(seconds) % 60;

        std::ostringstream oss;
        if (h > 0) oss << h << "h ";
        if (h > 0 || m > 0) oss << m << "m ";
        oss << s << "s";
        return oss.str();
    }

    void progress_bar::_update_state(float fraction,
                                      std::optional<int64_t> current,
                                      std::optional<int64_t> total)
    {
        // Must be called with mutex_ held.
        if (!active_)
        {
            start_        = clock_t::now();
            active_       = true;
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

        last_suffix_   = suffix.str();
        last_fraction_ = fraction;
        last_current_  = current;
        last_total_    = total;

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

    void progress_bar::_draw() const
    {
        // Pure draw at the current cursor position — no cursor movement.
        if (suffix_width_ < 0)
            return;

        std::string padded = last_suffix_;
        if (static_cast<int>(padded.size()) < suffix_width_)
            padded += std::string(suffix_width_ - padded.size(), ' ');

        constexpr int prefix_w = 11;
        constexpr int brackets = 3;
        const int     term_w   = terminal_width();
        const int     bar_w    = std::max(10, term_w - prefix_w - brackets - suffix_width_);
        const int     filled   = static_cast<int>(std::round(last_fraction_ * bar_w));
        const int     empty    = bar_w - filled;

        std::string filled_str, tip_str, empty_str;
        if (style_ == bar_style::BLOCK)
        {
            for (int i = 0; i < filled; ++i) filled_str += "\xe2\x96\x88";
            for (int i = 0; i < empty;  ++i) empty_str  += "\xe2\x96\x91";
        }
        else
        {
            if (filled > 0)
            {
                filled_str = std::string(filled - 1, '=');
                tip_str    = (last_fraction_ < 1.0f) ? ">" : "=";
            }
            empty_str = std::string(empty, ' ');
        }

        std::cout << "\033[2K\r"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                  << "[PROGRESS]"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                  << " ["
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD})
                  << filled_str << tip_str
                  << ansi(colour_tag::WHITE, {style_tag::DIM})
                  << empty_str
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                  << "]"
                  << ansi()
                  << padded
                  << '\n';
    }

} // namespace mist::logger