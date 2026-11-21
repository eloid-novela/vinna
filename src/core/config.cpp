#include "suave/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace suave {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string normalize_key(std::string key) {
    while (!key.empty() && key.front() == '-') {
        key.erase(key.begin());
    }
    for (char& c : key) {
        c = c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return key;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::optional<double> parse_number(const std::string& text) {
    const std::string t = trim(text);
    if (t.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0') {
        return std::nullopt;
    }
    return v;
}

std::optional<bool> parse_bool(const std::string& text) {
    const std::string t = lower(trim(text));
    if (t.empty() || t == "true" || t == "1" || t == "yes" || t == "on") {
        return true;
    }
    if (t == "false" || t == "0" || t == "no" || t == "off") {
        return false;
    }
    return std::nullopt;
}

std::string unquote(const std::string& text) {
    std::string t = trim(text);
    if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') ||
                          (t.front() == '\'' && t.back() == '\''))) {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

// Strips a trailing "# comment" that is not inside quotes.
std::string strip_comment(const std::string& line) {
    bool in_quotes = false;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if ((c == '"' || c == '\'') && (!in_quotes || c == quote)) {
            in_quotes = !in_quotes;
            quote = c;
        } else if (c == '#' && !in_quotes) {
            return line.substr(0, i);
        }
    }
    return line;
}

}  // namespace

std::optional<double> parse_duration_ms(const std::string& text) {
    std::string t = lower(trim(text));
    double scale = 1.0;
    auto ends_with = [&](const std::string& suffix) {
        return t.size() > suffix.size() &&
               t.compare(t.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with("ms")) {
        t.resize(t.size() - 2);
    } else if (ends_with("s")) {
        t.resize(t.size() - 1);
        scale = 1000.0;
    } else if (ends_with("m")) {
        t.resize(t.size() - 1);
        scale = 60000.0;
    }
    const auto v = parse_number(t);
    if (!v || *v < 0.0) {
        return std::nullopt;
    }
    return *v * scale;
}

bool is_flag(const std::string& key) {
    const std::string k = normalize_key(key);
    return k == "dry_run" || k == "verbose";
}

std::string apply_setting(Config& config, const std::string& raw_key, const std::string& raw_value) {
    const std::string key = normalize_key(raw_key);
    const std::string value = unquote(raw_value);

    auto number = [&](double& out, double min, double max) -> std::string {
        const auto v = parse_number(value);
        if (!v) {
            return "'" + raw_key + "' expects a number, got '" + value + "'";
        }
        if (*v < min || *v > max) {
            std::ostringstream msg;
            msg << "'" << raw_key << "' must be between " << min << " and " << max;
            return msg.str();
        }
        out = *v;
        return {};
    };
    auto duration = [&](double& out, double min, double max) -> std::string {
        const auto v = parse_duration_ms(value);
        if (!v) {
            return "'" + raw_key + "' expects a duration like 150ms or 3s, got '" + value + "'";
        }
        if (*v < min || *v > max) {
            std::ostringstream msg;
            msg << "'" << raw_key << "' must be between " << min << "ms and " << max << "ms";
            return msg.str();
        }
        out = *v;
        return {};
    };
    auto boolean = [&](bool& out) -> std::string {
        const auto v = parse_bool(value);
        if (!v) {
            return "'" + raw_key + "' expects true or false";
        }
        out = *v;
        return {};
    };

    auto& c = config.controller;
    if (key == "threshold" || key == "threshold_lufs") return number(c.threshold_lufs, -70, 0);
    if (key == "ratio") return number(c.ratio, 1, 100);
    if (key == "max_cut" || key == "max_cut_db") return number(c.max_cut_db, 0, 60);
    if (key == "attack") return duration(c.attack_ms, 1, 10000);
    if (key == "release") return duration(c.release_ms, 10, 120000);
    if (key == "hold") return duration(c.hold_ms, 0, 60000);
    if (key == "gate" || key == "gate_lufs") return number(c.gate_lufs, -100, 0);

    if (key == "backend") {
        const std::string b = lower(value);
        if (b != "auto" && b != "vlc-http" && b != "vlc_http" && b != "pulse" && b != "wasapi" &&
            b != "none") {
            return "unknown backend '" + value + "' (auto, vlc-http, pulse, wasapi, none)";
        }
        config.backend = b == "vlc_http" ? "vlc-http" : b;
        return {};
    }
    if (key == "app") {
        config.app = value;
        return {};
    }
    if (key == "vlc_host") {
        config.vlc_host = value;
        return {};
    }
    if (key == "vlc_port") {
        double port = 0;
        auto err = number(port, 1, 65535);
        if (err.empty()) config.vlc_port = static_cast<int>(port);
        return err;
    }
    if (key == "vlc_password") {
        config.vlc_password = value;
        return {};
    }
    if (key == "device") {
        config.device = value;
        return {};
    }
    if (key == "window") return duration(config.window_ms, 50, 3000);
    if (key == "tick") return duration(config.tick_ms, 10, 1000);
    if (key == "latency") return duration(config.latency_ms, 0, 2000);
    if (key == "min_step" || key == "min_step_db") return number(config.min_step_db, 0, 6);
    if (key == "dry_run") return boolean(config.dry_run);
    if (key == "verbose") return boolean(config.verbose);

    return "unknown option '" + raw_key + "'";
}

std::vector<std::string> load_config_file(Config& config, const std::string& path) {
    std::vector<std::string> errors;
    std::ifstream in(path);
    if (!in) {
        errors.push_back("cannot open config file '" + path + "'");
        return errors;
    }
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::string content = trim(strip_comment(line));
        if (content.empty() || content.front() == '[') {
            continue;
        }
        const auto eq = content.find('=');
        if (eq == std::string::npos) {
            errors.push_back(path + ":" + std::to_string(number) + ": expected 'key = value'");
            continue;
        }
        const std::string err =
            apply_setting(config, trim(content.substr(0, eq)), content.substr(eq + 1));
        if (!err.empty()) {
            errors.push_back(path + ":" + std::to_string(number) + ": " + err);
        }
    }
    return errors;
}

std::string default_config_path() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::string(appdata) + "\\suave\\suave.toml";
    }
    return "suave.toml";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::string(xdg) + "/suave/suave.toml";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::string(home) + "/.config/suave/suave.toml";
    }
    return "suave.toml";
#endif
}

std::string describe(const Config& config) {
    const auto& c = config.controller;
    std::ostringstream out;
    out << "  backend      " << config.backend << " (app: " << config.app << ")\n"
        << "  device       " << (config.device.empty() ? "<system output>" : config.device) << "\n"
        << "  threshold    " << c.threshold_lufs << " LUFS, ratio " << c.ratio << ":1, max cut "
        << c.max_cut_db << " dB\n"
        << "  attack       " << c.attack_ms << " ms, hold " << c.hold_ms << " ms, release "
        << c.release_ms << " ms\n"
        << "  gate         " << c.gate_lufs << " LUFS\n"
        << "  window       " << config.window_ms << " ms, tick " << config.tick_ms
        << " ms, latency " << config.latency_ms << " ms\n";
    if (config.dry_run) {
        out << "  dry run      volume will NOT be changed\n";
    }
    return out.str();
}

}  // namespace suave
