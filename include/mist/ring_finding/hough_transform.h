#pragma once
#include <mist/logger/logger.h>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <cmath>
#include <limits>
#include <algorithm>
#include <string>

namespace mist::ring_finding
{

    /**
     * @file hough_transform.h
     * @brief Circular Hough-transform ring-finder.
     *
     * The class operates on @ref hit — a plain POD struct that the caller
     * populates from their detector-specific hit representation.
     *
     * ### Two-phase workflow
     *  1. **Setup** (once per run / geometry change) — call @ref build_lut with
     *     the channel-to-position map to pre-compute which accumulator cells each
     *     LUT key votes for at every candidate radius.
     *  2. **Per-event processing** — call @ref find_rings with a vector of
     *     @ref hit. The method votes, finds the best peak, removes the tagged
     *     hits from the active set, resets the accumulator, and re-votes for the
     *     next ring. Returns a summary of all rings found.
     *
     * ### Ring-extraction logic
     * After the first ring is found its contributing hits are removed from the
     * active set and the accumulator is reset to zero before searching for the
     * next ring. This avoids the spatial-suppression artefacts that arise when
     * two rings are close together. The acceptance threshold for each pass is
     * re-evaluated against the number of hits still active at that point.
     *
     * ### Typical usage
     * @code{.cpp}
     * mist::ring_finding::hough_transform ht;
     *
     * // --- Once per run ---
     * std::map<int, std::array<float,2>> geometry = load_geometry();
     * ht.build_lut(geometry, 30.f, 80.f, 1.f, 3.2f);
     *
     * // --- Per event ---
     * std::vector<mist::ring_finding::hit> hits = make_hits(raw_hits);
     * auto rings = ht.find_rings(hits, 0.3f, 5);
     * @endcode
     */

    // ============================================================
    //  Hit type
    // ============================================================

    /**
     * @brief Minimal hit descriptor consumed by @ref hough_transform.
     *
     * The caller is responsible for populating this struct from whatever
     * detector-specific hit representation is in use. The @c lut_key must
     * match the keys used when building the LUT via @ref hough_transform::build_lut.
     */
    struct hit
    {
        float x;     ///< Hit x-position in the detector plane [mm].
        float y;     ///< Hit y-position in the detector plane [mm].
        float time;  ///< Calibrated hit time [ns].
        int lut_key; ///< Key into the LUT — typically `global_channel_index / 4`.
    };

    // ============================================================
    //  Result type
    // ============================================================

    /**
     * @brief Describes a single ring candidate found by @ref hough_transform::find_rings.
     */
    struct ring_result
    {
        float cx;        ///< x-coordinate of the reconstructed ring centre [mm].
        float cy;        ///< y-coordinate of the reconstructed ring centre [mm].
        float radius;    ///< Reconstructed ring radius [mm].
        int peak_votes;  ///< Number of votes in the winning accumulator cell.
        float mean_time; ///< Mean hit time of hits associated with this ring [ns].

        /// Indices into the input @ref hit vector of hits assigned to this ring.
        std::vector<int> hit_indices;
    };

    // ============================================================
    //  hough_transform
    // ============================================================

    /**
     * @brief Circular Hough-transform ring-finder.
     *
     * The algorithm works in the (x, y) detector plane. For a given candidate
     * radius R and a hit at position (hx, hy), the set of possible ring centres
     * lies on a circle of radius R centred at (hx, hy). The accumulator counts
     * how many hit arcs pass through each (cx, cy, R) cell; a high-count cell
     * indicates a real ring.
     *
     * To avoid per-event arc-drawing the class pre-computes a look-up table (LUT)
     * that maps each integer LUT key to the flat accumulator cell indices it votes
     * for at every R bin. The LUT depends only on detector geometry and must be
     * rebuilt with @ref build_lut whenever the geometry changes.
     *
     * ### Thread safety
     * The LUT is immutable after @ref build_lut returns and may be read from
     * multiple threads concurrently. The per-event accumulator is mutated during
     * @ref find_rings and is **not** thread-safe; use separate instances per thread.
     */
    class hough_transform
    {
    public:
        // ================================================================
        //  Constructors
        // ================================================================

        /// Default constructor — creates an uninitialised finder.
        /// @note @ref build_lut must be called before @ref find_rings.
        hough_transform() = default;

        /**
         * @brief Convenience constructor that immediately builds the LUT.
         *
         * @param index_to_hit_xy  Map from LUT key to (x, y) [mm].
         * @param r_min            Minimum ring radius [mm].
         * @param r_max            Maximum ring radius [mm].
         * @param r_step           Radial bin step [mm].
         * @param cell_size        Accumulator cell size in (x, y) [mm].
         */
        hough_transform(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
                        float r_min, float r_max, float r_step, float cell_size);

        // ================================================================
        /** @name LUT Construction */
        ///@{

        /**
         * @brief Pre-compute the Hough-transform look-up table.
         *
         * For every key in @p index_to_hit_xy and every radius bin the method
         * determines which accumulator cells lie on the corresponding arc and
         * stores their flat indices. Duplicate cells within each R bin are
         * removed.
         *
         * @param index_to_hit_xy  Map from LUT key to hit position [mm].
         * @param r_min            Minimum ring radius [mm].
         * @param r_max            Maximum ring radius [mm].
         * @param r_step           Step between candidate radii [mm].
         * @param cell_size        Linear size of each accumulator cell [mm].
         */
        void build_lut(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
                       float r_min, float r_max, float r_step, float cell_size);

        /// Return whether the LUT has been built and is ready for use.
        [[nodiscard]] bool is_lut_ready() const
        {
            return !lut_.empty() && !r_bins_.empty();
        }

        ///@}

        // ================================================================
        /** @name Per-event Ring Finding */
        ///@{

        /**
         * @brief Find ring candidates in a vector of @ref hit.
         *
         * For each pass:
         *  1. Vote using the active hit set and the pre-computed LUT.
         *  2. Find the global accumulator peak.
         *  3. Collect hits within @p collection_radius of the ring arc.
         *  4. Remove those hits from the active set.
         *  5. Reset the accumulator and repeat for the next ring.
         *
         * @param hits                Input hit vector (read-only).
         * @param threshold_fraction  Minimum fraction of currently-active hits
         *                            required in the peak cell (range 0–1).
         * @param min_hits            Minimum absolute vote count for acceptance.
         * @param min_active          Minimum active hits to attempt another ring.
         * @param max_rings           Maximum number of rings to extract (default 2).
         * @param collection_radius   Distance from the ring arc within which a hit
         *                            is assigned to the ring [mm] (default 6).
         * @return                    Vector of @ref ring_result in descending
         *                            peak-vote order.
         */
        std::vector<ring_result> find_rings(const std::vector<hit> &hits,
                                            float threshold_fraction,
                                            int min_hits,
                                            int min_active,
                                            int max_rings = 2,
                                            float collection_radius = 6.f);

        ///@}

        // ================================================================
        /** @name Accumulator Accessors */
        ///@{

        /// Flat accumulator array after the last @ref find_rings call.
        /// Layout: `accum[iR * nx * ny + iy * nx + ix]`.
        [[nodiscard]] const std::vector<int> &get_accumulator() const { return accum_; }
        [[nodiscard]] const std::vector<float> &get_r_bins() const { return r_bins_; }
        [[nodiscard]] int get_nx() const { return nx_; }
        [[nodiscard]] int get_ny() const { return ny_; }
        [[nodiscard]] float get_x_min() const { return x_min_; }
        [[nodiscard]] float get_y_min() const { return y_min_; }
        [[nodiscard]] float get_cell_size() const { return cell_size_; }

        ///@}

    private:
        // ================================================================
        //  Accumulator geometry
        // ================================================================

        float cell_size_ = 3.2f;
        float x_min_ = 0.f;
        float x_max_ = 0.f;
        float y_min_ = 0.f;
        float y_max_ = 0.f;
        int nx_ = 0;
        int ny_ = 0;

        // ================================================================
        //  LUT and accumulator storage
        // ================================================================

        std::vector<float> r_bins_;
        std::vector<int> accum_;

        /// `lut_[lut_key][r_bin_index]` → vector of flat cell indices
        /// that this key votes for at that radius bin.
        std::unordered_map<int, std::vector<std::vector<int>>> lut_;

        // ================================================================
        //  Private helpers
        // ================================================================

        /**
         * @brief Fill the accumulator and return the index of the global maximum.
         *
         * @param hits            Full hit vector.
         * @param active_indices  Indices into @p hits to vote with.
         * @param[out] best_iR    Radial bin index of the maximum cell.
         * @param[out] best_cell  Flat (iy * nx + ix) index of the maximum cell.
         * @return                Vote count of the maximum cell.
         */
        int vote_and_find_peak(const std::vector<hit> &hits,
                               const std::vector<int> &active_indices,
                               int &best_iR, int &best_cell);

        /**
         * @brief Collect hits within @p collection_radius of a ring arc.
         *
         * @param hits              Full hit vector.
         * @param active_indices    Candidate indices to test.
         * @param cx                Ring centre x [mm].
         * @param cy                Ring centre y [mm].
         * @param R                 Ring radius [mm].
         * @param collection_radius Acceptance half-width around the arc [mm].
         * @return                  Populated @ref ring_result.
         */
        ring_result collect_ring_hits(const std::vector<hit> &hits,
                                      const std::vector<int> &active_indices,
                                      float cx, float cy, float R,
                                      float collection_radius) const;
    };

} // namespace mist::ring_finding
