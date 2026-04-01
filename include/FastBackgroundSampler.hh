#ifndef FAST_BACKGROUND_SAMPLER_HH
#define FAST_BACKGROUND_SAMPLER_HH

#include "BackgroundOperator.hh"
#include "MoshitWriter.hh"

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

class FastBackgroundSampler {
public:
    FastBackgroundSampler(std::shared_ptr<const BackgroundOperator> op, uint32_t seed);

    void SampleInto(MoshitWriter& writer);

private:
    uint16_t sample_pixel();
    float sample_time(uint16_t pixel);

    std::shared_ptr<const BackgroundOperator> op_;
    std::vector<float> pixel_cdf_;
    float total_rate_{0.0f};
    float bin_width_ns_{0.0f};
    std::mt19937 rng_;
    std::uniform_real_distribution<float> uni_{0.0f, 1.0f};
};

#endif
