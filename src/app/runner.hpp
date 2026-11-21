#pragma once

#include "suave/config.hpp"

namespace suave {

// `suave run`: listens and adjusts the volume until Ctrl+C.
int run_controller(const Config& config);

// `suave monitor`: prints the loudness that suave sees, changes nothing.
int run_monitor(const Config& config);

// `suave devices`: lists audio devices usable with --device.
int list_devices();

}  // namespace suave
