/**
 * @file logger.cxx
 * @brief Implementation of the @ref mist::logger free-function logging API and
 *        the global anchored-object registry.
 *
 * The registry, the level filter, and the named-update map are all guarded by
 * a single recursive mutex (returned by @ref anchor_object::registry_lock).
 * Lock order across the codebase is registry → bar (i.e. @ref render_line
 * overrides may acquire their own internal mutex while the registry mutex is
 * held by the caller of @ref redraw_all).
 */

#include <mist/logger/logger.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <optional>

namespace mist::logger
{
    // =========================================================================
    // File-local RAII guard — erase anchors before printing, redraw after.
    // Acquires the global registry mutex on construction so the entire
    // erase → print → redraw sequence is observed atomically by other threads.
    // =========================================================================
    struct log_print_guard
    {
        std::unique_lock<std::recursive_mutex> lk_;

        log_print_guard()
            : lk_(anchor_object::registry_lock())
        {
            anchor_object::erase_all();
        }

        ~log_print_guard()
        {
            anchor_object::redraw_all();
            // Lock released as lk_ destructs.
        }

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

    std::recursive_mutex &anchor_object::_registry_mutex()
    {
        static std::recursive_mutex m;
        return m;
    }

    std::unique_lock<std::recursive_mutex> anchor_object::registry_lock()
    {
        return std::unique_lock<std::recursive_mutex>(_registry_mutex());
    }

    anchor_object::anchor_object()
    {
        auto lk = registry_lock();
        _registry().push_back(this);
    }

    anchor_object::~anchor_object()
    {
        auto lk = registry_lock();
        auto &reg = _registry();
        reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());
    }

    int anchor_object::total_anchored_lines()
    {
        auto lk = registry_lock();
        int total = 0;
        for (const anchor_object *obj : _registry())
            total += obj->rendered_line_count();
        return total;
    }

    void anchor_object::erase_all()
    {
        auto lk = registry_lock();
        // Cursor-control escapes only make sense on a TTY.  When redirected to
        // a file or piped, suppress them entirely so log files stay clean.
        if (!is_tty())
            return;
        // Each anchor emitted a '\n' after its last line, so the cursor is
        // currently sitting one line BELOW the bottom anchor. We need to:
        //   1. move up (total_lines) times to reach the top anchor line
        //   2. erase each line on the way back down
        // leaving the cursor at the start of the topmost anchor line.
        const int n = total_anchored_lines();
        if (n <= 0)
            return;
        for (int i = 0; i < n; ++i)
            std::cout << "\033[1A\r\033[2K"; // up one line, go to col 0, erase
    }

    void anchor_object::redraw_all()
    {
        auto lk = registry_lock();
        // On a non-TTY destination the anchored band is not maintained — each
        // log line already prints with its own '\n' and no in-place updates
        // are possible. Skip the redraw entirely.
        if (!is_tty())
            return;
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

    // =========================================================================
    // Named-update bookkeeping
    //
    // The single source of truth for whether a name is "live" or "ended" is
    // the @c update_anchor_state struct below. Previously this used two
    // separate containers and the invariant ("a name is ended iff it's in the
    // ended set") was only implicit — see B10 in the mist audit.
    //
    // Both helpers are accessed only with the global registry mutex held by
    // the caller, so no additional locking is needed here.
    // =========================================================================

    struct update_anchor_state
    {
        std::optional<update_anchor> live;  ///< Present iff the anchor is currently registered.
        bool ended_recently = false;        ///< True between end_update() and the next update().
    };

    static std::map<std::string, update_anchor_state> &_update_anchors()
    {
        static std::map<std::string, update_anchor_state> m;
        return m;
    }

    // =========================================================================
    // Globals
    //
    // g_min_level is read on every log() call and only written by the user via
    // set_min_level — atomic relaxed access is sufficient.
    // =========================================================================

    namespace
    {
        std::atomic<level_tag> g_min_level{level_tag::DEBUG};
    }

    void set_min_level(level_tag level)
    {
        g_min_level.store(level, std::memory_order_relaxed);
    }

    level_tag get_min_level()
    {
        return g_min_level.load(std::memory_order_relaxed);
    }

    bool check_level(level_tag tag)
    {
        if (tag == level_tag::PLAIN)
            return true;
        return static_cast<int>(tag) <=
               static_cast<int>(g_min_level.load(std::memory_order_relaxed));
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
            styled_msg = ansi(colour_tag::RED, {style_tag::BOLD, style_tag::UNDERLINE}) + "[ERROR]" + ansi(colour_tag::RED, {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::WARNING:
            styled_msg = ansi(colour_tag::YELLOW, {style_tag::BOLD, style_tag::UNDERLINE}) + "[WARNING]" + ansi(colour_tag::YELLOW, {style_tag::NONE}) + " " + std::string(msg) + ansi();
            break;
        case level_tag::INFO:
            styled_msg = ansi(colour_tag::BRIGHT_BLUE, {style_tag::BOLD, style_tag::UNDERLINE}) + "[INFO]" + ansi(colour_tag::BRIGHT_BLUE, {style_tag::NONE}) + "    " + std::string(msg) + ansi();
            break;
        case level_tag::DEBUG:
            styled_msg = ansi(colour_tag::CYAN, {style_tag::BOLD, style_tag::UNDERLINE}) + "[DEBUG]" + ansi(colour_tag::CYAN, {style_tag::NONE}) + "   " + std::string(msg) + ansi();
            break;
        case level_tag::PLAIN:
        default:
            styled_msg = ansi(colour_tag::WHITE, {style_tag::NONE}) + std::string(msg) + ansi();
            break;
        }

        {
            log_print_guard guard; // erase anchors, print, redraw
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
        auto lk = anchor_object::registry_lock();
        auto &anchors = _update_anchors();

        // try_emplace creates a default-constructed update_anchor_state if the
        // name is new; otherwise we just look at the existing entry.
        auto [it, inserted] = anchors.try_emplace(update_name);
        update_anchor_state &state = it->second;

        // Warn once if this name was previously end_update()'d, then recreate.
        // The flag is the single source of truth — no separate "ended" set.
        if (state.ended_recently)
        {
            state.ended_recently = false;
            // Release the lock around the log call so the warning's own
            // log_print_guard can re-acquire it (recursive mutex — safe either
            // way, but we keep the order explicit and minimise nesting).
            lk.unlock();
            log(level_tag::WARNING,
                "update(\"" + update_name + "\") called after end_update() — recreating anchor.");
            lk.lock();
        }

        // Construct the live anchor on first call; update its message otherwise.
        if (!state.live.has_value())
            state.live.emplace(update_name);
        state.live->set_msg(std::string(msg));

        // Erase all anchor lines, redraw them all (this anchor now has the
        // updated message stored, so redraw_all() will print the new text).
        anchor_object::erase_all();
        anchor_object::redraw_all();

        if (flush)
            std::cout << std::flush;
    }

    void end_update(std::string update_name, bool flush)
    {
        auto lk = anchor_object::registry_lock();
        auto &anchors = _update_anchors();
        auto it = anchors.find(update_name);
        if (it == anchors.end() || !it->second.live.has_value())
            return;

        // Capture the last message before erasing the anchor.
        const std::string last_msg = it->second.live->last_msg();

        // Erase all anchor lines, then destroy the live anchor (its destructor
        // deregisters it from the global anchor list).  We KEEP the state entry
        // in the map with ended_recently=true so a future update() with the
        // same name can warn.
        anchor_object::erase_all();
        it->second.live.reset();
        it->second.ended_recently = true;

        // Commit the final state of this update as a permanent scrolling line,
        // then redraw any remaining anchors below it.
        std::cout << ansi(colour_tag::BRIGHT_GREEN, {style_tag::BOLD, style_tag::UNDERLINE})
                  << "[" << update_name << "]"
                  << ansi(colour_tag::BRIGHT_GREEN, {style_tag::NONE})
                  << " " << last_msg << ansi() << '\n';
        anchor_object::redraw_all(); // redraws everything except the removed anchor

        if (flush)
            std::cout << std::flush;
    }

} // namespace mist::logger