# MIST — Make It Simple, Toolkit

> A dependency-free C++20 toolkit of primitives for physics-reconstruction
> software: random numbers, terminal logging with anchored progress bars,
> circular Hough ring finding, and generic algorithmic helpers.

[![Documentation](https://img.shields.io/badge/docs-online-blue)](https://nikolajal.github.io/mist/)
[![Docs build](https://github.com/Nikolajal/mist/actions/workflows/docs.yml/badge.svg)](https://github.com/Nikolajal/mist/actions/workflows/docs.yml)
[![CI](https://github.com/Nikolajal/mist/actions/workflows/ci.yml/badge.svg)](https://github.com/Nikolajal/mist/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

---

## Table of Contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Building](#building)
- [Testing](#testing)
- [Usage](#usage)
- [Subsystems](#subsystems)
- [Documentation](#documentation)
- [Open design questions and feature queue](#open-design-questions-and-feature-queue)
- [Project structure](#project-structure)
- [Sibling repositories](#sibling-repositories)
- [License](#license)

---

## Overview

mist is a modular C++20 toolkit providing reusable infrastructure for
detector-physics software without pulling in ROOT or other heavy
frameworks for primitives such as logging, random-number generation, or
basic algorithmic utilities.

The library exposes these subsystems:

| Subsystem        | Namespace                | Description                                                                |
|------------------|--------------------------|----------------------------------------------------------------------------|
| Random numbers   | `mist::`                 | `std::mt19937` wrapper with uniform / normal / Poisson / `generate_phi`    |
| Logger           | `mist::logger::`         | Coloured terminal logger, single-bar and multi-bar progress, anchored output |
| Ring finding     | `mist::ring_finding::`   | LUT-accelerated circular Hough-transform ring-finder; grid-free RANSAC ring-finder with a completeness-corrected score (recovers far-off-centre arcs under a noise majority); closed-form circle-fit refinement (Kåsa / Taubin / Pratt); parametric Cherenkov ring-density model |
| Generic algorithms | `mist::algo::`         | Header-only primitives: `block_mean`, `block_rms`, `moving_mean`, `sign`, `log_binning`, line `intersect`/`zero_crossing` (with error propagation) |
| Bit masks        | `mist::bits::`           | 32-bit mask encode/decode helpers over C++20 `<bit>` |
| Statistics       | `mist::stats::`          | ROOT-free HEP statistics: sideband subtraction; same-frame timing (triangle acceptance, Poisson-rate MLE) |
| I/O              | `mist::io::`             | Header-only delimited-text readers: `read_csv` (empty fields preserved), `read_txt` (whitespace runs collapse); ragged rows padded, missing file yields empty (no throw) |
| Time             | `mist::time::`           | Header-only timestamp `parse` / `to_string` round-trip over `<ctime>`, with chronological ordering and invalid-input rejection |

ROOT-typed analysis helpers built on top of mist live in the sibling
repository [mist-hep](https://github.com/Nikolajal/mist-hep); see the
[Sibling repositories](#sibling-repositories) section.

---

## Requirements

- C++20 compliant compiler (GCC ≥ 10, Clang ≥ 10, AppleClang from Xcode 12.5+, MSVC ≥ 19.29)
- CMake ≥ 3.14
- No external dependencies

---

## Building

```bash
git clone https://github.com/Nikolajal/mist.git
cd mist
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Installing

```bash
cmake --install build --prefix /usr/local
```

Or use the bundled script — installs to `$HOME/.local` by default, override with `MIST_INSTALL_PREFIX`:

```bash
bash scripts/install.sh                          # → $HOME/.local
MIST_INSTALL_PREFIX=/opt/mist bash scripts/install.sh   # → /opt/mist
```

The install lays down:

- `libmist.a` (or `.so`) under `lib/`
- Headers under `include/mist/`
- CMake package config under `lib/cmake/mist/` (consumable via `find_package(mist REQUIRED)`)

---

## Testing

MIST ships a small CTest-driven test suite (off by default so downstream consumers aren't affected). Enable it with `-DMIST_BUILD_TESTS=ON`:

```bash
cmake -B build -DMIST_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Or use the helper script which builds and runs everything in one shot:

```bash
bash scripts/install_with_tests.sh --run
```

The suite contains thirteen binaries (~11 s total on a modest laptop):

| Binary        | Source                                          | Coverage                                                            |
|---------------|-------------------------------------------------|---------------------------------------------------------------------|
| `test_logger` | [test/tester_logger.cxx](test/tester_logger.cxx) | level filter, ANSI escape generation, single and multi progress bars, named update anchors, regression tests for the layout-recompute, data-race, and unknown-total fixes |
| `test_rnd`    | [test/tester_rnd.cxx](test/tester_rnd.cxx)       | deterministic seeding, statistical moments of `uniform` / `normal` / `poisson`, `generate_phi` range, invalid-λ rejection, debug-assert contract on inverted bounds |
| `test_hough`  | [test/tester_hough.cxx](test/tester_hough.cxx)   | LUT readiness, single and dual ring recovery from synthetic data, sorted-by-votes invariant, accumulator shape consistency, options-struct call sites |
| `test_algo`   | [test/tester_algo.cxx](test/tester_algo.cxx)     | `block_mean`, `block_rms`, `moving_mean` against hand-computed expectations; `drop_partial` semantics; iterator-pair and range overloads; `sign`; edge cases (`n == 0`, `n > size`, empty input) |
| `test_bits`   | [test/tester_bits.cxx](test/tester_bits.cxx)     | `encode_bit` / `encode_bits` / `count_trailing_zeros` / `decode_bits`, constexpr use, out-of-range (debug vs release), encode/decode round-trips |
| `test_circle_fit` | [test/tester_circle_fit.cxx](test/tester_circle_fit.cxx) | exact recovery and partial arcs for Kåsa / Taubin / Pratt, three-point, degenerate (collinear / too few), noisy fit, `Hit` interop |
| `test_ring_model` | [test/tester_ring_model.cxx](test/tester_ring_model.cxx) | logistic-window symmetry, baseline width with/without features, polar peak at `R0`, radial signal integral = `N/(2πR0)`, polar/xy agreement, closed σ-level contour |
| `test_sideband` | [test/tester_sideband.cxx](test/tester_sideband.cxx) | pure-signal / flat-background / signal-over-background recovery, edge clamping, span types, invalid inputs |
| `test_timing` | [test/tester_timing.cxx](test/tester_timing.cxx) | triangle acceptance (peak, linear falloff, floor, out-of-support zero), Poisson-rate MLE = 1/mean, empty / zero-mean handling |
| `test_intersect` | [test/tester_intersect.cxx](test/tester_intersect.cxx) | line intersection (known crossing, line-swap symmetry, hand-checked error propagation, parallel → not ok), zero-crossing (`-q/m`, propagated error, horizontal → not ok) |
| `test_ransac` | [test/tester_ransac.cxx](test/tester_ransac.cxx) | far-off-centre bright arc recovered under a uniform-noise majority; sparse far arc recovered via the completeness correction (with and without an explicit sensor fiducial); cross-platform determinism; two-ring remove-and-repeat; pure noise yields no spurious ring |
| `test_io`     | [test/tester_io.cxx](test/tester_io.cxx)         | delimited-text readers: `read_csv` field preservation, `read_txt` whitespace collapsing, ragged-row padding, missing file → empty (no throw) |
| `test_time`   | [test/tester_time.cxx](test/tester_time.cxx)     | timestamp `parse` / `to_string` round-trip, chronological ordering, invalid-input rejection |

### Continuous integration

Every push and pull request runs the matrix below via [`.github/workflows/ci.yml`](.github/workflows/ci.yml):

|              | Release         | Debug           |
|--------------|-----------------|-----------------|
| Linux        | build + ctest   | build + ctest   |
| macOS        | build + ctest   | build + ctest   |
| Windows      | build + ctest   | build + ctest   |

A failure in any matrix leg blocks the PR from being merged.

---

## Usage

### Single include

```cpp
#include <mist/mist.h>   // pulls in everything
```

Or include only what is needed:

```cpp
#include <mist/rnd.h>                                  // RNG only
#include <mist/logger/logger.h>                        // log and named updates
#include <mist/logger/progress_bar.h>                  // single progress bar
#include <mist/logger/multi_progress_bar.h>            // composite multi-bar
#include <mist/ring_finding/hough_transform.h>         // Hough ring-finder
#include <mist/ring_finding/ransac_ring_finder.h>      // grid-free RANSAC ring-finder
#include <mist/ring_finding/circle_fit.h>              // Kåsa / Taubin / Pratt fit
#include <mist/ring_finding/ring_model.h>              // Cherenkov ring density
#include <mist/algo/binning.h>                         // block_mean, block_rms
#include <mist/algo/smoothing.h>                       // moving_mean
#include <mist/algo/intersect.h>                       // line intersect / zero-crossing
#include <mist/stats/sideband.h>                       // sideband subtraction
#include <mist/stats/timing.h>                         // triangle accept., rate MLE
```

### Integrating via CMake

After installing, downstream projects find mist with:

```cmake
find_package(mist 1 REQUIRED)            # SameMajorVersion: accepts any 1.x
target_link_libraries(my_target PRIVATE mist::mist)
```

Or, when consuming mist as a subdirectory:

```cmake
add_subdirectory(mist)
target_link_libraries(my_target PRIVATE mist::mist)
```

Or via `FetchContent`. Pin to a release tag for reproducible builds
(e.g. `v1.0.0` once the first release is cut; a commit SHA in the
interim):

```cmake
include(FetchContent)
FetchContent_Declare(mist
    GIT_REPOSITORY https://github.com/Nikolajal/mist.git
    GIT_TAG        <release-tag-or-sha>)
FetchContent_MakeAvailable(mist)
```

---

## Subsystems

### `mist::Rnd` — Random Number Generator

A thin wrapper around `std::mt19937` with convenient distribution methods.
Supports deterministic and non-deterministic seeding. Not thread-safe —
give each thread its own instance.

```cpp
mist::Rnd rng;                        // non-deterministic seed
mist::Rnd rng(42);                    // deterministic seed

double x   = rng.uniform(0.0, 1.0);  // Uniform[0, 1)
float  y   = rng.normal(0.f, 1.f);   // N(0, 1)
int    z   = rng.poisson(5);         // Poisson(λ=5)  — throws if λ ≤ 0
double phi = rng.generate_phi();     // Uniform[-π, π)

rng.reseed(123);                      // reset sequence
```

---

### `mist::logger` — Coloured Terminal Logger

ANSI-coloured logger with level filtering, `std::cerr` routing for errors,
TTY auto-detection (colours are suppressed automatically when output is
redirected to a file), and in-place progress bars.

#### Basic logging

```cpp
mist::logger::set_colour_enabled(true);                       // override TTY detection
mist::logger::set_min_level(mist::logger::LevelTag::Debug);  // show all levels

mist::logger::info("Initialising detector");
mist::logger::debug("n_channels = 1024");
mist::logger::warning("Calibration file not found, using defaults");
mist::logger::error("Failed to open geometry file");
mist::logger::plain("Raw output, no prefix");

// Custom colour and style
mist::logger::log("Custom message",
    mist::logger::ColourTag::Magenta,
    {mist::logger::StyleTag::Bold, mist::logger::StyleTag::Italic});
```

All five convenience wrappers accept `std::string_view`, so string literals
and `std::string_view` substrings incur no heap allocation.

#### In-place update line

```cpp
for (int i = 0; i < n_spills; ++i)
{
    mist::logger::update("spill", "Processing spill " + std::to_string(i));
    process(i);
}
mist::logger::end_update("spill");
mist::logger::info("All spills processed.");
```

#### Single progress bar

```cpp
// Driven by current / total count
mist::logger::ProgressBar bar(mist::logger::BarStyle::Block);
for (int i = 0; i <= n; ++i)
{
    bar.update(i, n);
    do_work(i);
}
bar.finish();

// Or with a tag
mist::logger::ProgressBar bar("framer");
bar.update(0.42);   // 42%
bar.finish();
```

Tags can be reassigned at any time via `bar.assign_tag(...)` — the cached
layout is invalidated so the new tag width takes effect on the next render.

#### Multi-line progress bar

For workloads with a top-level cycle (spills, batches) and several parallel
sub-tasks per cycle, `MultiProgressBar` renders a header bar plus one
sub-line per task, all anchored together at the bottom of the terminal:

```cpp
mist::logger::MultiProgressBar multi;
auto &loader = multi.add_subtask("loader");
auto &parser = multi.add_subtask("parser");
auto &writer = multi.add_subtask("writer");

for (int spill = 0; spill < n_spills; ++spill)
{
    loader.update(...);
    parser.update(...);
    writer.update(...);
    multi.update(spill, n_spills);          // header
}
loader.finish();
parser.finish();
writer.finish();
multi.finish();
```

Unknown-total mode (no percentage, no ETA — just current count + elapsed
time) is selected by passing the named sentinel:

```cpp
multi.update(processed, mist::logger::MultiProgressBar::kUnknownTotal);
```

#### Platform note

The progress bar auto-detects terminal width via `ioctl(TIOCGWINSZ)` and
displays percentage, current/total count, elapsed time, and ETA. A normal
`log()` call while the bar is active auto-commits it first so output is
never corrupted.

> Progress bars and coloured output rely on ANSI escape codes and
> `ioctl(TIOCGWINSZ)` for terminal-width detection.  Both are standard on
> **Linux and macOS**.  On **Windows** you need a terminal that supports
> VT/ANSI sequences (Windows Terminal ≥ 1.0, VS Code integrated terminal) —
> the legacy `cmd.exe` and older PowerShell hosts do not.
> When stdout is redirected to a file or pipe all cursor-control escapes
> are suppressed automatically, so log files remain clean on every platform.

---

### `mist::ring_finding::HoughTransform` — Circular Hough Transform

A two-phase, LUT-accelerated circular Hough transform for ring reconstruction
in the detector (x, y) plane. The LUT is built once per geometry and reused
across all events, making per-event cost purely proportional to the number of
active hits.

#### Hit and result types

```cpp
// Input
mist::ring_finding::Hit h;
h.x       = 123.4f;   // Hit x-position [mm]
h.y       =  56.7f;   // Hit y-position [mm]
h.time    =   8.9f;   // calibrated Hit time [ns]
h.lut_key =     3;    // typically global_channel_index / 4

// Output
mist::ring_finding::RingResult r;
r.cx          // ring centre x [mm]
r.cy          // ring centre y [mm]
r.radius      // ring radius [mm]
r.peak_votes  // accumulator peak vote count
r.mean_time   // mean time of assigned hits [ns]
r.hit_indices // indices into the input Hit vector
```

#### Typical workflow

```cpp
// --- Once per run / geometry change ---
std::map<int, std::array<float, 2>> geometry = load_geometry();

mist::ring_finding::HoughTransform ht;
ht.build_lut(geometry,
    30.f,                                                    // r_min [mm]
    80.f,                                                    // r_max [mm]
     1.f,                                                    // r_step [mm]
     mist::ring_finding::HoughTransform::kDefaultCellSizeMm // cell_size [mm]
);

// --- Per event ---
std::vector<mist::ring_finding::Hit> hits = make_hits(raw_hits);

auto rings = ht.find_rings(hits, {
    .threshold_fraction       = 0.3f,   // min fraction of active hits in peak
    .min_hits                 = 5,      // minimum absolute vote count
    .min_active               = 5,      // minimum hits to attempt next ring
    .max_rings                = 2,
    // .collection_radius       defaults to kDefaultCollectionRadiusMm
    // .aggregation_window_cells defaults to 1 (single-cell peak); set to 2
    //                          for sub-cell-fragmentation recovery on a
    //                          halved cell_size / r_step grid.
});

for (auto &ring : rings)
    mist::logger::info(
        "Ring: cx=" + std::to_string(ring.cx) +
        " cy="      + std::to_string(ring.cy) +
        " R="       + std::to_string(ring.radius) +
        " votes="   + std::to_string(ring.peak_votes));
```

#### Algorithm notes

- The LUT maps each `lut_key` to the flat accumulator cell indices it votes
  for at every radius bin, computed once at `build_lut` time.
- Per-event cost is O(hits × R_bins × arc_cells), with arc_cells typically
  small after deduplication.
- After each ring is found, its contributing hits are removed from the active
  set and the accumulator is reset before searching for the next ring. This
  avoids spatial-suppression artefacts when two rings are close together.
- The returned vector is sorted by descending `peak_votes`, so `rings[0]` is
  the strongest candidate even though the extraction order is "first found,
  next-best after removal".

#### Sub-cell aggregation (`aggregation_window_cells`)

The default peak finder reports the **single accumulator cell** with the
most votes.  When the underlying detector resolution is comparable to
`cell_size`, a real ring's votes can fragment across 2–3 adjacent cells
(boundary effect) and the single-cell peak undercounts.

Setting `aggregation_window_cells = W` (with `W > 1`) switches the peak
finder to a **sliding `W × W × W` sub-cell window**: at every position on
the accumulator grid it sums the W³ cells in the window and reports the
position with the maximum sum.  The reported `(cx, cy, radius)` is the
window's **centre** (sub-cell-precision back-projection); the reported
`peak_votes` is the aggregated sum.

The intended usage is together with halved `cell_size` / `r_step` at
LUT-build time, so that `W × cell_size` matches the original cell width.
Then the aggregated count probes the same physical volume as the legacy
single-cell finder, and threshold knobs (`min_hits`,
`threshold_fraction`) retain their physical meaning — no re-tuning
needed.  Without halving the grid, `W = 2` covers a `(2·cell_size)`³
volume which is coarser than the legacy finder; useful only if you
explicitly want bigger probed cells.

**Implementation**: for `W > 1` the peak finder uses a 3-D
**Summed-Area-Table** (integral image).  A prefix-sum array is built
from `accum_` in O(n_cells) via three sequential 1-D cumulative-sum
passes (along x, then y, then R).  Each window sum is then evaluated
in O(1) via inclusion-exclusion on the 8 corners of the 3-D box.
Total cost: `O(n_cells × 8)` — constant in W, versus `O(n_cells × W³)`
for a naive scan.  Results are bit-for-bit identical to the naive
approach.  The SAT scratch buffer is a pre-allocated `mutable` member,
so `find_peak` incurs no heap allocation.

Tests pass for `W = 1, 2`.  Values >2 are accepted but give
diminishing returns.

---

### `mist::ring_finding::find_rings_ransac` — grid-free RANSAC ring finder

A complementary, accumulator-free ring finder for the regime the Hough grid
handles poorly: a **far-off-centre Cherenkov arc** (centre well outside the
sensor) sitting on a **uniform-noise majority**. It reuses the same `Hit` /
`RingResult` types and the Taubin `circle_fit`, and is header-only.

```cpp
#include <mist/ring_finding/ransac_ring_finder.h>

std::vector<mist::ring_finding::Hit> hits = make_hits(raw_hits);

mist::ring_finding::RansacOptions opt;
opt.max_rings        = 1;
opt.iterations       = 1500;   // 3-point samples per ring
opt.inlier_band      = 6.0;    // |dist − R| < band ⇒ inlier [mm]
opt.min_inliers      = 50;
opt.min_significance = 5.0;    // accept only if excess > Nσ over background
opt.r_min            = 50.0;
opt.r_max            = 1000.0;
// For sparse per-event frames, pass the KNOWN sensor window so the
// completeness correction has a geometric reference (else it falls back to
// the hit bounding box, which a few clustered hits do not fill):
// opt.fiducial_xmin/xmax/ymin/ymax = …;

auto rings = mist::ring_finding::find_rings_ransac(hits, opt);
```

#### Algorithm notes

- **No accumulator** → the centre/radius range is unbounded for free; a circle
  through three arc points lands at the true centre however far off-sensor it is,
  where a single least-squares fit collapses to the noise centroid near the origin.
- **Completeness-corrected score** → candidates are ranked by inlier *excess over
  background* per mm of *on-sensor* arc length, so a 36° far arc showing ~10 % of
  its circumference competes on equal footing with a fully-visible small ring,
  rather than always losing on raw count.
- **Significance gate** rejects pure noise (excess must exceed `min_significance`
  σ of the Poisson background over the visible arc).
- **Optional per-hit `weights`** down-weight high-occupancy channels so a *faint*
  arc can still beat a bright background.
- **Deterministic and portable** — the sampler is seeded and draws indices with a
  Lemire multiply-shift over the engine's raw output rather than
  `std::uniform_int_distribution`, so results are identical across calls *and*
  across standard libraries (libstdc++ / libc++ / MSVC).

---

## Documentation

- **API reference** (Doxygen, auto-deployed on push to `main`):
  <https://nikolajal.github.io/mist/>
- **Coding conventions**: [`docs/CODING_CONVENTIONS.md`](docs/CODING_CONVENTIONS.md)
  — naming rules, output discipline (use `mist::logger`, never `cout`/`cerr`
  outside the logger implementation), file layout, include order.
- **Compatibility contract**: [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md)
  — semver guarantees, deprecation policy, supported toolchain baseline,
  public API surface definition. Binding from v1.0.0 onward.
- **Contributing**: [`CONTRIBUTING.md`](CONTRIBUTING.md) — branching model
  (`main`/`dev`/`dev_patch_*`/`dev_<feature>`), release procedure,
  conventions cross-reference.
- **Design log**: [`DISCUSSION.md`](DISCUSSION.md) — open architectural
  questions, TODOs surfaced by the latest audit, attention points, and the
  feature queue. This is the place to look before proposing API changes.

The Doxygen site is built and published by
[`.github/workflows/docs.yml`](.github/workflows/docs.yml) on every push to
`main`; manual rebuilds are possible from the Actions tab via the
`workflow_dispatch` trigger.

---

## Open design questions and feature queue

The canonical record lives in [`DISCUSSION.md`](DISCUSSION.md), which
follows the hub / triage-taxonomy template:

- **`D-XX`** — open architectural questions awaiting a decision.
- **`TODO`** — concrete fixes in the queue, no design pending.
- **Attention points** — latent caveats to keep in mind.
- **`F-XX`** — feature queue (e.g. a neural-network ring-finder companion
  to the Hough transform, terminal table and spinner widgets).

API-affecting proposals should be filed there before any topic branch is
opened.

---

## Project structure

```
mist/
├── CMakeLists.txt
├── CHANGELOG.md
├── CONTRIBUTING.md                     # branching model + release procedure
├── DISCUSSION.md                       # design log
├── LICENSE
├── README.md
├── .github/workflows/
│   ├── ci.yml                          # build + ctest matrix
│   └── docs.yml                        # Doxygen + GitHub Pages publish
├── cmake/
│   └── mistConfig.cmake.in
├── docs/
│   ├── CODING_CONVENTIONS.md
│   ├── COMPATIBILITY.md                # binding stability contract
│   └── Doxyfile
├── include/mist/
│   ├── mist.h                          # umbrella include
│   ├── rnd.h                           # header-only RNG
│   ├── bits.h                          # 32-bit mask helpers
│   ├── logger/
│   │   ├── logger_types.h              # enums, ansi()
│   │   ├── logger.h                    # logging functions + anchor registry
│   │   ├── progress_bar.h              # single-line progress bar
│   │   └── multi_progress_bar.h        # composite header + subtask bars
│   ├── ring_finding/
│   │   ├── hough_transform.h
│   │   ├── ransac_ring_finder.h        # grid-free RANSAC + completeness score
│   │   ├── circle_fit.h                # Kåsa / Taubin / Pratt refinement
│   │   └── ring_model.h                # Cherenkov ring-density model
│   ├── algo/
│   │   ├── binning.h                   # block_mean, block_rms
│   │   ├── smoothing.h                 # moving_mean
│   │   ├── edges.h                     # log_binning
│   │   ├── intersect.h                 # line intersect / zero-crossing
│   │   └── util.h                      # sign
│   └── stats/
│       ├── sideband.h                  # sideband_subtract
│       └── timing.h                    # triangle_acceptance, poisson_rate_mle
├── src/
│   ├── logger/
│   │   ├── logger.cxx
│   │   ├── logger_types.cxx
│   │   ├── progress_bar.cxx
│   │   └── multi_progress_bar.cxx
│   ├── ring_finding/
│   │   └── hough_transform.cxx         # RANSAC + circle-fit are header-only
│   └── algo/
│       └── algo.cxx                    # TU placeholder; templates instantiate
│                                       # at the call site
├── test/
│   ├── tester_logger.cxx
│   ├── tester_rnd.cxx
│   ├── tester_hough.cxx
│   ├── tester_algo.cxx
│   ├── tester_bits.cxx
│   ├── tester_circle_fit.cxx
│   ├── tester_ring_model.cxx
│   ├── tester_sideband.cxx
│   ├── tester_timing.cxx
│   ├── tester_intersect.cxx
│   ├── tester_ransac.cxx
│   ├── tester_io.cxx
│   └── tester_time.cxx
└── scripts/
    ├── install.sh                      # honours MIST_INSTALL_PREFIX
    └── install_with_tests.sh           # build + optionally run tests
```

---

## Sibling repositories

mist is deliberately ROOT-free. Helpers that need ROOT or RooFit live in
the sibling library **[mist-hep](https://github.com/Nikolajal/mist-hep)**,
which depends on `mist::mist` and exposes its own `mist::hep` namespace.
A downstream app that wants both writes:

```cmake
target_link_libraries(my_target PRIVATE mist::mist mist::hep)
```

See `mist-hep`'s README for the two-step `FetchContent` recipe.

---

## License

MIT License. See [`LICENSE`](LICENSE) for details.
