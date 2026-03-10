#pragma once
#include <mist/logger/logger_types.h>
#include <mist/logger/progress_bar_registry.h>
#include <iostream>
#include <string>
#include <string_view>

/**
 * @file logger.h
 * @brief Free-function logging interface for the mist::logger subsystem.
 *
 * ### Bar-aware printing
 * All log functions are aware of any active `progress_bar` or
 * `multi_progress_bar`. Before printing, the active bar region is erased from
 * the terminal; after printing, it is redrawn below the new line. This keeps
 * log output readable while bars float at the bottom of the terminal.
 *
 * The entire erase → print → redraw sequence is atomic with respect to the
 * bar render mutex, so concurrent updates from worker threads cannot produce
 * visual artifacts.
 *
 * No call-site changes are needed — existing mist::logger::log() calls
 * just work correctly whether or not a bar is active.
 */
namespace mist::logger
{
    // ------------------------------------------------------------------
    // Level filter
    // ------------------------------------------------------------------

    void      set_min_level(level_tag level);
    level_tag get_min_level();

    // ------------------------------------------------------------------
    // Core log function (implementation in logger.cpp)
    // ------------------------------------------------------------------

    /**
     * @brief Emit a styled log line at the given level.
     *
     * Bar-aware: acquires the registry lock, erases any active bar, prints
     * the message, then redraws the bar. Thread-safe.
     */
    void log(level_tag tag, std::string_view msg, bool flush = true);

    /** @brief Emit an unstyled message with custom colour/style. */
    void log(std::string_view msg,
             colour_tag c,
             std::initializer_list<style_tag> s = {style_tag::NONE});

    // ------------------------------------------------------------------
    // Convenience wrappers — thin inline delegates to log()
    // ------------------------------------------------------------------

    inline void error  (const std::string& msg, bool flush = true) { log(level_tag::ERROR,   msg, flush); }
    inline void warning(const std::string& msg, bool flush = true) { log(level_tag::WARNING, msg, flush); }
    inline void info   (const std::string& msg, bool flush = true) { log(level_tag::INFO,    msg, flush); }
    inline void debug  (const std::string& msg, bool flush = true) { log(level_tag::DEBUG,   msg, flush); }

    // ------------------------------------------------------------------
    // In-place update line (for simple single-line progress, not bars)
    // ------------------------------------------------------------------

    void update    (std::string_view msg, bool flush = true);
    void end_update(bool flush = true);

} // namespace mist::logger