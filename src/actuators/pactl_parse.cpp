#include "pactl_parse.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace suave::pulse {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

// `key = "value"` -> value
std::string property_value(const std::string& line) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
        return {};
    }
    std::string v = trim(line.substr(eq + 1));
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        v = v.substr(1, v.size() - 2);
    }
    return v;
}

}  // namespace

std::vector<SinkInput> parse_sink_inputs(const std::string& text) {
    std::vector<SinkInput> inputs;
    std::istringstream in(text);
    std::string raw_line;
    SinkInput* current = nullptr;

    while (std::getline(in, raw_line)) {
        const std::string line = trim(raw_line);
        if (starts_with(line, "Sink Input #")) {
            inputs.push_back({});
            current = &inputs.back();
            current->index = static_cast<std::uint32_t>(std::strtoul(line.c_str() + 12, nullptr, 10));
            continue;
        }
        if (!current) {
            continue;
        }
        if (starts_with(line, "Volume:")) {
            // "Volume: front-left: 65536 / 100% / 0.00 dB,   front-right: ..."
            // or "Volume: mono: 65536 / 100% / 0.00 dB"
            const auto colon = line.find(':', 7);
            if (colon != std::string::npos) {
                current->raw_volume =
                    static_cast<std::uint32_t>(std::strtoul(line.c_str() + colon + 1, nullptr, 10));
            }
        } else if (starts_with(line, "application.process.binary")) {
            current->process_binary = property_value(line);
        } else if (starts_with(line, "application.name")) {
            current->application_name = property_value(line);
        }
    }
    return inputs;
}

bool matches(const SinkInput& input, const std::string& app) {
    if (app.empty()) {
        return false;
    }
    const std::string needle = lower(app);
    return lower(input.process_binary).find(needle) != std::string::npos ||
           lower(input.application_name).find(needle) != std::string::npos;
}

double raw_to_linear(std::uint32_t raw) {
    const double v = static_cast<double>(raw) / kVolumeNorm;
    return v * v * v;
}

std::uint32_t linear_to_raw(double linear) {
    if (linear <= 0.0) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::lround(std::cbrt(linear) * kVolumeNorm));
}

}  // namespace suave::pulse
