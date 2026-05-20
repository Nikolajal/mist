/**
 * @file logger_types.cxx
 * @brief Implementation of colour/TTY detection helpers for the mist logger.
 *
 * The colour and TTY flags are kept as @c std::atomic so reads from multiple
 * threads are well-defined.  Lazy init for each flag uses an acquire-release
 * pair on a separate "checked" atomic so the visible value is the one that
 * was actually probed — a classic double-checked-locking-free pattern.
 */

#include <mist/logger/logger_types.h>
#include <atomic>

namespace mist::logger
{
    namespace
    {
        // Colour flag: separately overridable via set_colour_enabled().
        std::atomic<bool> g_colour_enabled{false};
        std::atomic<bool> g_colour_checked{false};

        // TTY flag: not overridable.  Probes isatty() once and caches.
        std::atomic<bool> g_is_tty{false};
        std::atomic<bool> g_tty_checked{false};
    }

    void set_colour_enabled(bool enabled)
    {
        // Order matters: store the value first, then mark as checked, so any
        // racing reader either sees the unchecked path (and probes again) or
        // sees the new value.
        g_colour_enabled.store(enabled, std::memory_order_release);
        g_colour_checked.store(true, std::memory_order_release);
    }

    bool is_colour_enabled()
    {
        if (!g_colour_checked.load(std::memory_order_acquire))
        {
#if defined(MIST_HAS_ISATTY)
            const bool detected = isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
#else
            const bool detected = false;
#endif
            // Race-tolerant init: multiple threads may all land here on first
            // call; they will all write the same value, then mark as checked.
            g_colour_enabled.store(detected, std::memory_order_release);
            g_colour_checked.store(true, std::memory_order_release);
        }
        return g_colour_enabled.load(std::memory_order_acquire);
    }

    bool is_tty()
    {
        if (!g_tty_checked.load(std::memory_order_acquire))
        {
#if defined(MIST_HAS_ISATTY)
            const bool detected = isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
#else
            const bool detected = false;
#endif
            g_is_tty.store(detected, std::memory_order_release);
            g_tty_checked.store(true, std::memory_order_release);
        }
        return g_is_tty.load(std::memory_order_acquire);
    }

    void set_tty(bool enabled)
    {
        // Mirror set_colour_enabled: store the value first, then mark as
        // checked so a racing reader either probes (and overwrites with the
        // probe result — harmless on first call) or sees the new value.
        g_is_tty.store(enabled, std::memory_order_release);
        g_tty_checked.store(true, std::memory_order_release);
    }

    std::string ansi(colour_tag colour,
                     std::initializer_list<style_tag> styles,
                     std::optional<bg_colour_tag> bg)
    {
        if (!is_colour_enabled())
            return {};

        std::ostringstream oss;
        oss << "\033[";
        for (style_tag s : styles)
        {
            // NONE (= 0) is SGR "reset all attributes" — emit it explicitly
            // so callers can use {style_tag::NONE} to clear bold/underline/etc
            // before applying a new colour. Previously this was skipped, which
            // caused attributes from a prior sequence to bleed into the next.
            oss << static_cast<int>(s) << ';';
        }
        if (bg.has_value())
            oss << static_cast<int>(bg.value()) << ';';
        oss << static_cast<int>(colour) << 'm';
        return oss.str();
    }

} // namespace mist::logger