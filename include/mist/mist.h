// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file mist.h
 * @brief MIST — Make It Simple, Toolkit
 *
 * Top-level umbrella header. Include this to pull in the entire library.
 * Individual subsystem headers can be included directly for finer control:
 *
 * @code{.cpp}
 * #include <mist/mist.h>                         // everything
 * #include <mist/logger/logger.h>                // logger + ProgressBar
 * #include <mist/logger/progress_bar.h>          // ProgressBar only
 * #include <mist/ring_finding/hough_transform.h> // Hough ring-finder
 * #include <mist/algo/binning.h>                 // block_mean, block_rms
 * #include <mist/algo/smoothing.h>               // moving_mean
 * #include <mist/rnd.h>                          // RNG only
 * @endcode
 *
 * Cascade structure:
 *
 *   mist.h
 *   ├── rnd.h
 *   ├── logger/logger.h
 *   │   ├── logger/logger_types.h    (ColourTag, StyleTag, LevelTag, ansi())
 *   │   ├── logger/progress_bar.h
 *   │   └── logger/multi_progress_bar.h
 *   ├── ring_finding/hough_transform.h
 *   └── algo/
 *       ├── binning.h                 (block_mean, block_rms)
 *       └── smoothing.h               (moving_mean)
 */

//  --- Random utility
#include <mist/rnd.h>

//  --- Logger utility
#include <mist/logger/logger.h>
#include <mist/logger/progress_bar.h>
#include <mist/logger/multi_progress_bar.h>

//  --- Domain algorithms
#include <mist/ring_finding/hough_transform.h>
#include <mist/ring_finding/circle_fit.h>
#include <mist/ring_finding/ring_model.h>

//  --- Generic algorithmic primitives
#include <mist/algo/binning.h>
#include <mist/algo/smoothing.h>
#include <mist/algo/util.h>
#include <mist/algo/edges.h>

//  --- Bit-mask helpers
#include <mist/bits.h>

//  --- Statistics
#include <mist/stats/sideband.h>
#include <mist/stats/timing.h>

//  --- I/O and time helpers
#include <mist/io.h>
#include <mist/time.h>
