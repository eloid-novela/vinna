#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vinna/level_meter.hpp"

namespace vinna {

struct CaptureDeviceInfo {
    std::string name;
    bool is_default = false;
    // True for devices that record what the computer is playing
    // (PulseAudio/PipeWire monitors, WASAPI loopback of an output).
    bool is_loopback = false;
};

// Records what the computer is playing and accumulates K-weighted power.
class AudioCapture {
public:
    virtual ~AudioCapture() = default;

    // Opens the device. Returns false and fills `error` on failure.
    virtual bool start(std::string& error) = 0;
    virtual void stop() = 0;

    // Takes the K-weighted power accumulated since the last call.
    virtual KWeightingFilter::Block take() = 0;

    virtual unsigned sample_rate() const = 0;
    virtual unsigned channels() const = 0;
    virtual std::string device_name() const = 0;
};

// `device_filter` empty = default system output loopback / monitor.
std::unique_ptr<AudioCapture> make_capture(const std::string& device_filter);

// Lists devices that can be used with `--device`.
std::vector<CaptureDeviceInfo> list_capture_devices(std::string& error);

}  // namespace vinna
