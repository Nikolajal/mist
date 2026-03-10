#pragma once
#include <mutex>

namespace mist::logger
{
    // Forward declarations — avoids pulling in the full bar headers here.
    class progress_bar;
    class multi_progress_bar;

    namespace detail
    {
        // =====================================================================
        // active_bar_handle
        // =====================================================================
        /**
         * @brief Type-erased handle to whichever bar is currently rendering.
         *
         * Holds either a `progress_bar*` or a `multi_progress_bar*` — exactly
         * one can be active at a time. This is a plain tagged union rather than
         * `std::variant` to avoid pulling in heavy headers at this layer.
         */
        struct active_bar_handle
        {
            enum class kind { none, single, multi } type = kind::none;
            union {
                progress_bar*       single;
                multi_progress_bar* multi;
            } ptr = { nullptr };

            [[nodiscard]] bool has_bar() const { return type != kind::none; }
        };

        // =====================================================================
        // progress_bar_registry
        // =====================================================================
        /**
         * @brief Singleton registry that tracks the one active bar group.
         *
         * ### Why a singleton?
         * The logger free functions (`mist::logger::info` etc.) have no natural
         * owner to pass context through — a process-global registry is the
         * minimal coupling that lets them reach the active bar without changing
         * every call site.
         *
         * ### Thread safety
         * All methods are guarded by an internal `std::mutex`. The mutex is
         * exposed via `lock()` / `unlock()` so that callers can hold it across
         * a multi-step erase → print → redraw sequence without races.
         *
         * ### Lifetime contract
         * `register_bar` / `unregister_bar` must be called with a valid pointer.
         * After `unregister_bar` the pointer is never dereferenced again.
         * It is UB to destroy a bar without calling `unregister_bar` first.
         */
        class progress_bar_registry
        {
        public:
            /** @brief Access the process-global instance. */
            static progress_bar_registry& instance();

            // Non-copyable singleton.
            progress_bar_registry(const progress_bar_registry&)            = delete;
            progress_bar_registry& operator=(const progress_bar_registry&) = delete;

            // -----------------------------------------------------------------
            // Registration
            // -----------------------------------------------------------------

            /**
             * @brief Register a standalone `progress_bar` as the active bar.
             *
             * Replaces any previously registered bar without finishing it —
             * only one bar is tracked at a time. Called by `progress_bar` on
             * first render.
             */
            void register_bar(progress_bar* bar);

            /**
             * @brief Register a `multi_progress_bar` group as the active bar.
             *
             * Called by `multi_progress_bar` on first render.
             */
            void register_bar(multi_progress_bar* bar);

            /**
             * @brief Deregister the given bar.
             *
             * No-op if the pointer does not match the currently registered bar
             * (guards against double-unregister on finish()). Called from
             * `progress_bar::finish()` and `multi_progress_bar::finish()`.
             */
            void unregister_bar(progress_bar*       bar);
            void unregister_bar(multi_progress_bar* bar);

            // -----------------------------------------------------------------
            // Commit protocol (called by logger free functions)
            // -----------------------------------------------------------------

            /**
             * @brief Erase the active bar region from the terminal.
             *
             * Must be called with the registry mutex held (via `lock()`).
             * If no bar is active this is a no-op.
             *
             * After this call the terminal is clean — the cursor sits at the
             * line where the bar top was. The caller should print its log line
             * and then call `redraw_active_bar_locked()`.
             */
            void erase_active_bar_locked();

            /**
             * @brief Redraw the active bar after a log line has been printed.
             *
             * Must be called with the registry mutex held (via `lock()`).
             * If no bar is active this is a no-op.
             */
            void redraw_active_bar_locked();

            // -----------------------------------------------------------------
            // Mutex access for atomic erase → print → redraw sequences
            // -----------------------------------------------------------------

            /**
             * @brief Acquire the registry mutex.
             *
             * Logger free functions call `lock()` before printing, perform the
             * erase → print → redraw sequence, then call `unlock()`. This
             * ensures no bar render from another thread can interleave.
             *
             * Example inside `mist::logger::info()`:
             * @code{.cpp}
             * auto& reg = mist::logger::detail::bar_registry::instance();
             * reg.lock();
             * reg.erase_active_bar_locked();
             * std::cout << formatted_message << "\n";
             * reg.redraw_active_bar_locked();
             * reg.unlock();
             * @endcode
             */
            void lock();
            void unlock();

            /** @brief True if any bar is currently registered. */
            [[nodiscard]] bool has_active_bar() const;

        private:
            progress_bar_registry() = default;

            std::mutex         mutex_;
            active_bar_handle  current_;
        };

        // =====================================================================
        // RAII helper — holds the registry lock for a scoped sequence
        // =====================================================================
        /**
         * @brief RAII guard that holds the registry lock and performs the
         *        erase → [caller prints] → redraw sequence automatically.
         *
         * Construct before printing, destroy after. Example:
         * @code{.cpp}
         * {
         *     mist::logger::detail::log_print_guard guard;
         *     std::cout << "[info] " << message << "\n";
         * }  // bar is redrawn here
         * @endcode
         */
        class log_print_guard
        {
        public:
            log_print_guard();   ///< Locks registry, erases active bar.
            ~log_print_guard();  ///< Redraws active bar, unlocks registry.

            log_print_guard(const log_print_guard&)            = delete;
            log_print_guard& operator=(const log_print_guard&) = delete;
        };

    } // namespace detail
} // namespace mist::logger