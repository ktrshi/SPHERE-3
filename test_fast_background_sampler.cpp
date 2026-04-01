#include "BackgroundOperator.hh"
#include "FastBackgroundSampler.hh"
#include "MoshitReader.hh"
#include "MoshitWriter.hh"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool nearly_equal(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool check_or_fail(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }
    std::cerr << "test_fast_background_sampler: " << message << '\n';
    return false;
}

struct TempFileCleanup {
    std::filesystem::path path;

    ~TempFileCleanup() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

std::shared_ptr<BackgroundOperator> make_operator() {
    auto op = std::make_shared<BackgroundOperator>();
    op->header = make_background_operator_header(1, -1000.0f, 0.0f, 0.0f, 128.0f, 4);
    op->pixel_rate_per_event.assign(kBackgroundPixels, 0.0f);
    op->pixel_time_cdf.assign(static_cast<size_t>(kBackgroundPixels) * kBackgroundTimeBins, 1.0f);
    op->neighbor_fire_prob.assign(static_cast<size_t>(kBackgroundPixels) * kBackgroundNeighborSlots, 0.0f);
    return op;
}

}  // namespace

int main() {
    const std::filesystem::path out_path =
        std::filesystem::temp_directory_path() / "test_fast_background_sampler.moshit.zst";
    const TempFileCleanup cleanup{out_path};

    try {
        auto op = make_operator();
        op->pixel_rate_per_event[100] = 20.0f;

        const size_t hot_pixel_time_base = static_cast<size_t>(100) * kBackgroundTimeBins;
        std::fill(op->pixel_time_cdf.begin() + static_cast<std::ptrdiff_t>(hot_pixel_time_base),
                  op->pixel_time_cdf.begin() + static_cast<std::ptrdiff_t>(hot_pixel_time_base + kBackgroundTimeBins),
                  0.0f);
        op->pixel_time_cdf[hot_pixel_time_base + 7] = 1.0f;
        for (size_t bin = 8; bin < kBackgroundTimeBins; ++bin) {
            op->pixel_time_cdf[hot_pixel_time_base + bin] = 1.0f;
        }
        op->neighbor_fire_prob[static_cast<size_t>(100) * kBackgroundNeighborSlots + 1] = 1.0f;

        ValidateBackgroundOperatorForSampling(*op);
        if (!check_or_fail(BackgroundOperatorCompatibilityError(*op, 1, -1000.0f, 0.0f, 0.0f).empty(),
                           "expected matching operator metadata to be accepted")) return 1;
        if (!check_or_fail(!BackgroundOperatorCompatibilityError(*op, 2, -1000.0f, 0.0f, 0.0f).empty(),
                           "expected catm mismatch to be reported")) return 1;

        FastBackgroundSampler sampler(op, 12345u);
        MoshitWriter writer;
        writer.Begin(-1000.0f, 0.0f, 0.0f);
        const FastBackgroundSampleStats stats = sampler.SampleInto(writer);
        writer.Flush(out_path.string());

        const MoshitFile moshit = MoshitReader::Read(out_path.string());
        if (!check_or_fail(!moshit.hits.empty(), "expected at least one sampled hit")) return 1;
        if (!check_or_fail(moshit.header.n_hits == moshit.hits.size(), "header hit count mismatch")) return 1;
        if (!check_or_fail(stats.injected_hits == moshit.hits.size(), "sample stats hit count mismatch")) return 1;

        const float bin_width = op->header.window_ns / static_cast<float>(op->header.time_bins);
        bool saw_neighbor = false;
        float observed_min_t = moshit.hits.front().t;
        float observed_max_t = moshit.hits.front().t;
        for (const auto& hit : moshit.hits) {
            if (!check_or_fail(hit.origin == 2, "all sampled hits must have origin=2")) return 1;
            if (!check_or_fail(hit.t0 >= 7.0f * bin_width, "hit t0 below configured time bin")) return 1;
            if (!check_or_fail(hit.t0 < 8.0f * bin_width, "hit t0 above configured time bin")) return 1;
            if (!check_or_fail(nearly_equal(hit.t, hit.t0), "background hit t and t0 must match")) return 1;
            if (hit.pixel != 100) {
                saw_neighbor = true;
            }
            observed_min_t = std::min(observed_min_t, hit.t);
            observed_max_t = std::max(observed_max_t, hit.t);
        }
        if (!check_or_fail(saw_neighbor, "expected at least one deterministic neighbor hit")) return 1;
        if (!check_or_fail(nearly_equal(stats.tmin_ns, observed_min_t), "sample stats tmin mismatch")) return 1;
        if (!check_or_fail(nearly_equal(stats.tmax_ns, observed_max_t), "sample stats tmax mismatch")) return 1;

        auto zero_op = make_operator();
        FastBackgroundSampler zero_sampler(zero_op, 777u);
        MoshitWriter zero_writer;
        zero_writer.Begin(-1000.0f, 0.0f, 0.0f);
        const FastBackgroundSampleStats zero_stats = zero_sampler.SampleInto(zero_writer);
        if (!check_or_fail(zero_writer.HitCount() == 0, "zero-rate operator should emit no hits")) return 1;
        if (!check_or_fail(zero_stats.injected_hits == 0, "zero-rate operator should report zero hits")) return 1;

        bool threw_invalid = false;
        try {
            auto invalid_op = make_operator();
            invalid_op->header.n_pixels = 12;
            FastBackgroundSampler invalid_sampler(invalid_op, 1u);
            (void)invalid_sampler;
        } catch (const std::runtime_error&) {
            threw_invalid = true;
        }
        if (!check_or_fail(threw_invalid, "invalid operator dimensions should throw")) return 1;

        bool threw_bad_cdf = false;
        try {
            auto bad_cdf_op = make_operator();
            bad_cdf_op->pixel_rate_per_event[77] = 1.0f;
            const size_t bad_base = static_cast<size_t>(77) * kBackgroundTimeBins;
            std::fill(bad_cdf_op->pixel_time_cdf.begin() + static_cast<std::ptrdiff_t>(bad_base),
                      bad_cdf_op->pixel_time_cdf.begin() + static_cast<std::ptrdiff_t>(bad_base + kBackgroundTimeBins),
                      0.0f);
            bad_cdf_op->pixel_time_cdf[bad_base + kBackgroundTimeBins - 1] = 0.5f;
            ValidateBackgroundOperatorForSampling(*bad_cdf_op);
        } catch (const std::runtime_error&) {
            threw_bad_cdf = true;
        }
        if (!check_or_fail(threw_bad_cdf, "active pixel CDF ending below one should throw")) return 1;
    } catch (const std::exception& e) {
        std::cerr << "test_fast_background_sampler: unexpected exception: " << e.what() << '\n';
        return 1;
    }

    std::cout << "fast_background_sampler ok\n";
    return 0;
}
