#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace suave {

// Second-order IIR section (Direct Form I).
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    double process(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
    void reset() { x1 = x2 = y1 = y2 = 0; }
};

// Accumulates K-weighted signal power (ITU-R BS.1770 / EBU R128 weighting).
// Feed it interleaved float frames; read and clear the accumulated power.
// Not thread-safe by itself: the capture layer protects it.
class KWeightingFilter {
public:
    KWeightingFilter(unsigned sample_rate, unsigned channels);

    // Processes interleaved samples and adds sum of squares to the accumulator.
    void process(const float* interleaved, std::size_t frames);

    // Returns {sum of per-channel mean squares summed over channels * frames, frames}
    // and resets the accumulator.
    struct Block {
        double power_sum = 0.0;  // Sum over frames of (sum over channels of y^2).
        std::size_t frames = 0;
    };
    Block take();

    unsigned sample_rate() const { return sample_rate_; }
    unsigned channels() const { return channels_; }

private:
    unsigned sample_rate_;
    unsigned channels_;
    std::vector<Biquad> shelf_;     // One per channel.
    std::vector<Biquad> highpass_;  // One per channel.
    Block acc_;
};

// Converts a mean-square value (K-weighted, summed over channels) to LUFS.
double power_to_lufs(double mean_square);

// Sliding-window loudness: keeps blocks and reports loudness over `window_ms`.
// The default 400 ms matches EBU R128 "momentary" loudness.
class LoudnessWindow {
public:
    explicit LoudnessWindow(double window_ms = 400.0) : window_ms_(window_ms) {}

    void push(const KWeightingFilter::Block& block, unsigned sample_rate);

    // Loudness in LUFS over the current window, or -inf if empty/silent.
    double lufs() const;

    void reset();

private:
    struct Entry {
        double power_sum;
        std::size_t frames;
    };
    double window_ms_;
    std::deque<Entry> entries_;
    double total_power_ = 0.0;
    std::size_t total_frames_ = 0;
    unsigned sample_rate_ = 48000;
};

}  // namespace suave
