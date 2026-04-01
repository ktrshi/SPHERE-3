#include "BackgroundOperator.hh"
#include "MoshitReader.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kMoshitSuffix = ".moshit.zst";

bool has_moshit_suffix(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    if (name.size() < std::char_traits<char>::length(kMoshitSuffix)) {
        return false;
    }
    return name.compare(name.size() - std::char_traits<char>::length(kMoshitSuffix),
                        std::char_traits<char>::length(kMoshitSuffix),
                        kMoshitSuffix) == 0;
}

int cluster_of(uint16_t pixel) { return pixel / 7; }

int slot_in_cluster(uint16_t pixel) { return pixel % 7; }

int relative_slot(uint16_t seed, uint16_t other) {
    const int a = slot_in_cluster(seed);
    const int b = slot_in_cluster(other);
    if (cluster_of(seed) != cluster_of(other) || a == b) {
        return -1;
    }
    return (b - a + 7) % 7 - 1;
}

void validate_pixel_or_throw(uint16_t pixel,
                             const std::filesystem::path& file_path,
                             size_t hit_index) {
    if (pixel < static_cast<uint16_t>(kBackgroundPixels)) {
        return;
    }
    throw std::runtime_error("Invalid pixel index " + std::to_string(pixel) +
                             " in " + file_path.string() +
                             " at hit #" + std::to_string(hit_index));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 9) {
            std::cerr << "Usage: build_background_operator <moshits_dir> <out.bgop.zst> "
                         "<catm> <zz> <phi> <the> <window_ns> <calibration_events>"
                      << std::endl;
            return 1;
        }

        const std::filesystem::path root = argv[1];
        const std::string out_path = argv[2];
        const uint16_t catm = static_cast<uint16_t>(std::stoi(argv[3]));
        const float zz = std::stof(argv[4]);
        const float phi = std::stof(argv[5]);
        const float the = std::stof(argv[6]);
        const float window_ns = std::stof(argv[7]);
        const uint32_t calibration_events = static_cast<uint32_t>(std::stoul(argv[8]));

        if (window_ns <= 0.0f) {
            std::cerr << "window_ns must be > 0" << std::endl;
            return 1;
        }
        if (calibration_events == 0) {
            std::cerr << "calibration_events must be > 0" << std::endl;
            return 1;
        }

        BackgroundOperator op;
        op.header = make_background_operator_header(catm, zz, phi, the, window_ns, calibration_events);
        op.pixel_rate_per_event.assign(kBackgroundPixels, 0.0f);
        op.pixel_time_cdf.assign(kBackgroundPixels * kBackgroundTimeBins, 0.0f);
        op.neighbor_fire_prob.assign(kBackgroundPixels * kBackgroundNeighborSlots, 0.0f);

        std::vector<float> seed_counts(kBackgroundPixels, 0.0f);

        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!has_moshit_suffix(entry.path())) {
                continue;
            }

            const MoshitFile mf = MoshitReader::Read(entry.path().string());
            for (size_t idx = 0; idx < mf.hits.size(); ++idx) {
                validate_pixel_or_throw(mf.hits[idx].pixel, entry.path(), idx);
            }

            for (const auto& hit : mf.hits) {
                if (hit.origin != 2) {
                    continue;
                }
                op.pixel_rate_per_event[hit.pixel] += 1.0f;
                seed_counts[hit.pixel] += 1.0f;

                const int bin = std::clamp(
                    static_cast<int>(hit.t / window_ns * static_cast<float>(kBackgroundTimeBins)),
                    0,
                    static_cast<int>(kBackgroundTimeBins) - 1);
                op.pixel_time_cdf[static_cast<size_t>(hit.pixel) * kBackgroundTimeBins + bin] += 1.0f;
            }

            for (size_t i = 0; i < mf.hits.size(); ++i) {
                if (mf.hits[i].origin != 2) {
                    continue;
                }
                std::array<bool, kBackgroundNeighborSlots> slot_seen{};
                for (size_t j = 0; j < mf.hits.size(); ++j) {
                    if (i == j) {
                        continue;
                    }
                    if (mf.hits[j].origin != 2) {
                        continue;
                    }
                    if (std::fabs(mf.hits[i].t - mf.hits[j].t) > 5.0f) {
                        continue;
                    }
                    const int slot = relative_slot(mf.hits[i].pixel, mf.hits[j].pixel);
                    if (slot >= 0 && slot < kBackgroundNeighborSlots) {
                        if (!slot_seen[static_cast<size_t>(slot)]) {
                            slot_seen[static_cast<size_t>(slot)] = true;
                            op.neighbor_fire_prob[static_cast<size_t>(mf.hits[i].pixel) *
                                                      kBackgroundNeighborSlots +
                                                  static_cast<size_t>(slot)] += 1.0f;
                        }
                    }
                }
            }
        }

        for (int p = 0; p < kBackgroundPixels; ++p) {
            op.pixel_rate_per_event[p] /= static_cast<float>(calibration_events);

            float running = 0.0f;
            const size_t base = static_cast<size_t>(p) * kBackgroundTimeBins;
            const float total = std::max(1.0f, seed_counts[p]);
            for (int b = 0; b < kBackgroundTimeBins; ++b) {
                running += op.pixel_time_cdf[base + static_cast<size_t>(b)] / total;
                op.pixel_time_cdf[base + static_cast<size_t>(b)] = std::min(running, 1.0f);
            }

            for (int s = 0; s < kBackgroundNeighborSlots; ++s) {
                const size_t idx =
                    static_cast<size_t>(p) * kBackgroundNeighborSlots + static_cast<size_t>(s);
                op.neighbor_fire_prob[idx] /= total;
            }
        }

        write_background_operator(out_path, op);
        std::cout << "Wrote background operator to " << out_path << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 2;
    }
}
