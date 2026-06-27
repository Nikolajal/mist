# Project design log

Project-wide design reference + hub. This file is the **main entry point**
for anyone reading mist's architectural thinking. mist is small enough today
that no satellite `DISCUSSION.md` files exist — everything lives here. When a
subsystem grows enough to warrant its own design notes, add a satellite
under `include/mist/<subsystem>/DISCUSSION.md` and index it from the hub
table below.

| Section | What it holds | Removal trigger |
|---|---|---|
| [Satellite discussions](#satellite-discussions--hub) | Pointers to per-area `DISCUSSION.md` files. mist has none yet — everything is in this hub. | Satellite file added / removed → update the hub. |
| [Triage taxonomy](#triage-taxonomy) | Project-wide convention for tagging items (Design / TODO / Attention / Feature) + priority. | Taxonomy change → update the chapter. |
| [Design discussions](#design-discussions) | Open architectural questions — **decision needed before any code change**. Each `D-XX` is a self-contained proposal with options + recommendation. | Decision made → entry deleted or moved to TODOs. |
| [TODOs](#todos--concrete-fixes-in-the-queue) | Concrete code-work items. No design decision pending — just hands on the keyboard. | Fix lands on `main` → row removed. |
| [Attention points](#attention-points--latent-issues-to-be-careful-about) | Latent caveats in the codebase that don't need a design discussion but **do** need a heads-up so they don't get propagated or forgotten. | Caveat resolved or formally captured as a design question / TODO. |
| [Feature queue](#feature-queue) | Features that have been discussed but not yet designed. Each is a one-liner; once a feature gets actively designed it becomes a `D-XX`. | Feature shipped or formally designed (promoted to `D-XX`). |
| [Coding conventions](#coding-conventions) | Naming + style reference for the project. | Reference material — no removal trigger. |

---

## Satellite discussions — hub

No satellites yet. Add a row here when one lands. Convention:

- One satellite per major `include/mist/<subsystem>/` — not per file.
- The satellite holds the *narrative* (why, options, history); the hub
  just indexes it.
- When a satellite's discussion section closes (decision taken, feature
  shipped), strike it through but keep the prose — it's historical context
  for the next reader.

---

## Triage taxonomy

Items in this log carry one of four tags:

| Tag | Lives in | Means |
|---|---|---|
| **D-XX** | Design discussions | Architectural question; decision needed before code. |
| **TODO** | TODOs | Concrete fix; no design pending. |
| **Attention** | Attention points | Latent caveat; not actionable yet, but readers should know. |
| **F-XX** | Feature queue | Future addition; not yet designed enough to be `D-XX`. |

When in doubt: if it changes the API or the build, it's `D-XX`. If it's
"this line is wrong, fix it", it's a TODO. If it's "watch out, this looks
fine but isn't", it's Attention.

---

## Design discussions

Open architectural questions. Each `D-XX` is a self-contained proposal —
File, observation, options, recommendation, decision needed — ready to be
moved into a GitHub issue when it gets actively worked on.

### ~~D-01 — `Rnd::uniform()` silent parameter swap~~ → RESOLVED

**File:** `include/mist/rnd.h`

Decision taken (v1.0.0 contract): the hybrid of Option A/B — **assert the
precondition `start <= end` in debug builds, retain the silent swap in
release**. Inverted bounds are a caught bug during development and never UB in
production. Documented in the header and in the v1.0.0 CHANGELOG `Changed`
entry. Callers MUST pass sorted arguments; the release-build swap tolerance is
not promised beyond the 1.x line. No further change for v1.0.0.

---

### ~~D-02 — `HoughTransform::find_rings()` options struct (API change)~~ → RESOLVED

**File:** `include/mist/ring_finding/hough_transform.h`

Decision taken: `find_rings` now takes a single `FindRingsOptions` struct
(`{.threshold_fraction, .min_hits, .min_active, .max_rings,
.collection_radius, .aggregation_window_cells}`) with a default `= {}`. The
positional overload was removed outright rather than shimmed — acceptable
while pre-tag, since no released call site exists yet (the v1.0.0 freeze is
the first tag). All `tester_hough` call sites use designated initialisers.
This is the API that ships at v1.0.0.

---

### ~~D-03 — SPDX licence headers~~ → RESOLVED

Decision taken: apply `// SPDX-License-Identifier: MIT` as the first line of
every header and source file, matching the sibling `mist-hep` convention.
Backfilled across all 12 legacy files (logger, ring_finding, rnd, mist.h);
the `algo/` files already carried it. Every `.h`/`.cxx` in `include/` and
`src/` now opens with the identifier.

---

### ~~D-04 — Branch policy and `main` creation~~ → RESOLVED

Decision taken (Option C, post-v1.0.0 policy): `main` now exists as the
stable-releases-only branch; `dev` is the integration branch. The docs
workflow publishes on push to `main`. The `CONTRIBUTING.md` branching model
reflects this. No further action.

---

### D-06 — Compass circle-fit algorithms: how far to go without a linalg backend

**Context.** The [Compass](https://github.com/tomasuciu/compass) library offers
~22 circle-fit algorithms (header-only, but depending on **Eigen 3.3**) across
three classes: algebraic, geometric (iterative), specialized.

`mist::ring_finding::circle_fit` now implements the **Eigen-free algebraic
subset** — Kåsa, Taubin, Pratt — via scalar Newton on the characteristic
polynomial (Chernov form). These cover the common "refine a Hough candidate"
need and add no dependency.

**Not implemented (paused):**
- **Geometric/iterative** (Levenberg–Marquardt, Landau, Späth, Trust) and the
  **specialized/algebraic variants** that genuinely need an eigensolver / SVD.

**Options for the rest:**

| Option | Mechanism | Trade-off |
|---|---|---|
| A — stop at the algebraic trio | Keep core dependency-free | Loses LM-grade accuracy on heavy noise; usually unnecessary for ring refinement |
| B — add the iterative fits with a tiny hand-rolled solver | Implement a fixed-size (3×3) symmetric solver in `mist::math`; no Eigen | More code to own/verify; covers LM without a dependency |
| C — optional Eigen-backed component | `mist-linalg` or an `MIST_WITH_EIGEN` target carrying the full Compass set | Pulls a large header dependency into the build; only for consumers that opt in — never into the dependency-free core |

**Decision taken (v1.0.0):** **Option A** — ship the Eigen-free algebraic trio
(Kåsa / Taubin / Pratt) only; keep the core dependency-free. The
geometric/iterative fits (Option B, hand-rolled small solver) and the optional
Eigen-backed component (Option C) are deferred until a consumer actually needs
a geometric fit. Hyperaccurate (Hyper) remains a cheap algebraic, Eigen-free
addition to the trio if wanted later. Closed for v1.0.0.

---

## TODOs — concrete fixes in the queue

No design decision pending — just hands on the keyboard. Each row links the
finding back to where it was surfaced.

| ID | Item | Where | Source |
|---|---|---|---|
| ~~T-01~~ | **DONE** — `export(EXPORT mistTargets …)` now follows the `install(EXPORT …)` block in `CMakeLists.txt`, so consumers can point `-Dmist_DIR=…/build` at an un-installed build tree. Supersedes attention point A-03. | `CMakeLists.txt`. | Discovered while scaffolding sibling `mist-hep`. |
| ~~T-02~~ | **DONE** — fixed all 7 stale `@file` Doxygen tags (PascalCase → snake_case, incl. `rnd.h` which warned only on case-sensitive filesystems). Doxygen now reports zero `@file` warnings. | logger/`progress_bar.h`, `multi_progress_bar.h`, `rnd.h`, ring_finding/`hough_transform.h`, and the matching `.cxx`. | Quality sweep. |
| ~~T-03~~ | **DONE** — the `@ref` symbols all existed; the failures were Doxygen *resolution* (unqualified refs in file-level `@file` blocks don't resolve). Fixed by fully-qualifying (`@ref mist::logger::AnchorObject::erase_all` etc.) for the linkable cases and downgrading template/private refs (`update(T,T)`, `_set_main_progress`) to `@c`. The `mist.h` `#include` block was wrapped in `@code` to stop the `#`-autolink. Doxygen now reports zero code-doc `\ref` warnings. | logger.h, logger.cxx, multi_progress_bar.{h,cxx}, progress_bar.cxx, hough_transform.h, mist.h. | Quality sweep. |
| T-04 | Tests use raw `std::cout`/`std::cerr` for assertion macros. The coding conventions explicitly carve tests out, so this is *allowed* — but at least `tester_rnd` and `tester_hough` (not `tester_logger`, which intercepts the streams) could migrate to `mist::logger::info`/`warning` for consistency. Judgement call; revisit when convenient. | `test/tester_rnd.cxx`, `test/tester_hough.cxx`. | Same audit. |

---

## Attention points — latent issues to be careful about

Latent caveats that don't need a design discussion but **do** need a
heads-up so they don't get propagated or forgotten.

### A-01 — Anchor protocol invariant in the logger

Any new UI feature in `mist::logger` that writes to the terminal **must**
inherit from `AnchorObject` and use `erase_all` / `redraw_all` in its
`update` / `finish` methods. Direct writes to `std::cout` from a
progress-bar-like type will corrupt in-flight bars and break the colour
discipline.

This was the root cause of a regression after the 2026-03 refactor. The
carveout for the logger's own implementation (allowed to write to
`cout`/`cerr` directly) does **not** extend to new UI types added after
the refactor: those go through the protocol.

### A-02 — Two narrow `std::cout`/`std::cerr` exceptions

The coding conventions ban raw stdio everywhere — except:

1. **The logger implementation itself** (`src/logger/*.cxx`) is the one
   place that *must* write to `cout`/`cerr`. New writes there must use the
   anchor protocol.
2. **`mist::logger::ScopedCoutToMist`** is the mechanism for funnelling
   third-party output (ROOT's minimiser, RooFit, etc.) through the logger
   so it doesn't corrupt bars. Reach for it when wrapping a library you
   don't control.

Outside those two cases, raw stdio is a convention violation.

### ~~A-03 — `mist_DIR` development workflow~~ → RESOLVED (T-01)

With T-01 landed, `mistTargets.cmake` is now emitted into the build tree by
`export(EXPORT …)`, so a downstream can point `mist_DIR` directly at mist's
build directory without an intermediate install:

```bash
cmake -B build                     # in mist
cmake -B build -Dmist_DIR=…/mist/build   # in the downstream
```

The install-to-prefix path still works and remains the recommended route for
consumers that are not co-developing mist.

---

## Feature queue

Features that have been discussed but not yet designed. Each is a one-liner
— promote to `D-XX` when it's worth a real design pass.

| ID | Feature | Notes |
|----|---------|-------|
| F-01 | `mist::ring_finding::nn_transform` | ONNX Runtime inference; same `Hit`/`RingResult` interface as Hough transform. Drop-in companion to `HoughTransform`. |
| F-02 | `mist::logger::table` | Formatted column-aligned table anchored in the terminal band. Must follow the anchor protocol (see A-01). |
| F-03 | `mist::logger::spinner` | Single-line animated spinner for unbounded waits. Same anchor protocol. |
| F-04 | Windows `GetConsoleScreenBufferInfo` fallback in `terminal_width()` | Replace the `#ifdef MIST_HAS_IOCTL` guard with a cross-platform wrapper. |

---

## Coding conventions

Naming, output, file-layout, and include-order rules live in
[`docs/CODING_CONVENTIONS.md`](docs/CODING_CONVENTIONS.md). The same
conventions apply verbatim across the sibling repository `mist-hep`
(only the macro prefix changes: `MIST_*` → `MIST_HEP_*`).

---

*This file is the canonical hub. Edits land in git, get published to the
Doxygen Pages site on the next push to `main`, and are reviewable like any
other source change.*
