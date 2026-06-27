# Coding conventions

Naming and structural conventions for all languages used across projects.
Drop this file into any repo unchanged; add a project-specific section at
the bottom only for rules that genuinely deviate.

---

## All languages

### File and directory naming

`snake_case` for filenames in every language.
No loose files in root — group by theme into subdirs.

### Comments and documentation

Write no comments by default. Add one only when the **why** is
non-obvious: a hidden constraint, a subtle invariant, a workaround for a
specific bug. One line max. Never explain what the code does — names do
that. No dated comments; they rot immediately.

### Constants and magic numbers

All magic numbers are named constants. Zero bare literals for domain
values.

---

## Python

### Naming

| Kind                              | Style               | Examples                              |
|-----------------------------------|---------------------|---------------------------------------|
| Variables, free functions, methods | `snake_case`       | `bar_width`, `emit_photon`, `update`  |
| Private (module or class)         | `_leading_underscore` | `_frame`, `_cache`                  |
| Classes                           | `PascalCase`        | `MovingParticle`, `ColorPalette`      |
| Module-level constants            | `UPPER_CASE`        | `ELECTRON`, `PHOTON`                  |
| Filenames                         | `snake_case`        | `palette.py`, `frame.py`             |

### Type hints

Required on all public function signatures — parameters and return type.
Optional on private helpers, but encouraged.

### Docstrings

One line max. Only when the why is non-obvious. Omit entirely when the
name makes the purpose clear. No multi-line blocks.

### Import order

Three groups, one blank line between each:

1. Standard library
2. Third-party
3. Local / project

### Output

`print` is acceptable. No debug prints left in committed code.

### Package structure

Each subpackage exposes its public API through `__init__.py`; callers
import from the package, not from internal modules directly.

---

## C++

### Naming

| Kind                               | Style                | Examples                                    |
|------------------------------------|----------------------|---------------------------------------------|
| Variables, free functions, methods | `snake_case`         | `bar_width`, `current_tick`, `erase_all`    |
| Private members                    | `snake_case_`        | `tag_`, `cursor_row_`, `subtasks_`          |
| Classes, structs, type aliases     | `PascalCase`         | `ProgressBar`, `AnchorObject`, `Hit`        |
| Enum values                        | `PascalCase`         | `BrightGreen`, `Bold`, `Info`               |
| Project macros                     | `PROJECT_` + `ALL_CAPS` | `MIST_BUILD_TESTS`                       |
| Namespaces                         | `lowercase`          | `mist`, `mist::ring_finding`                |
| Filenames                          | `snake_case`         | `progress_bar.cxx`, `hough_transform.h`     |

### Local-vs-type collision rule

When a local variable would otherwise be spelled identically to its type
(`Hit hit`, `ProgressBar progressbar`), break the collision with one of:

1. **`current_` prefix** when the variable is simply "the one being processed
   right now": `Hit current_hit;`.
2. **A semantic role name** when context warrants more specificity:
   `Hit candidate_hit`, `Hit reference_hit`.

Prefer the semantic role when several instances of the same type appear in
the same scope; reach for `current_` when no better name is available.

### Output

Never write to stdout/stderr directly from application code. Route all
output through the project's logging facility. Tests may use
`std::cout`/`std::cerr` for terse assertion macros where pulling in the
logger would obscure the test.

### File layout

Public headers under `include/<project>/<module>/`; implementation TUs
under `src/<module>/`. The directory under `include/<project>/` mirrors
the namespace under `<project>::`.

### Include order

1. The header this TU implements (`.cxx` only)
2. C / C++ standard library
3. Other project modules
4. Third-party libraries
5. Project-internal headers from the same module
