#include "vinna/restore_state.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "vinna/config.hpp"

namespace vinna {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::optional<double> parse_volume(const std::string& text) {
    const char* start = text.c_str();
    char* end = nullptr;
    const double value = std::strtod(start, &end);
    if (end == start || *end != '\0' || !std::isfinite(value) || value < 0.0) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::string default_state_path() {
    const std::filesystem::path config(default_config_path());
    return (config.parent_path() / "restore.state").string();
}

std::string format_restore_state(const RestoreState& state) {
    std::ostringstream out;
    out.precision(6);
    out << "# Written by vinna while it has the volume lowered. Safe to delete.\n"
        << "target = " << state.target << "\n"
        << "reference = " << state.reference << "\n"
        << "written = " << state.written << "\n";
    return out.str();
}

std::optional<RestoreState> parse_restore_state(const std::string& text) {
    RestoreState state;
    bool has_target = false, has_reference = false, has_written = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return std::nullopt;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "target") {
            state.target = value;
            has_target = !value.empty();
        } else if (key == "reference" || key == "written") {
            const auto volume = parse_volume(value);
            if (!volume) {
                return std::nullopt;
            }
            (key == "reference" ? state.reference : state.written) = *volume;
            (key == "reference" ? has_reference : has_written) = true;
        }
    }
    if (!has_target || !has_reference || !has_written) {
        return std::nullopt;
    }
    return state;
}

bool save_restore_state(const std::string& path, const RestoreState& state) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    // Write to a temporary file and rename, so a crash mid-write never leaves
    // a half-written state behind.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            return false;
        }
        out << format_restore_state(state);
        if (!out.flush()) {
            return false;
        }
    }
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}

std::optional<RestoreState> load_restore_state(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse_restore_state(text.str());
}

void clear_restore_state(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // namespace vinna
