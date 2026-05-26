/**
 * @file HoughTransform.cxx
 * @brief Implementation of @ref mist::ring_finding::HoughTransform.
 */

#include <mist/ring_finding/hough_transform.h>
#include <cassert>
#include <numeric>

namespace mist::ring_finding
{

    // ============================================================
    //  Constructors
    // ============================================================

    HoughTransform::HoughTransform(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
                                     float r_min, float r_max, float r_step, float cell_size)
    {
        build_lut(index_to_hit_xy, r_min, r_max, r_step, cell_size);
    }

    // ============================================================
    //  LUT construction
    // ============================================================

    void HoughTransform::build_lut(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
                                    float r_min, float r_max, float r_step, float cell_size)
    {
        cell_size_ = cell_size;

        // Derive accumulator bounds from Hit positions
        x_min_ = y_min_ = std::numeric_limits<float>::max();
        x_max_ = y_max_ = std::numeric_limits<float>::lowest();
        for (auto &[idx, pos] : index_to_hit_xy)
        {
            x_min_ = std::min(x_min_, pos[0]);
            x_max_ = std::max(x_max_, pos[0]);
            y_min_ = std::min(y_min_, pos[1]);
            y_max_ = std::max(y_max_, pos[1]);
        }

        // Pad by r_max so ring centres outside the Hit area are reachable
        x_min_ -= r_max;
        x_max_ += r_max;
        y_min_ -= r_max;
        y_max_ += r_max;
        nx_ = static_cast<int>((x_max_ - x_min_) / cell_size) + 1;
        ny_ = static_cast<int>((y_max_ - y_min_) / cell_size) + 1;

        // Build R bins
        r_bins_.clear();
        for (float r = r_min; r <= r_max; r += r_step)
            r_bins_.push_back(r);

        // Build LUT: for each key, for each R bin, which accumulator cells does it vote for?
        // Arc rasterisation samples the circle at angles spaced so that each
        // accumulator cell on the arc is Hit at least twice — this avoids
        // undersampling at large radii where 360 fixed angles would leave gaps
        // (B11 fix).
        //   arc length over one angular step ≈ R · Δθ
        //   target step size                ≈ cell_size / 2
        //   ⇒ n_angles ≈ 2π · R / (cell_size / 2) = 4π R / cell_size
        // We never go below 360 angles, which preserves the previous behaviour
        // for typical small/medium rings.
        constexpr int kMinAngles = 360;
        constexpr float two_pi = 2.f * 3.14159265358979323846f;

        lut_.clear();
        for (auto &[key, pos] : index_to_hit_xy)
        {
            auto &entry = lut_[key];
            entry.resize(r_bins_.size());

            for (int iR = 0; iR < static_cast<int>(r_bins_.size()); ++iR)
            {
                const float R = r_bins_[iR];
                const int n_angles = std::max(
                    kMinAngles,
                    static_cast<int>(std::ceil(2.f * two_pi * R / cell_size)));

                for (int ia = 0; ia < n_angles; ++ia)
                {
                    const float angle = two_pi * ia / n_angles;
                    const int ix = static_cast<int>(
                        (pos[0] + R * std::cos(angle) - x_min_) / cell_size + 0.5f);
                    const int iy = static_cast<int>(
                        (pos[1] + R * std::sin(angle) - y_min_) / cell_size + 0.5f);

                    if (ix < 0 || ix >= nx_ || iy < 0 || iy >= ny_)
                        continue;

                    entry[iR].push_back(iy * nx_ + ix);
                }

                std::sort(entry[iR].begin(), entry[iR].end());
                entry[iR].erase(
                    std::unique(entry[iR].begin(), entry[iR].end()),
                    entry[iR].end());
            }
        }

        accum_.assign(r_bins_.size() * nx_ * ny_, 0);

        // std::to_string replaces ROOT's Form() for portable string formatting.
        mist::logger::info(
            "(HoughTransform::build_lut) LUT built: " + std::to_string(lut_.size()) + " keys, " + std::to_string(r_bins_.size()) + " R bins, grid " + std::to_string(nx_) + "x" + std::to_string(ny_));
    }

    // ============================================================
    //  Private helpers
    // ============================================================

    void HoughTransform::vote(const std::vector<Hit> &hits,
                              const std::vector<int> &active_indices)
    {
        std::fill(accum_.begin(), accum_.end(), 0);

        const int n_cells = nx_ * ny_;
        const int n_r = static_cast<int>(r_bins_.size());

        for (int i : active_indices)
        {
            auto it = lut_.find(hits[i].lut_key);
            if (it == lut_.end())
                continue;

            const auto &entry = it->second;
            for (int iR = 0; iR < n_r; ++iR)
                for (int cell : entry[iR])
                {
                    // Defensive bounds check (B13): LUT construction already
                    // clamps ix/iy into [0, nx_) × [0, ny_), so this assert
                    // should never fire — but if anyone refactors build_lut
                    // and drops the clamp, we want a loud failure in debug
                    // builds rather than silent out-of-bounds writes.
                    assert(cell >= 0 && cell < n_cells);
                    ++accum_[iR * n_cells + cell];
                }
        }
    }

    int HoughTransform::find_peak(int window, int &best_iR, int &best_ix, int &best_iy) const
    {
        const int n_cells = nx_ * ny_;
        const int n_r = static_cast<int>(r_bins_.size());

        best_iR = -1;
        best_ix = -1;
        best_iy = -1;
        int best_count = 0;

        // Single-cell scan — exact equivalent of the legacy peak finder.
        // Kept on its own branch because (a) it's the common case and
        // benefits from the absence of inner window loops, and (b) the
        // semantics differ subtly: the single-cell anchor IS the cell,
        // whereas the windowed anchor is the lower-corner of the window.
        if (window <= 1)
        {
            for (int iR = 0; iR < n_r; ++iR)
            {
                const int *plane = &accum_[iR * n_cells];
                for (int iy = 0; iy < ny_; ++iy)
                    for (int ix = 0; ix < nx_; ++ix)
                    {
                        const int val = plane[iy * nx_ + ix];
                        if (val > best_count)
                        {
                            best_count = val;
                            best_iR = iR;
                            best_ix = ix;
                            best_iy = iy;
                        }
                    }
            }
            return best_count;
        }

        // Sliding-window scan over (iR, iy, ix) anchors.  At each anchor
        // we sum `window` cells along each axis.  Anchors near the upper
        // bound where the window would overrun are skipped — no wrap-
        // around, and no zero-padding past the array edge (that would
        // bias the peak toward the edges, since boundary windows would
        // include phantom zeros).
        //
        // Complexity: O(n_cells × window^3).  For window=2 that's 8×
        // the single-cell scan, still trivial at the scales we run.  A
        // summed-area-table would bring it to O(n_cells); not worth the
        // bookkeeping until window^3 dominates the profile.
        const int max_iR = n_r - window;
        const int max_iy = ny_ - window;
        const int max_ix = nx_ - window;
        for (int iR = 0; iR <= max_iR; ++iR)
            for (int iy = 0; iy <= max_iy; ++iy)
                for (int ix = 0; ix <= max_ix; ++ix)
                {
                    int sum = 0;
                    for (int dR = 0; dR < window; ++dR)
                    {
                        const int *plane = &accum_[(iR + dR) * n_cells];
                        for (int dy = 0; dy < window; ++dy)
                        {
                            const int *row = plane + (iy + dy) * nx_;
                            for (int dx = 0; dx < window; ++dx)
                                sum += row[ix + dx];
                        }
                    }
                    if (sum > best_count)
                    {
                        best_count = sum;
                        best_iR = iR;
                        best_ix = ix;
                        best_iy = iy;
                    }
                }

        return best_count;
    }

    RingResult HoughTransform::collect_ring_hits(const std::vector<Hit> &hits,
                                                   const std::vector<int> &active_indices,
                                                   float cx, float cy, float R,
                                                   float collection_radius) const
    {
        RingResult result;
        result.cx = cx;
        result.cy = cy;
        result.radius = R;
        result.peak_votes = 0; // filled by caller
        result.mean_time = 0.f;

        float time_sum = 0.f;
        for (int i : active_indices)
        {
            const float dist = std::hypot(hits[i].x - cx, hits[i].y - cy);
            if (std::fabs(dist - R) < collection_radius)
            {
                result.hit_indices.push_back(i);
                time_sum += hits[i].time;
            }
        }

        if (!result.hit_indices.empty())
            result.mean_time = time_sum / static_cast<float>(result.hit_indices.size());

        return result;
    }

    // ============================================================
    //  Per-event ring finding
    // ============================================================

    std::vector<RingResult> HoughTransform::find_rings(const std::vector<Hit> &hits,
                                                         float threshold_fraction,
                                                         int min_hits,
                                                         int min_active,
                                                         int max_rings,
                                                         float collection_radius,
                                                         int aggregation_window_cells)
    {
        std::vector<RingResult> found_rings;

        if (!is_lut_ready())
        {
            mist::logger::error("(HoughTransform::find_rings) LUT is empty — call build_lut() first.");
            return found_rings;
        }

        // Normalise the window parameter.  Callers passing 0 or negative
        // get the legacy single-cell behaviour rather than a hard error.
        const int window = std::max(1, aggregation_window_cells);

        // Build the initial active set: only hits whose LUT key is known
        std::vector<int> active_indices;
        active_indices.reserve(hits.size());
        for (int i = 0; i < static_cast<int>(hits.size()); ++i)
            if (lut_.count(hits[i].lut_key))
                active_indices.push_back(i);

        // Threshold is fixed against the initial active count so that the bar
        // is consistent across all passes.
        //
        // Note on window>1 semantics: peak_votes now sums `window^3` cells,
        // each of volume cell_size^3.  When the caller compensates by
        // halving cell_size / r_step (the intended usage), the physical
        // volume probed by the peak finder matches the legacy single-cell
        // volume — so `min_hits` and `threshold_fraction` retain their
        // physical meaning ("votes in a `(window·cell_size)`³ region").
        // No automatic rescaling here; the caller is responsible for
        // matching grid spacing to the chosen window.
        const int threshold = std::max(min_hits,
                                       static_cast<int>(std::ceil(threshold_fraction * active_indices.size())));

        // Half-cell offset for sub-cell-precision back-projection when
        // window > 1: the reported (cx, cy, R) is the geometric centre
        // of the winning window, not its lower-corner anchor.  For
        // window = 1 this reduces to 0, preserving the legacy convention
        // that best_ix maps to x_min_ + best_ix * cell_size_.
        const float window_offset_cells = 0.5f * (window - 1);

        while (static_cast<int>(found_rings.size()) < max_rings &&
               static_cast<int>(active_indices.size()) >= min_active)
        {
            vote(hits, active_indices);

            int best_iR, best_ix, best_iy;
            const int best_count = find_peak(window, best_iR, best_ix, best_iy);

            if (best_count < threshold)
                break;

            const float cx = x_min_ + (best_ix + window_offset_cells) * cell_size_;
            const float cy = y_min_ + (best_iy + window_offset_cells) * cell_size_;
            // For the radius: average the bin centres covered by the
            // window.  Equivalent to (r_bins_[best_iR] + r_bins_[best_iR
            // + window - 1]) / 2 when r_bins_ is uniformly spaced (the
            // build_lut invariant), but written as a generic mean so
            // future non-uniform R sampling would still work.
            float R = 0.f;
            for (int dR = 0; dR < window; ++dR)
                R += r_bins_[best_iR + dR];
            R /= static_cast<float>(window);

            RingResult ring = collect_ring_hits(hits, active_indices, cx, cy, R, collection_radius);
            ring.peak_votes = best_count;

            if (static_cast<int>(ring.hit_indices.size()) < min_hits)
                break;

            found_rings.push_back(ring);

            // Remove ring hits from the active set.
            // std::unordered_set gives O(1) lookup during the erase-remove pass.
            std::unordered_set<int> ring_set(ring.hit_indices.begin(), ring.hit_indices.end());
            active_indices.erase(
                std::remove_if(active_indices.begin(), active_indices.end(),
                               [&ring_set](int idx)
                               { return ring_set.count(idx) > 0; }),
                active_indices.end());

            if (static_cast<int>(active_indices.size()) < threshold)
                break;
        }

        // Sort by descending peak votes so callers can rely on rings[0] being
        // the strongest candidate (B12 fix — the extraction order is *usually*
        // but not strictly monotonic).
        std::sort(found_rings.begin(), found_rings.end(),
                  [](const RingResult &a, const RingResult &b)
                  { return a.peak_votes > b.peak_votes; });

        return found_rings;
    }

} // namespace mist::ring_finding
