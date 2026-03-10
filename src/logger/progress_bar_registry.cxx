#include <mist/logger/progress_bar_registry.h>
#include <mist/logger/progress_bar.h>
#include <mist/logger/multi_progress_bar.h>
#include <iostream>
#include <string>

#if defined(MIST_HAS_IOCTL)
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace mist::logger::detail
{
    // =========================================================================
    // Internal helpers
    // =========================================================================
    namespace
    {
        /** @brief Query terminal width — same helper as in progress_bar. */
        int terminal_width()
        {
#if defined(MIST_HAS_IOCTL)
            struct winsize w;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
                return static_cast<int>(w.ws_col);
#endif
            return 80;
        }

        /**
         * @brief Erase N lines upward from the current cursor position.
         *
         * The ANSI sequence used here:
         *  - `\r`         : move cursor to start of current line
         *  - `\033[2K`    : erase entire current line (EL — Erase in Line)
         *  - `\033[1A`    : move cursor up one line
         *
         * We repeat the up+erase for each line above the first, then erase
         * the top line and leave the cursor there ready for new output.
         *
         * This is equivalent to what Cargo and Ninja do for their progress
         * displays — it leaves no visual artifacts even if the previous render
         * was narrower than the terminal.
         */
        void erase_lines(int n)
        {
            if (n <= 0) return;
            std::string seq;
            seq.reserve(n * 12);
            // Erase current line first, then walk up erasing each one.
            seq += "\r\033[2K";
            for (int i = 1; i < n; ++i)
                seq += "\033[1A\r\033[2K";
            std::cout << seq << std::flush;
        }
    } // anonymous namespace


    // =========================================================================
    // bar_registry — singleton
    // =========================================================================
    progress_bar_registry& progress_bar_registry::instance()
    {
        // Meyers singleton — constructed once, destroyed at program exit.
        // Thread-safe in C++11 and later (magic statics).
        static progress_bar_registry inst;
        return inst;
    }

    // -------------------------------------------------------------------------
    // Registration
    // -------------------------------------------------------------------------
    void progress_bar_registry::register_bar(progress_bar* bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        current_.type       = active_bar_handle::kind::single;
        current_.ptr.single = bar;
    }

    void progress_bar_registry::register_bar(multi_progress_bar* bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        current_.type      = active_bar_handle::kind::multi;
        current_.ptr.multi = bar;
    }

    void progress_bar_registry::unregister_bar(progress_bar* bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (current_.type == active_bar_handle::kind::single &&
            current_.ptr.single == bar)
            current_ = {};
    }

    void progress_bar_registry::unregister_bar(multi_progress_bar* bar)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (current_.type == active_bar_handle::kind::multi &&
            current_.ptr.multi == bar)
            current_ = {};
    }

    // -------------------------------------------------------------------------
    // Erase / redraw  (must be called with mutex_ already held)
    // -------------------------------------------------------------------------
    void progress_bar_registry::erase_active_bar_locked()
    {
        // mutex_ is already held by the caller — do NOT re-lock.
        if (!current_.has_bar()) return;

        int lines = 0;
        if (current_.type == active_bar_handle::kind::single)
            lines = current_.ptr.single->rendered_line_count();
        else
            lines = current_.ptr.multi->rendered_line_count();

        erase_lines(lines);
    }

    void progress_bar_registry::redraw_active_bar_locked()
    {
        // mutex_ is already held by the caller — do NOT re-lock.
        if (!current_.has_bar()) return;

        if (current_.type == active_bar_handle::kind::single)
            current_.ptr.single->render_unlocked(false);
        else
            current_.ptr.multi->render_unlocked(false);
    }

    // -------------------------------------------------------------------------
    // Mutex exposure
    // -------------------------------------------------------------------------
    void progress_bar_registry::lock()   { mutex_.lock(); }
    void progress_bar_registry::unlock() { mutex_.unlock(); }

    bool progress_bar_registry::has_active_bar() const
    {
        // Intentionally not locking — snapshot read for quick checks.
        // The caller must not rely on this value remaining stable.
        return current_.has_bar();
    }

    // =========================================================================
    // log_print_guard
    // =========================================================================
    log_print_guard::log_print_guard()
    {
        // Lock the registry for the entire erase → print → redraw window.
        // This prevents any bar render from another thread from interleaving
        // between our erase and the caller's std::cout write.
        progress_bar_registry::instance().lock();
        progress_bar_registry::instance().erase_active_bar_locked();
    }

    log_print_guard::~log_print_guard()
    {
        progress_bar_registry::instance().redraw_active_bar_locked();
        progress_bar_registry::instance().unlock();
    }

} // namespace mist::logger::detail