#include "BackgroundOperator.hh"
#include "MoshitReader.hh"
#include "MoshitWriter.hh"

#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool nearly_equal(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps;
}

bool check_or_fail(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }
    std::cerr << "test_background_operator: " << message << '\n';
    return false;
}

struct TempFileCleanup {
    std::filesystem::path first;
    std::filesystem::path second;

    ~TempFileCleanup() {
        std::error_code ec1;
        std::filesystem::remove(first, ec1);
        std::error_code ec2;
        std::filesystem::remove(second, ec2);
    }
};

}  // namespace

int main() {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    const std::filesystem::path moshit_path = temp_dir / "test_background_operator.moshit.zst";
    const std::filesystem::path bgop_path = temp_dir / "test_background_operator.bgop.zst";
    const TempFileCleanup cleanup{moshit_path, bgop_path};

    try {
        MoshitWriter writer;
        writer.Begin(-1000.0f, 12.5f, -7.5f);
        writer.AddHit(42, 10.25f, 1, 100, 200, 3, 9.5f);
        writer.AddHit(43, 11.25f, 2, 101, 201, 4, 10.5f);
        writer.Flush(moshit_path.string());

        const MoshitFile moshit = MoshitReader::Read(moshit_path.string());
        if (!check_or_fail(std::memcmp(moshit.header.magic, "MOSH", 4) == 0, "MOSH magic mismatch")) return 1;
        if (!check_or_fail(moshit.header.version == 1, "MOSH version mismatch")) return 1;
        if (!check_or_fail(moshit.header.n_hits == 2, "MOSH hit count mismatch")) return 1;
        if (!check_or_fail(moshit.hits.size() == 2, "MOSH hit vector size mismatch")) return 1;
        if (!check_or_fail(moshit.hits[0].pixel == 42, "first MOSH pixel mismatch")) return 1;
        if (!check_or_fail(moshit.hits[0].origin == 1, "first MOSH origin mismatch")) return 1;
        if (!check_or_fail(moshit.hits[0].ii == 100, "first MOSH ii mismatch")) return 1;
        if (!check_or_fail(moshit.hits[0].jj == 200, "first MOSH jj mismatch")) return 1;
        if (!check_or_fail(moshit.hits[0].kk == 3, "first MOSH kk mismatch")) return 1;
        if (!check_or_fail(nearly_equal(moshit.hits[0].t, 10.25f), "first MOSH t mismatch")) return 1;
        if (!check_or_fail(nearly_equal(moshit.hits[0].t0, 9.5f), "first MOSH t0 mismatch")) return 1;

        BackgroundOperator op;
        op.header = make_background_operator_header(1, -1000.0f, 0.25f, 1.5f, 640.0f, 5);
        op.pixel_rate_per_event.assign(kBackgroundPixels, 0.0f);
        op.pixel_time_cdf.assign(kBackgroundPixels * kBackgroundTimeBins, 0.0f);
        op.neighbor_fire_prob.assign(kBackgroundPixels * kBackgroundNeighborSlots, 0.0f);

        op.pixel_rate_per_event[0] = 0.125f;
        op.pixel_rate_per_event[kBackgroundPixels - 1] = 0.875f;
        op.pixel_time_cdf[0] = 0.1f;
        op.pixel_time_cdf[kBackgroundTimeBins - 1] = 1.0f;
        op.neighbor_fire_prob[0] = 0.33f;
        op.neighbor_fire_prob[kBackgroundNeighborSlots - 1] = 0.77f;

        write_background_operator(bgop_path.string(), op);
        const BackgroundOperator loaded = read_background_operator(bgop_path.string());

        if (!check_or_fail(std::memcmp(loaded.header.magic, "BGOP", 4) == 0, "BGOP magic mismatch")) return 1;
        if (!check_or_fail(loaded.header.version == 1, "BGOP version mismatch")) return 1;
        if (!check_or_fail(loaded.header.time_bins == kBackgroundTimeBins, "BGOP time_bins mismatch")) return 1;
        if (!check_or_fail(loaded.header.n_pixels == kBackgroundPixels, "BGOP n_pixels mismatch")) return 1;
        if (!check_or_fail(loaded.header.catm == 1, "BGOP catm mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.header.zz, -1000.0f), "BGOP zz mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.header.phi, 0.25f), "BGOP phi mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.header.the, 1.5f), "BGOP the mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.header.window_ns, 640.0f), "BGOP window_ns mismatch")) return 1;
        if (!check_or_fail(loaded.header.calibration_events == 5, "BGOP calibration_events mismatch")) return 1;

        if (!check_or_fail(loaded.pixel_rate_per_event.size() == op.pixel_rate_per_event.size(),
                           "BGOP pixel_rate_per_event size mismatch")) return 1;
        if (!check_or_fail(loaded.pixel_time_cdf.size() == op.pixel_time_cdf.size(),
                           "BGOP pixel_time_cdf size mismatch")) return 1;
        if (!check_or_fail(loaded.neighbor_fire_prob.size() == op.neighbor_fire_prob.size(),
                           "BGOP neighbor_fire_prob size mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.pixel_rate_per_event[0], 0.125f), "BGOP rate[0] mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.pixel_rate_per_event[kBackgroundPixels - 1], 0.875f),
                           "BGOP rate[last] mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.pixel_time_cdf[0], 0.1f), "BGOP cdf[0] mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.pixel_time_cdf[kBackgroundTimeBins - 1], 1.0f),
                           "BGOP cdf[last-bin] mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.neighbor_fire_prob[0], 0.33f), "BGOP neighbor[0] mismatch")) return 1;
        if (!check_or_fail(nearly_equal(loaded.neighbor_fire_prob[kBackgroundNeighborSlots - 1], 0.77f),
                           "BGOP neighbor[last-slot] mismatch")) return 1;
    } catch (const std::exception& e) {
        std::cerr << "test_background_operator: unexpected exception: " << e.what() << '\n';
        return 1;
    }

    std::cout << "background_operator ok\n";
    return 0;
}
