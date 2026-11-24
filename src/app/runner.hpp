#pragma once

#include "vinna/config.hpp"

namespace vinna {

// `vinna run`: listens and adjusts the volume until Ctrl+C.
int run_controller(const Config& config);

// `vinna monitor`: prints the loudness that vinna sees, changes nothing.
int run_monitor(const Config& config);

// `vinna restore`: puts the user's volume back if a previous `vinna run` was
// killed while the volume was lowered.
int restore_saved_volume(const Config& config);

// `vinna devices`: lists audio devices usable with --device.
int list_devices();

}  // namespace vinna
