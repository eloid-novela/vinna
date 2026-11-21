#include "suave/volume_actuator.hpp"

namespace suave {

namespace {

// Used by --backend none: keeps the value in memory and touches nothing.
class NullActuator final : public VolumeActuator {
public:
    std::string name() const override { return "none (volume is not changed)"; }
    bool connect(std::string&) override { return true; }
    std::optional<double> read() override { return volume_; }
    bool write(double linear) override {
        volume_ = linear;
        return true;
    }

private:
    double volume_ = 1.0;
};

}  // namespace

std::unique_ptr<VolumeActuator> make_null_actuator() { return std::make_unique<NullActuator>(); }

std::unique_ptr<VolumeActuator> make_actuator(const Config& config, std::string& error) {
    std::string backend = config.backend;
    if (backend == "auto") {
        if (!config.vlc_password.empty()) {
            backend = "vlc-http";
        } else {
#ifdef _WIN32
            backend = "wasapi";
#else
            backend = "pulse";
#endif
        }
    }

    if (backend == "none") {
        return make_null_actuator();
    }
    if (backend == "vlc-http") {
        if (config.vlc_password.empty()) {
            error = "the vlc-http backend needs --vlc-password (VLC requires one for its web interface)";
            return nullptr;
        }
        return make_vlc_http_actuator(config);
    }
#ifdef _WIN32
    if (backend == "wasapi") {
        return make_wasapi_actuator(config);
    }
    if (backend == "pulse") {
        error = "the pulse backend is only available on Linux; use wasapi or vlc-http";
        return nullptr;
    }
#else
    if (backend == "pulse") {
        return make_pulse_actuator(config);
    }
    if (backend == "wasapi") {
        error = "the wasapi backend is only available on Windows; use pulse or vlc-http";
        return nullptr;
    }
#endif
    error = "unknown backend '" + backend + "'";
    return nullptr;
}

}  // namespace suave
