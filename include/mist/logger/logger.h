#pragma once
#include <mist/logger/logger_types.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file logger.h
 * @brief Free-function logging interface for the mist::logger subsystem.
 *
 * ### Anchored objects
 * Any object that occupies a fixed band of terminal lines (progress bars,
 * multi-bars, status panels) inherits from anchor_object and registers itself
 * in a global list.  log() uses that list to know how many lines to erase
 * before printing and redraw after, keeping log output above the anchored
 * region at all times.
 */
namespace mist::logger
{
    // ------------------------------------------------------------------
    // Anchored-object registry
    // ------------------------------------------------------------------

    class anchor_object
    {
    public:
        anchor_object();
        virtual ~anchor_object();

        anchor_object(const anchor_object &) = delete;
        anchor_object &operator=(const anchor_object &) = delete;
        anchor_object(anchor_object &&) = delete;
        anchor_object &operator=(anchor_object &&) = delete;

        [[nodiscard]] virtual int rendered_line_count() const = 0;
        virtual void render_line() const = 0;

        static int total_anchored_lines();
        static void erase_all();
        static void redraw_all();

    private:
        static std::vector<anchor_object *> &_registry();
    };

    // ------------------------------------------------------------------
    // Level filter
    // ------------------------------------------------------------------

    void set_min_level(level_tag level);
    level_tag get_min_level();
    bool check_level(level_tag level);

    // ------------------------------------------------------------------
    // Core log functions
    // ------------------------------------------------------------------

    void log(level_tag tag, std::string_view msg, bool flush = true);
    void log(std::string_view msg,
             colour_tag c,
             std::initializer_list<style_tag> s = {style_tag::NONE});

    // ------------------------------------------------------------------
    // Convenience wrappers
    // ------------------------------------------------------------------

    inline void error  (const std::string &msg, bool flush = true) { log(level_tag::ERROR,   msg, flush); }
    inline void warning(const std::string &msg, bool flush = true) { log(level_tag::WARNING,  msg, flush); }
    inline void info   (const std::string &msg, bool flush = true) { log(level_tag::INFO,     msg, flush); }
    inline void debug  (const std::string &msg, bool flush = true) { log(level_tag::DEBUG,    msg, flush); }
    inline void plain  (const std::string &msg, bool flush = true) { log(level_tag::PLAIN,    msg, flush); }

    // ------------------------------------------------------------------
    // In-place update line
    // ------------------------------------------------------------------

    void update(std::string update_name, std::string_view msg, bool flush = true);
    void end_update(std::string update_name, bool flush = true);

} // namespace mist::logger
