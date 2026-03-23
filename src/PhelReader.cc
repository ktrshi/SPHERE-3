#include "PhelReader.hh"
#include <zstd.h>
#include <fstream>
#include <cstring>

PhelEvent PhelReader::Read(const std::string& filename) {
    // Read compressed file
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if (!ifs) throw std::runtime_error("PhelReader: cannot open " + filename);

    auto compressedSize = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0);
    std::vector<char> compressed(compressedSize);
    ifs.read(compressed.data(), compressedSize);
    ifs.close();

    // Get decompressed size
    auto decompressedSize = ZSTD_getFrameContentSize(compressed.data(), compressedSize);
    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN || decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("PhelReader: cannot determine decompressed size for " + filename);
    }

    // Decompress
    std::vector<char> decompressed(decompressedSize);
    size_t result = ZSTD_decompress(decompressed.data(), decompressedSize,
                                     compressed.data(), compressedSize);
    if (ZSTD_isError(result)) {
        throw std::runtime_error("PhelReader: zstd error: " +
                                  std::string(ZSTD_getErrorName(result)));
    }

    // Validate size
    if (decompressedSize < sizeof(PhelFileHeader)) {
        throw std::runtime_error("PhelReader: file too small for header");
    }

    // Parse header
    PhelFileHeader hdr;
    std::memcpy(&hdr, decompressed.data(), sizeof(hdr));

    if (std::memcmp(hdr.magic, "PHEL", 4) != 0) {
        throw std::runtime_error("PhelReader: bad magic in " + filename);
    }

    size_t expectedSize = sizeof(PhelFileHeader) + hdr.n_photons * sizeof(PhelFilePhoton);
    if (decompressedSize != expectedSize) {
        throw std::runtime_error("PhelReader: size mismatch: expected " +
                                  std::to_string(expectedSize) + ", got " +
                                  std::to_string(decompressedSize));
    }

    // Build PhelEvent
    PhelEvent event;
    event.xsh = hdr.xsh;
    event.ysh = hdr.ysh;
    event.zz  = hdr.zz;
    event.tmin = hdr.tmin;
    event.tmax = hdr.tmax;
    event.tbig = hdr.tbig;
    event.catm = hdr.catm;
    event.clone_num = hdr.clone_num;
    event.has_background = (hdr.flags & 0x01) != 0;
    event.n_photons = hdr.n_photons;

    // Parse photons
    event.photons.resize(hdr.n_photons);
    const auto* raw = reinterpret_cast<const PhelFilePhoton*>(
        decompressed.data() + sizeof(PhelFileHeader));

    for (uint32_t n = 0; n < hdr.n_photons; ++n) {
        auto& p = event.photons[n];
        p.i = raw[n].i;
        p.j = raw[n].j;
        p.time_bin = raw[n].k & 0x7F;
        p.is_background = (raw[n].k & 0x80) != 0;
        p.x = raw[n].x;
        p.y = raw[n].y;
        p.t = raw[n].t;
    }

    return event;
}
