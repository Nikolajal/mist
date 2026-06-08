# Coding conventions

Naming and structural conventions followed across the mist toolkit.
Reference material for new contributors and code review — not a discussion
log. The same conventions apply to sibling repositories (`mist-hep`,
`beam-test-analysis`); only the macro prefix and example pool change.

## Naming

| Kind                                       | Style                  | Examples                                                  |
|--------------------------------------------|------------------------|-----------------------------------------------------------|
| Variables, free functions, methods         | `snake_case`           | `bar_width`, `current_tick`, `erase_all`, `update`        |
| Private members                            | `snake_case_`          | `tag_`, `cursor_row_`, `subtasks_`                        |
| Classes, structs, type aliases             | `PascalCase`           | `ProgressBar`, `MultiProgressBar`, `AnchorObject`, `Hit`  |
| Enum values (incl. legacy plain enums)     | `PascalCase`           | `BrightGreen`, `Bold`, `Info`, `Warning`                  |
| Project macros                             | `MIST_` + `ALL_CAPS`   | `MIST_BUILD_TESTS`                                        |
| Namespaces                                 | `lowercase`            | `mist`, `mist::logger`, `mist::ring_finding`              |
| Filenames (incl. macro entry-points)       | `snake_case`           | `progress_bar.cxx`, `hough_transform.cxx`, `rnd.h`        |

## Local-vs-type collision rule

When a local variable would otherwise be spelled identically to its type
(`Hit hit`, `ProgressBar progressbar`), break the collision with one of:

1. **`current_` prefix** when the variable is simply "the one being processed
   right now": `Hit current_hit;`, `ProgressBar current_bar;`.
2. **A semantic role name** when the context warrants more specificity:
   `Hit candidate_hit`, `Hit reference_hit`, `ProgressBar outer_bar`.

Prefer the semantic role when several instances of the same type appear in
the same scope; reach for `current_` when no better name is available.

## Output: never `std::cout` / `std::cerr` / `printf`

All textual output goes through `mist::logger`. This is the whole reason the
logger exists — it owns the terminal (cursor positioning, anchor protocol,
progress-bar redraw) and any raw write to `cout`/`cerr` will corrupt
in-flight progress bars and break colour discipline.

```cpp
// no
std::cout << "loaded " << n << " events\n";
std::cerr << "warning: …\n";
std::printf("done\n");

// yes
mist::logger::info("loaded {} events", n);
mist::logger::warning("…");
mist::logger::done("done");
```

**Two narrow exceptions, both inside `mist` itself:**

1. **The logger implementation** (`src/logger/*.cxx`) is the one place that
   *must* write to `cout`/`cerr` — it is the thing being implemented. New
   writes there go through the anchor protocol (`erase_all` / `redraw_all`),
   never raw.
2. **Third-party output capture** (e.g. ROOT's minimiser writing to `cout`
   directly) is funneled through `mist::logger::ScopedCoutToMist`, which
   redirects the stream into the logger for the duration of a scope. Reach
   for it when wrapping a library call that you don't control.

Tests may use `std::cout` / `std::cerr` for terse assertion macros where
pulling in the logger would obscure the test itself — keep this minimal and
prefer the logger where it reads cleanly.

## File layout

Public headers live under `include/mist/<module>/`; implementation TUs under
`src/<module>/`. The directory under `include/mist/` is the namespace under
`mist::` — `include/mist/ring_finding/hough_transform.h` declares symbols in
`mist::ring_finding`. Don't break this mapping; downstream consumers rely on
the include path matching the namespace.

## Include order

Inside each TU, group includes top-to-bottom and separate groups with a
blank line:

1. The header this TU implements (for `.cxx` files only)
2. C / C++ standard library
3. Other mist modules
4. Third-party libraries (ROOT, etc.) — none in core mist
5. Project-internal headers from the same module
