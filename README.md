# MIST — Make It Simple, Toolkit

> *A lightweight C++17 library for physics reconstruction utilities.*
> *Internally known as WHALE (Whoo, Here's Another Library: Egregious!).*

---

## Overview

MIST is a modular C++17 toolkit born out of the need for clean, reusable infrastructure in detector physics software — without pulling in ROOT or other heavy frameworks for things as simple as logging or random number generation.

It currently provides three subsystems:

| Subsystem | Namespace | Description |
|---|---|---|
| Random numbers | `mist::` | RNG wrapper with convenient distributions |
| Logger | `mist::logger::` | Coloured terminal logger with progress bars |
| Ring finding | `mist::ring_finding::` | Circular Hough-transform ring-finder |

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

This installs:
- `libmist.a` (or `.so`) under `lib/`
- Headers under `include/mist/`
- CMake package config under `lib/cmake/mist/`

---

## Usage

### Single include

```cpp
#include <mist/mist.h>   // pulls in everything
```

Or include only what you need:

```cpp
#include <mist/rnd.h>                             // RNG only
#include <mist/logger/logger.h>                   // logger + progress_bar
#include <mist/ring_finding/hough_transform.h>    // Hough ring-finder
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

---

## Subsystems

### `mist::rnd` — Random Number Generator

A thin wrapper around `std::mt19937` with convenient distribution methods.
Supports deterministic and non-deterministic seeding. Not thread-safe —
give each thread its own instance.

```cpp
mist::rnd rng;                        // non-deterministic seed
mist::rnd rng(42);                    // deterministic seed

double x   = rng.uniform(0.0, 1.0);  // Uniform[0, 1)
float  y   = rng.normal(0.f, 1.f);   // N(0, 1)
int    z   = rng.poisson(5);         // Poisson(λ=5)
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
mist::logger::set_colour_enabled(true);              // override TTY detection
mist::logger::set_min_level(mist::logger::level_tag::DEBUG); // show all levels

mist::logger::info("Initialising detector");
mist::logger::debug("n_channels = 1024");
mist::logger::warning("Calibration file not found, using defaults");
mist::logger::error("Failed to open geometry file");
mist::logger::plain("Raw output, no prefix");

// Custom colour and style
mist::logger::log("Custom message",
    mist::logger::colour_tag::MAGENTA,
    {mist::logger::style_tag::BOLD, mist::logger::style_tag::ITALIC});
```

#### In-place update line

```cpp
for (int i = 0; i < n_spills; ++i)
{
    mist::logger::update("Processing spill " + std::to_string(i));
    process(i);
}
mist::logger::end_update();
mist::logger::info("All spills processed.");
```

#### Progress bar

```cpp
// Driven by current / total count
mist::logger::progress_bar bar(mist::logger::bar_style::BLOCK);
for (int i = 0; i <= n; ++i)
{
    bar.update(i, n);
    do_work(i);
}
bar.finish();

// Driven by pre-computed fraction
mist::logger::progress_bar bar(mist::logger::bar_style::ARROW);
bar.update(0.42);   // 42%
bar.finish();
```

The progress bar auto-detects terminal width via `ioctl(TIOCGWINSZ)` and
displays percentage, current/total count, elapsed time, and ETA. A normal
`log()` call while the bar is active auto-commits it first so output is
never corrupted.

---

### `mist::ring_finding::hough_transform` — Circular Hough Transform

A two-phase, LUT-accelerated circular Hough transform for ring reconstruction
in the detector (x, y) plane. The LUT is built once per geometry and reused
across all events, making per-event cost purely proportional to the number of
active hits.

#### Hit and result types

```cpp
// Input
mist::ring_finding::hit h;
h.x       = 123.4f;   // hit x-position [mm]
h.y       =  56.7f;   // hit y-position [mm]
h.time    =   8.9f;   // calibrated hit time [ns]
h.lut_key =     3;    // typically global_channel_index / 4

// Output
mist::ring_finding::ring_result r;
r.cx          // ring centre x [mm]
r.cy          // ring centre y [mm]
r.radius      // ring radius [mm]
r.peak_votes  // accumulator peak vote count
r.mean_time   // mean time of assigned hits [ns]
r.hit_indices // indices into the input hit vector
```

#### Typical workflow

```cpp
// --- Once per run / geometry change ---
std::map<int, std::array<float, 2>> geometry = load_geometry();

mist::ring_finding::hough_transform ht;
ht.build_lut(geometry,
    30.f,   // r_min   [mm]
    80.f,   // r_max   [mm]
     1.f,   // r_step  [mm]
     3.2f   // cell_size [mm]
);

// --- Per event ---
std::vector<mist::ring_finding::hit> hits = make_hits(raw_hits);

auto rings = ht.find_rings(hits,
    0.3f,  // threshold_fraction: min fraction of active hits in peak
    5,     // min_hits: minimum absolute vote count
    5,     // min_active: minimum hits remaining to attempt next ring
    2,     // max_rings (default)
    6.f    // collection_radius [mm] (default)
);

for (auto &ring : rings)
{
    mist::logger::info(
        "Ring: cx=" + std::to_string(ring.cx) +
        " cy="      + std::to_string(ring.cy) +
        " R="       + std::to_string(ring.radius) +
        " votes="   + std::to_string(ring.peak_votes)
    );
}
```

#### Algorithm notes

- The LUT maps each `lut_key` to the flat accumulator cell indices it votes
  for at every radius bin, computed once at `build_lut` time.
- Per-event cost is O(hits × R_bins × arc_cells), with arc_cells typically
  small after deduplication.
- After each ring is found, its contributing hits are removed from the active
  set and the accumulator is reset before searching for the next ring. This
  avoids spatial-suppression artefacts when two rings are close together.
- The acceptance threshold is evaluated against the *initial* active hit count
  and held fixed across passes for consistency.

---

## Planned

- `mist::ring_finding::nn_transform` — neural network ring-finder (ONNX
  Runtime inference, trained in PyTorch) as a drop-in companion to the Hough
  transform, operating on the same `mist::ring_finding::hit` input type.

---

## Project structure

```
mist/
├── CMakeLists.txt
├── cmake/
│   └── mistConfig.cmake.in
├── include/mist/
│   ├── mist.h                          # umbrella include
│   ├── rnd.h                           # header-only RNG
│   ├── logger/
│   │   ├── logger_types.h              # enums, ansi()
│   │   ├── logger.h                    # logging functions
│   │   └── progress_bar.h             # progress_bar class
│   └── ring_finding/
│       └── hough_transform.h
└── src/
    ├── logger/
    │   ├── logger_types.cpp
    │   ├── logger.cpp
    │   └── progress_bar.cpp
    └── ring_finding/
        └── hough_transform.cpp
```

---

## License

MIT License. See `LICENSE` for details.

---

*MIST: because physics software deserves infrastructure that doesn't get in the way.*
