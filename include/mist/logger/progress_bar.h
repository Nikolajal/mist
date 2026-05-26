#pragma once
#include <mist/logger/logger.h>
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

/**
 * @file ProgressBar.h
 * @brief @ref mist::logger::ProgressBar — single-line in-place progress bar
 *        that participates in the anchored-band registry so log lines never
 *        scroll across it.  See @ref mist::logger::MultiProgressBar for the
 *        composite multi-line variant.
 */

namespace mist::logger
{
    enum class BarStyle
    {
        Arrow, ///< [=====>    ]  classic ASCII
        Block  ///< [██████░░░░]  Unicode block characters
    };

    /**
     * @brief In-place terminal progress bar with elapsed time and ETA.
     *
     * Inherits AnchorObject — registers itself on first render and
     * deregisters on finish(), so log() calls automatically erase and
     * redraw the bar region without any extra wiring.
     */
    class ProgressBar : public AnchorObject
    {
    public:
        /**
         * @brief Construct an untagged progress bar.
         * @param style  Visual fill style (BLOCK Unicode or ARROW ASCII).
         */
        explicit ProgressBar(BarStyle style = BarStyle::Block);

        /**
         * @brief Construct a named progress bar.
         *
         * The tag is rendered as a bracketed prefix before the bar:
         * @code
         *   [framer]  [████████░░░░] 60.0%  elapsed: 2s  eta: 3s
         * @endcode
         * If the tag is empty the bar renders exactly as the untagged constructor.
         *
         * @param tag    Short label shown in brackets before the bar.
         * @param style  Visual fill style (BLOCK or ARROW).
         */
        explicit ProgressBar(std::string tag, BarStyle style = BarStyle::Block);

        /**
         * @brief Destructor — safety net that commits the last state if
         *        @ref finish was never called, preventing dangling bar lines.
         */
        ~ProgressBar() override;

        /**
         * @brief Set or replace the tag at any time.
         *
         * Safe to call before the first @ref update, between updates, or after
         * @ref finish — the cached layout width is invalidated so the next
         * render recomputes column placement from the new tag length.
         *
         * @param tag  New label; pass an empty string to revert to the default
         *             @c [PROGRESS] prefix (equivalent to @ref clear_tag).
         */
        void assign_tag(std::string tag);

        /** @brief Remove the tag — bar reverts to the default [PROGRESS] prefix. */
        void clear_tag();

        /**
         * @brief Update the bar with an integral (current / total) pair.
         *
         * Activates the bar on the first call and starts its internal clock.
         * No-op if @p total is non-positive.
         *
         * @tparam T      Integral type for @p current and @p total.
         * @param current Progress count so far.
         * @param total   Target count corresponding to 100 %.
         * @param flush   If @c true (default), flush stdout after redraw.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0)
                return;
            const float fraction = static_cast<float>(current) / static_cast<float>(total);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                _update_state(std::clamp(fraction, 0.0f, 1.0f),
                              static_cast<int64_t>(current),
                              static_cast<int64_t>(total));
            }
            //  Hold the registry lock across BOTH erase_all and redraw_all so
            //  concurrent updates from other threads cannot interleave between
            //  them.  Without this, T2's erase_all can land after T1's erase
            //  but before T1's redraw — cursor sits at the top of the anchor
            //  band, T2's cursor-up walks into the scroll history, and the
            //  bar appears to "stack" new lines instead of update in place.
            //  registry_lock is recursive → safe to hold while bar's own
            //  per-instance mutex_ is acquired elsewhere on this thread.
            {
                auto reg_lk = AnchorObject::registry_lock();
                AnchorObject::erase_all();
                AnchorObject::redraw_all();
            }
            if (flush)
                std::cout << std::flush;
        }

        /**
         * @brief Update the bar with a raw fraction in [0, 1].
         *
         * Activates the bar on the first call.  No @c current / @c total
         * counter is shown — only the percentage.
         *
         * @param fraction  Value in [0, 1]; clamped if out of range.
         * @param flush     If @c true, flush stdout after redraw.
         */
        void update(double fraction, bool flush = true);

        /**
         * @brief Mark the bar as complete at 100 % and finalise its line.
         *
         * Releases the anchor slot — subsequent log calls will not erase or
         * redraw this bar.  Call exactly once at the end of a task; calling
         * @ref update again afterwards reactivates the bar as a fresh cycle.
         *
         * @param flush  If @c true, flush stdout after committing.
         */
        void finish(bool flush = true);

        /// @brief Returns @c true between the first @ref update and @ref finish.
        [[nodiscard]] bool is_active() const;

        // AnchorObject interface
        [[nodiscard]] int rendered_line_count() const override;
        void render_line() const override;

    private:
        using clock_t = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        mutable std::mutex mutex_; ///< Guards all per-instance state below.

        std::string tag_;           ///< Optional label shown as [tag] before the bar.
        BarStyle style_;           ///< Visual fill style (BLOCK or ARROW).
        bool active_ = false;       ///< True between first @ref update and @ref finish.
        int suffix_width_ = -1;     ///< Cached width of the right-side info column; -1 = uninitialised.
        float last_fraction_ = 0.0f;///< Last fraction passed to @ref _update_state.
        std::string last_suffix_;   ///< Cached suffix text ("X%  N/M  elapsed: ...  eta: ...").
        std::optional<int64_t> last_current_; ///< Last @c current value (empty if none yet).
        std::optional<int64_t> last_total_;   ///< Last @c total value (empty if none yet).
        time_point start_;          ///< Set when the bar transitions to active; drives elapsed/ETA.

        /// @brief Best-effort terminal column count via @c TIOCGWINSZ; falls back to 80.
        [[nodiscard]] static int terminal_width();

        /// @brief Format a duration in "[Xh ]Ym Zs" style.
        [[nodiscard]] static std::string format_duration(double seconds);

        /**
         * @brief Update all cached state for the next draw.
         * @param fraction Clamped progress fraction in [0, 1].
         * @param current  Optional current count for the "N/M" display.
         * @param total    Optional total count for the "N/M" display.
         * @pre @c mutex_ must be held by the caller.
         */
        void _update_state(float fraction,
                           std::optional<int64_t> current,
                           std::optional<int64_t> total);

        /**
         * @brief Emit the bar line at the current cursor position.
         *
         * Pure draw — no cursor movement (the anchor framework owns that).
         * No-op when not attached to a TTY (see @ref is_tty).
         * @pre @c mutex_ must be held by the caller to read the cached state.
         */
        void _draw() const;
    };

} // namespace mist::logger