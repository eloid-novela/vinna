#pragma once

#include <optional>
#include <string>
#include <vector>

#include "suave/controller.hpp"

namespace suave {

struct Config {
    ControllerSettings controller;

    // Volume backend: auto | vlc-http | pulse | wasapi | none
    std::string backend = "auto";
    // Application to control (process name / binary, case-insensitive substring).
    std::string app = "vlc";

    // VLC HTTP interface (enable it in VLC: Tools > Preferences > All > Interface >
    // Main interfaces > "Web", and set a password under Main interfaces > Lua).
    std::string vlc_host = "127.0.0.1";
    int vlc_port = 8080;
    std::string vlc_password;

    // Capture device: empty = system loopback / default monitor.
    // Otherwise a case-insensitive substring of the device name (see `suave devices`).
    std::string device;

    // Loudness measurement window.
    double window_ms = 400.0;
    // How often the control loop runs.
    double tick_ms = 50.0;
    // Estimated delay between changing the volume and hearing it in the capture.
    double latency_ms = 120.0;
    // Do not send volume changes smaller than this (dB).
    double min_step_db = 0.5;

    bool dry_run = false;
    bool verbose = false;
};

// Parses durations such as "150", "150ms", "3s", "1.5s", "2m". Returns milliseconds.
std::optional<double> parse_duration_ms(const std::string& text);

// Applies one setting. `key` may use '-' or '_' (e.g. "attack" or "max-cut").
// Returns an error message, or an empty string on success.
std::string apply_setting(Config& config, const std::string& key, const std::string& value);

// Returns true if the key is a boolean flag (accepted without a value on the CLI).
bool is_flag(const std::string& key);

// Loads a simple TOML-like file: `key = value` lines, '#' comments,
// optional [sections] (ignored), quoted or bare values.
// Returns error messages (one per bad line); empty on success.
std::vector<std::string> load_config_file(Config& config, const std::string& path);

// Default config file location for this platform (may not exist).
std::string default_config_path();

// Human-readable dump of the effective configuration.
std::string describe(const Config& config);

}  // namespace suave
