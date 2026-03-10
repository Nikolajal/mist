#include <mist/logger/logger.h>
#include <mist/logger/progress_bar_registry.h>

namespace mist::logger
{
    namespace
    {
        level_tag g_min_level    = level_tag::DEBUG;
        bool      g_in_update_mode = false;
    }

    namespace detail
    {
        // Kept for progress_bar.cpp compatibility — progress_bar still calls
        // this to signal that a plain update() line is active (not a bar).
        // Once progress_bar is fully migrated to the registry this can be removed.
        void set_update_mode(bool active) { g_in_update_mode = active; }
    }

    void set_min_level(level_tag level) { g_min_level = level; }
    level_tag get_min_level()           { return g_min_level; }

    void log(level_tag tag, std::string_view msg, bool flush)
    {
        if (static_cast<int>(tag) > static_cast<int>(g_min_level))
            return;

        const bool use_cerr = (tag == level_tag::ERROR || tag == level_tag::WARNING);
        std::ostream& out = use_cerr ? std::cerr : std::cout;

        std::string styled_msg;
        switch (tag)
        {
        case level_tag::ERROR:
            styled_msg = ansi(colour_tag::RED,          {style_tag::BOLD, style_tag::UNDERLINE}) + "[ERROR]"   + ansi(colour_tag::RED,          {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::WARNING:
            styled_msg = ansi(colour_tag::YELLOW,       {style_tag::BOLD, style_tag::UNDERLINE}) + "[WARNING]" + ansi(colour_tag::YELLOW,       {style_tag::NONE}) + " "   + std::string(msg) + ansi();
            break;
        case level_tag::INFO:
            styled_msg = ansi(colour_tag::BRIGHT_BLUE,  {style_tag::BOLD, style_tag::UNDERLINE}) + "[INFO]"    + ansi(colour_tag::BRIGHT_BLUE,  {style_tag::NONE}) + "    " + std::string(msg) + ansi();
            break;
        case level_tag::DEBUG:
            styled_msg = ansi(colour_tag::CYAN,         {style_tag::BOLD, style_tag::UNDERLINE}) + "[DEBUG]"   + ansi(colour_tag::CYAN,         {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::PLAIN:
        default:
            styled_msg = ansi(colour_tag::WHITE, {style_tag::NONE}) + std::string(msg) + ansi();
            break;
        }

        // Bar-aware print: erase active bar → print → redraw bar.
        {
            detail::log_print_guard guard;
            out << styled_msg << '\n';
            if (flush)
                out << std::flush;
        }
    }

    void log(std::string_view msg, colour_tag c, std::initializer_list<style_tag> s)
    {
        detail::log_print_guard guard;
        std::cout << ansi(c, s) << msg << ansi() << '\n';
    }

    void update(std::string_view msg, bool flush)
    {
        if (static_cast<int>(level_tag::PROGRESS) > static_cast<int>(g_min_level))
            return;

        std::cout << "\033[2K\r"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                  << "[PROGRESS]"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                  << " " << msg << ansi();

        g_in_update_mode = true;
        if (flush)
            std::cout << std::flush;
    }

    void end_update(bool flush)
    {
        if (!g_in_update_mode)
            return;
        std::cout << '\n';
        if (flush)
            std::cout << std::flush;
        g_in_update_mode = false;
    }

} // namespace mist::logger