#pragma once
#include <string>
#include <initializer_list>
#include <optional>
#include <sstream>

#if defined(__has_include) && __has_include(<unistd.h>)
#include <unistd.h>
#define MIST_HAS_ISATTY 1
#endif

namespace mist::logger
{
    // ------------------------------------------------------------------
    // Enumerations
    // ------------------------------------------------------------------

    /// @brief ANSI 8/16-colour foreground enumeration.
    /// Values match the raw SGR codes so they can be emitted directly.
    enum class colour_tag : int
    {
        // Foreground
        BLACK = 30,
        RED = 31,
        GREEN = 32,
        YELLOW = 33,
        BLUE = 34,
        MAGENTA = 35,
        CYAN = 36,
        WHITE = 37,
        // Foreground bright variants (non-standard but universally supported)
        BRIGHT_BLACK = 90,
        BRIGHT_RED = 91,
        BRIGHT_GREEN = 92,
        BRIGHT_YELLOW = 93,
        BRIGHT_BLUE = 94,
        BRIGHT_MAGENTA = 95,
        BRIGHT_CYAN = 96,
        BRIGHT_WHITE = 97,
        RESET = 0
    };

    /// @brief ANSI 8/16-colour background enumeration (same numbering as @ref colour_tag + 10).
    enum class bg_colour_tag : int
    {
        // Background
        BLACK = 40,
        RED = 41,
        GREEN = 42,
        YELLOW = 43,
        BLUE = 44,
        MAGENTA = 45,
        CYAN = 46,
        WHITE = 47,
        // Background bright variants
        BRIGHT_BLACK = 100,
        BRIGHT_RED = 101,
        BRIGHT_GREEN = 102,
        BRIGHT_YELLOW = 103,
        BRIGHT_BLUE = 104,
        BRIGHT_MAGENTA = 105,
        BRIGHT_CYAN = 106,
        BRIGHT_WHITE = 107
    };

    /// @brief ANSI SGR style attributes — combine in a brace-enclosed list passed to @ref ansi.
    /// @note Many of the rarer codes (FRAKTUR, ENCIRCLED, …) are not honoured by typical terminals.
    enum class style_tag : int
    {
        NONE = 0,
        // --- Widely supported
        BOLD = 1,
        DIM = 2, ///< reduced intensity, aka faint
        ITALIC = 3,
        UNDERLINE = 4,
        BLINK_SLOW = 5, ///< < 150 blinks/min; often ignored by terminals
        BLINK_FAST = 6, ///< >= 150 blinks/min; rarely supported
        REVERSED = 7,   ///< swap fg/bg colours
        HIDDEN = 8,     ///< aka conceal; useful for passwords
        STRIKETHROUGH = 9,
        // --- Font selection (SGR 10-20)
        DEFAULT_FONT = 10,
        ALT_FONT_1 = 11,
        ALT_FONT_2 = 12,
        ALT_FONT_3 = 13,
        ALT_FONT_4 = 14,
        ALT_FONT_5 = 15,
        ALT_FONT_6 = 16,
        ALT_FONT_7 = 17,
        ALT_FONT_8 = 18,
        ALT_FONT_9 = 19,
        FRAKTUR = 20,          ///< rarely supported
        DOUBLE_UNDERLINE = 21, ///< may disable BOLD on some terminals
        // --- Attribute resets
        RESET_BOLD_DIM = 22, ///< neither bold nor faint
        RESET_ITALIC = 23,
        RESET_UNDERLINE = 24, ///< neither single nor double underline
        RESET_BLINK = 25,
        RESET_REVERSED = 27,
        RESET_HIDDEN = 28,
        RESET_STRIKE = 29,
        // --- Rarely supported decorations
        FRAMED = 51,
        ENCIRCLED = 52,
        OVERLINED = 53,    ///< decent support in modern terminals
        RESET_FRAMED = 54, ///< neither framed nor encircled
        RESET_OVERLINED = 55,
        // --- Underline colour (Kitty, iTerm2, VTE)
        UNDERLINE_COLOUR = 58,
        RESET_UNDERLINE_COLOUR = 59
    };

    /// @brief Severity levels for @ref log.
    /// @c PLAIN is exempt from the level filter; ordering goes most → least severe.
    enum class level_tag : int
    {
        ERROR   = 0, ///< Fatal or near-fatal condition; written to stderr.
        WARNING = 1, ///< Recoverable anomaly; written to stderr.
        INFO    = 2, ///< Routine progress information; stdout.
        DEBUG   = 3, ///< Verbose diagnostic; stdout.
        PLAIN   = 4  ///< Unstyled output; bypasses the level filter.
    };

    // ------------------------------------------------------------------
    // Colour enabled state — shared across logger and progress_bar
    // ------------------------------------------------------------------

    /// @brief Override automatic TTY detection to force colour on or off.
    /// Thread-safe; visible immediately to subsequent @ref is_colour_enabled calls.
    void set_colour_enabled(bool enabled);

    /// @brief Returns true if ANSI colour output is enabled.
    /// Auto-detects via @c isatty() on first call unless overridden by
    /// @ref set_colour_enabled. Thread-safe.
    bool is_colour_enabled();

    /// @brief Returns true if both stdout and stderr are attached to a TTY.
    ///
    /// Independent of colour preference — used by @ref anchor_object to decide
    /// whether emitting cursor-control escapes is safe. When output is being
    /// piped or redirected to a file this returns @c false and the anchored-band
    /// machinery degrades to no-ops, keeping log files free of control bytes.
    /// Thread-safe; the underlying @c isatty probe is cached after the first call.
    bool is_tty();

    /// @brief Override the cached TTY state.
    ///
    /// Primarily useful for tests that capture @c std::cout / @c std::cerr via
    /// custom stream buffers — capturing makes @c isatty return @c false, which
    /// would otherwise suppress all bar / anchor rendering during the test.
    /// Pass @c true inside the test fixture, then restore the previous value
    /// on tear-down. Thread-safe.
    void set_tty(bool enabled);

    // ------------------------------------------------------------------
    // Core ANSI formatting
    // ------------------------------------------------------------------

    /**
     * @brief Build an ANSI SGR escape sequence.
     *
     * Returns an empty string when colour is disabled (e.g. output is
     * redirected to a file), so log output is never polluted with raw
     * escape codes.
     *
     * @param colour  Foreground colour.
     * @param styles  Brace-enclosed list of style attributes.
     *                Uses std::initializer_list to avoid heap allocation.
     * @param bg      Optional background colour.
     */
    std::string ansi(colour_tag colour = colour_tag::RESET,
                     std::initializer_list<style_tag> styles = {style_tag::NONE},
                     std::optional<bg_colour_tag> bg = std::nullopt);

} // namespace mist::logger
