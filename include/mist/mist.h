#pragma once

/**
 * @file mist.h
 * @brief MIST — Make It Simple, Toolkit
 *
 * Top-level umbrella header. Include this to pull in the entire library.
 * Individual subsystem headers can be included directly for finer control:
 *
 *   #include <mist/mist.h>                        // everything
 *   #include <mist/logger/logger.h>               // logger + ProgressBar
 *   #include <mist/logger/progress_bar.h>         // ProgressBar only
 *   #include <mist/ring_finding/hough_transform.h> // Hough ring-finder
 *   #include <mist/rnd.h>                         // RNG only
 *
 * Cascade structure:
 *
 *   mist.h
 *   ├── Rnd.h
 *   └── logger/logger.h
 *       ├── logger/logger_types.h   (ColourTag, StyleTag, LevelTag, ansi())
 *       └── logger/ProgressBar.h
 *   └── ring_finding/HoughTransform.h
 */

//  --- Random utility
#include <mist/rnd.h>

//  --- Logger utility
#include <mist/logger/logger.h>
#include <mist/logger/progress_bar.h>
#include <mist/logger/multi_progress_bar.h>

//  --- Algorithm utility
#include <mist/ring_finding/hough_transform.h>
