# Contributing to mist

This document defines the conventions and workflow expected for changes to
mist. The substance of the project — design decisions, open questions, and
the feature queue — lives in [`DISCUSSION.md`](DISCUSSION.md); the code
style lives in [`docs/CODING_CONVENTIONS.md`](docs/CODING_CONVENTIONS.md);
this file covers process.

## Versioning and stability contract

mist follows [Semantic Versioning](https://semver.org/) with the
`SameMajorVersion` compatibility policy declared in
[`CMakeLists.txt`](CMakeLists.txt).

| Phase | Contract |
|---|---|
| Pre-1.0 (current) | No backward-compatibility guarantees. Any release may break any consumer. Active development happens on the `dev` branch directly. Topic branches are encouraged but not required. |
| Post-1.0 | Breaking changes require a major-version bump. Additive changes within `1.x` are permitted. Removal of any symbol requires at least one minor release with a `[[deprecated]]` notice first. The branching model below applies. |

## Branching model (effective at v1.0.0)

| Branch | Role | Lifecycle |
|---|---|---|
| `main` | Delivered software. Holds release-tagged commits only. | Long-lived. Receives merges only from `dev` at release time. Default GitHub branch (visitors land here). |
| `dev` | Integration branch. Active work converges here. | Long-lived. Receives merges from topic branches. |
| `dev_patch_<name>` | Short-lived bug-fix branch. | Created from `dev`; merged back to `dev`; deleted. |
| `dev_<feature_name>` | Short-lived feature branch. | Same pattern as `dev_patch_<name>`. |

### Release procedure (post-1.0)

1. All substantive work for the release lands on `dev` via topic branches.
2. `CHANGELOG.md` and `CMakeLists.txt` version are updated on `dev`.
3. `git push origin dev:main` (fast-forward or merge).
4. `git tag -a vX.Y.Z -m "..."` on `main`.
5. `git push origin main --tags`.
6. The docs workflow auto-fires on the `main` push; verify the new
   version appears on the Pages site.
7. Cut a GitHub Release from the tag with notes derived from the
   `CHANGELOG.md` entry.

## Build and test

```bash
cmake -B build -DMIST_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Tests must pass on the working branch before any merge into `dev`.
CI runs the matrix declared in
[`.github/workflows/ci.yml`](.github/workflows/ci.yml) and blocks merges
that break the green state.

## Toolchain baseline

mist targets **C++20** from v1.0.0 onwards. Minimum supported compilers:

| Compiler | Minimum version |
|---|---|
| GCC | 10 |
| Clang | 10 |
| AppleClang | Xcode 12.5+ |
| MSVC | 19.29 (Visual Studio 2019 16.10) |

CMake ≥ 3.14. No external runtime dependencies.

## Coding conventions

Defined in [`docs/CODING_CONVENTIONS.md`](docs/CODING_CONVENTIONS.md).
Summary:

- `snake_case` for variables, free functions, methods.
- `snake_case_` for private members.
- `PascalCase` for classes, structs, enums.
- `MIST_*` prefix for project macros.
- `lowercase` for namespaces.
- All textual output goes through `mist::logger`; no raw `std::cout` or
  `std::cerr` outside the logger implementation itself.

## Filing issues

Concrete bugs and feature requests belong in the GitHub issue tracker.
Open design questions belong in [`DISCUSSION.md`](DISCUSSION.md) first;
once a question is settled enough to act on, it becomes either a GitHub
issue, a topic branch, or both.

## Sibling repository

ROOT-typed analysis helpers are in
[`mist-hep`](https://github.com/Nikolajal/mist-hep), a sibling repository
that depends on mist and follows the same conventions and branching model
(only the macro prefix differs: `MIST_HEP_*`). Changes that affect both
repositories should be reviewed in coordination.
