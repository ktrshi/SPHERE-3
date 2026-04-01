#ifndef FAST_BACKGROUND_SAMPLER_HH
#define FAST_BACKGROUND_SAMPLER_HH

#include "BackgroundOperator.hh"
#include "MoshitWriter.hh"

#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

struct FastBackgroundSampleStats {
    uint32_t injected_hits{0};
    float tmin_ns{std::numeric_limits<float>::max()};
    float tmax_ns{-std::numeric_limits<float>::max()};

    bool HasHits() const { return injected_hits > 0; }
};

void ValidateBackgroundOperatorForSampling(const BackgroundOperator& op);
std::string BackgroundOperatorCompatibilityError(const BackgroundOperator& op,
                                                 uint16_t catm,
                                                 float zz,
                                                 float phi_deg,
                                                 float the_deg);

class FastBackgroundSampler {
public:
    FastBackgroundSampler(std::shared_ptr<const BackgroundOperator> op, uint32_t seed);

    FastBackgroundSampleStats SampleInto(MoshitWriter& writer);

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
