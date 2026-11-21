// Controls the per-application volume on Linux through `pactl`.
// Works with PulseAudio and with PipeWire (pipewire-pulse).
#ifndef _WIN32

#include <cstdio>
#include <string>
#include <vector>

#include "pactl_parse.hpp"
#include "suave/volume_actuator.hpp"

namespace suave {

namespace {

// Runs a fixed command (no user input is ever interpolated) and returns stdout.
bool run_command(const std::string& command, std::string& output) {
    output.clear();
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        return false;
    }
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, n);
    }
    return ::pclose(pipe) == 0;
}

class PulseActuator final : public VolumeActuator {
public:
    explicit PulseActuator(const Config& config) : app_(config.app) {}

    std::string name() const override { return "pulse (app: " + app_ + ")"; }

    bool connect(std::string& error) override {
        std::string output;
        if (!run_command("LC_ALL=C pactl list sink-inputs 2>/dev/null", output)) {
            error = "could not run `pactl`. Install pulseaudio-utils (works with PipeWire too).";
            return false;
        }
        streams_.clear();
        for (const auto& input : pulse::parse_sink_inputs(output)) {
            if (pulse::matches(input, app_)) {
                streams_.push_back(input);
            }
        }
        if (streams_.empty()) {
            error = "no audio stream from '" + app_ + "' is playing";
            return false;
        }
        return true;
    }

    std::optional<double> read() override {
        std::string error;
        // Re-read so that changes made by the user (or a new stream) are seen.
        if (!connect(error)) {
            return std::nullopt;
        }
        return pulse::raw_to_linear(streams_.front().raw_volume);
    }

    bool write(double linear) override {
        if (streams_.empty()) {
            return false;
        }
        const auto raw = pulse::linear_to_raw(linear);
        bool ok = true;
        std::string output;
        for (const auto& stream : streams_) {
            const std::string command = "pactl set-sink-input-volume " +
                                        std::to_string(stream.index) + " " + std::to_string(raw) +
                                        " 2>/dev/null";
            ok = run_command(command, output) && ok;
        }
        return ok;
    }

    double resolution() const override { return 0.002; }

private:
    std::string app_;
    std::vector<pulse::SinkInput> streams_;
};

}  // namespace

std::unique_ptr<VolumeActuator> make_pulse_actuator(const Config& config) {
    return std::make_unique<PulseActuator>(config);
}

}  // namespace suave

#endif  // !_WIN32
