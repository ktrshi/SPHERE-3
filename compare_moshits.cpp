#include "MoshitReader.hh"

#include <algorithm>
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

struct Stats {
    uint64_t total_hits = 0;
    std::vector<uint64_t> per_pixel = std::vector<uint64_t>(2653, 0);
};

Stats collect(const std::filesystem::path& root) {
    Stats out;
    size_t consumed_files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!has_moshit_suffix(entry.path())) {
            continue;
        }
        consumed_files += 1;
        const MoshitFile mf = MoshitReader::Read(entry.path().string());
        for (size_t idx = 0; idx < mf.hits.size(); ++idx) {
            const auto& hit = mf.hits[idx];
            if (hit.pixel >= out.per_pixel.size()) {
                throw std::runtime_error("Invalid pixel index " + std::to_string(hit.pixel) +
                                         " in " + entry.path().string() +
                                         " at hit #" + std::to_string(idx));
            }
            if (hit.origin != 2) {
                continue;
            }
            out.total_hits += 1;
            out.per_pixel[hit.pixel] += 1;
        }
    }
    if (consumed_files == 0) {
        throw std::runtime_error("No .moshit.zst files found under " + root.string());
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: compare_moshits <reference_dir> <candidate_dir>" << std::endl;
            return 1;
        }

        const Stats ref = collect(argv[1]);
        const Stats cand = collect(argv[2]);

        const double total_delta =
            (ref.total_hits == 0)
                ? ((cand.total_hits == 0) ? 0.0 : 100.0)
                : 100.0 * std::fabs(static_cast<double>(cand.total_hits) -
                                    static_cast<double>(ref.total_hits)) /
                      static_cast<double>(ref.total_hits);

        double worst_pixel_delta = 0.0;
        for (size_t p = 0; p < ref.per_pixel.size(); ++p) {
            if (ref.per_pixel[p] == 0) {
                if (cand.per_pixel[p] > 0) {
                    worst_pixel_delta = std::max(worst_pixel_delta, 100.0);
                }
                continue;
            }
            const double d = 100.0 *
                             std::fabs(static_cast<double>(cand.per_pixel[p]) -
                                       static_cast<double>(ref.per_pixel[p])) /
                             static_cast<double>(ref.per_pixel[p]);
            worst_pixel_delta = std::max(worst_pixel_delta, d);
        }

        std::cout << "total_delta_pct=" << total_delta << std::endl;
        std::cout << "worst_pixel_delta_pct=" << worst_pixel_delta << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 2;
    }
}
