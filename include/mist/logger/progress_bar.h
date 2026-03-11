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
     * Inherits anchor_object — registers itself on first render and
     * deregisters on finish(), so log() calls automatically erase and
     * redraw the bar region without any extra wiring.
     */
    class progress_bar : public anchor_object
    {
    public:
        explicit progress_bar(bar_style style = bar_style::BLOCK);
        ~progress_bar() override;

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
            anchor_object::erase_all();
            anchor_object::redraw_all();
            if (flush) std::cout << std::flush;
        }

        void update(double fraction, bool flush = true);
        void finish(bool flush = true);

        [[nodiscard]] bool is_active() const;

        // anchor_object interface
        [[nodiscard]] int rendered_line_count() const override;
        void render_line() const override;

    private:
        using clock_t    = std::chrono::steady_clock;
        using time_point = std::chrono::time_point<clock_t>;

        mutable std::mutex mutex_;

        bar_style style_;
        bool      active_       = false;
        int       suffix_width_ = -1;
        float     last_fraction_ = 0.0f;
        std::string            last_suffix_;
        std::optional<int64_t> last_current_;
        std::optional<int64_t> last_total_;
        time_point start_;

        [[nodiscard]] static int         terminal_width();
        [[nodiscard]] static std::string format_duration(double seconds);

        // _update_state: update all internal fields (call with mutex_ held).
        // _draw:         emit the bar line at the current cursor position,
        //                no cursor movement — anchor_object owns that.
        void _update_state(float fraction,
                           std::optional<int64_t> current,
                           std::optional<int64_t> total);
        void _draw() const;
    };

} // namespace mist::logger