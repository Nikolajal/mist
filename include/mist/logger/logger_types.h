#pragma once
#include <string>
#include <initializer_list>
#include <optional>
#include <sstream>

#if defined(__has_include) && __has_include(<unistd.h>)
#include <unistd.h>
#define MIST_HAS_ISATTY 1
#endif

/**
 * @file logger_types.h
 * @brief Enumerations and ANSI-escape helpers shared by every component of
 *        the @c mist::logger subsystem.
 *
 * Contains @ref mist::logger::ColourTag, @ref mist::logger::BgColourTag,
 * @ref mist::logger::StyleTag, and @ref mist::logger::LevelTag, plus the
 * core @ref mist::logger::ansi formatter.  No subsystem state — the global
 * colour/TTY toggles live alongside in @ref logger.h.
 */

namespace mist::logger
{
    // ------------------------------------------------------------------
    // Enumerations
    // ------------------------------------------------------------------

    /// @brief ANSI 8/16-colour foreground enumeration.
    /// Values match the raw SGR codes so they can be emitted directly.
    enum class ColourTag : int
    {
        // Foreground
        Black   = 30,
        Red     = 31,
        Green   = 32,
        Yellow  = 33,
        Blue    = 34,
        Magenta = 35,
        Cyan    = 36,
        White   = 37,
        // Foreground bright variants (non-standard but universally supported)
        BrightBlack   = 90,
        BrightRed     = 91,
        BrightGreen   = 92,
        BrightYellow  = 93,
        BrightBlue    = 94,
        BrightMagenta = 95,
        BrightCyan    = 96,
        BrightWhite   = 97,
        Reset = 0
    };

    /// @brief ANSI 8/16-colour background enumeration (same numbering as @ref ColourTag + 10).
    enum class BgColourTag : int
    {
        // Background
        Black   = 40,
        Red     = 41,
        Green   = 42,
        Yellow  = 43,
        Blue    = 44,
        Magenta = 45,
        Cyan    = 46,
        White   = 47,
        // Background bright variants
        BrightBlack   = 100,
        BrightRed     = 101,
        BrightGreen   = 102,
        BrightYellow  = 103,
        BrightBlue    = 104,
        BrightMagenta = 105,
        BrightCyan    = 106,
        BrightWhite   = 107
    };

    /// @brief ANSI SGR style attributes — combine in a brace-enclosed list passed to @ref ansi.
    /// @note Many of the rarer codes (FRAKTUR, ENCIRCLED, …) are not honoured by typical terminals.
    enum class StyleTag : int
    {
        None = 0,
        // --- Widely supported
        Bold      = 1,
        Dim       = 2, ///< reduced intensity, aka faint
        Italic    = 3,
        Underline = 4,
        BlinkSlow = 5, ///< < 150 blinks/min; often ignored by terminals
        BlinkFast = 6, ///< >= 150 blinks/min; rarely supported
        Reversed  = 7, ///< swap fg/bg colours
        Hidden    = 8, ///< aka conceal; useful for passwords
        Strikethrough = 9,
        // --- Font selection (SGR 10-20)
        DefaultFont = 10,
        AltFont1 = 11,
        AltFont2 = 12,
        AltFont3 = 13,
        AltFont4 = 14,
        AltFont5 = 15,
        AltFont6 = 16,
        AltFont7 = 17,
        AltFont8 = 18,
        AltFont9 = 19,
        Fraktur = 20,          ///< rarely supported
        DoubleUnderline = 21,  ///< may disable Bold on some terminals
        // --- Attribute resets
        ResetBoldDim = 22, ///< neither bold nor faint
        ResetItalic = 23,
        ResetUnderline = 24, ///< neither single nor double underline
        ResetBlink = 25,
        ResetReversed = 27,
        ResetHidden = 28,
        ResetStrike = 29,
        // --- Rarely supported decorations
        Framed = 51,
        Encircled = 52,
        Overlined = 53,    ///< decent support in modern terminals
        ResetFramed = 54,  ///< neither framed nor encircled
        ResetOverlined = 55,
        // --- Underline colour (Kitty, iTerm2, VTE)
        UnderlineColour = 58,
        ResetUnderlineColour = 59
    };

    /// @brief Severity levels for @ref log.
    /// @c PLAIN is exempt from the level filter; ordering goes most → least severe.
    enum class LevelTag : int
    {
        Error   = 0, ///< Fatal or near-fatal condition; written to stderr.
        Warning = 1, ///< Recoverable anomaly; written to stderr.
        Info    = 2, ///< Routine progress information; stdout.
        Debug   = 3, ///< Verbose diagnostic; stdout.
        Plain   = 4  ///< Unstyled output; bypasses the level filter.
    };

    // ------------------------------------------------------------------
    // Colour enabled state — shared across logger and ProgressBar
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
    /// Independent of colour preference — used by @ref AnchorObject to decide
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
    std::string ansi(ColourTag colour = ColourTag::Reset,
                     std::initializer_list<StyleTag> styles = {StyleTag::None},
                     std::optional<BgColourTag> bg = std::nullopt);

} // namespace mist::logger
