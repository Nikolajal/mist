#pragma once
#include <mist/logger/logger_types.h>
#include <chrono>
#include <optional>
#include <cstdint>
#include <algorithm>

#if defined(__has_include) && __has_include(<sys/ioctl.h>)
#   include <sys/ioctl.h>
#   include <unistd.h>
#   define MIST_HAS_IOCTL 1
#endif

namespace mist::logger
{
    enum class bar_style
    {
        ARROW, ///< [=====>    ]  classic ASCII
        BLOCK  ///< [██████░░░░]  Unicode block characters
    };

    /**
     * @brief In-place terminal progress bar with elapsed time and ETA.
     *
     * Owns its own state (start time, style, active flag) so multiple
     * independent bars can coexist. Integrates with the logger safety-commit
     * mechanism — a normal log() call while the bar is active will
     * automatically commit it before printing.
     *
     * The bar auto-detects terminal width via ioctl(TIOCGWINSZ), falling
     * back to 80 columns on non-POSIX platforms or if the query fails.
     *
     * Example:
     * @code{.cpp}
     * mist::logger::progress_bar bar;
     * for (int i = 0; i <= 100; ++i)
     * {
     *     bar.update(i, 100);
     *     do_work(i);
     * }
     * bar.finish();
     * mist::logger::info("All done.");
     * @endcode
     */
    class progress_bar
    {
    public:
        explicit progress_bar(bar_style style = bar_style::BLOCK);

        /**
         * @brief Drive by current + total — fraction computed internally.
         * Templated on any integral type so integer literals resolve
         * unambiguously to this overload rather than the floating-point one.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0) return;
            const float fraction = static_cast<float>(current) / static_cast<float>(total);
            render(std::clamp(fraction, 0.0f, 1.0f),
                   static_cast<int64_t>(current),
                   static_cast<int64_t>(total),
                   flush);
        }

        /**
         * @brief Drive by pre-computed fraction in [0.0, 1.0].
         * Accepts float or double — floating-point literals resolve
         * unambiguously here.
         */
        void update(double fraction, bool flush = true);

        /**
         * @brief Commit the bar with a newline and stop the clock.
         * Safe to call multiple times — no-op after the first.
         */
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const { return active_; }

    private:
        using clock_t    = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        bar_style  style_;
        bool       active_ = false;
        time_point start_;

        [[nodiscard]] static int         terminal_width();
        [[nodiscard]] static std::string format_duration(double seconds);

        void render(float                  fraction,
                    std::optional<int64_t> current,
                    std::optional<int64_t> total,
                    bool                   flush);
    };

} // namespace mist::logger
