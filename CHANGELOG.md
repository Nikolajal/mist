# Changelog

All notable changes to MIST are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

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

[1.1.0]: https://github.com/Nikolajal/mist/releases/tag/v1.1.0
[1.0.0]: https://github.com/Nikolajal/mist/releases/tag/v1.0.0
[0.1.0]: https://github.com/Nikolajal/mist/releases/tag/v0.1.0
