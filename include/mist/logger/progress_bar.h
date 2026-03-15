#pragma once
#include <mist/logger/logger.h>
#include <chrono>
#include <mutex>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <string>

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
     * Inherits anchor_object — registers itself on construction and
     * deregisters on finish(), so log() calls automatically erase and
     * redraw the bar region without any extra wiring.
     *
     * The bar auto-detects terminal width via ioctl(TIOCGWINSZ), falling
     * back to 80 columns on non-POSIX platforms or if the query fails.
     *
     * Example:
     * @code{.cpp}
     * mist::logger::progress_bar bar;
     * for (int i = 0; i <= 100; ++i) {
     *     bar.update(i, 100);
     *     do_work(i);
     * }
     * bar.finish();
     * @endcode
     */
    class progress_bar : public anchor_object
    {
    public:
        explicit progress_bar(bar_style style = bar_style::BLOCK);

        /**
         * @brief Construct a named progress bar.
         *
         * The tag is rendered as a bracketed prefix before the bar:
         * @code
         *   [framer]  [████████░░░░] 60.0%  elapsed: 2s  eta: 3s
         * @endcode
         * If the tag is empty the bar renders exactly as the untagged constructor.
         */
        explicit progress_bar(std::string tag, bar_style style = bar_style::BLOCK);

        ~progress_bar() override;

        /** @brief Set or replace the tag at any time. */
        void assign_tag(std::string tag);

        /** @brief Remove the tag — bar reverts to the default [PROGRESS] prefix. */
        void clear_tag();

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
            {
                std::lock_guard<std::mutex> lk(mutex_);
                _update_state(std::clamp(fraction, 0.0f, 1.0f),
                              static_cast<int64_t>(current),
                              static_cast<int64_t>(total));
            }
            anchor_object::erase_all();
            anchor_object::redraw_all();
            if (flush) std::cout << std::flush;
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

        [[nodiscard]] bool is_active() const;

        // anchor_object interface
        [[nodiscard]] int rendered_line_count() const override;
        void render_line() const override;

    private:
        using clock_t    = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        mutable std::mutex mutex_;

        std::string            tag_;          ///< Optional label shown as [tag] before the bar.
        bar_style              style_;
        bool                   active_       = false;
        int                    suffix_width_ = -1;
        float                  last_fraction_= 0.0f;
        std::optional<int64_t> last_current_;
        std::optional<int64_t> last_total_;
        time_point             start_;

        [[nodiscard]] static int         terminal_width();
        [[nodiscard]] static std::string format_duration(double seconds);

        void _update_state(float                  fraction,
                           std::optional<int64_t> current,
                           std::optional<int64_t> total);
        void _draw() const;
    };

} // namespace mist::logger
