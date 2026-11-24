#include "vinna/controller.hpp"

#include <algorithm>
#include <cmath>

namespace vinna {

namespace {

// One-pole smoothing coefficient for a time constant and a step of dt.
double smoothing(double dt_ms, double tau_ms) {
    if (tau_ms <= 0.0) {
        return 1.0;
    }
    return 1.0 - std::exp(-dt_ms / tau_ms);
}

}  // namespace

Controller::Controller(const ControllerSettings& settings) : settings_(settings) {
    settings_.ratio = std::max(1.0, settings_.ratio);
    settings_.max_cut_db = std::max(0.0, settings_.max_cut_db);
}

double Controller::target_gain_db(double source_lufs) const {
    const double over = source_lufs - settings_.threshold_lufs;
    if (!(over > 0.0)) {  // Also catches NaN / -inf.
        return 0.0;
    }
    const double cut = over * (1.0 - 1.0 / settings_.ratio);
    return -std::min(cut, settings_.max_cut_db);
}

double Controller::update(double source_lufs, double dt_ms) {
    if (dt_ms <= 0.0) {
        return gain_db_;
    }

    const bool silent = !std::isfinite(source_lufs) || source_lufs < settings_.gate_lufs;
    if (silent) {
        // Freeze: neither cut nor release while nothing meaningful is playing.
        return gain_db_;
    }

    const double target = target_gain_db(source_lufs);

    if (target < gain_db_) {
        // Too loud: turn down quickly.
        gain_db_ += (target - gain_db_) * smoothing(dt_ms, settings_.attack_ms);
        since_loud_ms_ = 0.0;
    } else {
        since_loud_ms_ += dt_ms;
        if (target < 0.0) {
            // Still above the threshold but less than before: the current cut
            // is deeper than needed; treat it as a loud moment for the hold timer.
            since_loud_ms_ = 0.0;
        }
        if (since_loud_ms_ >= settings_.hold_ms || target < 0.0) {
            gain_db_ += (target - gain_db_) * smoothing(dt_ms, settings_.release_ms);
        }
    }

    gain_db_ = std::clamp(gain_db_, -settings_.max_cut_db, 0.0);
    if (gain_db_ > -0.01) {
        gain_db_ = 0.0;
    }
    return gain_db_;
}

void Controller::reset() {
    gain_db_ = 0.0;
    since_loud_ms_ = 1e12;
}

}  // namespace vinna
