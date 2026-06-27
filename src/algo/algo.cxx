// SPDX-License-Identifier: MIT
//
// Translation unit placeholder for the mist::algo module.
//
// mist::algo is currently header-only: all templates are instantiated at the
// call site. This TU exists so the mist static library has a concrete object
// file contribution for the algo module, and so non-template helpers added in
// the future have a home without restructuring the build.
//
// If the module remains header-only indefinitely this file can be removed
// and the corresponding entry deleted from CMakeLists.txt; until then it
// keeps the include/src directory layout symmetric across modules.

#include <mist/algo/binning.h>
#include <mist/algo/edges.h>
#include <mist/algo/intersect.h>
#include <mist/algo/smoothing.h>
#include <mist/algo/util.h>

// Intentionally empty.
