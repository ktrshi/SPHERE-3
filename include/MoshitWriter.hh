#ifndef MOSHIT_WRITER_HH
#define MOSHIT_WRITER_HH

#include <cstdint>
#include <string>
#include <vector>

struct MoshitFileHeader {
    char     magic[4];    // "MOSH"
    uint8_t  version;     // 1
    uint8_t  _pad0;       // alignment
    uint16_t _pad1;       // alignment
    float    zz;          // detector altitude, meters
    float    xsh;         // shower core shift X, meters
    float    ysh;         // shower core shift Y, meters
    uint32_t n_hits;      // number of detection records
};
static_assert(sizeof(MoshitFileHeader) == 24, "MoshitFileHeader must be 24 bytes");

struct MoshitFileHit {
    uint16_t pixel;       // absolute pixel (0-2652)
    uint8_t  origin;      // 1=Cherenkov, 2=background
    uint8_t  kk;          // CORSIKA k-index
    uint16_t ii;          // CORSIKA i-index
    uint16_t jj;          // CORSIKA j-index
    float    t;           // detection time, ns
    float    t0;          // emission time, ns
};
static_assert(sizeof(MoshitFileHit) == 16, "MoshitFileHit must be 16 bytes");

class MoshitWriter {
public:
    void Begin(float zz, float xsh, float ysh);
    void AddHit(uint16_t pixel, float t, uint8_t origin,
                uint16_t ii, uint16_t jj, uint8_t kk, float t0);
    void Flush(const std::string& filename);
    void Reset();

    uint32_t HitCount() const { return n_hits_; }

private:
    float zz_ = 0, xsh_ = 0, ysh_ = 0;
    uint32_t n_hits_ = 0;
    std::vector<MoshitFileHit> hits_;
};

#endif
