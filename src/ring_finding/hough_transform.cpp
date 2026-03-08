#include <mist/ring_finding/hough_transform.h>
#include <numeric>

namespace mist::ring_finding
{

    // ============================================================
    //  Constructors
    // ============================================================

    hough_transform::hough_transform(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
                                     float r_min, float r_max, float r_step, float cell_size)
    {
        build_lut(index_to_hit_xy, r_min, r_max, r_step, cell_size);
    }

    // ============================================================
    //  LUT construction
    // ============================================================

    void hough_transform::build_lut(const std::map<int, std::array<float, 2>> &index_to_hit_xy,
                                    float r_min, float r_max, float r_step, float cell_size)
    {
        cell_size_ = cell_size;

        // Derive accumulator bounds from hit positions
        x_min_ = y_min_ = std::numeric_limits<float>::max();
        x_max_ = y_max_ = std::numeric_limits<float>::lowest();
        for (auto &[idx, pos] : index_to_hit_xy)
        {
            x_min_ = std::min(x_min_, pos[0]);
            x_max_ = std::max(x_max_, pos[0]);
            y_min_ = std::min(y_min_, pos[1]);
            y_max_ = std::max(y_max_, pos[1]);
        }

        // Pad by r_max so ring centres outside the hit area are reachable
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
        // Arc rasterisation samples the circle at 360 equally-spaced angles.
        // Duplicate cells within each R bin are removed via sort+unique.
        constexpr int n_angles = 360;
        constexpr float two_pi = 2.f * 3.14159265358979323846f;

        lut_.clear();
        for (auto &[key, pos] : index_to_hit_xy)
        {
            auto &entry = lut_[key];
            entry.resize(r_bins_.size());

            for (int iR = 0; iR < static_cast<int>(r_bins_.size()); ++iR)
            {
                const float R = r_bins_[iR];
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
            "(hough_transform::build_lut) LUT built: " + std::to_string(lut_.size()) + " keys, " + std::to_string(r_bins_.size()) + " R bins, grid " + std::to_string(nx_) + "x" + std::to_string(ny_));
    }

    // ============================================================
    //  Private helpers
    // ============================================================

    int hough_transform::vote_and_find_peak(const std::vector<hit> &hits,
                                            const std::vector<int> &active_indices,
                                            int &best_iR, int &best_cell)
    {
        std::fill(accum_.begin(), accum_.end(), 0);

        const int n_cells = nx_ * ny_;
        const int n_r = static_cast<int>(r_bins_.size());

        best_iR = -1;
        best_cell = -1;
        int best_count = 0;

        for (int i : active_indices)
        {
            auto it = lut_.find(hits[i].lut_key);
            if (it == lut_.end())
                continue;

            const auto &entry = it->second;
            for (int iR = 0; iR < n_r; ++iR)
                for (int cell : entry[iR])
                {
                    const int val = ++accum_[iR * n_cells + cell];
                    if (val > best_count)
                    {
                        best_count = val;
                        best_iR = iR;
                        best_cell = cell;
                    }
                }
        }

        return best_count;
    }

    ring_result hough_transform::collect_ring_hits(const std::vector<hit> &hits,
                                                   const std::vector<int> &active_indices,
                                                   float cx, float cy, float R,
                                                   float collection_radius) const
    {
        ring_result result;
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

    std::vector<ring_result> hough_transform::find_rings(const std::vector<hit> &hits,
                                                         float threshold_fraction,
                                                         int min_hits,
                                                         int min_active,
                                                         int max_rings,
                                                         float collection_radius)
    {
        std::vector<ring_result> found_rings;

        if (!is_lut_ready())
        {
            mist::logger::error("(hough_transform::find_rings) LUT is empty — call build_lut() first.");
            return found_rings;
        }

        // Build the initial active set: only hits whose LUT key is known
        std::vector<int> active_indices;
        active_indices.reserve(hits.size());
        for (int i = 0; i < static_cast<int>(hits.size()); ++i)
            if (lut_.count(hits[i].lut_key))
                active_indices.push_back(i);

        // Threshold is fixed against the initial active count so that the bar
        // is consistent across all passes.
        const int threshold = std::max(min_hits,
                                       static_cast<int>(std::ceil(threshold_fraction * active_indices.size())));

        while (static_cast<int>(found_rings.size()) < max_rings &&
               static_cast<int>(active_indices.size()) >= min_active)
        {
            int best_iR, best_cell;
            const int best_count = vote_and_find_peak(hits, active_indices, best_iR, best_cell);

            if (best_count < threshold)
                break;

            const int best_ix = best_cell % nx_;
            const int best_iy = best_cell / nx_;
            const float cx = x_min_ + best_ix * cell_size_;
            const float cy = y_min_ + best_iy * cell_size_;
            const float R = r_bins_[best_iR];

            ring_result ring = collect_ring_hits(hits, active_indices, cx, cy, R, collection_radius);
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

        return found_rings;
    }

} // namespace mist::ring_finding
