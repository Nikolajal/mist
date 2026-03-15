#pragma once

/**
 * @file mist.h
 * @brief MIST — Make It Simple, Toolkit
 *
 * Top-level umbrella header. Include this to pull in the entire library.
 * Individual subsystem headers can be included directly for finer control:
 *
 *   #include <mist/mist.h>                         // everything
 *   #include <mist/logger/logger.h>                // logger + progress bars
 *   #include <mist/logger/progress_bar.h>          // single progress bar
 *   #include <mist/logger/multi_progress_bar.h>    // multi-bar + subtasks
 *   #include <mist/ring_finding/hough_transform.h> // Hough ring-finder
 *   #include <mist/math/vec.h>                     // lightweight math vector
 *   #include <mist/rnd.h>                          // RNG only
 *
 * Cascade structure:
 *
 *   mist.h
 *   ├── rnd.h
 *   ├── math/vec.h
 *   ├── ring_finding/hough_transform.h
 *   └── logger/logger.h
 *       ├── logger/logger_types.h        (colour_tag, style_tag, level_tag, ansi())
 *       ├── logger/progress_bar.h        (progress_bar — anchor_object subclass)
 *       └── logger/multi_progress_bar.h  (multi_progress_bar, subtask_progress_bar)
 *
 * ScopedCoutToMist:
 *   mist::logger::ScopedCoutToMist (also: mist::logger::scoped_cout_to_mist)
 *   RAII guard that redirects std::cout to mist::logger::plain, keeping
 *   the anchor system intact even when third-party code writes to cout.
 */

#include <mist/rnd.h>
#include <mist/math/vec.h>
#include <mist/logger/logger.h>
#include <mist/ring_finding/hough_transform.h>
