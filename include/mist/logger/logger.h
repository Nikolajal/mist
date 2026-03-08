#pragma once
#include <mist/logger/logger_types.h>
#include <string_view>
#include <iostream>

namespace mist::logger
{
    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    void set_min_level(level_tag level);
    level_tag get_min_level();

    // ------------------------------------------------------------------
    // Internal detail — used by progress_bar to coordinate update mode.
    // Not intended for direct use.
    // ------------------------------------------------------------------
    namespace detail
    {
        void set_update_mode(bool active);
    }

    // ------------------------------------------------------------------
    // Logging
    // ------------------------------------------------------------------

    /**
     * @brief Log a message at the given level.
     *
     * Routes ERROR and WARNING to std::cerr, everything else to std::cout.
     * If a progress_bar is active, auto-commits it with a newline before
     * printing so output is never corrupted.
     */
    void log(level_tag tag, std::string_view msg, bool flush = true);

    /// Log with explicit colour and style — always goes to std::cout.
    void log(std::string_view msg,
             colour_tag c = colour_tag::RESET,
             std::initializer_list<style_tag> s = {style_tag::NONE});

    // ------------------------------------------------------------------
    // Convenience wrappers
    // ------------------------------------------------------------------

    inline void error(std::string_view msg, bool flush = true) { log(level_tag::ERROR, msg, flush); }
    inline void warning(std::string_view msg, bool flush = true) { log(level_tag::WARNING, msg, flush); }
    inline void info(std::string_view msg, bool flush = true) { log(level_tag::INFO, msg, flush); }
    inline void debug(std::string_view msg, bool flush = true) { log(level_tag::DEBUG, msg, flush); }
    inline void plain(std::string_view msg, bool flush = true) { log(level_tag::PLAIN, msg, flush); }

    // ------------------------------------------------------------------
    // Update / in-place line
    // ------------------------------------------------------------------

    /**
     * @brief Overwrite the current terminal line in place.
     *
     * Uses \033[2K (erase line) + \r (carriage return) to rewrite without
     * advancing to a new line. Respects the PROGRESS level filter.
     * Call end_update() when done; a normal log() call will auto-commit
     * if you forget.
     */
    void update(std::string_view msg, bool flush = true);
    void end_update(bool flush = true);

} // namespace mist::logger

// Cascade include — logger carries progress_bar along with it.
#include <mist/logger/progress_bar.h>
