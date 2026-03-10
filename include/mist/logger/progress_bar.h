#pragma once
#include <mist/logger/logger_types.h>
#include <mist/logger/progress_bar_registry.h>
#include <chrono>
#include <mutex>
#include <optional>
#include <cstdint>
#include <algorithm>

#if defined(__has_include) && __has_include(<sys/ioctl.h>)
#include <sys/ioctl.h>
#include <unistd.h>
#define MIST_HAS_IOCTL 1
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
     * automatically erase the bar, print the message, and redraw the bar
     * below it, so log output and bar rendering never corrupt each other.
     *
     * The bar auto-detects terminal width via ioctl(TIOCGWINSZ), falling
     * back to 80 columns on non-POSIX platforms or if the query fails.
     *
     * ### Thread safety
     * All public methods are thread-safe. A single `std::mutex` guards all
     * mutable state so any number of threads may call `update()` or `finish()`
     * concurrently without external synchronisation.
     *
     * ### Lock acquisition order
     * When a log call and a bar render race, locks are always acquired in
     * this order — never the reverse:
     *   1. `bar_registry::mutex_`  (acquired by `log_print_guard`)
     *   2. `progress_bar::mutex_`  (acquired inside `render()`)
     *
     * `render_unlocked()` is called by the registry while holding (1) but
     * not (2), writing directly to stdout without re-acquiring the bar mutex.
     * This is what prevents deadlock when a worker thread holds (2) and races
     * with a log call that holds (1).
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
         *
         * Thread-safe. Templated on any integral type so integer literals
         * resolve unambiguously to this overload rather than the double one.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0)
                return;
            const float fraction = static_cast<float>(current) / static_cast<float>(total);
            render(std::clamp(fraction, 0.0f, 1.0f),
                   static_cast<int64_t>(current),
                   static_cast<int64_t>(total),
                   flush);
        }

        /** @brief Drive by pre-computed fraction in [0.0, 1.0]. Thread-safe. */
        void update(double fraction, bool flush = true);

        /**
         * @brief Commit the bar with a newline and stop the clock.
         *
         * Thread-safe. Safe to call multiple times — no-op after the first.
         * Unregisters this bar from the global registry so subsequent log
         * calls no longer trigger erase/redraw.
         */
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const
        {
            std::lock_guard<std::mutex> lk(mutex_);
            return active_;
        }

        // -----------------------------------------------------------------
        // Registry callbacks
        // Called by bar_registry with its own mutex already held.
        // MUST NOT acquire mutex_ — see lock acquisition order above.
        // -----------------------------------------------------------------

        /**
         * @brief How many terminal lines this bar currently occupies.
         * Returns 0 if never rendered or already finished, 1 otherwise.
         * Used by the registry to know how many lines to erase before printing.
         */
        [[nodiscard]] int rendered_line_count() const
        {
            return (suffix_width_ == -1 || !active_) ? 0 : 1;
        }

        /**
         * @brief Redraw the bar without acquiring mutex_.
         *
         * Called by the registry after a log line has been printed, while
         * the registry mutex is held. Acquiring mutex_ here would deadlock
         * if a worker thread is mid-render (holds mutex_, blocked on the
         * registry lock). The registry lock alone is sufficient to serialise
         * this redraw against concurrent log calls.
         */
        void render_unlocked(bool flush);

    private:
        using clock_t    = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        mutable std::mutex mutex_;  // mutable: locked in const methods

        bar_style  style_;
        bool       active_       = false;
        int        suffix_width_ = -1;  ///< Fixed on first render; -1 = uninitialised.
        time_point start_;

        [[nodiscard]] static int         terminal_width();
        [[nodiscard]] static std::string format_duration(double seconds);

        /**
         * @brief Core render — acquires mutex_, draws the bar.
         *
         * On first call, registers this bar with the global registry so
         * subsequent log calls trigger the erase/redraw protocol.
         */
        void render(float fraction,
                    std::optional<int64_t> current,
                    std::optional<int64_t> total,
                    bool flush);
    };

} // namespace mist::logger