#ifndef PHEL_READER_HH
#define PHEL_READER_HH

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

#pragma pack(push, 1)
struct PhelFileHeader {
    char     magic[4];     // "PHEL"
    uint8_t  version;      // 1
    uint8_t  flags;        // bit 0: has_background
    uint16_t clone_num;
    float    xsh;          // shower core shift X, meters
    float    ysh;          // shower core shift Y, meters
    float    zz;           // detector altitude, meters (negative)
    uint16_t catm;         // atmosphere model
    uint16_t _pad;
    float    tmin;         // min arrival time, ns
    float    tmax;         // max arrival time, ns
    float    tbig;         // reference time, ns
    uint32_t n_photons;
};
static_assert(sizeof(PhelFileHeader) == 40, "PhelFileHeader must be 40 bytes");

struct PhelFilePhoton {
    uint16_t i;            // X-bin 0..1279
    uint16_t j;            // Y-bin 0..1279
    uint8_t  k;            // bit7: is_background, bits 0-6: time-bin
    uint8_t  _reserved[3];
    float    x;            // X coordinate, meters
    float    y;            // Y coordinate, meters
    float    t;            // arrival time, ns
};
static_assert(sizeof(PhelFilePhoton) == 20, "PhelFilePhoton must be 20 bytes");
#pragma pack(pop)

struct PhelPhoton {
    uint16_t i, j;
    uint8_t  time_bin;
    bool     is_background;
    float    x, y, t;
};

struct PhelEvent {
    float    xsh, ysh, zz;
    float    tmin, tmax, tbig;
    uint16_t catm, clone_num;
    bool     has_background;
    uint32_t n_photons;
    std::vector<PhelPhoton> photons;
};

class PhelReader {
public:
    // Reads entire .phel.zst file. Throws std::runtime_error on:
    // - file open failure
    // - zstd decompression failure
    // - magic mismatch
    // - n_photons vs actual record count mismatch
    static PhelEvent Read(const std::string& filename);
};

#endif
