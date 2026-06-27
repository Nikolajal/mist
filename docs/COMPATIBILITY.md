# Compatibility contract

This document declares the stability guarantees mist provides to downstream
consumers from v1.0.0 onward. It is binding: any change that violates the
guarantees below requires a corresponding major-version bump.

## Versioning

mist follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

| Component of `MAJOR.MINOR.PATCH` | Bump triggers |
|---|---|
| **MAJOR** | Any backward-incompatible change to the public API, ABI of the static library, CMake target structure, or supported-toolchain baseline. |
| **MINOR** | Additive changes: new symbols, new template parameters with defaults, new CMake targets, new options. Existing call sites must continue to compile and behave identically. |
| **PATCH** | Bug fixes and internal refactors. No observable behavioural change to documented contracts. |

The CMake config file declares `SameMajorVersion` compatibility:

```cmake
find_package(mist 1 REQUIRED)   # accepts any 1.x.y
```

## Deprecation policy

Any symbol marked for removal at the next major version MUST first be
marked `[[deprecated("...")]]` in at least one preceding minor release.
The deprecation message MUST state the replacement (or that there is no
replacement) and the release in which removal is planned.

Example timeline for removing symbol `S`:

| Release | State of `S` |
|---|---|
| 1.4.0 | Functional, undocumented as deprecated. |
| 1.5.0 | Functional, `[[deprecated("Use T instead; removed in 2.0")]]`. |
| 1.6.0 – 1.x | Functional, still deprecated. Warning emitted on use. |
| 2.0.0 | Removed. |

The `[[deprecated]]` lifetime is the contract; minor versions added solely
to clear the deprecation cycle are acceptable. A symbol introduced as
deprecated (i.e. never had a non-deprecated release) violates the policy
and indicates a design error that should be corrected before the symbol
ships.

## Public API surface

Everything reachable through the headers in `include/mist/` is part of the
public API. This includes:

- All types and free functions in `mist::`, `mist::logger::`,
  `mist::ring_finding::`, `mist::algo::`, `mist::stats::`, and `mist::bits::`.
  The free functions in `include/mist/io.h` (`read_csv`, `read_txt`,
  `get<T>`, `get_column<T>`) and `include/mist/time.h` (`parse`,
  `to_string`) are also part of the public API even though they do not live
  in a named sub-namespace.
- Public data members of public types (e.g. `ring_finding::Hit::x`,
  `ring_finding::FindRingsOptions::threshold_fraction`).
- All `inline constexpr` named constants (`ring_finding::kDefaultCellSizeMm`,
  `ring_finding::kDefaultCollectionRadiusMm`, and the
  `ring_finding::detail::kNewton*` / `kDetRelTol` constants in
  `circle_fit.h` — though these live in `detail::` and are therefore
  **not** part of the stability contract).
- CMake target `mist::mist`, its `INTERFACE_COMPILE_FEATURES`, its
  `INTERFACE_INCLUDE_DIRECTORIES`, and the `find_package(mist X REQUIRED)`
  surface.

Anything in a `detail::` sub-namespace, anything in a `private:` section,
and anything reachable only through internal includes is **not** part of
the public API and may change between any two releases without bump.

## Toolchain baseline

mist 1.x requires:

| Component | Minimum version |
|---|---|
| C++ standard | C++20 |
| GCC | 10 |
| Clang | 10 |
| AppleClang | Xcode 12.5+ |
| MSVC | 19.29 (Visual Studio 2019 16.10) |
| CMake | 3.14 |

Raising any of these minimums is a MAJOR-version change.

## ABI

mist is a static library; the binary-interface surface seen by consumers
is the symbols exported by `libmist.a`. The library makes no
binary-compatibility guarantee across compiler versions or build
configurations. Header-only consumers (`mist::Rnd`, the `mist::algo`
templates) are unaffected: their compatibility is purely source-level.

Within a `MAJOR.x` line, recompiling against a newer mist should not
require code changes; relinking remains necessary because the static
archive's symbol set can grow.

## Branching and release

Documented in [`CONTRIBUTING.md`](../CONTRIBUTING.md). Summary: `main`
holds release-tagged commits only; `dev` is the integration branch;
topic branches (`dev_patch_<name>`, `dev_<feature_name>`) merge to `dev`.
Each tag is created on `main` after a `dev → main` merge.

## Sibling repositories

[`mist-hep`](https://github.com/Nikolajal/mist-hep) follows an independent
version train and declares its own compatibility contract. Its dependency
on mist is expressed via `find_package(mist X REQUIRED)` with the
`SameMajorVersion` semantics above.

A future heavy-runtime sibling (provisionally `mist-heavyalgo`) would
follow the same pattern when introduced.
