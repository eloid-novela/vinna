#include "vinna/level_meter.hpp"

#include <cmath>
#include <limits>

namespace vinna {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Coefficients follow the sample-rate independent derivation used by
// libebur128, so the weighting is correct at 44.1 kHz, 48 kHz, 96 kHz, ...
Biquad make_shelf(double rate) {
    const double f0 = 1681.974450955533;
    const double gain_db = 3.999843853973347;
    const double q = 0.7071752369554196;

    const double k = std::tan(kPi * f0 / rate);
    const double vh = std::pow(10.0, gain_db / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;

    Biquad bq;
    bq.b0 = (vh + vb * k / q + k * k) / a0;
    bq.b1 = 2.0 * (k * k - vh) / a0;
    bq.b2 = (vh - vb * k / q + k * k) / a0;
    bq.a1 = 2.0 * (k * k - 1.0) / a0;
    bq.a2 = (1.0 - k / q + k * k) / a0;
    return bq;
}

Biquad make_highpass(double rate) {
    const double f0 = 38.13547087602444;
    const double q = 0.5003270373238773;

    const double k = std::tan(kPi * f0 / rate);
    const double a0 = 1.0 + k / q + k * k;

    Biquad bq;
    bq.b0 = 1.0;
    bq.b1 = -2.0;
    bq.b2 = 1.0;
    bq.a1 = 2.0 * (k * k - 1.0) / a0;
    bq.a2 = (1.0 - k / q + k * k) / a0;
    return bq;
}

}  // namespace

KWeightingFilter::KWeightingFilter(unsigned sample_rate, unsigned channels)
    : sample_rate_(sample_rate), channels_(channels == 0 ? 1 : channels) {
    shelf_.assign(channels_, make_shelf(sample_rate_));
    highpass_.assign(channels_, make_highpass(sample_rate_));
}

void KWeightingFilter::process(const float* interleaved, std::size_t frames) {
    if (interleaved == nullptr) {
        // Treat missing data as silence so time keeps advancing.
        acc_.frames += frames;
        return;
    }
    double sum = 0.0;
    for (std::size_t f = 0; f < frames; ++f) {
        for (unsigned c = 0; c < channels_; ++c) {
            const double x = interleaved[f * channels_ + c];
            const double y = highpass_[c].process(shelf_[c].process(x));
            sum += y * y;
        }
    }
    acc_.power_sum += sum;
    acc_.frames += frames;
}

KWeightingFilter::Block KWeightingFilter::take() {
    Block out = acc_;
    acc_ = Block{};
    return out;
}

double power_to_lufs(double mean_square) {
    if (mean_square <= 1e-20) {
        return -std::numeric_limits<double>::infinity();
    }
    return -0.691 + 10.0 * std::log10(mean_square);
}

void LoudnessWindow::push(const KWeightingFilter::Block& block, unsigned sample_rate) {
    if (block.frames == 0) {
        return;
    }
    sample_rate_ = sample_rate == 0 ? sample_rate_ : sample_rate;
    entries_.push_back({block.power_sum, block.frames});
    total_power_ += block.power_sum;
    total_frames_ += block.frames;

    const auto max_frames =
        static_cast<std::size_t>(window_ms_ * static_cast<double>(sample_rate_) / 1000.0);
    // Drop whole blocks from the front while the window stays full without them.
    while (entries_.size() > 1 && total_frames_ - entries_.front().frames >= max_frames) {
        total_power_ -= entries_.front().power_sum;
        total_frames_ -= entries_.front().frames;
        entries_.pop_front();
    }
    if (total_power_ < 0.0) {
        total_power_ = 0.0;  // Guard against floating point drift.
    }
}

double LoudnessWindow::lufs() const {
    if (total_frames_ == 0) {
        return -std::numeric_limits<double>::infinity();
    }
    return power_to_lufs(total_power_ / static_cast<double>(total_frames_));
}

void LoudnessWindow::reset() {
    entries_.clear();
    total_power_ = 0.0;
    total_frames_ = 0;
}

}  // namespace vinna
