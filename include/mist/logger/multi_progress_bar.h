#pragma once
#include <mist/logger/progress_bar.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

/**
 * @file MultiProgressBar.h
 * @brief @ref mist::logger::MultiProgressBar — composite progress widget
 *        with one main header bar plus N labelled subtask lines.  Used for
 *        workloads with a top-level cycle (spills, events, batches) and a
 *        fixed set of parallel sub-tasks per cycle (loader, parser, writer…).
 */

namespace mist::logger
{
    class MultiProgressBar;

    // =========================================================================
    // SubtaskProgressBar
    // =========================================================================

    /**
     * @brief One sub-line of a @ref MultiProgressBar, identified by a tag.
     *
     * Instances are owned by the parent @ref MultiProgressBar and obtained
     * via @ref MultiProgressBar::add_subtask.  Updating a subtask drives a
     * coordinated redraw through the parent so all subtask lines stay
     * properly aligned in the anchored band.
     *
     * @note Lifetime is tied to the parent.  Subtask references must not
     *       outlive the @ref MultiProgressBar that produced them.
     * @note Non-copyable, non-movable for the same reason — addresses are
     *       handed out to callers and stored in the registry.
     */
    class SubtaskProgressBar
    {
    public:
        /**
         * @brief Update the subtask with an integral (current / total) pair.
         *
         * Activates the subtask on the first call and triggers a coordinated
         * redraw through the parent multi-bar.  No-op if @p total <= 0.
         *
         * @tparam T      Integral type for @p current / @p total.
         * @param current Progress count so far.
         * @param total   Target count corresponding to 100 %.
         * @param flush   If @c true, flush stdout after redraw.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0)
                return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _update_impl(std::clamp(frac, 0.0f, 1.0f),
                         static_cast<int64_t>(current),
                         static_cast<int64_t>(total),
                         flush);
        }

        /**
         * @brief Update the subtask with a raw fraction in [0, 1].
         * @param fraction  Value in [0, 1]; clamped if out of range.
         * @param flush     If @c true, flush stdout after redraw.
         */
        void update(double fraction, bool flush = true);

        /**
         * @brief Mark this subtask as complete; increments the parent's
         *        finished-tasks counter and triggers a redraw.
         * @param flush  If @c true, flush stdout after redraw.
         */
        void finish(bool flush = true);

        /**
         * @brief Reset this subtask's per-cycle state for a fresh iteration.
         *
         * Clears @c fraction_, @c current_, @c total_, and restarts the
         * internal clock so the next @ref update shows "elapsed: 0s" rather
         * than accumulating across iterations.  Use this when a subtask
         * naturally cycles (e.g. once per spill in a streaming framer).
         *
         * Does NOT remove the subtask from the parent — the line stays in
         * the anchored region and the next update redraws it.
         *
         * @param flush  If @c true, flush stdout after the redraw.
         */
        void restart(bool flush = true);

        /// @brief Returns @c true while the subtask has not yet called @ref finish.
        ///
        /// Unlike @ref ProgressBar::is_active, this is @c true from construction
        /// (not from the first @ref update) because a subtask line is shown as
        /// "pending" even before any progress has been reported.
        [[nodiscard]] bool is_active() const { return active_; }

        /// @brief This subtask's label, as passed to @ref MultiProgressBar::add_subtask.
        [[nodiscard]] const std::string &tag() const { return tag_; }

    private:
        friend class MultiProgressBar;

        SubtaskProgressBar(std::string tag, MultiProgressBar &parent)
            : tag_(std::move(tag)), parent_(parent) {}

        SubtaskProgressBar(const SubtaskProgressBar &) = delete;
        SubtaskProgressBar &operator=(const SubtaskProgressBar &) = delete;
        SubtaskProgressBar(SubtaskProgressBar &&) = delete;
        SubtaskProgressBar &operator=(SubtaskProgressBar &&) = delete;

        void _update_impl(float fraction,
                          std::optional<int64_t> current,
                          std::optional<int64_t> total,
                          bool flush);

        std::string tag_;
        MultiProgressBar &parent_;

        float fraction_ = 0.0f;
        int64_t current_ = 0;
        int64_t total_ = 0;
        bool active_ = true;

        /// Per-subtask clock: started on the first @ref update or @ref restart
        /// after activation.  Drives the elapsed-time display in the rendered
        /// subtask line.  Plain @c time_point — protected by the parent's
        /// @c mutex_ via the friend access pattern.
        std::chrono::time_point<std::chrono::steady_clock> start_;
        bool start_set_ = false;

        /// When the subtask is finished, its elapsed display is frozen so
        /// later redraws (triggered by other subtasks updating) don't show
        /// the duration still ticking.  Holds the wall-time between
        /// @c start_ and the @ref finish call, in seconds.
        double frozen_elapsed_seconds_ = 0.0;
    };

    // =========================================================================
    // MultiProgressBar
    // =========================================================================

    /**
     * @brief Composite progress bar: one main bar plus N tagged subtask lines.
     *
     * Renders a header bar (overall progress) followed by a labelled sub-line
     * for each subtask added via @ref add_subtask.  The whole block lives in
     * the anchored region at the bottom of the terminal and survives log
     * output via the @ref AnchorObject machinery.
     *
     * @note Non-copyable, non-movable — subtasks hold a reference back to
     *       their parent, so the parent must have a stable address.
     */
    class MultiProgressBar : public AnchorObject
    {
    public:
        /**
         * @brief Construct an empty multi-bar with no subtasks yet.
         * @param style  Visual fill style applied to all internal bars.
         */
        explicit MultiProgressBar(BarStyle style = BarStyle::Block);

        MultiProgressBar(const MultiProgressBar &) = delete;
        MultiProgressBar &operator=(const MultiProgressBar &) = delete;
        MultiProgressBar(MultiProgressBar &&) = delete;
        MultiProgressBar &operator=(MultiProgressBar &&) = delete;

        /// @brief Destructor — deregisters from the anchor registry.
        ~MultiProgressBar();

        /**
         * @brief Register a new subtask line and return a reference to it.
         *
         * Subtasks are owned by the multi-bar and live until it is destroyed.
         * Calling @c add_subtask after the first @ref update is allowed; the
         * tag-column width is recomputed on the next redraw.
         *
         * @param tag  Label shown at the start of the subtask line.
         * @return     Reference to the new subtask handle (stable for the
         *             lifetime of this @ref MultiProgressBar).
         */
        SubtaskProgressBar &add_subtask(std::string tag);

        /**
         * @brief Drive the main bar with an integral (current / total) pair.
         *
         * Affects only the header line; subtasks must be updated individually.
         *
         * Two modes are supported via @p total:
         *  - @p total > 0:  classic progress mode — shows "X.X%  current/total
         *                   unit  elapsed: …  eta: …".
         *  - @p total <= 0: unknown-total mode — shows "current unit
         *                   elapsed: …" without a percentage or ETA.  Useful
         *                   for streaming workloads where the spill count is
         *                   not known in advance.
         *
         * @tparam T      Integral type.
         * @param current Progress so far.
         * @param total   Target count, or any non-positive value to enter
         *                unknown-total mode.
         * @param flush   If @c true, flush stdout after redraw.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            const bool unknown_total = !(total > 0);
            const float frac = unknown_total
                                   ? 0.0f
                                   : std::clamp(static_cast<float>(current) /
                                                    static_cast<float>(total),
                                                0.0f, 1.0f);
            _set_main_progress(frac,
                               static_cast<int64_t>(current),
                               unknown_total ? kUnknownTotal : static_cast<int64_t>(total),
                               flush);
        }

        /**
         * @brief Drive the main bar with a raw fraction in [0, 1].
         *
         * Does NOT update the current/total counters — useful when the caller
         * only knows the fraction (e.g. cascaded child bar).
         *
         * @param fraction  Value in [0, 1]; clamped if out of range.
         * @param flush     If @c true, flush stdout after redraw.
         */
        void update(double fraction, bool flush = true);

        /**
         * @brief Customise the unit label shown next to the main-bar counter.
         *
         * Default is @c "tasks".  Beam-test framing typically uses @c "spills".
         * Set to an empty string to suppress the counter entirely.
         *
         * @param unit  New label (copied).
         * @param flush If @c true, flush stdout after redraw.
         */
        void set_unit(std::string unit, bool flush = true);

        /**
         * @brief Reset cycle-level state and restart the main-bar clock.
         *
         * Used by drivers that reuse the same multi-bar across logical
         * cycles (e.g. one cycle per spill).  Resets @c main_fraction_,
         * @c main_current_, the start time, and cascades into every
         * subtask's @ref SubtaskProgressBar::restart.
         *
         * Does NOT touch the subtask list itself — labels and ownership are
         * preserved across the restart.
         *
         * @param flush If @c true, flush stdout after redraw.
         */
        void restart(bool flush = true);

        /**
         * @brief Finalise the whole multi-bar and release its anchor slot.
         *
         * Commits the final frame (header + subtasks) as scrolling output
         * and stops participating in future @ref erase_all / @ref redraw_all
         * cycles.  Subsequent @c update or subtask updates reactivate it.
         *
         * @param flush  If @c true, flush stdout after committing.
         */
        void finish(bool flush = true);

        /**
         * @brief Switch the main bar to a plain text header line.
         *
         * Instead of the [PROGRESS] bar, the first line is rendered as
         * `[tag] msg`.  Useful when overall progress is not meaningful but
         * a running status label is needed above the subtask bars.
         * Calling set_header() again updates the text in-place.
         * Pass an empty @p tag to restore the default progress-bar mode.
         */
        void set_header(std::string tag, std::string_view msg = "", bool flush = true);

        /// @brief Sentinel value for @ref update(T, T) and @ref _set_main_progress
        ///        indicating that the total is not yet known.
        ///
        /// Pass as the @p total argument to enter unknown-total mode (no
        /// percentage or ETA shown, only current count and elapsed time).
        static constexpr int64_t kUnknownTotal = -1;

        /// @brief Returns @c true between the first update and @ref finish.
        [[nodiscard]] bool is_active() const { return active_; }

        // AnchorObject interface

        /**
         * @brief Number of terminal lines the multi-bar occupies right now
         *        (header + separator + one line per subtask).
         *
         * Returns the value cached at the last redraw — @c active_ controls
         * whether @ref render_line repaints, while this count drives erase.
         */
        [[nodiscard]] int rendered_line_count() const override
        {
            // Returns how many lines are currently on screen.
            // active_ controls redrawing (render_line), not erasing.
            return last_line_count_;
        }

        /// @brief Repaint the whole multi-bar block in place (anchor interface).
        void render_line() const override;

    private:
        friend class SubtaskProgressBar;

        using clock_t = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        /**
         * @brief Subtask-update callback invoked by @ref SubtaskProgressBar.
         *
         * Re-renders the multi-bar to reflect the subtask change.  Briefly
         * releases @p lk around the anchor calls to preserve the global lock
         * order registry → bar; see "Subtask callback notes" in the .cxx file.
         */
        void _subtask_updated_locked(const SubtaskProgressBar *who,
                                     std::unique_lock<std::mutex> &lk, bool flush);

        /**
         * @brief Mark a subtask complete and re-render.
         *
         * Increments @c finished_count_, sets @c who->active_ = false, then
         * re-renders.  Same lock-release pattern as @ref _subtask_updated_locked.
         */
        void _subtask_finished_locked(SubtaskProgressBar *who,
                                      std::unique_lock<std::mutex> &lk, bool flush);

        /// @brief Update the main-bar fraction under lock and trigger a redraw.
        ///
        /// Internal helper used by the @c update(double) overload.  Leaves
        /// @c main_current_ / @c main_total_ untouched.
        void _set_main_fraction(float frac, bool flush);

        /// @brief Update the main-bar fraction and counters atomically.
        ///
        /// @param frac     Clamped progress fraction in [0, 1].
        /// @param current  Current counter value (e.g. spill index).
        /// @param total    Total count, or @c -1 to enter unknown-total mode.
        /// @param flush    If @c true, flush stdout after redraw.
        void _set_main_progress(float frac, int64_t current, int64_t total, bool flush);

        /// @brief Recompute cached layout state for the next draw.
        /// @pre @c mutex_ must be held by the caller.
        void _update_state_locked(float frac);

        /// @brief Emit all multi-bar lines at the current cursor position.
        /// No cursor movement (the anchor framework owns that).  No-op when
        /// not attached to a TTY.
        /// @pre @c mutex_ must be held by the caller.
        void _draw_locked();

        /// @brief Append the rendered main-bar line (or header line in header
        ///        mode) to @p out.
        void _render_main(std::string &out) const;

        /// @brief Append one rendered subtask line for @p s to @p out.
        void _render_subtask(std::string &out, const SubtaskProgressBar &s) const;

        /// @brief Append @p line followed by a terminal-clear escape to @p out.
        static void _emit_line(std::string &out, const std::string &line, int term_width);

        /// @brief Best-effort terminal column count; falls back to 80.
        [[nodiscard]] static int _terminal_width();

        /// @brief Format a duration in "[Xh ]Ym Zs" style.
        [[nodiscard]] static std::string _format_duration(double seconds);

        mutable std::mutex mutex_; ///< Guards every per-instance member below.

        BarStyle   style_;       ///< Visual fill style applied to every line.
        bool        active_ = false; ///< True between first update and @ref finish.
        time_point  start_;       ///< Set when the multi-bar transitions to active.

        // header-only mode (replaces the main [PROGRESS] bar) — see @ref set_header.
        bool        header_mode_ = false;
        std::string header_tag_;
        std::string header_msg_;

        float main_fraction_ = 0.0f; ///< Last main-bar fraction (header line).
        int64_t main_current_ = 0;   ///< Current main counter (e.g. processed spills).
        int64_t main_total_ = kUnknownTotal; ///< Total main counter; <0 means unknown.
        std::string main_unit_ = "tasks"; ///< Label shown next to main counter.
        int finished_count_ = 0;     ///< Subtasks that have called @ref SubtaskProgressBar::finish.
        int total_subtasks_ = 0;     ///< Number of subtasks ever registered.

        std::vector<std::unique_ptr<SubtaskProgressBar>> subtasks_; ///< Owned subtask handles.

        int last_line_count_ = 0;    ///< Number of terminal lines the multi-bar took on the last draw.
        int tag_col_width_ = -1;     ///< Cached width of the tag column; -1 = needs recompute.
        mutable int suffix_width_ = -1; ///< Cached width of the main-bar suffix area.
    };

} // namespace mist::logger