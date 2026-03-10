#pragma once
#include <mist/logger/progress_bar.h>
#include <mist/logger/progress_bar_registry.h>
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
     * @brief Lightweight non-owning handle to one subtask row inside a
     *        multi_progress_bar group.
     *
     * Holds a tag string and a back-reference to its parent. Every mutating
     * call (update, finish) is delegated to the parent, which holds the single
     * mutex that guards the entire group.
     *
     * @warning The parent multi_progress_bar must outlive all subtask handles.
     *          Calling any method after the parent is destroyed is UB.
     *
     * @code{.cpp}
     * auto& s = mb.add_subtask("Hit finding");
     * for (int i = 0; i <= n; ++i) { s.update(i, n); do_work(i); }
     * s.finish();   // collapses row; auto-ticks main bar by 1/N
     * @endcode
     */
    class subtask_progress_bar
    {
    public:
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0) return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _update_impl(std::clamp(frac, 0.0f, 1.0f),
                         static_cast<int64_t>(current),
                         static_cast<int64_t>(total),
                         flush);
        }

        /** @brief Drive by pre-computed fraction in [0.0, 1.0]. */
        void update(double fraction, bool flush = true);

        /**
         * @brief Mark this subtask complete.
         * Removes its row (lines collapse upward) and auto-ticks the main bar.
         * Safe to call multiple times — no-op after the first.
         */
        void finish(bool flush = true);

        [[nodiscard]] bool              is_active() const { return active_; }
        [[nodiscard]] const std::string& tag()      const { return tag_; }

    private:
        friend class multi_progress_bar;

        subtask_progress_bar(std::string tag, multi_progress_bar& parent)
            : tag_(std::move(tag)), parent_(parent) {}

        subtask_progress_bar(const subtask_progress_bar&)            = delete;
        subtask_progress_bar& operator=(const subtask_progress_bar&) = delete;
        subtask_progress_bar(subtask_progress_bar&&)                 = delete;
        subtask_progress_bar& operator=(subtask_progress_bar&&)      = delete;

        void _update_impl(float fraction,
                          std::optional<int64_t> current,
                          std::optional<int64_t> total,
                          bool flush);

        std::string         tag_;
        multi_progress_bar& parent_;

        float   fraction_ = 0.0f;
        int64_t current_  = 0;
        int64_t total_    = 0;
        bool    active_   = true;
    };


    // =========================================================================
    // multi_progress_bar
    // =========================================================================
    /**
     * @brief Composite progress display: one main bar with N collapsible
     *        subtask rows rendered below it.
     *
     * ### Rendering model
     * The group owns a terminal region of:
     *   - 1 line  : main bar
     *   - 1 line  : separator  (only when subtasks are active)
     *   - K lines : one per active subtask  (K ∈ [0, N])
     *
     * On every render the cursor moves up by `last_line_count_` using
     * `\033[{n}A`, then all lines are redrawn in-place. When a subtask
     * finishes, K shrinks by one and the region contracts on the next render.
     *
     * ### Log interleaving
     * Registered with the global `bar_registry` on first render. Any call to
     * `mist::logger::info()` / `warning()` / `error()` while this group is
     * active will atomically erase the bar region, print the log line, and
     * redraw the bar below it. No call-site changes are needed.
     *
     * ### Progress driving
     *  - **Auto**: each `subtask.finish()` increments an internal counter and
     *    maps it to `finished/total_subtasks` on the main bar.
     *  - **Manual override**: call `update()` directly on the multi_progress_bar
     *    at any time to set the main bar fraction explicitly.
     *
     * ### Thread safety
     * A single `std::mutex` on `multi_progress_bar` serialises all writes
     * from any subtask handle or direct caller.
     *
     * ### Lock acquisition order
     * Always:
     *   1. `bar_registry::mutex_`      (held by `log_print_guard`)
     *   2. `multi_progress_bar::mutex_` (held by render / update / finish)
     *
     * `render_unlocked()` is called by the registry while holding (1) but
     * not (2) — it writes directly to stdout without re-acquiring mutex_.
     *
     * @code{.cpp}
     * mist::logger::multi_progress_bar mb;
     * auto& calib = mb.add_subtask("Calibration");
     * auto& hits  = mb.add_subtask("Hit finding");
     *
     * calib.update(42, 1000);
     * hits.update(0.17);
     * calib.finish();          // row collapses; main bar → 1/2
     *
     * mist::logger::info("Still going…");  // bar erased, redrawn automatically
     *
     * mb.finish();
     * @endcode
     */
    class multi_progress_bar
    {
    public:
        explicit multi_progress_bar(bar_style style = bar_style::BLOCK);

        multi_progress_bar(const multi_progress_bar&)            = delete;
        multi_progress_bar& operator=(const multi_progress_bar&) = delete;
        multi_progress_bar(multi_progress_bar&&)                 = delete;
        multi_progress_bar& operator=(multi_progress_bar&&)      = delete;

        ~multi_progress_bar();

        // --- subtask management ----------------------------------------------

        /**
         * @brief Register a new subtask and return a stable handle to it.
         *
         * The returned reference is valid for the lifetime of this
         * multi_progress_bar. Subtasks are rendered in registration order.
         *
         * @param tag  Short label shown left of the bar (e.g. "Hit finding").
         *             Padded/truncated to a fixed width so all bars align.
         */
        subtask_progress_bar& add_subtask(std::string tag);

        // --- main bar manual override ----------------------------------------

        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0) return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _set_main_fraction(std::clamp(frac, 0.0f, 1.0f), flush);
        }

        void update(double fraction, bool flush = true);

        /**
         * @brief Commit the entire group.
         *
         * Fills main bar to 100%, clears remaining subtask rows, emits a
         * newline, and unregisters from the global registry.
         * Safe to call multiple times — no-op after the first.
         */
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const { return active_; }

        // -----------------------------------------------------------------
        // Registry callbacks
        // Called by bar_registry with its own mutex already held.
        // MUST NOT acquire mutex_ — see lock acquisition order above.
        // -----------------------------------------------------------------

        /**
         * @brief Total terminal lines currently occupied by this group.
         * = 1 (main) + (active_subtasks > 0 ? 1 + active_subtasks : 0)
         * Returns 0 if never rendered or already finished.
         */
        [[nodiscard]] int rendered_line_count() const
        {
            if (last_line_count_ == 0 || !active_) return 0;
            return last_line_count_;
        }

        /**
         * @brief Redraw all lines without acquiring mutex_.
         *
         * Called by the registry after a log line has been printed, while
         * the registry mutex is held. Acquiring mutex_ here would deadlock
         * if a worker thread is mid-render (holds mutex_, blocked on the
         * registry lock).
         */
        void render_unlocked(bool flush);

    private:
        friend class subtask_progress_bar;

        using clock_t    = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        // Called (under lock) by subtask_progress_bar::_update_impl
        void _subtask_updated_locked(const subtask_progress_bar* who, bool flush);

        // Called (under lock) by subtask_progress_bar::finish
        void _subtask_finished_locked(subtask_progress_bar* who, bool flush);

        void _set_main_fraction(float frac, bool flush);

        // Redraws everything — must be called with mutex_ held.
        void _render_all_locked(bool flush);

        // Render helpers — write into a string buffer, no locking.
        void _render_main(std::string& out) const;
        void _render_subtask(std::string& out, const subtask_progress_bar& s) const;
        static void _emit_line(std::string& out, const std::string& line, int term_width);

        [[nodiscard]] static int         _terminal_width();
        [[nodiscard]] static std::string _format_duration(double seconds);

        mutable std::mutex mutex_;

        bar_style  style_;
        bool       active_          = false;
        time_point start_;

        float main_fraction_  = 0.0f;
        int   finished_count_ = 0;
        int   total_subtasks_ = 0;

        std::vector<std::unique_ptr<subtask_progress_bar>> subtasks_;

        // Render bookkeeping
        int last_line_count_ = 0;
        int tag_col_width_   = -1;  ///< max tag length + padding; -1 = uninit
        int suffix_width_    = -1;  ///< elapsed/eta suffix width;  -1 = uninit
    };

} // namespace mist::logger