# Changelog

All notable changes to MIST are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [1.3.0] — algo/rnd/io expansion + Python bindings

Additive only, fully backward-compatible (`SameMajorVersion`).

### Added
- **`mist::algo::weighted_block_mean`** (`include/mist/algo/binning.h`) — block
  mean with per-element weights; returns `vector<T>`. Range overload included.
  Companion to `block_mean` / `block_rms`.
- **`mist::algo::ema`** (`include/mist/algo/smoothing.h`) — exponential moving
  average with smoothing factor `alpha ∈ (0, 1]`. Iterator-pair + range overloads;
  returns `vector<T>`.
- **`mist::algo::gaussian_smooth`** (`include/mist/algo/smoothing.h`) — 1-D
  Gaussian kernel convolution with width parameter `sigma` (in samples). Kernel
  truncated at ±3σ. Iterator-pair + range overloads; returns `vector<T>`.
- **`mist::algo::linspace`** (`include/mist/algo/edges.h`) — evenly-spaced bin
  edges `[x_min, x_max]` with pinned endpoints; ROOT-free gap-filler until
  `std::linspace` lands in C++26. Analogous to `log_binning` on a linear scale.
- **`mist::algo::sign`** (`include/mist/algo/util.h`) — sign function returning
  +1 / 0 / −1 for any arithmetic type; constexpr, concept-constrained.
- **`mist::Rnd::exponential<T>(rate)`** (`include/mist/rnd.h`) — draws from an
  exponential distribution with mean `1/rate`; rejects non-positive `rate` with
  `std::invalid_argument`. Complements `uniform`, `normal`, `poisson`.
- **`mist::Rnd::discrete(weights)`** (`include/mist/rnd.h`) — samples an integer
  index from a discrete distribution given a weight list or `std::span<const
  double>`.
- **`mist::io::get<T>(table, col, row)`** / **`mist::io::get_column<T>(table,
  col)`** (`include/mist/io.h`) — typed accessors for the `read_csv` /
  `read_txt` column-keyed table. `get<T>` returns `T{}` for a missing column or
  out-of-range row (never throws); `get_column<T>` returns an empty vector for a
  missing column.
- **`mist::logger::set_log_file(path)`** (`include/mist/logger/logger.h`) — route
  all logger output to a plain-text (no ANSI) file in addition to the terminal;
  opened in append mode. Pass `""` to close. Thread-safe (registry mutex).
- **`mist::logger::done(msg, flush)` / `log(msg, ColourTag, styles, flush)`** —
  `flush` parameter (default `true`) propagated to the free-colour log path and
  the `done()` completion line, which previously had no flush control.
- **`mist::ring_finding::HoughTransform::get_x_max()` / `get_y_max()`** — public
  getters for the detector-extent upper bounds; `get_x_min()` / `get_y_min()`
  were already exposed, completing the symmetric set.
- **Python bindings** (`bindings/mist_ring.cpp`) — exposes the ring-finding
  subsystem (`Hit`, `RingResult`, `HoughTransform` / `FindRingsOptions`,
  `find_rings_ransac` / `RansacOptions`, `circle_fit` / `CircleFitResult` /
  `circle_method`) to Python via pybind11. Enabled with `-DMIST_BUILD_PYTHON=ON`.
  Import as `import mist_ring`.

### Changed
- **`mist::ring_finding::circle_fit` default method** changed from
  `circle_method::kasa` to `circle_method::taubin`. Taubin is recommended in the
  header documentation and has always been used by the RANSAC refiner; the
  default now matches the guidance. Call sites that relied on the implicit `kasa`
  default are unaffected if they pass the argument explicitly; unmarked call
  sites now get Taubin.

### Fixed
- **`mist::Rnd::exponential<T>(rate)`** — missing guard on non-positive `rate`
  caused `std::exponential_distribution` to invoke UB. Now throws
  `std::invalid_argument` for `rate ≤ 0`.
- **`mist::algo::block_rms`** — the iterator template was constrained to
  `std::input_iterator` but the implementation traverses the range twice (once
  for the mean, once for the RMS), which is UB for single-pass iterators. The
  constraint has been tightened to `std::forward_iterator`.
- **`mist::logger::log` file sink** — the file write occurred outside the
  `log_print_guard` scope, introducing a data race with a concurrent
  `set_log_file()` call on another thread. Moved inside the guard so the file
  access is covered by the registry mutex.
- **`HoughTransform::build_lut`** — calling `build_lut` with an empty
  `index_to_hit_xy` map caused a dereference of a null iterator
  (`std::sort`/`std::unique` on a zero-size container is UB in MSVC). Now
  returns early with a warning.

### Internal
- Newton solver constants in `circle_fit.h` extracted to named constants
  (`kNewtonMaxIter`, `kNewtonRelTol`, `kNewtonInitY`, `kDetRelTol`).
- Orphaned audit-tag references (`B1`–`B13`) in logger and ring-finding sources
  replaced with self-contained explanations.
- Dead `int term_width` parameter removed from
  `MultiProgressBar::_emit_line` — was suppressed with `/**/` and never
  used after the renderer was simplified.
- `HoughTransform::build_lut` log line converted to `std::format` overload.
- Test coverage added: `test_exponential_rejects_invalid_rate`,
  `test_rnd_engine_access`, `get<T>` / `get_column<T>` suite in `test_io`,
  `test_set_log_file` in `test_logger`.

---

## [1.2.0] — grid-free ring finding

Additive only, fully backward-compatible (`SameMajorVersion`).

### Added
- **`mist::ring_finding::find_rings_ransac`**
  (`include/mist/ring_finding/ransac_ring_finder.h`) — a grid-free RANSAC ring
  finder for the regime the Hough accumulator handles poorly: a far-off-centre
  Cherenkov arc (centre well outside the sensor) on a uniform-noise majority,
  where a single least-squares circle collapses to the noise centroid near the
  origin. Samples 3 hits → the closed-form circle, scores by inlier **excess
  over background per mm of on-sensor arc length** (the *completeness
  correction* — a far 36° arc showing ~10 % of its ring competes on equal
  footing with a fully-visible small ring), gates on Poisson significance to
  reject pure noise, and refines the winner with the Taubin `circle_fit`.
  Reuses the shared `Hit` / `RingResult` types; supports optional per-hit
  `weights` (down-weight high-occupancy channels) and an explicit sensor
  fiducial (`RansacOptions::fiducial_*`) for sparse per-event frames.
  Header-only and ROOT-free. New `test_ransac` covers far-arc recovery under a
  noise majority, the sparse / fiducial regimes, two-ring remove-and-repeat,
  pure-noise rejection, and cross-platform determinism.

### Fixed
- **`find_package(mist 1.x)` version check** — the CMake `project(... VERSION)`
  had been left at `1.0.0` through the 1.1.0 release, so the installed
  `mistConfigVersion.cmake` advertised `1.0.0` and a downstream
  `find_package(mist 1.1)` (or `1.2`) failed the `SameMajorVersion` check
  despite the API being present. The project version now tracks the release.

### Internal
- Adopted a shared `.clang-format` (LLVM-based, repo conventions) with a repo-
  wide reformat, and a CI `clang-format` gate mirroring the sibling
  beam-test-analysis workflow. The formatter is pinned to clang-format 22 (via
  the PyPI wheel) so CI matches the maintainer's local toolchain exactly;
  Ubuntu's apt clang-format-18 drifts on some constructs.
- The RANSAC sampler draws triplet indices with a portable Lemire multiply-shift
  over the engine's raw 32-bit output instead of `std::uniform_int_distribution`
  (whose mapping is implementation-defined), making the finder bit-identical
  across libstdc++ / libc++ / MSVC.

---

## [1.1.0] — geometry primitives

First minor release on the 1.x line: additive only, fully backward-compatible
(`SameMajorVersion`).

### Added
- **`mist::algo::intersect_lines` / `line_zero_crossing`**
  (`include/mist/algo/intersect.h`) — closed-form intersection of two fitted
  lines, and the zero-crossing of a single line, each with first-order error
  propagation from the (independent) slope/intercept uncertainties. Returns
  `{x, x_err, y, y_err, ok}` / `{value, error, ok}`; `ok` is false for parallel
  lines or a horizontal line. Detector-agnostic (e.g. breakdown-voltage
  extraction); ROOT-free. Gives the mist bundle a correct home for the
  intercept maths that downstream analyses had been re-deriving.

---

## [1.0.0] — API freeze

First release covered by the stability contract documented in
[`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md): semantic versioning with
the `SameMajorVersion` compatibility policy, deprecation cycle required for
any symbol removal within 1.x, and the branching model documented in
[`CONTRIBUTING.md`](CONTRIBUTING.md).

### Added
- **`mist::ring_finding::ring_model`** (`include/mist/ring_finding/ring_model.h`)
  — parametric Cherenkov-ring signal density: a normalised Gaussian-in-radius
  ring of yield `N_gamma` on a flat background, with an azimuthally-varying
  width assembled from optional difference-of-logistic acceptance features.
  Provides `logistic_window`, `ring_sigma`, `ring_density_polar` /
  `ring_density_xy`, and `ring_contour` (Cartesian σ-level contour points,
  ROOT-free replacement for the beam-test `plot_ring_integral` `TGraph`
  factory). `TMath::Pi`/`TMath::Gaus` replaced by `<numbers>`/`<cmath>`. The
  consuming `TH2` chi-squared fit stays in the ROOT-coupled downstream.
- **`mist::stats::triangle_acceptance` / `poisson_rate_mle`**
  (`include/mist/stats/timing.h`) — same-frame timing primitives:
  `triangle_acceptance(dt, L)` gives the triangular pair-acceptance
  `(L-|dt|)/L` (floored inside the support, 0 outside ±L) for flattening a Δt
  distribution; `poisson_rate_mle(intervals)` is the closed-form
  `λ = 1/mean(Δt)` MLE, the ROOT-free counterpart of fitting `expo` to a
  consecutive-Δt histogram. Both upstreamed from the beam-test lightdata
  writer.
- **`mist::logger` `std::format` overloads** — `log` and the
  `error`/`warning`/`info`/`debug`/`plain` wrappers now accept a
  `std::format_string` plus arguments (`info("fitted {} spectra", n)`). They
  require ≥ 1 format argument so zero-argument calls still bind the existing
  `(std::string_view, bool)` overloads; a lone-`bool` argument is deliberately
  left ambiguous (documented) to preserve the `flush` form.
- **`mist::logger::done(...)`** — green/bold completion line; always shown
  (routes through the free-colour `log`, not a new `LevelTag`). `std::format`
  overload included.
- **`mist::logger::ScopedCoutToMist`** — RAII guard routing `std::cout` /
  `std::cerr` through the logger's anchor protocol for its lifetime (so ROOT
  `Fit()`/minimiser chatter cooperates with progress bars); restores the
  stream buffers on destruction and is a no-op when output is not a TTY.
  These three reconcile the `docs/CODING_CONVENTIONS.md` examples, which
  advertised APIs that did not yet exist.
- **`mist::io`** (`include/mist/io.h`) — delimited-text reader: `read_csv`
  (empty fields preserved) and `read_txt` (whitespace runs collapsed) into a
  column-keyed `std::map`, with an optional out-parameter for column order.
  Diagnostics via `mist::logger`; a missing file is logged and yields an empty
  result, never an exception.
- **`mist::time`** (`include/mist/time.h`) — `parse` / `to_string` for the
  compact `"YYYYMMDD-HHMMSS"` timestamp (`std::optional<std::time_t>`).
- **`mist::algo::log_binning`** (`include/mist/algo/edges.h`) — `n_bins + 1`
  log10-spaced bin edges over `[x_min, x_max]`; ROOT-free core counterpart of
  the mist-hep histogram helper.
- **`mist::bits`** (`include/mist/bits.h`) — 32-bit mask helpers
  `encode_bit`, `encode_bits` (range/concept-generalised), `count_trailing_zeros`,
  `decode_bits`. Upstreamed from a downstream detector framework; modernised
  against C++20 `<bit>` (`std::countr_zero`, `std::popcount`).
- **`mist::ring_finding::circle_fit`** (`include/mist/ring_finding/circle_fit.h`)
  — closed-form least-squares circle fit refining a set of points into
  `{x0, y0, radius}` + RMS residual. Complements the Hough transform (which
  finds rings but cannot refine them). ROOT-free and dependency-free; takes
  any forward range of `Point2` (`.x`/`.y`), interoperates with
  `ring_finding::Hit`. Replaces a downstream `ROOT::Fit::Fitter`-based helper.
  Selectable `circle_method`: `kasa` (default), `taubin` (recommended; least
  small-arc bias), `pratt` — the Eigen-free algebraic fits from the Compass
  library, via scalar Newton on the characteristic polynomial. The
  geometric/iterative Compass algorithms (which need a linear-algebra backend)
  are intentionally not included; see `DISCUSSION.md` D-06.
- **`mist::stats::sideband_subtract`** (`include/mist/stats/sideband.h`) —
  detector-agnostic sideband-subtraction signal estimate over a binned
  spectrum (peak window minus equal-width flanking sidebands), returning
  `{signal, error, peak, background}`. ROOT-free, operates on a
  `std::span<const double>` of bin contents. New `mist::stats` namespace for
  ROOT-free HEP statistics (distinct from the ROOT-backed `mist::hep::stats`
  in the sibling repo).
- **C++20** is the new minimum language standard. Toolchain baselines
  (`GCC ≥ 10`, `Clang ≥ 10`, `AppleClang from Xcode 12.5+`, `MSVC ≥ 19.29`)
  are documented in `README.md` and `CONTRIBUTING.md`.
- **`mist::algo`** — new namespace for cross-domain algorithmic primitives.
  Initial contents (`mist:D-05`):
  - `mist::algo::block_mean(first, last, n, drop_partial = false)`
  - `mist::algo::block_rms(first, last, n, drop_partial = false)`
  - `mist::algo::moving_mean(first, last, n)`
  Iterator-pair primary signature with a range-based convenience overload;
  floating-point constrained via the `std::floating_point` concept;
  returns an owning `std::vector<T>`. The `drop_partial = false` default
  fixes the off-by-one in the corresponding AAU sources; pass `true` to
  reproduce the AAU behaviour bit-for-bit.
- **`ring_finding::FindRingsOptions`** (`mist:D-02`) — aggregate struct
  carrying the five tuning parameters previously passed positionally to
  `HoughTransform::find_rings`. Defaults preserve the historical 0.1.0
  positional-argument behaviour. C++20 designated-initialiser call sites:
  ```cpp
  auto rings = ht.find_rings(hits,
      {.threshold_fraction = 0.2f, .max_rings = 3});
  ```
- **`ring_finding::kDefaultCellSizeMm`** / **`kDefaultCollectionRadiusMm`**
  promoted to namespace scope as `inline constexpr float`. Backward
  aliases remain as static members of `HoughTransform` for source
  compatibility through the 1.x line.
- Defaulted three-way comparison (`operator<=>`) on `ring_finding::Hit`
  and `ring_finding::RingResult`. Lexicographic over the public data
  members; gives `==`, `!=`, `<`, `<=`, `>`, `>=` for free.
- Build-tree CMake export (`mist:T-01`): `export(EXPORT mistTargets ...)`
  writes `mistTargets.cmake` into the build directory so downstream
  consumers can point `-Dmist_DIR=/path/to/mist/build` at an un-installed
  build tree without going through `cmake --install` first.
- `CONTRIBUTING.md` — branching model (`main`/`dev`/`dev_patch_*`/
  `dev_<feature>`), release procedure, toolchain baseline, conventions
  cross-reference.
- `docs/CODING_CONVENTIONS.md` — naming, output discipline, file layout,
  include order.
- `docs/COMPATIBILITY.md` — declared semver + `SameMajorVersion` contract.
- `DISCUSSION.md` promoted from local scratchpad to tracked + published
  design log; structured on the hub / triage-taxonomy / design / TODO /
  attention / feature-queue / conventions template.
- `.github/workflows/docs.yml` — Doxygen + GitHub Pages publish on push
  to `main`.

### Changed
- **BREAKING** (`mist:D-02`): `HoughTransform::find_rings()` six-positional
  signature replaced by `find_rings(hits, const FindRingsOptions& = {})`.
  Pre-1.0 freedom permits the clean break; no backward-compatible
  positional overload. Call sites must migrate to the options struct.
- `Rnd::uniform(start, end)` argument-order contract (`mist:D-01`):
  debug builds assert the precondition `start <= end`; release builds
  retain the silent-swap behaviour. Callers MUST pass sorted arguments;
  release tolerance is not promised beyond 1.x.
- High-tier C++20 modernisations (M-1 sweep):
  - `Rnd::uniform`, `Rnd::normal`: `std::enable_if_t<is_floating_point_v>`
    SFINAE replaced by the `std::floating_point` concept.
  - `ProgressBar::update`, `SubtaskProgressBar::update`,
    `MultiProgressBar::update`: `std::enable_if_t<is_integral_v>` SFINAE
    replaced by the `std::integral` concept.
  - `Rnd::generate_phi`, `HoughTransform::build_lut`: hand-rolled `π`
    literals replaced by `std::numbers::pi` and `std::numbers::pi_v<float>`.

### Fixed
- `MultiProgressBar` — every internal `AnchorObject::erase_all()` /
  `AnchorObject::redraw_all()` pair (in `update`, `set_header`, `finish`,
  `_subtask_updated_locked`, `_subtask_finished_locked`, `_set_main_fraction`,
  `_set_main_progress`, `set_unit`, `restart`) is now wrapped in an
  `AnchorObject::registry_lock()` scope. Without it, a concurrent updater
  could slip an erase or redraw between these two steps and corrupt the
  cursor band — leaving stray bar segments above the committed log output.
- `MultiProgressBar::finish()` — committed bar lines were occasionally
  overwritten by the next log line ("active mismatch" bug).
  `_draw_locked()` sets `last_line_count_ = 1 + 1 + n_subtasks` because it
  expects those lines to remain editable anchor content, but `finish()`
  has just set `active_ = false`, marking them as permanent scrolling
  output. The next `erase_all()` (from any subsequent logger call) read
  the non-zero `last_line_count_` and walked the cursor back up into the
  committed output. Fixed by zeroing `last_line_count_` after
  `_draw_locked()` inside the registry-lock scope.

### Tooling
- Sibling repository **[mist-hep](https://github.com/Nikolajal/mist-hep)**
  declared as the canonical home for ROOT-typed analysis helpers built on
  mist. mist itself stays ROOT-free.

---

## [0.1.0] — initial release

### Added
- `mist::Rnd` — `std::mt19937` wrapper with `uniform`, `normal`, `poisson`,
  `generate_phi`, and `reseed`.
- `mist::logger` — coloured ANSI terminal logger with level filtering, TTY
  auto-detection, in-place update lines (`update` / `end_update`), and the
  anchor-object registry that keeps progress bars intact while log lines scroll.
- `mist::logger::ProgressBar` — single-bar progress widget with BLOCK and
  ARROW fill styles; auto-detects terminal width via `ioctl(TIOCGWINSZ)`.
- `mist::logger::MultiProgressBar` — composite bar: one main header line
  plus N labelled subtask lines, each independently driven and timed.  Supports
  unknown-total mode (`total ≤ 0`) and header-text mode (`set_header()`).
- `mist::ring_finding::HoughTransform` — LUT-accelerated circular Hough
  transform; two-phase (build_lut once, find_rings per event); iterative ring
  extraction with active-Hit removal between passes.
- CMake install rules with `find_package(mist REQUIRED)` support
  (`mistConfig.cmake`, `mistConfigVersion.cmake`, `mistTargets.cmake`).
- `scripts/install.sh` and `scripts/install_with_tests.sh` for one-command
  install; both honour the `MIST_INSTALL_PREFIX` environment variable
  (fall-back: `$HOME/.local`).

### Fixed
- `MultiProgressBar::finish()` — `_draw_locked()` was called without holding
  `mutex_`, introducing a data race on the final committed frame.  Added a
  `std::lock_guard` scope around the call.
- `ProgressBar::assign_tag()` — did not reset `suffix_width_` after changing
  the tag, so the layout was not recomputed for the new tag width.  Now sets
  `suffix_width_ = -1` to force a recompute on the next `update()`.
- `HoughTransform::find_rings()` — inverted cluster-size comparison caused the
  algorithm to keep a *smaller* cluster over the current best.  Changed `<` to
  `<=` so only strictly larger clusters replace the winner.

### Changed
- `mist::logger` convenience wrappers (`error`, `warning`, `info`, `debug`,
  `plain`) now accept `std::string_view` instead of `const std::string &`,
  avoiding unnecessary heap allocations when called with string literals or
  `std::string_view` arguments.
- `HoughTransform::find_rings()` default `collection_radius` and
  `HoughTransform` private `cell_size_` member now reference the named
  constants `kDefaultCollectionRadiusMm` and `kDefaultCellSizeMm` instead of
  bare literals `6.f` and `3.2f`.
- `MultiProgressBar` exposes `kUnknownTotal = -1` as a named constant;
  internal `-1` sentinels replaced.
- `SubtaskProgressBar::is_active()` documentation corrected: `active_` is
  `true` from construction (not from the first `update()` call).
- `cmake/mistConfig.cmake.in` now sets `mist_FOUND TRUE` before
  `check_required_components(mist)`, satisfying consumers that test
  `<Pkg>_FOUND` after `find_package`.
- `CMakeLists.txt` — `enable_testing()` called unconditionally (before
  `if(MIST_BUILD_TESTS)`), so `ctest` works when the option is toggled from
  the command line without a full CMake re-run.  `add_test()` registration
  added for `test_logger`.

---

[1.3.0]: https://github.com/Nikolajal/mist/releases/tag/v1.3.0
[1.2.0]: https://github.com/Nikolajal/mist/releases/tag/v1.2.0
[1.1.0]: https://github.com/Nikolajal/mist/releases/tag/v1.1.0
[1.0.0]: https://github.com/Nikolajal/mist/releases/tag/v1.0.0
[0.1.0]: https://github.com/Nikolajal/mist/releases/tag/v0.1.0
