#pragma once

#include <memory>
#include <optional>
#include <string>

#include "vinna/config.hpp"

namespace vinna {

// Something whose volume vinna can read and change: VLC itself, or the
// per-application volume of the operating system mixer.
//
// Volumes are linear amplitude factors: 1.0 = 100 %, 0.5 = -6 dB.
class VolumeActuator {
public:
    virtual ~VolumeActuator() = default;

    virtual std::string name() const = 0;

    // Locates the target (process, stream, HTTP endpoint). Called at start-up
    // and again whenever a read or write fails (e.g. VLC was restarted).
    virtual bool connect(std::string& error) = 0;

    // Current volume, or nullopt if the target is not available right now.
    virtual std::optional<double> read() = 0;

    // Sets the volume. Returns false if the target is gone.
    virtual bool write(double linear) = 0;

    // Smallest volume step the backend can represent (used to tell our own
    // changes apart from the user's).
    virtual double resolution() const { return 0.001; }
};

// Creates the backend selected in `config.backend`. "auto" picks:
//   vlc-http  if a VLC password is configured,
//   wasapi    on Windows,
//   pulse     elsewhere.
std::unique_ptr<VolumeActuator> make_actuator(const Config& config, std::string& error);

// Individual backends (some only exist on one platform).
std::unique_ptr<VolumeActuator> make_vlc_http_actuator(const Config& config);
std::unique_ptr<VolumeActuator> make_null_actuator();
#ifdef _WIN32
std::unique_ptr<VolumeActuator> make_wasapi_actuator(const Config& config);
#else
std::unique_ptr<VolumeActuator> make_pulse_actuator(const Config& config);
#endif

}  // namespace vinna
