#include <mist/logger/logger.h>
#include <algorithm>
#include <map>
#include <set>

namespace mist::logger
{
    // =========================================================================
    // File-local RAII guard — erase anchors before printing, redraw after.
    // Used by log() so every log call automatically keeps anchors intact.
    // =========================================================================
    struct log_print_guard
    {
        log_print_guard()  { anchor_object::erase_all();   }
        ~log_print_guard() { anchor_object::redraw_all();  }

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
        const int n = total_anchored_lines();
        if (n <= 0) return;
        // Each anchor ends its last line with '\n', so the cursor is one line
        // below the bottom anchor.  Move up (n) lines erasing each one.
        for (int i = 0; i < n; ++i)
            std::cout << "\033[1A\r\033[2K";
    }

    void anchor_object::redraw_all()
    {
        // Cursor is at the start of the line where anchors should begin.
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

        [[nodiscard]] int rendered_line_count() const override
        {
            // 0 until render_line() has been called at least once.
            return rendered_ ? 1 : 0;
        }

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
    level_tag get_min_level()           { return g_min_level;  }

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
        if (!check_level(tag)) return;

        const bool use_cerr = (tag == level_tag::ERROR || tag == level_tag::WARNING);
        std::ostream &out = use_cerr ? std::cerr : std::cout;

        std::string styled_msg;
        switch (tag)
        {
        case level_tag::ERROR:
            styled_msg = ansi(colour_tag::RED,         {style_tag::BOLD, style_tag::UNDERLINE}) + "[ERROR]"
                       + ansi(colour_tag::RED,         {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::WARNING:
            styled_msg = ansi(colour_tag::YELLOW,      {style_tag::BOLD, style_tag::UNDERLINE}) + "[WARNING]"
                       + ansi(colour_tag::YELLOW,      {style_tag::NONE}) + " " + std::string(msg) + ansi();
            break;
        case level_tag::INFO:
            styled_msg = ansi(colour_tag::BRIGHT_BLUE, {style_tag::BOLD, style_tag::UNDERLINE}) + "[INFO]"
                       + ansi(colour_tag::BRIGHT_BLUE, {style_tag::NONE}) + "    " + std::string(msg) + ansi();
            break;
        case level_tag::DEBUG:
            styled_msg = ansi(colour_tag::CYAN,        {style_tag::BOLD, style_tag::UNDERLINE}) + "[DEBUG]"
                       + ansi(colour_tag::CYAN,        {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::PLAIN:
        default:
            styled_msg = ansi(colour_tag::WHITE, {style_tag::NONE}) + std::string(msg) + ansi();
            break;
        }

        {
            log_print_guard guard;  // erase anchors, print, redraw
            out << styled_msg << '\n';
            if (flush) out << std::flush;
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

        if (ended.count(update_name))
        {
            ended.erase(update_name);
            log(level_tag::WARNING,
                "update(\"" + update_name + "\") called after end_update() — recreating anchor.");
        }

        auto [it, inserted] = anchors.try_emplace(update_name, update_name);
        it->second.set_msg(std::string(msg));

        anchor_object::erase_all();
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

    void end_update(std::string update_name, bool flush)
    {
        auto &anchors = _update_anchors();
        auto it = anchors.find(update_name);
        if (it == anchors.end()) return;

        const std::string last_msg = it->second.last_msg();

        anchor_object::erase_all();
        anchors.erase(it);  // ~update_anchor deregisters from _registry()
        _ended_names().insert(update_name);

        std::cout << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                  << "[" << update_name << "]"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                  << " " << last_msg << ansi() << '\n';
        anchor_object::redraw_all();
        if (flush) std::cout << std::flush;
    }

} // namespace mist::logger
