#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "runner.hpp"
#include "suave/config.hpp"

#ifndef SUAVE_VERSION
#define SUAVE_VERSION "0.1.0"
#endif

namespace {

const char* kUsage = R"(suave - keeps movie audio at a comfortable level

Usage:
  suave run      [options]   Listen and turn the volume down when it gets loud
  suave monitor  [options]   Show the loudness suave hears (changes nothing)
  suave devices              List audio devices for --device
  suave help                 Show this help
  suave version              Show the version

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
  --device <name>        Capture device (part of a name from `suave devices`)

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

void print_usage() { std::printf("%s%s\n", kUsage, suave::default_config_path().c_str()); }

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
        std::printf("suave %s\n", SUAVE_VERSION);
        return 0;
    }
    if (command == "devices") {
        return suave::list_devices();
    }
    if (command != "run" && command != "monitor") {
        std::fprintf(stderr, "suave: unknown command '%s'. Try `suave help`.\n", command.c_str());
        return 2;
    }

    suave::Config config;

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
        config_path = suave::default_config_path();
    }
    if (explicit_config || file_exists(config_path)) {
        const auto errors = suave::load_config_file(config, config_path);
        for (const auto& e : errors) {
            std::fprintf(stderr, "suave: %s\n", e.c_str());
        }
        if (!errors.empty()) {
            return 2;
        }
    }

    // 2. Command line options override the file.
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            std::fprintf(stderr, "suave: unexpected argument '%s'\n", arg.c_str());
            return 2;
        }
        std::string key = arg.substr(2);
        std::string value;
        const auto eq = key.find('=');
        if (eq != std::string::npos) {
            value = key.substr(eq + 1);
            key = key.substr(0, eq);
        } else if (suave::is_flag(key)) {
            value = "true";
        } else {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "suave: option --%s needs a value\n", key.c_str());
                return 2;
            }
            value = args[++i];
        }
        if (key == "config") {
            continue;  // Already handled.
        }
        const std::string error = suave::apply_setting(config, key, value);
        if (!error.empty()) {
            std::fprintf(stderr, "suave: %s\n", error.c_str());
            return 2;
        }
    }

    if (command == "monitor") {
        return suave::run_monitor(config);
    }
    return suave::run_controller(config);
}
