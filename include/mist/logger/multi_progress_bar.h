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
    class subtask_progress_bar
    {
    public:
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

        void update(double fraction, bool flush = true);
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const { return active_; }
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
    class multi_progress_bar : public anchor_object
    {
    public:
        explicit multi_progress_bar(bar_style style = bar_style::BLOCK);

        multi_progress_bar(const multi_progress_bar &) = delete;
        multi_progress_bar &operator=(const multi_progress_bar &) = delete;
        multi_progress_bar(multi_progress_bar &&) = delete;
        multi_progress_bar &operator=(multi_progress_bar &&) = delete;

        ~multi_progress_bar();

        subtask_progress_bar &add_subtask(std::string tag);

        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        void update(T current, T total, bool flush = true)
        {
            if (total <= 0)
                return;
            const float frac = static_cast<float>(current) / static_cast<float>(total);
            _set_main_fraction(std::clamp(frac, 0.0f, 1.0f), flush);
        }

        void update(double fraction, bool flush = true);
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const { return active_; }

        // anchor_object interface
        [[nodiscard]] int rendered_line_count() const override
        {
            // Returns how many lines are currently on screen.
            // active_ controls redrawing (render_line), not erasing.
            return last_line_count_;
        }
        void render_line() const override;

    private:
        friend class subtask_progress_bar;

        using clock_t = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        void _subtask_updated_locked(const subtask_progress_bar *who,
                                     std::unique_lock<std::mutex> &lk, bool flush);
        void _subtask_finished_locked(subtask_progress_bar *who,
                                      std::unique_lock<std::mutex> &lk, bool flush);
        void _set_main_fraction(float frac, bool flush);

        // _update_state_locked: update internal fields (call with mutex_ held).
        // _draw_locked:         emit all bar lines at the current cursor position,
        //                       no cursor movement — anchor_object owns that.
        void _update_state_locked(float frac);
        void _draw_locked();

        void _render_main(std::string &out) const;
        void _render_subtask(std::string &out, const subtask_progress_bar &s) const;
        static void _emit_line(std::string &out, const std::string &line, int term_width);

        [[nodiscard]] static int _terminal_width();
        [[nodiscard]] static std::string _format_duration(double seconds);

        mutable std::mutex mutex_;

        bar_style style_;
        bool active_ = false;
        time_point start_;

        float main_fraction_ = 0.0f;
        int finished_count_ = 0;
        int total_subtasks_ = 0;

        std::vector<std::unique_ptr<subtask_progress_bar>> subtasks_;

        int last_line_count_ = 0;
        int tag_col_width_ = -1;
        mutable int suffix_width_ = -1;
        time_point last_render_;
    };

} // namespace mist::logger