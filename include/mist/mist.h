#pragma once

/**
 * @file mist.h
 * @brief MIST — Make It Simple, Toolkit
 *
 * Top-level umbrella header. Include this to pull in the entire library.
 * Individual subsystem headers can be included directly for finer control:
 *
 *   #include <mist/mist.h>                        // everything
 *   #include <mist/logger/logger.h>               // logger + progress_bar
 *   #include <mist/logger/progress_bar.h>         // progress_bar only
 *   #include <mist/ring_finding/hough_transform.h> // Hough ring-finder
 *   #include <mist/rnd.h>                         // RNG only
 *
 * Cascade structure:
 *
 *   mist.h
 *   ├── rnd.h
 *   └── logger/logger.h
 *       ├── logger/logger_types.h   (colour_tag, style_tag, level_tag, ansi())
 *       └── logger/progress_bar.h
 *   └── ring_finding/hough_transform.h
 */

#include <mist/rnd.h>
#include <mist/logger/logger.h>
#include <mist/ring_finding/hough_transform.h>
