/**
 * @file logger.cxx
 * @brief Implementation of the @ref mist::logger free-function logging API and
 *        the global anchored-object registry.
 *
 * The registry, the level filter, and the named-update map are all guarded by
 * a single recursive mutex (returned by @ref AnchorObject::registry_lock).
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
            : lk_(AnchorObject::registry_lock())
        {
            AnchorObject::erase_all();
        }

        ~log_print_guard()
        {
            AnchorObject::redraw_all();
            // Lock released as lk_ destructs.
        }

        log_print_guard(const log_print_guard &) = delete;
        log_print_guard &operator=(const log_print_guard &) = delete;
    };
    // =========================================================================
    // AnchorObject registry
    // =========================================================================

    std::vector<AnchorObject *> &AnchorObject::_registry()
    {
        static std::vector<AnchorObject *> reg;
        return reg;
    }

    std::recursive_mutex &AnchorObject::_registry_mutex()
    {
        static std::recursive_mutex m;
        return m;
    }

    std::unique_lock<std::recursive_mutex> AnchorObject::registry_lock()
    {
        return std::unique_lock<std::recursive_mutex>(_registry_mutex());
    }

    AnchorObject::AnchorObject()
    {
        auto lk = registry_lock();
        _registry().push_back(this);
    }

    AnchorObject::~AnchorObject()
    {
        auto lk = registry_lock();
        auto &reg = _registry();
        reg.erase(std::remove(reg.begin(), reg.end(), this), reg.end());
    }

    int AnchorObject::total_anchored_lines()
    {
        auto lk = registry_lock();
        int total = 0;
        for (const AnchorObject *obj : _registry())
            total += obj->rendered_line_count();
        return total;
    }

    void AnchorObject::erase_all()
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

    void AnchorObject::redraw_all()
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
        for (AnchorObject *obj : _registry())
            obj->render_line();
    }

    // =========================================================================
    // update_anchor — concrete anchor for a single named update line
    // =========================================================================

    class update_anchor : public AnchorObject
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
                      << ansi(ColourTag::BrightGreen, {StyleTag::Bold, StyleTag::Underline})
                      << "[" << name_ << "]"
                      << ansi(ColourTag::BrightGreen, {StyleTag::None})
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
        std::atomic<LevelTag> g_min_level{LevelTag::Debug};
    }

    void set_min_level(LevelTag level)
    {
        g_min_level.store(level, std::memory_order_relaxed);
    }

    LevelTag get_min_level()
    {
        return g_min_level.load(std::memory_order_relaxed);
    }

    bool check_level(LevelTag tag)
    {
        if (tag == LevelTag::Plain)
            return true;
        return static_cast<int>(tag) <=
               static_cast<int>(g_min_level.load(std::memory_order_relaxed));
    }

    // =========================================================================
    // log
    // =========================================================================

    void log(LevelTag tag, std::string_view msg, bool flush)
    {
        if (!check_level(tag))
            return;

        const bool use_cerr = (tag == LevelTag::Error || tag == LevelTag::Warning);
        std::ostream &out = use_cerr ? std::cerr : std::cout;

        std::string styled_msg;
        switch (tag)
        {
        case LevelTag::Error:
            styled_msg = ansi(ColourTag::Red, {StyleTag::Bold, StyleTag::Underline}) + "[ERROR]" + ansi(ColourTag::Red, {StyleTag::None}) + "   " + std::string(msg) + ansi();
            break;
        case LevelTag::Warning:
            styled_msg = ansi(ColourTag::Yellow, {StyleTag::Bold, StyleTag::Underline}) + "[WARNING]" + ansi(ColourTag::Yellow, {StyleTag::None}) + " " + std::string(msg) + ansi();
            break;
        case LevelTag::Info:
            styled_msg = ansi(ColourTag::BrightBlue, {StyleTag::Bold, StyleTag::Underline}) + "[INFO]" + ansi(ColourTag::BrightBlue, {StyleTag::None}) + "    " + std::string(msg) + ansi();
            break;
        case LevelTag::Debug:
            styled_msg = ansi(ColourTag::Cyan, {StyleTag::Bold, StyleTag::Underline}) + "[DEBUG]" + ansi(ColourTag::Cyan, {StyleTag::None}) + "   " + std::string(msg) + ansi();
            break;
        case LevelTag::Plain:
        default:
            styled_msg = ansi(ColourTag::White, {StyleTag::None}) + std::string(msg) + ansi();
            break;
        }

        {
            log_print_guard guard; // erase anchors, print, redraw
            out << styled_msg << '\n';
            if (flush)
                out << std::flush;
        }
    }

    void log(std::string_view msg, ColourTag c, std::initializer_list<StyleTag> s)
    {
        log_print_guard guard;
        std::cout << ansi(c, s) << msg << ansi() << '\n';
    }

    // =========================================================================
    // update / end_update
    // =========================================================================

    void update(std::string update_name, std::string_view msg, bool flush)
    {
        auto lk = AnchorObject::registry_lock();
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
            log(LevelTag::Warning,
                "update(\"" + update_name + "\") called after end_update() — recreating anchor.");
            lk.lock();
        }

        // Construct the live anchor on first call; update its message otherwise.
        if (!state.live.has_value())
            state.live.emplace(update_name);
        state.live->set_msg(std::string(msg));

        // Erase all anchor lines, redraw them all (this anchor now has the
        // updated message stored, so redraw_all() will print the new text).
        AnchorObject::erase_all();
        AnchorObject::redraw_all();

        if (flush)
            std::cout << std::flush;
    }

    void end_update(std::string update_name, bool flush)
    {
        auto lk = AnchorObject::registry_lock();
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
        AnchorObject::erase_all();
        it->second.live.reset();
        it->second.ended_recently = true;

        // Commit the final state of this update as a permanent scrolling line,
        // then redraw any remaining anchors below it.
        std::cout << ansi(ColourTag::BrightGreen, {StyleTag::Bold, StyleTag::Underline})
                  << "[" << update_name << "]"
                  << ansi(ColourTag::BrightGreen, {StyleTag::None})
                  << " " << last_msg << ansi() << '\n';
        AnchorObject::redraw_all(); // redraws everything except the removed anchor

        if (flush)
            std::cout << std::flush;
    }

} // namespace mist::logger