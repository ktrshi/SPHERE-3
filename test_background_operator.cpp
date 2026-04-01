#include "BackgroundOperator.hh"
#include "MoshitReader.hh"
#include "MoshitWriter.hh"

#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace {

bool nearly_equal(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    const std::filesystem::path moshit_path = temp_dir / "test_background_operator.moshit.zst";
    const std::filesystem::path bgop_path = temp_dir / "test_background_operator.bgop.zst";

    MoshitWriter writer;
    writer.Begin(-1000.0f, 12.5f, -7.5f);
    writer.AddHit(42, 10.25f, 1, 100, 200, 3, 9.5f);
    writer.AddHit(43, 11.25f, 2, 101, 201, 4, 10.5f);
    writer.Flush(moshit_path.string());

    const MoshitFile moshit = MoshitReader::Read(moshit_path.string());
    assert(std::memcmp(moshit.header.magic, "MOSH", 4) == 0);
    assert(moshit.header.version == 1);
    assert(moshit.header.n_hits == 2);
    assert(moshit.hits.size() == 2);
    assert(moshit.hits[0].pixel == 42);
    assert(moshit.hits[0].origin == 1);
    assert(moshit.hits[0].ii == 100);
    assert(moshit.hits[0].jj == 200);
    assert(moshit.hits[0].kk == 3);
    assert(nearly_equal(moshit.hits[0].t, 10.25f));
    assert(nearly_equal(moshit.hits[0].t0, 9.5f));

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

    assert(std::memcmp(loaded.header.magic, "BGOP", 4) == 0);
    assert(loaded.header.version == 1);
    assert(loaded.header.time_bins == kBackgroundTimeBins);
    assert(loaded.header.n_pixels == kBackgroundPixels);
    assert(loaded.header.catm == 1);
    assert(nearly_equal(loaded.header.zz, -1000.0f));
    assert(nearly_equal(loaded.header.phi, 0.25f));
    assert(nearly_equal(loaded.header.the, 1.5f));
    assert(nearly_equal(loaded.header.window_ns, 640.0f));
    assert(loaded.header.calibration_events == 5);

    assert(loaded.pixel_rate_per_event.size() == op.pixel_rate_per_event.size());
    assert(loaded.pixel_time_cdf.size() == op.pixel_time_cdf.size());
    assert(loaded.neighbor_fire_prob.size() == op.neighbor_fire_prob.size());
    assert(nearly_equal(loaded.pixel_rate_per_event[0], 0.125f));
    assert(nearly_equal(loaded.pixel_rate_per_event[kBackgroundPixels - 1], 0.875f));
    assert(nearly_equal(loaded.pixel_time_cdf[0], 0.1f));
    assert(nearly_equal(loaded.pixel_time_cdf[kBackgroundTimeBins - 1], 1.0f));
    assert(nearly_equal(loaded.neighbor_fire_prob[0], 0.33f));
    assert(nearly_equal(loaded.neighbor_fire_prob[kBackgroundNeighborSlots - 1], 0.77f));

    std::filesystem::remove(moshit_path);
    std::filesystem::remove(bgop_path);
    std::cout << "background_operator ok\n";
    return 0;
}
