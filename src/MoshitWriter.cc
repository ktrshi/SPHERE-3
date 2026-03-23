#include "MoshitWriter.hh"
#include <zstd.h>
#include <fstream>
#include <cstring>
#include <stdexcept>

void MoshitWriter::Begin(float zz, float xsh, float ysh) {
    zz_  = zz;
    xsh_ = xsh;
    ysh_ = ysh;
    n_hits_ = 0;
    hits_.clear();
    hits_.reserve(10000);
}

void MoshitWriter::AddHit(uint16_t pixel, float t, uint8_t origin,
                           uint16_t ii, uint16_t jj, uint8_t kk, float t0) {
    hits_.push_back({pixel, origin, kk, ii, jj, t, t0});
    ++n_hits_;
}

void MoshitWriter::Flush(const std::string& filename) {
    // Build raw buffer: header + hits
    MoshitFileHeader hdr{};
    std::memcpy(hdr.magic, "MOSH", 4);
    hdr.version = 1;
    hdr._pad0 = 0;
    hdr._pad1 = 0;
    hdr.zz  = zz_;
    hdr.xsh = xsh_;
    hdr.ysh = ysh_;
    hdr.n_hits = n_hits_;

    size_t rawSize = sizeof(hdr) + n_hits_ * sizeof(MoshitFileHit);
    std::vector<char> raw(rawSize);
    std::memcpy(raw.data(), &hdr, sizeof(hdr));
    if (n_hits_ > 0) {
        std::memcpy(raw.data() + sizeof(hdr), hits_.data(),
                     n_hits_ * sizeof(MoshitFileHit));
    }

    // Compress
    size_t bound = ZSTD_compressBound(rawSize);
    std::vector<char> compressed(bound);
    size_t compressedSize = ZSTD_compress(compressed.data(), bound,
                                           raw.data(), rawSize, 3);
    if (ZSTD_isError(compressedSize)) {
        throw std::runtime_error("MoshitWriter: zstd error: " +
                                  std::string(ZSTD_getErrorName(compressedSize)));
    }

    // Write
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) throw std::runtime_error("MoshitWriter: cannot open " + filename);
    ofs.write(compressed.data(), compressedSize);
}

void MoshitWriter::Reset() {
    n_hits_ = 0;
    hits_.clear();
}
