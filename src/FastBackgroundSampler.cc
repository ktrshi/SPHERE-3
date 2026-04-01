#include "FastBackgroundSampler.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr float kValidationEpsilon = 1e-5f;

int cluster_of(uint16_t pixel) {
    return static_cast<int>(pixel) / 7;
}

int slot_in_cluster(uint16_t pixel) {
    return static_cast<int>(pixel) % 7;
}

uint16_t neighbor_pixel_for_slot(uint16_t pixel, int neighbor_slot) {
    const int cluster = cluster_of(pixel);
    const int slot = slot_in_cluster(pixel);
    const int neighbor_slot_in_cluster = (slot + neighbor_slot + 1) % 7;
    return static_cast<uint16_t>(cluster * 7 + neighbor_slot_in_cluster);
}

void validate_operator_or_throw(const BackgroundOperator& op) {
    if (std::memcmp(op.header.magic, "BGOP", 4) != 0) {
        throw std::runtime_error("FastBackgroundSampler: operator has invalid magic");
    }
    if (op.header.version != 1) {
        throw std::runtime_error("FastBackgroundSampler: unsupported operator version");
    }
    if (op.header.n_pixels != kBackgroundPixels) {
        throw std::runtime_error("FastBackgroundSampler: operator pixel count mismatch");
    }
    if (op.header.time_bins != kBackgroundTimeBins) {
        throw std::runtime_error("FastBackgroundSampler: operator time bin count mismatch");
    }
    if (!std::isfinite(op.header.window_ns) || op.header.window_ns <= 0.0f) {
        throw std::runtime_error("FastBackgroundSampler: operator window_ns must be > 0");
    }

    const size_t expected_pixels = static_cast<size_t>(op.header.n_pixels);
    const size_t expected_time_bins = static_cast<size_t>(op.header.time_bins);
    if (op.pixel_rate_per_event.size() != expected_pixels) {
        throw std::runtime_error("FastBackgroundSampler: pixel_rate_per_event size mismatch");
    }
    if (op.pixel_time_cdf.size() != expected_pixels * expected_time_bins) {
        throw std::runtime_error("FastBackgroundSampler: pixel_time_cdf size mismatch");
    }
    if (op.neighbor_fire_prob.size() != expected_pixels * static_cast<size_t>(kBackgroundNeighborSlots)) {
        throw std::runtime_error("FastBackgroundSampler: neighbor_fire_prob size mismatch");
    }

    for (size_t pixel = 0; pixel < expected_pixels; ++pixel) {
        const float rate = op.pixel_rate_per_event[pixel];
        if (!std::isfinite(rate) || rate < 0.0f) {
            throw std::runtime_error("FastBackgroundSampler: pixel_rate_per_event contains invalid values");
        }

        const size_t cdf_base = pixel * expected_time_bins;
        float previous = 0.0f;
        for (size_t bin = 0; bin < expected_time_bins; ++bin) {
            const float value = op.pixel_time_cdf[cdf_base + bin];
            if (!std::isfinite(value) || value < -kValidationEpsilon || value > 1.0f + kValidationEpsilon) {
                throw std::runtime_error("FastBackgroundSampler: pixel_time_cdf contains invalid values");
            }
            if (value + kValidationEpsilon < previous) {
                throw std::runtime_error("FastBackgroundSampler: pixel_time_cdf must be non-decreasing");
            }
            previous = value;
        }

        if (rate > 0.0f && previous <= 0.0f) {
            throw std::runtime_error("FastBackgroundSampler: active pixels must have a non-empty time CDF");
        }
    }

    for (const float probability : op.neighbor_fire_prob) {
        if (!std::isfinite(probability) ||
            probability < -kValidationEpsilon ||
            probability > 1.0f + kValidationEpsilon) {
            throw std::runtime_error("FastBackgroundSampler: neighbor_fire_prob contains invalid values");
        }
    }
}

}  // namespace

FastBackgroundSampler::FastBackgroundSampler(std::shared_ptr<const BackgroundOperator> op, uint32_t seed)
    : op_(std::move(op)), rng_(seed) {
    if (!op_) {
        throw std::runtime_error("FastBackgroundSampler: operator is null");
    }

    validate_operator_or_throw(*op_);

    pixel_cdf_.reserve(op_->pixel_rate_per_event.size());
    float running = 0.0f;
    for (const float rate : op_->pixel_rate_per_event) {
        running += rate;
        pixel_cdf_.push_back(running);
    }
    total_rate_ = running;
    bin_width_ns_ = op_->header.window_ns / static_cast<float>(op_->header.time_bins);
}

uint16_t FastBackgroundSampler::sample_pixel() {
    const float target = std::max(uni_(rng_) * total_rate_,
                                  std::numeric_limits<float>::min());
    const auto it = std::lower_bound(pixel_cdf_.begin(), pixel_cdf_.end(), target);
    if (it == pixel_cdf_.end()) {
        return static_cast<uint16_t>(pixel_cdf_.size() - 1);
    }
    return static_cast<uint16_t>(std::distance(pixel_cdf_.begin(), it));
}

float FastBackgroundSampler::sample_time(uint16_t pixel) {
    const size_t base = static_cast<size_t>(pixel) * op_->header.time_bins;
    const float u = uni_(rng_);
    const auto begin = op_->pixel_time_cdf.begin() + static_cast<std::ptrdiff_t>(base);
    const auto end = begin + static_cast<std::ptrdiff_t>(op_->header.time_bins);
    const auto it = std::lower_bound(begin, end, u);
    const size_t bin_index =
        (it == end) ? static_cast<size_t>(op_->header.time_bins - 1)
                    : static_cast<size_t>(std::distance(begin, it));
    return (static_cast<float>(bin_index) + uni_(rng_)) * bin_width_ns_;
}

void FastBackgroundSampler::SampleInto(MoshitWriter& writer) {
    if (total_rate_ <= 0.0f) {
        return;
    }

    std::poisson_distribution<int> hit_count_distribution(total_rate_);
    const int hit_count = hit_count_distribution(rng_);
    if (hit_count <= 0) {
        return;
    }

    for (int hit_index = 0; hit_index < hit_count; ++hit_index) {
        const uint16_t pixel = sample_pixel();
        const float t = sample_time(pixel);
        writer.AddHit(pixel, t, 2, 0, 0, 0, t);

        const size_t neighbor_base = static_cast<size_t>(pixel) * kBackgroundNeighborSlots;
        for (int neighbor_slot = 0; neighbor_slot < kBackgroundNeighborSlots; ++neighbor_slot) {
            const float probability = op_->neighbor_fire_prob[neighbor_base + static_cast<size_t>(neighbor_slot)];
            if (probability <= 0.0f) {
                continue;
            }
            if (uni_(rng_) < std::min(probability, 1.0f)) {
                const uint16_t neighbor_pixel = neighbor_pixel_for_slot(pixel, neighbor_slot);
                writer.AddHit(neighbor_pixel, t, 2, 0, 0, 0, t);
            }
        }
    }
}
