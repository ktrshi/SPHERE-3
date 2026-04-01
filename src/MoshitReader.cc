#include "MoshitReader.hh"

#include <zstd.h>

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

MoshitFile MoshitReader::Read(const std::string& filename) {
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if (!ifs) {
        throw std::runtime_error("MoshitReader: cannot open " + filename);
    }

    const std::streampos end_pos = ifs.tellg();
    if (end_pos < 0) {
        throw std::runtime_error("MoshitReader: failed to determine file size for " + filename);
    }
    const size_t compressed_size = static_cast<size_t>(end_pos);
    ifs.seekg(0, std::ios::beg);
    if (!ifs) {
        throw std::runtime_error("MoshitReader: failed to seek file " + filename);
    }

    std::vector<char> compressed(compressed_size);
    if (compressed_size > 0) {
        ifs.read(compressed.data(), static_cast<std::streamsize>(compressed_size));
        if (!ifs) {
            throw std::runtime_error("MoshitReader: failed to read compressed data from " + filename);
        }
    }
    ifs.close();

    const unsigned long long decompressed_size_ull =
        ZSTD_getFrameContentSize(compressed.data(), compressed_size);
    if (decompressed_size_ull == ZSTD_CONTENTSIZE_UNKNOWN ||
        decompressed_size_ull == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("MoshitReader: cannot determine decompressed size for " + filename);
    }

    const size_t decompressed_size = static_cast<size_t>(decompressed_size_ull);
    std::vector<char> decompressed(decompressed_size);
    const size_t result = ZSTD_decompress(decompressed.data(), decompressed_size,
                                          compressed.data(), compressed_size);
    if (ZSTD_isError(result)) {
        throw std::runtime_error("MoshitReader: zstd error: " + std::string(ZSTD_getErrorName(result)));
    }

    if (decompressed_size < sizeof(MoshitFileHeader)) {
        throw std::runtime_error("MoshitReader: file too small for header");
    }

    MoshitFile file;
    std::memcpy(&file.header, decompressed.data(), sizeof(file.header));

    if (std::memcmp(file.header.magic, "MOSH", 4) != 0) {
        throw std::runtime_error("MoshitReader: bad magic in " + filename);
    }
    if (file.header.version != 1) {
        throw std::runtime_error("MoshitReader: unsupported version in " + filename);
    }

    const size_t expected_size =
        sizeof(MoshitFileHeader) + static_cast<size_t>(file.header.n_hits) * sizeof(MoshitFileHit);
    if (decompressed_size != expected_size) {
        throw std::runtime_error("MoshitReader: size mismatch: expected " +
                                 std::to_string(expected_size) + ", got " +
                                 std::to_string(decompressed_size));
    }

    file.hits.resize(file.header.n_hits);
    if (!file.hits.empty()) {
        std::memcpy(file.hits.data(),
                    decompressed.data() + sizeof(MoshitFileHeader),
                    file.hits.size() * sizeof(MoshitFileHit));
    }

    return file;
}
