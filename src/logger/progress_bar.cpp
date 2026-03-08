#include <mist/logger/progress_bar.h>
#include <mist/logger/logger.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <array>
#include <utility>

namespace mist::logger
{
    // ------------------------------------------------------------------
    // SI formatter
    // ------------------------------------------------------------------

    // Formats an integer to 3 significant figures with an SI suffix.
    //   999        → "999"
    //   1000       → "1.00K"
    //   1234567    → "1.23M"
    //   1000000000 → "1.00G"
    static std::string format_si(int64_t n)
    {
        constexpr std::array<std::pair<int64_t, const char *>, 4> tiers = {{{1'000'000'000LL, "G"},
                                                                            {1'000'000LL, "M"},
                                                                            {1'000LL, "K"},
                                                                            {1LL, ""}}};

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
        return std::to_string(n); // fallback for 0
    }

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    progress_bar::progress_bar(bar_style style)
        : style_(style)
    {
    }

    // ------------------------------------------------------------------
    // Public interface
    // ------------------------------------------------------------------

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
        suffix_width_ = -1;
        detail::set_update_mode(false);
    }

    // ------------------------------------------------------------------
    // Private helpers
    // ------------------------------------------------------------------

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
            suffix_width_ = -1; // reset so it gets fixed on first render
            detail::set_update_mode(true);
        }

        const double elapsed =
            std::chrono::duration<double>(clock_t::now() - start_).count();

        // --- Build actual suffix ---
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

        const std::string suffix_str = suffix.str();

        // --- Fix suffix width on the first render ---
        // Pre-compute the worst-case suffix width so the bar width stays
        // stable across all frames regardless of how numbers grow.
        // SI formatting caps current/total at "1.00G/1.00G" (11 chars),
        // and durations are capped at "99h 59m 59s" (11 chars).
        if (suffix_width_ < 0)
        {
            std::ostringstream worst;
            worst << " 100.0%";
            if (current && total)
                worst << "  1.00G/1.00G";
            worst << "  elapsed: 99h 59m 59s  eta: 99h 59m 59s";
            suffix_width_ = static_cast<int>(worst.str().size());
        }

        // --- Compute stable bar width ---
        constexpr int prefix_w = 11; // "[PROGRESS] "
        constexpr int brackets = 3;  // '[', ']', ' '
        const int term_w = terminal_width();
        const int bar_w = std::max(10,
                                   term_w - prefix_w - brackets - suffix_width_);

        const int filled = static_cast<int>(std::round(fraction * bar_w));
        const int empty = bar_w - filled;

        // --- Pad suffix to fixed width so shrinking text doesn't leave ghosts ---
        std::string padded_suffix = suffix_str;
        if (static_cast<int>(padded_suffix.size()) < suffix_width_)
            padded_suffix += std::string(suffix_width_ - padded_suffix.size(), ' ');

        // --- Render fill and empty strings ---
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

        // --- Print ---
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
                  << padded_suffix;

        if (flush)
            std::cout << std::flush;
    }

} // namespace mist::logger