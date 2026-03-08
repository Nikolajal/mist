#include <mist/logger/logger_types.h>

namespace mist::logger
{
    namespace
    {
        bool g_colour_enabled = false;
        bool g_colour_checked = false;
    }

    void set_colour_enabled(bool enabled)
    {
        g_colour_enabled = enabled;
        g_colour_checked = true;
    }

    bool is_colour_enabled()
    {
        if (!g_colour_checked)
        {
            g_colour_checked = true;
#if defined(MIST_HAS_ISATTY)
            g_colour_enabled = isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
#else
            g_colour_enabled = false;
#endif
        }
        return g_colour_enabled;
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