#ifndef BACKGROUND_OPERATOR_HH
#define BACKGROUND_OPERATOR_HH

#include <cstdint>
#include <string>
#include <vector>

constexpr int kBackgroundPixels = 2653;
constexpr int kBackgroundNeighborSlots = 6;
constexpr uint8_t kBackgroundTimeBins = 128;

#pragma pack(push, 1)
struct BackgroundOperatorHeader {
    char magic[4];
    uint8_t version;
    uint8_t time_bins;
    uint16_t n_pixels;
    float zz;
    float phi;
    float the;
    uint16_t catm;
    uint16_t _pad;
    float window_ns;
    uint32_t calibration_events;
};
#pragma pack(pop)
static_assert(sizeof(BackgroundOperatorHeader) == 32,
              "BackgroundOperatorHeader must be 32 bytes");

struct BackgroundOperator {
    BackgroundOperatorHeader header{};
    std::vector<float> pixel_rate_per_event;
    std::vector<float> pixel_time_cdf;
    std::vector<float> neighbor_fire_prob;
};

BackgroundOperatorHeader make_background_operator_header(uint16_t catm,
                                                         float zz,
                                                         float phi,
                                                         float the,
                                                         float window_ns,
                                                         uint32_t calibration_events);
void write_background_operator(const std::string& path, const BackgroundOperator& op);
BackgroundOperator read_background_operator(const std::string& path);

#endif
