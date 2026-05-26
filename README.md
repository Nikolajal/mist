# MIST — Make It Simple, Toolkit

> *A lightweight C++17 library for physics reconstruction utilities.*
> *Internally known as WHALE (Whoo, Here's Another Library: Egregious!).*

---

## Overview

MIST is a modular C++17 toolkit born out of the need for clean, reusable infrastructure in detector physics software — without pulling in ROOT or other heavy frameworks for things as simple as logging or random number generation.

It currently provides three subsystems:

| Subsystem      | Namespace                | Description                                                                |
|----------------|--------------------------|----------------------------------------------------------------------------|
| Random numbers | `mist::`                 | `std::mt19937` wrapper with uniform / normal / Poisson / `generate_phi`    |
| Logger         | `mist::logger::`         | Coloured terminal logger, single-bar & multi-bar progress, anchored output |
| Ring finding   | `mist::ring_finding::`   | LUT-accelerated circular Hough-transform ring-finder                       |

A neural-network ring-finder (`mist::ring_finding::nn_transform`) is planned as a companion to the Hough transform.

---

## Requirements

- C++17 compliant compiler (GCC ≥ 7, Clang ≥ 5, MSVC ≥ 19.14)
- CMake ≥ 3.14
- No external dependencies

---

## Building

```bash
git clone https://github.com/your-org/mist.git
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

The suite contains three binaries (~9 s total on a modest laptop):

| Binary        | Source                          | Coverage                                                            |
|---------------|---------------------------------|---------------------------------------------------------------------|
| `test_logger` | [test/tester_logger.cxx](test/tester_logger.cxx) | level filter, ANSI escape generation, single & multi progress bars, named update anchors, regression tests for the layout-recompute / data-race / unknown-total fixes |
| `test_rnd`    | [test/tester_rnd.cxx](test/tester_rnd.cxx)    | deterministic seeding, statistical moments of `uniform` / `normal` / `poisson`, `generate_phi` range, invalid-λ rejection |
| `test_hough`  | [test/tester_hough.cxx](test/tester_hough.cxx)  | LUT readiness, single & dual ring recovery from synthetic data, sorted-by-votes invariant, accumulator shape consistency |

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

Or include only what you need:

```cpp
#include <mist/rnd.h>                                  // RNG only
#include <mist/logger/logger.h>                        // log + named updates
#include <mist/logger/progress_bar.h>                  // single progress bar
#include <mist/logger/multi_progress_bar.h>            // composite multi-bar
#include <mist/ring_finding/hough_transform.h>         // Hough ring-finder
```

### Integrating via CMake

After installing, downstream projects can find MIST with:

```cmake
find_package(mist REQUIRED)
target_link_libraries(my_target PRIVATE mist::mist)
```

Or, if using MIST as a subdirectory:

```cmake
add_subdirectory(mist)
target_link_libraries(my_target PRIVATE mist::mist)
```

Or via `FetchContent` (pin to a commit hash, not a branch, to avoid surprise breakage):

```cmake
include(FetchContent)
FetchContent_Declare(mist
    GIT_REPOSITORY https://github.com/your-org/mist.git
    GIT_TAG        <commit-sha>)
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

auto rings = ht.find_rings(hits,
    0.3f,  // threshold_fraction: min fraction of active hits in peak
    5,     // min_hits: minimum absolute vote count
    5,     // min_active: minimum hits remaining to attempt next ring
    2,     // max_rings (default)
    mist::ring_finding::HoughTransform::kDefaultCollectionRadiusMm,
    1      // aggregation_window_cells (default 1 = single-cell peak;
           // set to 2 for sub-cell-fragmentation recovery on a halved
           // cell_size / r_step grid — see "Sub-cell aggregation" below)
);

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

Cost: `O(n_cells × W³)` per peak-finding pass.  Tests pass for
`W = 1, 2`.  Values >2 are accepted but give diminishing returns.

---

## Planned

- `mist::ring_finding::nn_transform` — neural network ring-finder (ONNX
  Runtime inference, trained in PyTorch) as a drop-in companion to the Hough
  transform, operating on the same `mist::ring_finding::Hit` input type.

Open design questions and feature plans live as GitHub issues with the
`design` and `enhancement` labels.

---

## Project structure

```
mist/
├── CMakeLists.txt
├── CHANGELOG.md
├── cmake/
│   └── mistConfig.cmake.in
├── include/mist/
│   ├── mist.h                          # umbrella include
│   ├── Rnd.h                           # header-only RNG
│   ├── logger/
│   │   ├── logger_types.h              # enums, ansi()
│   │   ├── logger.h                    # logging functions + anchor registry
│   │   ├── ProgressBar.h              # single-line progress bar
│   │   └── MultiProgressBar.h        # composite header + subtask bars
│   └── ring_finding/
│       └── HoughTransform.h
├── src/
│   ├── logger/
│   │   ├── logger.cxx
│   │   ├── logger_types.cxx
│   │   ├── ProgressBar.cxx
│   │   └── MultiProgressBar.cxx
│   └── ring_finding/
│       └── HoughTransform.cxx
├── test/
│   ├── tester_logger.cxx               # logger + bars
│   ├── tester_rnd.cxx                  # RNG
│   └── tester_hough.cxx                # Hough transform
└── scripts/
    ├── install.sh                      # honours MIST_INSTALL_PREFIX
    └── install_with_tests.sh           # build + optionally run tests
```

---

## License

MIT License. See `LICENSE` for details.

---

*MIST: because physics software deserves infrastructure that doesn't get in the way.*
