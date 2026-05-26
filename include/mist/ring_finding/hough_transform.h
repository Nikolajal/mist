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
     * @file HoughTransform.h
     * @brief Circular Hough-transform ring-finder.
     *
     * The class operates on @ref Hit — a plain POD struct that the caller
     * populates from their detector-specific Hit representation.
     *
     * ### Two-phase workflow
     *  1. **Setup** (once per run / geometry change) — call @ref build_lut with
     *     the channel-to-position map to pre-compute which accumulator cells each
     *     LUT key votes for at every candidate radius.
     *  2. **Per-event processing** — call @ref find_rings with a vector of
     *     @ref Hit. The method votes, finds the best peak, removes the tagged
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
     * mist::ring_finding::HoughTransform ht;
     *
     * // --- Once per run ---
     * std::map<int, std::array<float,2>> geometry = load_geometry();
     * ht.build_lut(geometry, 30.f, 80.f, 1.f, 3.2f);
     *
     * // --- Per event ---
     * std::vector<mist::ring_finding::Hit> hits = make_hits(raw_hits);
     * auto rings = ht.find_rings(hits, 0.3f, 5);
     * @endcode
     */

    // ============================================================
    //  Hit type
    // ============================================================

    /**
     * @brief Minimal Hit descriptor consumed by @ref HoughTransform.
     *
     * The caller is responsible for populating this struct from whatever
     * detector-specific Hit representation is in use. The @c lut_key must
     * match the keys used when building the LUT via @ref HoughTransform::build_lut.
     */
    struct Hit
    {
        float x;     ///< Hit x-position in the detector plane [mm].
        float y;     ///< Hit y-position in the detector plane [mm].
        float time;  ///< Calibrated Hit time [ns].
        int lut_key; ///< Key into the LUT — typically `global_channel_index / 4`.
    };

    // ============================================================
    //  Result type
    // ============================================================

    /**
     * @brief Describes a single ring candidate found by @ref HoughTransform::find_rings.
     */
    struct RingResult
    {
        float cx;        ///< x-coordinate of the reconstructed ring centre [mm].
        float cy;        ///< y-coordinate of the reconstructed ring centre [mm].
        float radius;    ///< Reconstructed ring radius [mm].
        int peak_votes;  ///< Number of votes in the winning accumulator cell.
        float mean_time; ///< Mean Hit time of hits associated with this ring [ns].

        /// Indices into the input @ref Hit vector of hits assigned to this ring.
        std::vector<int> hit_indices;
    };

    // ============================================================
    //  HoughTransform
    // ============================================================

    /**
     * @brief Circular Hough-transform ring-finder.
     *
     * The algorithm works in the (x, y) detector plane. For a given candidate
     * radius R and a Hit at position (hx, hy), the set of possible ring centres
     * lies on a circle of radius R centred at (hx, hy). The accumulator counts
     * how many Hit arcs pass through each (cx, cy, R) cell; a high-count cell
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
    class HoughTransform
    {
    public:
        // ================================================================
        //  Constructors
        // ================================================================

        // ================================================================
        //  Named defaults — avoids magic-number repetition across callers
        // ================================================================

        /// Default linear cell size for the (x, y) accumulator grid [mm].
        static constexpr float kDefaultCellSizeMm = 3.2f;

        /// Default radial half-width within which a Hit is assigned to a
        /// ring arc during the collection step [mm].
        static constexpr float kDefaultCollectionRadiusMm = 6.f;

        // ================================================================
        //  Constructors
        // ================================================================

        /// Default constructor — creates an uninitialised finder.
        /// @note @ref build_lut must be called before @ref find_rings.
        HoughTransform() = default;

        /**
         * @brief Convenience constructor that immediately builds the LUT.
         *
         * @param index_to_hit_xy  Map from LUT key to (x, y) [mm].
         * @param r_min            Minimum ring radius [mm].
         * @param r_max            Maximum ring radius [mm].
         * @param r_step           Radial bin step [mm].
         * @param cell_size        Accumulator cell size in (x, y) [mm].
         */
        HoughTransform(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
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
         * @param index_to_hit_xy  Map from LUT key to Hit position [mm].
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
         * @brief Find ring candidates in a vector of @ref Hit.
         *
         * For each pass:
         *  1. Vote using the active Hit set and the pre-computed LUT.
         *  2. Find the global accumulator peak.
         *  3. Collect hits within @p collection_radius of the ring arc.
         *  4. Remove those hits from the active set.
         *  5. Reset the accumulator and repeat for the next ring.
         *
         * @param hits                Input Hit vector (read-only).
         * @param threshold_fraction  Minimum fraction of currently-active hits
         *                            required in the peak cell (range 0–1).
         * @param min_hits            Minimum absolute vote count for acceptance.
         * @param min_active          Minimum active hits to attempt another ring.
         * @param max_rings           Maximum number of rings to extract (default 2).
         * @param collection_radius   Distance from the ring arc within which a Hit
         *                            is assigned to the ring [mm] (default 6).
         * @param aggregation_window_cells  Sliding-window size, in accumulator
         *                            cells, used by the peak finder along each
         *                            axis @f$(c_x, c_y, R)@f$.  Default `1`
         *                            preserves the original behaviour
         *                            (peak = single-cell maximum).  Set to `2`
         *                            to scan with a 2×2×2 sub-cell sliding
         *                            window — combined with halved
         *                            `cell_size` / `r_step` at LUT build time,
         *                            this recovers votes that would otherwise
         *                            fragment across adjacent cell boundaries.
         *                            The reported @c peak_votes is then the
         *                            **aggregated** count over the winning
         *                            window; the reported @c (cx, cy, radius)
         *                            is the window's centre, giving sub-cell
         *                            precision.  Values >2 are accepted but
         *                            give diminishing returns; >4 is unusual.
         * @return                    Vector of @ref RingResult.  The result is
         *                            sorted in descending @c peak_votes order
         *                            after extraction, so callers may rely on
         *                            @c rings[0] being the strongest candidate
         *                            even though the extraction order itself is
         *                            "first ring found, then next-best after
         *                            removing its hits" (which is *typically*
         *                            but not strictly monotonic).
         */
        std::vector<RingResult> find_rings(const std::vector<Hit> &hits,
                                            float threshold_fraction,
                                            int min_hits,
                                            int min_active,
                                            int max_rings = 2,
                                            float collection_radius = kDefaultCollectionRadiusMm,
                                            int aggregation_window_cells = 1);

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

        float cell_size_ = kDefaultCellSizeMm; ///< Side length of one accumulator cell [mm].
        float x_min_ = 0.f;      ///< Lower-left x of the accumulator [mm].
        float x_max_ = 0.f;      ///< Upper-right x of the accumulator [mm].
        float y_min_ = 0.f;      ///< Lower-left y of the accumulator [mm].
        float y_max_ = 0.f;      ///< Upper-right y of the accumulator [mm].
        int nx_ = 0;             ///< Number of cells along x.
        int ny_ = 0;             ///< Number of cells along y.

        // ================================================================
        //  LUT and accumulator storage
        // ================================================================

        std::vector<float> r_bins_; ///< Candidate radii sampled during voting [mm].
        std::vector<int> accum_;    ///< Flat accumulator `accum_[iR*nx*ny + iy*nx + ix]`.

        /// Scratch buffer for the 3-D Summed-Area-Table used by @ref find_peak
        /// when `window > 1`.  Pre-allocated in @ref build_lut to the same size
        /// as @c accum_; marked `mutable` so that @ref find_peak (which is
        /// logically `const`) can fill it without a per-call heap allocation.
        mutable std::vector<int> sat_;

        /// `lut_[lut_key][r_bin_index]` → vector of flat cell indices
        /// that this key votes for at that radius bin.
        std::unordered_map<int, std::vector<std::vector<int>>> lut_;

        // ================================================================
        //  Private helpers
        // ================================================================

        /**
         * @brief Vote @p active_indices into the accumulator.
         *
         * Resets @c accum_ and increments the cells indicated by every hit's
         * LUT entry.  Pure side-effect on @c accum_; no peak finding.
         *
         * @param hits            Full Hit vector.
         * @param active_indices  Indices into @p hits to vote with.
         */
        void vote(const std::vector<Hit> &hits,
                  const std::vector<int> &active_indices);

        /**
         * @brief Scan @c accum_ for the position with the maximum aggregated
         *        vote count over a `window × window × window` cell window.
         *
         * For @p window `== 1`, this is exactly the single-cell global max
         * (same result as the legacy peak finder).  For @p window `> 1`,
         * the function builds a 3-D Summed-Area-Table (SAT / integral image)
         * from @c accum_ in O(n_cells) time (three sequential 1-D prefix-sum
         * passes), then evaluates each window sum in O(1) via inclusion-
         * exclusion on the 8 corners of the 3-D box.  Total cost is
         * O(n_cells × 8), versus O(n_cells × W³) for a naive scan.
         * Results are bit-for-bit identical to the naive approach.
         *
         * Anchors near the upper bounds where the window would overrun are
         * skipped (no wrap-around, no zero-padding past the edge).
         *
         * The SAT scratch buffer (@c sat_) is pre-allocated in @ref build_lut
         * so this function performs no heap allocation.
         *
         * @param window        Edge length of the window in accumulator cells.
         * @param[out] best_iR  Radial bin index of the window's lower-iR
         *                      anchor (or the single cell if `window == 1`).
         * @param[out] best_ix  X cell index of the window's lower-ix anchor.
         * @param[out] best_iy  Y cell index of the window's lower-iy anchor.
         * @return              Aggregated vote count over the winning window.
         */
        int find_peak(int window, int &best_iR, int &best_ix, int &best_iy) const;

        /**
         * @brief Collect hits within @p collection_radius of a ring arc.
         *
         * @param hits              Full Hit vector.
         * @param active_indices    Candidate indices to test.
         * @param cx                Ring centre x [mm].
         * @param cy                Ring centre y [mm].
         * @param R                 Ring radius [mm].
         * @param collection_radius Acceptance half-width around the arc [mm].
         * @return                  Populated @ref RingResult.
         */
        RingResult collect_ring_hits(const std::vector<Hit> &hits,
                                      const std::vector<int> &active_indices,
                                      float cx, float cy, float R,
                                      float collection_radius) const;
    };

} // namespace mist::ring_finding
