#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace suave::pulse {

// PulseAudio's "100 %" raw volume.
constexpr double kVolumeNorm = 65536.0;

struct SinkInput {
    std::uint32_t index = 0;
    std::uint32_t raw_volume = 0;  // First channel, raw units (65536 = 100 %).
    std::string application_name;
    std::string process_binary;
};

// Parses the output of `LC_ALL=C pactl list sink-inputs`.
std::vector<SinkInput> parse_sink_inputs(const std::string& text);

// True if `app` (case-insensitive substring) matches the stream's binary or name.
bool matches(const SinkInput& input, const std::string& app);

// PulseAudio volumes are cubic: linear amplitude = (raw / norm)^3.
double raw_to_linear(std::uint32_t raw);
std::uint32_t linear_to_raw(double linear);

}  // namespace suave::pulse
