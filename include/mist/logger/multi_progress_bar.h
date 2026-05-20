#pragma once
#include <mist/logger/progress_bar.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mist::logger
{
    class multi_progress_bar;

    // =========================================================================
    // subtask_progress_bar
    // =========================================================================

    /**
     * @brief One sub-line of a @ref multi_progress_bar, identified by a tag.
     *
     * Instances are owned by the parent @ref multi_progress_bar and obtained
     * via @ref multi_progress_bar::add_subtask.  Updating a subtask drives a
     * coordinated redraw through the parent so all subtask lines stay
     * properly aligned in the anchored band.
     *
     * @note Lifetime is tied to the parent.  Subtask references must not
     *       outlive the @ref multi_progress_bar that produced them.
     * @note Non-copyable, non-movable for the same reason — addresses are
     *       handed out to callers and stored in the registry.
     */
    class subtask_progress_bar
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

        /// @brief Returns @c true between the first @ref update and @ref finish.
        [[nodiscard]] bool is_active() const { return active_; }

        /// @brief This subtask's label, as passed to @ref multi_progress_bar::add_subtask.
        [[nodiscard]] const std::string &tag() const { return tag_; }

    private:
        friend class multi_progress_bar;

        subtask_progress_bar(std::string tag, multi_progress_bar &parent)
            : tag_(std::move(tag)), parent_(parent) {}

        subtask_progress_bar(const subtask_progress_bar &) = delete;
        subtask_progress_bar &operator=(const subtask_progress_bar &) = delete;
        subtask_progress_bar(subtask_progress_bar &&) = delete;
        subtask_progress_bar &operator=(subtask_progress_bar &&) = delete;

        void _update_impl(float fraction,
                          std::optional<int64_t> current,
                          std::optional<int64_t> total,
                          bool flush);

        std::string tag_;
        multi_progress_bar &parent_;

        float fraction_ = 0.0f;
        int64_t current_ = 0;
        int64_t total_ = 0;
        bool active_ = true;
    };

    // =========================================================================
    // multi_progress_bar
    // =========================================================================

    /**
     * @brief Composite progress bar: one main bar plus N tagged subtask lines.
     *
     * Renders a header bar (overall progress) followed by a labelled sub-line
     * for each subtask added via @ref add_subtask.  The whole block lives in
     * the anchored region at the bottom of the terminal and survives log
     * output via the @ref anchor_object machinery.
     *
     * @note Non-copyable, non-movable — subtasks hold a reference back to
     *       their parent, so the parent must have a stable address.
     */
    class multi_progress_bar : public anchor_object
    {
    public:
        /**
         * @brief Construct an empty multi-bar with no subtasks yet.
         * @param style  Visual fill style applied to all internal bars.
         */
        explicit multi_progress_bar(bar_style style = bar_style::BLOCK);

        multi_progress_bar(const multi_progress_bar &) = delete;
        multi_progress_bar &operator=(const multi_progress_bar &) = delete;
        multi_progress_bar(multi_progress_bar &&) = delete;
        multi_progress_bar &operator=(multi_progress_bar &&) = delete;

        /// @brief Destructor — deregisters from the anchor registry.
        ~multi_progress_bar();

        /**
         * @brief Register a new subtask line and return a reference to it.
         *
         * Subtasks are owned by the multi-bar and live until it is destroyed.
         * Calling @c add_subtask after the first @ref update is allowed; the
         * tag-column width is recomputed on the next redraw.
         *
         * @param tag  Label shown at the start of the subtask line.
         * @return     Reference to the new subtask handle (stable for the
         *             lifetime of this @ref multi_progress_bar).
         */
        subtask_progress_bar &add_subtask(std::string tag);

        /**
         * @brief Drive the main bar with an integral (current / total) pair.
         *
         * Affects only the header line; subtasks must be updated individually.
         * No-op if @p total is non-positive.
         *
         * @tparam T      Integral type.
         * @param current Progress so far.
         * @param total   Target count corresponding to 100 %.
         * @param flush   If @c true, flush stdout after redraw.
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0)
                return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _set_main_fraction(std::clamp(frac, 0.0f, 1.0f), flush);
        }

        /**
         * @brief Drive the main bar with a raw fraction in [0, 1].
         * @param fraction  Value in [0, 1]; clamped if out of range.
         * @param flush     If @c true, flush stdout after redraw.
         */
        void update(double fraction, bool flush = true);

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

        /// @brief Returns @c true between the first update and @ref finish.
        [[nodiscard]] bool is_active() const { return active_; }

        // anchor_object interface

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
        friend class subtask_progress_bar;

        using clock_t = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        /**
         * @brief Subtask-update callback invoked by @ref subtask_progress_bar.
         *
         * Re-renders the multi-bar to reflect the subtask change.  Briefly
         * releases @p lk around the anchor calls to preserve the global lock
         * order registry → bar; see "Subtask callback notes" in the .cxx file.
         */
        void _subtask_updated_locked(const subtask_progress_bar *who,
                                     std::unique_lock<std::mutex> &lk, bool flush);

        /**
         * @brief Mark a subtask complete and re-render.
         *
         * Increments @c finished_count_, sets @c who->active_ = false, then
         * re-renders.  Same lock-release pattern as @ref _subtask_updated_locked.
         */
        void _subtask_finished_locked(subtask_progress_bar *who,
                                      std::unique_lock<std::mutex> &lk, bool flush);

        /// @brief Update the main-bar fraction under lock and trigger a redraw.
        void _set_main_fraction(float frac, bool flush);

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
        void _render_subtask(std::string &out, const subtask_progress_bar &s) const;

        /// @brief Append @p line followed by a terminal-clear escape to @p out.
        static void _emit_line(std::string &out, const std::string &line, int term_width);

        /// @brief Best-effort terminal column count; falls back to 80.
        [[nodiscard]] static int _terminal_width();

        /// @brief Format a duration in "[Xh ]Ym Zs" style.
        [[nodiscard]] static std::string _format_duration(double seconds);

        mutable std::mutex mutex_; ///< Guards every per-instance member below.

        bar_style   style_;       ///< Visual fill style applied to every line.
        bool        active_ = false; ///< True between first update and @ref finish.
        time_point  start_;       ///< Set when the multi-bar transitions to active.

        // header-only mode (replaces the main [PROGRESS] bar) — see @ref set_header.
        bool        header_mode_ = false;
        std::string header_tag_;
        std::string header_msg_;

        float main_fraction_ = 0.0f; ///< Last main-bar fraction (header line).
        int finished_count_ = 0;     ///< Subtasks that have called @ref subtask_progress_bar::finish.
        int total_subtasks_ = 0;     ///< Number of subtasks ever registered.

        std::vector<std::unique_ptr<subtask_progress_bar>> subtasks_; ///< Owned subtask handles.

        int last_line_count_ = 0;    ///< Number of terminal lines the multi-bar took on the last draw.
        int tag_col_width_ = -1;     ///< Cached width of the tag column; -1 = needs recompute.
        mutable int suffix_width_ = -1; ///< Cached width of the main-bar suffix area.
    };

} // namespace mist::logger