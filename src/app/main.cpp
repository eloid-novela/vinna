#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "runner.hpp"
#include "vinna/config.hpp"

#ifndef VINNA_VERSION
#define VINNA_VERSION "0.1.0"
#endif

namespace {

const char* kUsage = R"(vinna - keeps movie audio at a comfortable level

Usage:
  vinna run      [options]   Listen and turn the volume down when it gets loud
  vinna monitor  [options]   Show the loudness vinna hears (changes nothing)
  vinna restore  [options]   Put back the volume if vinna was killed while it was lowered
  vinna devices              List audio devices for --device
  vinna help                 Show this help
  vinna version              Show the version

Detection:
  --threshold <LUFS>     Start reducing above this loudness        (default -20)
  --ratio <n>            Strength of the reduction, 1 = none       (default 4)
  --max-cut <dB>         Never go lower than this below your volume (default 20)
  --attack <time>        How fast to turn down                     (default 150ms)
  --hold <time>          Wait before turning back up               (default 1s)
  --release <time>       How slowly to turn back up                (default 3s)
  --gate <LUFS>          Below this it is silence: keep the volume (default -55)

Target:
  --backend <name>       auto | pulse | wasapi | vlc-http | none   (default auto)
  --app <name>           Application to control                    (default vlc)
  --vlc-host <host>      VLC web interface host                    (default 127.0.0.1)
  --vlc-port <port>      VLC web interface port                    (default 8080)
  --vlc-password <pw>    VLC web interface password (enables vlc-http in auto)
  --device <name>        Capture device (part of a name from `vinna devices`)

Advanced:
  --window <time>        Loudness measurement window               (default 400ms)
  --tick <time>          Control loop interval                     (default 50ms)
  --latency <time>       Delay before a volume change is heard     (default 120ms)
  --min-step <dB>        Smallest volume change to send            (default 0.5)
  --config <file>        Read options from a file (key = value)
  --dry-run              Compute everything but never touch the volume
  --verbose              Print every volume change

Times accept ms, s or m (150ms, 3s). Options in the config file use the same
names without dashes, e.g. `threshold = -22` or `release = "4s"`.
Default config file: )";

void print_usage() { std::printf("%s%s\n", kUsage, vinna::default_config_path().c_str()); }

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage();
        return 0;
    }

    const std::string command = args.front();
    args.erase(args.begin());

    if (command == "help" || command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }
    if (command == "version" || command == "--version" || command == "-V") {
        std::printf("vinna %s\n", VINNA_VERSION);
        return 0;
    }
    if (command == "devices") {
        return vinna::list_devices();
    }
    if (command != "run" && command != "monitor" && command != "restore") {
        std::fprintf(stderr, "vinna: unknown command '%s'. Try `vinna help`.\n", command.c_str());
        return 2;
    }

    vinna::Config config;

    // 1. Config file: --config <file>, or the default one if it exists.
    std::string config_path;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--config" && i + 1 < args.size()) {
            config_path = args[i + 1];
        } else if (args[i].rfind("--config=", 0) == 0) {
            config_path = args[i].substr(9);
        }
    }
    const bool explicit_config = !config_path.empty();
    if (!explicit_config) {
        config_path = vinna::default_config_path();
    }
    if (explicit_config || file_exists(config_path)) {
        const auto errors = vinna::load_config_file(config, config_path);
        for (const auto& e : errors) {
            std::fprintf(stderr, "vinna: %s\n", e.c_str());
        }
        if (!errors.empty()) {
            return 2;
        }
    }

    // 2. Command line options override the file.
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            std::fprintf(stderr, "vinna: unexpected argument '%s'\n", arg.c_str());
            return 2;
        }
        std::string key = arg.substr(2);
        std::string value;
        const auto eq = key.find('=');
        if (eq != std::string::npos) {
            value = key.substr(eq + 1);
            key = key.substr(0, eq);
        } else if (vinna::is_flag(key)) {
            value = "true";
        } else {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "vinna: option --%s needs a value\n", key.c_str());
                return 2;
            }
            value = args[++i];
        }
        if (key == "config") {
            continue;  // Already handled.
        }
        const std::string error = vinna::apply_setting(config, key, value);
        if (!error.empty()) {
            std::fprintf(stderr, "vinna: %s\n", error.c_str());
            return 2;
        }
    }

    if (command == "monitor") {
        return vinna::run_monitor(config);
    }
    if (command == "restore") {
        return vinna::restore_saved_volume(config);
    }
    return vinna::run_controller(config);
}
