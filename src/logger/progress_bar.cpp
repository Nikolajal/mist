#include <mist/logger/progress_bar.h>
#include <mist/logger/logger.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace mist::logger
{
    progress_bar::progress_bar(bar_style style)
        : style_(style)
    {
    }

    void progress_bar::update(double fraction, bool flush)
    {
        render(static_cast<float>(std::clamp(fraction, 0.0, 1.0)),
               std::nullopt, std::nullopt, flush);
    }

    void progress_bar::finish(bool flush)
    {
        if (!active_)
            return;
        std::cout << '\n';
        if (flush)
            std::cout << std::flush;
        active_ = false;
        detail::set_update_mode(false);
    }

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
        if (h > 0)
            oss << h << "h ";
        if (h > 0 || m > 0)
            oss << m << "m ";
        oss << s << "s";
        return oss.str();
    }

    void progress_bar::render(float fraction,
                              std::optional<int64_t> current,
                              std::optional<int64_t> total,
                              bool flush)
    {
        if (!active_)
        {
            start_ = clock_t::now();
            active_ = true;
            detail::set_update_mode(true);
        }

        const double elapsed =
            std::chrono::duration<double>(clock_t::now() - start_).count();

        std::ostringstream suffix;
        suffix << " " << std::fixed << std::setprecision(1)
               << (fraction * 100.0f) << "%";

        if (current && total)
            suffix << "  " << *current << "/" << *total;

        suffix << "  elapsed: " << format_duration(elapsed);

        if (fraction > 0.001f && fraction < 1.0f)
        {
            const double eta = (elapsed / fraction) * (1.0f - fraction);
            suffix << "  eta: " << format_duration(eta);
        }
        else if (fraction >= 1.0f)
        {
            suffix << "  eta: done";
        }

        const std::string suffix_str = suffix.str();

        constexpr int prefix_w = 11; // "[PROGRESS] "
        constexpr int brackets = 3;  // '[', ']', ' '
        const int term_w = terminal_width();
        const int bar_w = std::max(10,
                                   term_w - prefix_w - brackets - static_cast<int>(suffix_str.size()));

        const int filled = static_cast<int>(std::round(fraction * bar_w));
        const int empty = bar_w - filled;

        std::string filled_str, empty_str, tip_str;
        switch (style_)
        {
        case bar_style::BLOCK:
            for (int i = 0; i < filled; ++i)
                filled_str += "\xe2\x96\x88";
            for (int i = 0; i < empty; ++i)
                empty_str += "\xe2\x96\x91";
            break;
        case bar_style::ARROW:
        default:
            if (filled > 0)
            {
                filled_str = std::string(filled - 1, '=');
                tip_str = (fraction < 1.0f) ? ">" : "=";
            }
            empty_str = std::string(empty, ' ');
            break;
        }

        std::cout << "\033[2K\r"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                  << "[PROGRESS]"
                  << ansi(colour_tag::BRIGHT_GREEN)
                  << " ["
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD})
                  << filled_str << tip_str
                  << ansi(colour_tag::WHITE, {style_tag::DIM})
                  << empty_str
                  << ansi(colour_tag::BRIGHT_GREEN)
                  << "]"
                  << ansi()
                  << suffix_str;

        if (flush)
            std::cout << std::flush;
    }

} // namespace mist::logger
