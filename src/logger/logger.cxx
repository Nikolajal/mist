#include <mist/logger/logger.h>
#include <algorithm>
#include <map>
#include <set>

namespace mist::logger
{
    // =========================================================================
    // File-local RAII guard — erase anchors before printing, redraw after.
    // Replaces the old detail::log_print_guard from the now-deleted progress_bar_registry.
    // =========================================================================
    struct log_print_guard
    {
        log_print_guard()  { anchor_object::erase_all(); }
        ~log_print_guard() { anchor_object::redraw_all(); }

        log_print_guard(const log_print_guard &) = delete;
        log_print_guard &operator=(const log_print_guard &) = delete;
    };
    // =========================================================================
    // anchor_object registry
    // =========================================================================

    std::vector<anchor_object *> &anchor_object::_registry()
    {
        static std::vector<anchor_object *> reg;
        return reg;
    }

    anchor_object::anchor_object()
    {
        _registry().push_back(this);
    }

    anchor_object::~anchor_object()
    {
        auto &reg = _registry();
        reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());
    }

    int anchor_object::total_anchored_lines()
    {
        int total = 0;
        for (const anchor_object *obj : _registry())
            total += obj->rendered_line_count();
        return total;
    }

    void anchor_object::erase_all()
    {
        // Each anchor emitted a '\n' after its last line, so the cursor is
        // currently sitting one line BELOW the bottom anchor. We need to:
        //   1. move up (total_lines) times to reach the top anchor line
        //   2. erase each line on the way back down
        // leaving the cursor at the start of the topmost anchor line.
        const int n = total_anchored_lines();
        if (n <= 0)
            return;
        for (int i = 0; i < n; ++i)
            std::cout << "\033[1A\r\033[2K";   // up one line, go to col 0, erase
    }

    void anchor_object::redraw_all()
    {
        // Cursor is at the start of the line where anchors should begin.
        // Ask each anchor to reprint itself (with a trailing '\n' so the
        // next anchor starts on a fresh line).
        for (anchor_object *obj : _registry())
            obj->render_line();
    }

    // =========================================================================
    // update_anchor — concrete anchor for a single named update line
    // =========================================================================

    class update_anchor : public anchor_object
    {
    public:
        explicit update_anchor(std::string name) : name_(std::move(name)) {}

        // Returns 0 until render_line() has been called at least once.
        // This prevents erase_all() from moving the cursor up for lines
        // that were never actually printed to the terminal.
        [[nodiscard]] int rendered_line_count() const override { return rendered_ ? 1 : 0; }

        void render_line() const override
        {
            rendered_ = true;
            std::cout << "\r\033[2K"
                      << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                      << "[" << name_ << "]"
                      << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                      << " " << last_msg_ << ansi()
                      << '\n';
        }

        void set_msg(std::string msg) { last_msg_ = std::move(msg); }
        [[nodiscard]] const std::string &last_msg() const { return last_msg_; }

    private:
        std::string name_;
        std::string last_msg_;
        mutable bool rendered_ = false;
    };

    static std::map<std::string, update_anchor> &_update_anchors()
    {
        static std::map<std::string, update_anchor> m;
        return m;
    }

    // Tracks names that have been end_update()'d, so a subsequent update()
    // call on the same name can warn the user before recreating the anchor.
    static std::set<std::string> &_ended_names()
    {
        static std::set<std::string> s;
        return s;
    }

    // =========================================================================
    // Globals
    // =========================================================================

    namespace
    {
        level_tag g_min_level = level_tag::DEBUG;
    }

    void set_min_level(level_tag level) { g_min_level = level; }
    level_tag get_min_level() { return g_min_level; }

    bool check_level(level_tag tag)
    {
        if (tag == level_tag::PLAIN || tag == level_tag::PROGRESS)
            return true;
        return static_cast<int>(tag) <= static_cast<int>(g_min_level);
    }

    // =========================================================================
    // log
    // =========================================================================

    void log(level_tag tag, std::string_view msg, bool flush)
    {
        if (!check_level(tag))
            return;

        const bool use_cerr = (tag == level_tag::ERROR || tag == level_tag::WARNING);
        std::ostream &out = use_cerr ? std::cerr : std::cout;

        std::string styled_msg;
        switch (tag)
        {
        case level_tag::ERROR:
            styled_msg = ansi(colour_tag::RED,         {style_tag::BOLD, style_tag::UNDERLINE}) + "[ERROR]"   + ansi(colour_tag::RED,         {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::WARNING:
            styled_msg = ansi(colour_tag::YELLOW,      {style_tag::BOLD, style_tag::UNDERLINE}) + "[WARNING]" + ansi(colour_tag::YELLOW,      {style_tag::NONE}) + " "   + std::string(msg) + ansi();
            break;
        case level_tag::INFO:
            styled_msg = ansi(colour_tag::BRIGHT_BLUE, {style_tag::BOLD, style_tag::UNDERLINE}) + "[INFO]"    + ansi(colour_tag::BRIGHT_BLUE, {style_tag::NONE}) + "    " + std::string(msg) + ansi();
            break;
        case level_tag::DEBUG:
            styled_msg = ansi(colour_tag::CYAN,        {style_tag::BOLD, style_tag::UNDERLINE}) + "[DEBUG]"   + ansi(colour_tag::CYAN,        {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::PLAIN:
        default:
            styled_msg = ansi(colour_tag::WHITE, {style_tag::NONE}) + std::string(msg) + ansi();
            break;
        }

        {
            log_print_guard guard;   // erase anchors, print, redraw
            out << styled_msg << '\n';
            if (flush)
                out << std::flush;
        }
    }

    void log(std::string_view msg, colour_tag c, std::initializer_list<style_tag> s)
    {
        log_print_guard guard;
        std::cout << ansi(c, s) << msg << ansi() << '\n';
    }

    // =========================================================================
    // update / end_update
    // =========================================================================

    void update(std::string update_name, std::string_view msg, bool flush)
    {
        auto &anchors = _update_anchors();
        auto &ended   = _ended_names();

        // Warn once if this name was previously end_update()'d, then recreate.
        if (ended.count(update_name))
        {
            ended.erase(update_name);
            log(level_tag::WARNING,
                "update(\"" + update_name + "\") called after end_update() — recreating anchor.");
        }

        // Insert anchor on first call; get a reference to it either way.
        auto [it, inserted] = anchors.try_emplace(update_name, update_name);
        it->second.set_msg(std::string(msg));

        // Erase all anchor lines, redraw them all (this anchor now has the
        // updated message stored, so redraw_all() will print the new text).
        anchor_object::erase_all();
        anchor_object::redraw_all();

        if (flush)
            std::cout << std::flush;
    }

    void end_update(std::string update_name, bool flush)
    {
        auto &anchors = _update_anchors();
        auto it = anchors.find(update_name);
        if (it == anchors.end())
            return;

        // Capture the last message before erasing the anchor.
        const std::string last_msg = it->second.last_msg();

        // Erase all anchor lines, remove this anchor.
        anchor_object::erase_all();
        anchors.erase(it);              // ~update_anchor deregisters it
        _ended_names().insert(update_name);

        // Commit the final state of this update as a permanent scrolling line,
        // then redraw any remaining anchors below it.
        std::cout << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                  << "[" << update_name << "]"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                  << " " << last_msg << ansi() << '\n';
        anchor_object::redraw_all();    // redraws everything except the removed anchor

        if (flush)
            std::cout << std::flush;
    }

} // namespace mist::logger