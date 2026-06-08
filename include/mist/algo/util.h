// SPDX-License-Identifier: MIT
//
// mist/algo/util.h — small standalone algorithmic helpers that do not fit
// the binning / smoothing families.
//
// Header-only. ROOT-free.
//
#pragma once

#include <type_traits>

namespace mist::algo {

// ---------------------------------------------------------------------------
// sign: three-valued signum. Returns -1, 0, or +1.
//
//   sign(x) = (0 < x) - (x < 0)
//
// Branchless; constexpr; noexcept. The standard library has no signum, so
// this fills a common gap. Salvaged from the Bologna Laboratory Utility
// `sgn<T>` (mist:F-05).
// ---------------------------------------------------------------------------
template <typename T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] constexpr int sign(T value) noexcept
{
    return (T(0) < value) - (value < T(0));
}

}  // namespace mist::algo
