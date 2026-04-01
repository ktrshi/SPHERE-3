#include "BackgroundOperator.hh"

#include <zstd.h>

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<char> read_file_bytes(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        throw std::runtime_error("BackgroundOperator: cannot open " + path);
    }

    const auto size = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0);
    std::vector<char> data(size);
    ifs.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

void write_file_bytes(const std::string& path, const std::vector<char>& data) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        throw std::runtime_error("BackgroundOperator: cannot open for write " + path);
    }
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::vector<char> zstd_decompress(const std::vector<char>& compressed, const std::string& path) {
    const unsigned long long decompressed_size_ull =
        ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (decompressed_size_ull == ZSTD_CONTENTSIZE_UNKNOWN ||
        decompressed_size_ull == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("BackgroundOperator: cannot determine decompressed size for " + path);
    }

    const size_t decompressed_size = static_cast<size_t>(decompressed_size_ull);
    std::vector<char> decompressed(decompressed_size);
    const size_t result = ZSTD_decompress(decompressed.data(), decompressed_size,
                                          compressed.data(), compressed.size());
    if (ZSTD_isError(result)) {
        throw std::runtime_error("BackgroundOperator: zstd decompress error: " +
                                 std::string(ZSTD_getErrorName(result)));
    }
    if (result != decompressed_size) {
        throw std::runtime_error("BackgroundOperator: decompressed size mismatch for " + path);
    }
    return decompressed;
}

std::vector<char> zstd_compress(const std::vector<char>& raw) {
    const size_t bound = ZSTD_compressBound(raw.size());
    std::vector<char> compressed(bound);
    const size_t compressed_size = ZSTD_compress(compressed.data(), bound, raw.data(), raw.size(), 3);
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error("BackgroundOperator: zstd compress error: " +
                                 std::string(ZSTD_getErrorName(compressed_size)));
    }
    compressed.resize(compressed_size);
    return compressed;
}

size_t expected_payload_size(const BackgroundOperatorHeader& header) {
    const size_t n_pixels = header.n_pixels;
    const size_t time_bins = header.time_bins;
    return sizeof(BackgroundOperatorHeader) +
           n_pixels * sizeof(float) +
           n_pixels * time_bins * sizeof(float) +
           n_pixels * static_cast<size_t>(kBackgroundNeighborSlots) * sizeof(float);
}

}  // namespace

BackgroundOperatorHeader make_background_operator_header(uint16_t catm,
                                                         float zz,
                                                         float phi,
                                                         float the,
                                                         float window_ns,
                                                         uint32_t calibration_events) {
    BackgroundOperatorHeader header{};
    std::memcpy(header.magic, "BGOP", 4);
    header.version = 1;
    header.time_bins = kBackgroundTimeBins;
    header.n_pixels = kBackgroundPixels;
    header.zz = zz;
    header.phi = phi;
    header.the = the;
    header.catm = catm;
    header._pad = 0;
    header.window_ns = window_ns;
    header.calibration_events = calibration_events;
    return header;
}

void write_background_operator(const std::string& path, const BackgroundOperator& op) {
    if (std::memcmp(op.header.magic, "BGOP", 4) != 0) {
        throw std::runtime_error("BackgroundOperator: bad magic in header while writing " + path);
    }
    if (op.header.version != 1) {
        throw std::runtime_error("BackgroundOperator: unsupported version while writing " + path);
    }
    if (op.header.time_bins != kBackgroundTimeBins) {
        throw std::runtime_error("BackgroundOperator: unsupported time_bins while writing " + path);
    }
    if (op.header.n_pixels != kBackgroundPixels) {
        throw std::runtime_error("BackgroundOperator: unsupported n_pixels while writing " + path);
    }

    const size_t n_pixels = op.header.n_pixels;
    const size_t time_bins = op.header.time_bins;

    if (op.pixel_rate_per_event.size() != n_pixels) {
        throw std::runtime_error("BackgroundOperator: pixel_rate_per_event size mismatch while writing " + path);
    }
    if (op.pixel_time_cdf.size() != n_pixels * time_bins) {
        throw std::runtime_error("BackgroundOperator: pixel_time_cdf size mismatch while writing " + path);
    }
    if (op.neighbor_fire_prob.size() != n_pixels * static_cast<size_t>(kBackgroundNeighborSlots)) {
        throw std::runtime_error("BackgroundOperator: neighbor_fire_prob size mismatch while writing " + path);
    }

    std::vector<char> raw(expected_payload_size(op.header));
    size_t offset = 0;

    std::memcpy(raw.data() + offset, &op.header, sizeof(op.header));
    offset += sizeof(op.header);

    const size_t pixel_rate_bytes = op.pixel_rate_per_event.size() * sizeof(float);
    std::memcpy(raw.data() + offset, op.pixel_rate_per_event.data(), pixel_rate_bytes);
    offset += pixel_rate_bytes;

    const size_t pixel_time_bytes = op.pixel_time_cdf.size() * sizeof(float);
    std::memcpy(raw.data() + offset, op.pixel_time_cdf.data(), pixel_time_bytes);
    offset += pixel_time_bytes;

    const size_t neighbor_prob_bytes = op.neighbor_fire_prob.size() * sizeof(float);
    std::memcpy(raw.data() + offset, op.neighbor_fire_prob.data(), neighbor_prob_bytes);

    const std::vector<char> compressed = zstd_compress(raw);
    write_file_bytes(path, compressed);
}

BackgroundOperator read_background_operator(const std::string& path) {
    const std::vector<char> compressed = read_file_bytes(path);
    const std::vector<char> raw = zstd_decompress(compressed, path);

    if (raw.size() < sizeof(BackgroundOperatorHeader)) {
        throw std::runtime_error("BackgroundOperator: file too small for header");
    }

    BackgroundOperator op;
    std::memcpy(&op.header, raw.data(), sizeof(op.header));

    if (std::memcmp(op.header.magic, "BGOP", 4) != 0) {
        throw std::runtime_error("BackgroundOperator: bad magic in " + path);
    }
    if (op.header.version != 1) {
        throw std::runtime_error("BackgroundOperator: unsupported version in " + path);
    }
    if (op.header.time_bins != kBackgroundTimeBins) {
        throw std::runtime_error("BackgroundOperator: unsupported time_bins in " + path);
    }
    if (op.header.n_pixels != kBackgroundPixels) {
        throw std::runtime_error("BackgroundOperator: unsupported n_pixels in " + path);
    }

    const size_t expected_size = expected_payload_size(op.header);
    if (raw.size() != expected_size) {
        throw std::runtime_error("BackgroundOperator: size mismatch: expected " +
                                 std::to_string(expected_size) + ", got " +
                                 std::to_string(raw.size()));
    }

    const size_t n_pixels = op.header.n_pixels;
    const size_t time_bins = op.header.time_bins;
    size_t offset = sizeof(BackgroundOperatorHeader);

    op.pixel_rate_per_event.resize(n_pixels);
    const size_t pixel_rate_bytes = op.pixel_rate_per_event.size() * sizeof(float);
    std::memcpy(op.pixel_rate_per_event.data(), raw.data() + offset, pixel_rate_bytes);
    offset += pixel_rate_bytes;

    op.pixel_time_cdf.resize(n_pixels * time_bins);
    const size_t pixel_time_bytes = op.pixel_time_cdf.size() * sizeof(float);
    std::memcpy(op.pixel_time_cdf.data(), raw.data() + offset, pixel_time_bytes);
    offset += pixel_time_bytes;

    op.neighbor_fire_prob.resize(n_pixels * static_cast<size_t>(kBackgroundNeighborSlots));
    const size_t neighbor_prob_bytes = op.neighbor_fire_prob.size() * sizeof(float);
    std::memcpy(op.neighbor_fire_prob.data(), raw.data() + offset, neighbor_prob_bytes);

    return op;
}
