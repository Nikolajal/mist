// SPDX-License-Identifier: MIT
//
// Python bindings for the mist ring-finding subsystem.
//
// Exposes Hit, RingResult, HoughTransform/FindRingsOptions,
// find_rings_ransac/RansacOptions, and circle_fit/CircleFitResult/circle_method.
//
// Build (after installing pybind11):
//   cmake -B build -DMIST_BUILD_PYTHON=ON && cmake --build build
// Import:
//   import sys; sys.path.insert(0, "build")
//   import mist_ring
//
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <mist/ring_finding/circle_fit.h>
#include <mist/ring_finding/hough_transform.h>
#include <mist/ring_finding/ransac_ring_finder.h>

namespace py = pybind11;
using namespace mist::ring_finding;

PYBIND11_MODULE(mist_ring, m)
{
    m.doc() = "mist ring-finding: RANSAC, Hough transform, and algebraic circle fits";

    // ------------------------------------------------------------------
    // Hit
    // ------------------------------------------------------------------
    py::class_<Hit>(m, "Hit",
        "A detector hit with 2-D position, time, and an optional LUT key.")
        .def(py::init([](float x, float y, float time, int lut_key) {
                 return Hit{x, y, time, lut_key};
             }),
             py::arg("x"), py::arg("y"),
             py::arg("time") = 0.f, py::arg("lut_key") = 0)
        .def_readwrite("x",       &Hit::x)
        .def_readwrite("y",       &Hit::y)
        .def_readwrite("time",    &Hit::time)
        .def_readwrite("lut_key", &Hit::lut_key)
        .def("__repr__", [](const Hit &h) {
            return "Hit(x=" + std::to_string(h.x) +
                   ", y=" + std::to_string(h.y) +
                   ", time=" + std::to_string(h.time) + ")";
        });

    // ------------------------------------------------------------------
    // RingResult
    // ------------------------------------------------------------------
    py::class_<RingResult>(m, "RingResult",
        "Result of a ring-finding call: refined centre, radius, and inlier list.")
        .def_readonly("cx",          &RingResult::cx)
        .def_readonly("cy",          &RingResult::cy)
        .def_readonly("radius",      &RingResult::radius)
        .def_readonly("peak_votes",  &RingResult::peak_votes)
        .def_readonly("mean_time",   &RingResult::mean_time)
        .def_readonly("hit_indices", &RingResult::hit_indices)
        .def("__repr__", [](const RingResult &r) {
            return "RingResult(cx=" + std::to_string(r.cx) +
                   ", cy=" + std::to_string(r.cy) +
                   ", radius=" + std::to_string(r.radius) +
                   ", peak_votes=" + std::to_string(r.peak_votes) + ")";
        });

    // ------------------------------------------------------------------
    // FindRingsOptions (Hough)
    // ------------------------------------------------------------------
    py::class_<FindRingsOptions>(m, "FindRingsOptions",
        "Tuning knobs for HoughTransform.find_rings().")
        .def(py::init<>())
        .def_readwrite("threshold_fraction",      &FindRingsOptions::threshold_fraction)
        .def_readwrite("min_hits",                &FindRingsOptions::min_hits)
        .def_readwrite("min_active",              &FindRingsOptions::min_active)
        .def_readwrite("max_rings",               &FindRingsOptions::max_rings)
        .def_readwrite("collection_radius",       &FindRingsOptions::collection_radius)
        .def_readwrite("aggregation_window_cells",&FindRingsOptions::aggregation_window_cells);

    // ------------------------------------------------------------------
    // HoughTransform
    // ------------------------------------------------------------------
    py::class_<HoughTransform>(m, "HoughTransform",
        "Circular Hough transform ring finder. Build the LUT once, call "
        "find_rings() per event.")
        .def(py::init<>())
        .def(py::init<std::map<int, std::array<float, 2>>,
                      float, float, float, float, float>(),
             py::arg("lut"), py::arg("r_min"), py::arg("r_max"),
             py::arg("r_step"), py::arg("cell_size"),
             py::arg("centre_padding_mm") = -1.f)
        .def("build_lut", &HoughTransform::build_lut,
             py::arg("lut"), py::arg("r_min"), py::arg("r_max"),
             py::arg("r_step"), py::arg("cell_size"),
             py::arg("centre_padding_mm") = -1.f)
        .def("is_lut_ready",    &HoughTransform::is_lut_ready)
        .def("find_rings",      &HoughTransform::find_rings,
             py::arg("hits"), py::arg("opts") = FindRingsOptions{})
        .def("get_accumulator", &HoughTransform::get_accumulator)
        .def("get_r_bins",      &HoughTransform::get_r_bins)
        .def("get_nx",          &HoughTransform::get_nx)
        .def("get_ny",          &HoughTransform::get_ny)
        .def("get_cell_size",   &HoughTransform::get_cell_size);

    // ------------------------------------------------------------------
    // RansacOptions
    // ------------------------------------------------------------------
    py::class_<RansacOptions>(m, "RansacOptions",
        "Tuning knobs for find_rings_ransac().")
        .def(py::init<>())
        .def_readwrite("max_rings",            &RansacOptions::max_rings)
        .def_readwrite("iterations",           &RansacOptions::iterations)
        .def_readwrite("inlier_band",          &RansacOptions::inlier_band)
        .def_readwrite("min_inliers",          &RansacOptions::min_inliers)
        .def_readwrite("min_significance",     &RansacOptions::min_significance)
        .def_readwrite("r_min",                &RansacOptions::r_min)
        .def_readwrite("r_max",                &RansacOptions::r_max)
        .def_readwrite("min_visible_arc_frac", &RansacOptions::min_visible_arc_frac)
        .def_readwrite("fiducial_xmin",        &RansacOptions::fiducial_xmin)
        .def_readwrite("fiducial_xmax",        &RansacOptions::fiducial_xmax)
        .def_readwrite("fiducial_ymin",        &RansacOptions::fiducial_ymin)
        .def_readwrite("fiducial_ymax",        &RansacOptions::fiducial_ymax)
        .def_readwrite("seed",                 &RansacOptions::seed);

    // ------------------------------------------------------------------
    // circle_method enum
    // ------------------------------------------------------------------
    py::enum_<circle_method>(m, "circle_method",
        "Algebraic circle-fit algorithm.")
        .value("kasa",   circle_method::kasa,
               "Kasa (eta=0): fast algebraic least-squares.")
        .value("taubin", circle_method::taubin,
               "Taubin (recommended): cubic root, less biased than Kasa.")
        .value("pratt",  circle_method::pratt,
               "Pratt: quartic root, most robust near collinear points.")
        .export_values();

    // ------------------------------------------------------------------
    // CircleFitResult
    // ------------------------------------------------------------------
    py::class_<CircleFitResult>(m, "CircleFitResult",
        "Output of circle_fit().")
        .def_readonly("x0",           &CircleFitResult::x0)
        .def_readonly("y0",           &CircleFitResult::y0)
        .def_readonly("radius",       &CircleFitResult::radius)
        .def_readonly("rms_residual", &CircleFitResult::rms_residual)
        .def_readonly("n_points",     &CircleFitResult::n_points)
        .def_readonly("ok",           &CircleFitResult::ok)
        .def("__repr__", [](const CircleFitResult &r) {
            return "CircleFitResult(x0=" + std::to_string(r.x0) +
                   ", y0=" + std::to_string(r.y0) +
                   ", radius=" + std::to_string(r.radius) +
                   ", rms_residual=" + std::to_string(r.rms_residual) +
                   ", ok=" + (r.ok ? "True" : "False") + ")";
        });

    // ------------------------------------------------------------------
    // find_rings_ransac
    // ------------------------------------------------------------------
    m.def("find_rings_ransac",
          [](const std::vector<Hit> &hits, RansacOptions opts,
             std::vector<float> weights) {
              return find_rings_ransac(hits, opts, weights);
          },
          py::arg("hits"),
          py::arg("opts")    = RansacOptions{},
          py::arg("weights") = std::vector<float>{},
          "Run RANSAC ring finder on a list of Hit objects. "
          "Returns a list of RingResult sorted by descending inlier count.");

    // ------------------------------------------------------------------
    // circle_fit — instantiated for vector<Hit> (satisfies Point2 concept)
    // ------------------------------------------------------------------
    m.def("circle_fit",
          [](const std::vector<Hit> &hits, circle_method method) {
              return circle_fit(hits, method);
          },
          py::arg("hits"),
          py::arg("method") = circle_method::taubin, // taubin is the recommended default
          "Algebraic circle fit on a list of Hit objects (Kasa / Taubin / Pratt). "
          "Default method is Taubin — least small-arc bias, recommended for Hough refinement.");
}
