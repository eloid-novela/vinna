#pragma once

namespace vinna {

struct ControllerSettings {
    // Loudness (LUFS, momentary) above which vinna starts turning the volume down.
    double threshold_lufs = -20.0;
    // How strongly loud passages are pulled back towards the threshold.
    // 1 = no reduction, 4 = 4 dB over becomes 1 dB over, large values ~ limiter.
    double ratio = 4.0;
    // Never cut more than this many dB below the reference volume.
    double max_cut_db = 20.0;
    // Time constant for turning the volume down (fast).
    double attack_ms = 150.0;
    // Time constant for bringing the volume back up (slow).
    double release_ms = 3000.0;
    // Wait this long after the last loud moment before starting to release.
    double hold_ms = 1000.0;
    // Below this loudness the input is considered silence: the gain is frozen,
    // so a pause in the middle of an action scene does not reset the volume.
    double gate_lufs = -55.0;
};

// Pure gain computer. Receives the loudness of the *source* (before vinna's
// own volume change) and returns the gain to apply, in dB, always <= 0.
// 0 dB means "the volume the user had set".
class Controller {
public:
    explicit Controller(const ControllerSettings& settings = {});

    // Advances the controller by `dt_ms` with the given source loudness.
    // Returns the new gain in dB.
    double update(double source_lufs, double dt_ms);

    // Gain the controller wants right now (dB, <= 0).
    double gain_db() const { return gain_db_; }

    // Gain that would be reached with a steady input at `source_lufs`.
    double target_gain_db(double source_lufs) const;

    void reset();
    const ControllerSettings& settings() const { return settings_; }

private:
    ControllerSettings settings_;
    double gain_db_ = 0.0;
    double since_loud_ms_ = 1e12;
};

}  // namespace vinna
