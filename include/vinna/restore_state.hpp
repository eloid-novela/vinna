#pragma once

#include <optional>
#include <string>

namespace vinna {

// What vinna needs to undo its own volume change if it is stopped abruptly
// (window closed, process killed, crash, log-off). Saved to disk while the
// volume is lowered and deleted once the user's volume is back.
struct RestoreState {
    std::string target;     // Actuator name, e.g. "wasapi (app: vlc)".
    double reference = 1.0;  // Volume the user had chosen.
    double written = 1.0;    // Lowered volume vinna last wrote.
};

// Where the state file lives (next to the default config file).
std::string default_state_path();

std::string format_restore_state(const RestoreState& state);
std::optional<RestoreState> parse_restore_state(const std::string& text);

// File helpers. Saving creates the directory if needed. All return false on error.
bool save_restore_state(const std::string& path, const RestoreState& state);
std::optional<RestoreState> load_restore_state(const std::string& path);
void clear_restore_state(const std::string& path);

}  // namespace vinna
