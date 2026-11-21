#include "runner.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>

#include "suave/audio_capture.hpp"
#include "suave/controller.hpp"
#include "suave/level_meter.hpp"
#include "suave/volume_actuator.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace suave {

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true); }

#ifdef _WIN32
BOOL WINAPI on_console_event(DWORD) {
    g_stop.store(true);
    // Give the main loop time to restore the volume before Windows kills us.
    Sleep(1500);
    return TRUE;
}
#endif

void install_signal_handlers() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef _WIN32
    SetConsoleCtrlHandler(on_console_event, TRUE);
#endif
}

double db_to_linear(double db) { return std::pow(10.0, db / 20.0); }

double ms_since(Clock::time_point start, Clock::time_point now) {
    return std::chrono::duration<double, std::milli>(now - start).count();
}

std::string format_lufs(double lufs) {
    if (!std::isfinite(lufs)) {
        return "  -inf";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%6.1f", lufs);
    return buf;
}

// Remembers which gain was applied when, so that the measured loudness can be
// corrected for suave's own volume changes (the capture hears the result).
class GainHistory {
public:
    void push(Clock::time_point t, double linear_gain) {
        entries_.push_back({t, linear_gain});
        while (entries_.size() > 2 && entries_[1].first < t - std::chrono::seconds(5)) {
            entries_.pop_front();
        }
    }

    double at(Clock::time_point t) const {
        double value = 1.0;
        for (const auto& [time, gain] : entries_) {
            if (time > t) {
                break;
            }
            value = gain;
        }
        return value;
    }

private:
    std::deque<std::pair<Clock::time_point, double>> entries_;
};

// Draws a simple text meter from -60 to 0 LUFS with a mark at the threshold.
std::string meter_bar(double lufs, double threshold) {
    constexpr int kWidth = 40;
    constexpr double kMin = -60.0;
    const auto to_col = [](double v) {
        const double clamped = std::fmax(kMin, std::fmin(0.0, v));
        return static_cast<int>((clamped - kMin) / -kMin * kWidth);
    };
    const int fill = std::isfinite(lufs) ? to_col(lufs) : 0;
    const int mark = to_col(threshold);
    std::string bar(kWidth, ' ');
    for (int i = 0; i < fill && i < kWidth; ++i) {
        bar[static_cast<std::size_t>(i)] = i >= mark ? '#' : '=';
    }
    if (mark >= 0 && mark < kWidth && bar[static_cast<std::size_t>(mark)] == ' ') {
        bar[static_cast<std::size_t>(mark)] = '|';
    }
    return "[" + bar + "]";
}

std::unique_ptr<AudioCapture> open_capture(const Config& config) {
    auto capture = make_capture(config.device);
    std::string error;
    if (!capture->start(error)) {
        std::fprintf(stderr, "suave: %s\n", error.c_str());
        return nullptr;
    }
    std::printf("Listening to: %s (%u Hz, %u ch)\n", capture->device_name().c_str(),
                capture->sample_rate(), capture->channels());
    return capture;
}

}  // namespace

int list_devices() {
    std::string error;
    const auto devices = list_capture_devices(error);
    if (!error.empty()) {
        std::fprintf(stderr, "suave: %s\n", error.c_str());
        return 1;
    }
    if (devices.empty()) {
        std::printf("No audio devices found.\n");
        return 0;
    }
    std::printf("Devices usable with --device (* = default, L = records what is playing):\n\n");
    for (const auto& d : devices) {
        std::printf("  %c %c  %s\n", d.is_default ? '*' : ' ', d.is_loopback ? 'L' : ' ',
                    d.name.c_str());
    }
    std::printf("\nPass any part of a name, e.g. --device \"Monitor of\".\n");
    return 0;
}

int run_monitor(const Config& config) {
    install_signal_handlers();
    auto capture = open_capture(config);
    if (!capture) {
        return 1;
    }
    std::printf("Threshold %.1f LUFS. Press Ctrl+C to stop.\n\n", config.controller.threshold_lufs);

    Controller controller(config.controller);
    LoudnessWindow window(config.window_ms);
    double peak = -INFINITY;
    auto peak_reset = Clock::now();

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        window.push(capture->take(), capture->sample_rate());
        const double lufs = window.lufs();

        const auto now = Clock::now();
        if (ms_since(peak_reset, now) > 10000.0) {
            peak = -INFINITY;
            peak_reset = now;
        }
        if (std::isfinite(lufs) && lufs > peak) {
            peak = lufs;
        }

        const double cut = controller.target_gain_db(lufs);
        std::printf("\r%s LUFS %s  max(10s) %s  would cut %5.1f dB ", format_lufs(lufs).c_str(),
                    meter_bar(lufs, config.controller.threshold_lufs).c_str(),
                    format_lufs(peak).c_str(), -cut);
        std::fflush(stdout);
    }
    std::printf("\n");
    capture->stop();
    return 0;
}

int run_controller(const Config& config) {
    install_signal_handlers();

    std::string error;
    auto actuator = config.dry_run ? make_null_actuator() : make_actuator(config, error);
    if (!actuator) {
        std::fprintf(stderr, "suave: %s\n", error.c_str());
        return 2;
    }

    auto capture = open_capture(config);
    if (!capture) {
        return 1;
    }
    std::printf("Controlling: %s\n%s\nPress Ctrl+C to stop (the volume is restored).\n\n",
                actuator->name().c_str(), describe(config).c_str());

    Controller controller(config.controller);
    LoudnessWindow window(config.window_ms);
    GainHistory history;

    bool connected = false;
    bool have_reference = false;
    double reference = 1.0;      // The volume the user chose (gain 0 dB).
    double last_sent_db = 0.0;    // Gain we last applied.
    double last_written = 1.0;    // Volume value we last wrote.
    const auto started = Clock::now();
    auto last_tick = started;
    auto last_write = Clock::time_point{};
    auto last_check = Clock::time_point{};
    auto last_connect_try = Clock::time_point{};
    std::string last_connect_error;

    const auto tick = std::chrono::duration<double, std::milli>(config.tick_ms);
    const auto latency = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double, std::milli>(config.latency_ms));

    // Prints a message on its own line, above the live status line.
    auto note = [&](const char* fmt, auto... args) {
        char message[512];
        if constexpr (sizeof...(args) == 0) {
            std::snprintf(message, sizeof(message), "%s", fmt);
        } else {
            std::snprintf(message, sizeof(message), fmt, args...);
        }
        std::printf("\r%-110s\r%s\n", "", message);
    };

    auto try_connect = [&](Clock::time_point now) {
        last_connect_try = now;
        std::string err;
        if (!actuator->connect(err)) {
            if (err != last_connect_error) {
                note("Waiting: %s", err.c_str());
                last_connect_error = err;
            }
            return;
        }
        const auto volume = actuator->read();
        if (!volume) {
            return;
        }
        connected = true;
        last_connect_error.clear();

        // The mixer may remember the volume we had lowered when the old stream
        // ended (PulseAudio's stream-restore does). If the new stream comes back
        // with exactly that value, keep the user's reference instead of the cut.
        const double tolerance = std::fmax(0.03 * last_written, 2.0 * actuator->resolution());
        if (have_reference && last_sent_db != 0.0 &&
            std::fabs(*volume - last_written) <= tolerance) {
            last_written = *volume;
            history.push(now, db_to_linear(last_sent_db));
            note("Reconnected. Keeping reference volume %.0f%%", reference * 100.0);
            return;
        }

        have_reference = true;
        reference = *volume;
        last_written = *volume;
        last_sent_db = 0.0;
        history.push(now, 1.0);
        note("Connected. Reference volume: %.0f%%", reference * 100.0);
    };

    try_connect(Clock::now());

    while (!g_stop.load()) {
        std::this_thread::sleep_for(tick);
        const auto now = Clock::now();
        const double dt = ms_since(last_tick, now);
        last_tick = now;

        // 1. Measure, undoing our own volume change.
        auto block = capture->take();
        if (!config.dry_run) {
            const double g = history.at(now - latency);
            if (g > 1e-4) {
                block.power_sum /= g * g;
            }
        }
        window.push(block, capture->sample_rate());
        const double source = window.lufs();

        // 2. Decide.
        const double gain_db = controller.update(source, dt);

        // 3. Apply.
        if (!connected) {
            if (ms_since(last_connect_try, now) > 2000.0) {
                try_connect(now);
            }
        } else {
            // Did the user move the volume themselves? Then that is the new reference.
            if (ms_since(last_check, now) > 500.0 && ms_since(last_write, now) > 400.0) {
                last_check = now;
                const auto current = actuator->read();
                if (!current) {
                    connected = false;
                    note("Lost the target; will reconnect.");
                } else {
                    const double tolerance =
                        std::fmax(0.03 * last_written, 2.0 * actuator->resolution());
                    if (std::fabs(*current - last_written) > tolerance) {
                        reference = *current;
                        last_written = *current;
                        last_sent_db = 0.0;
                        controller.reset();
                        history.push(now, 1.0);
                        note("Volume changed by you: new reference %.0f%%", reference * 100.0);
                    }
                }
            }

            const bool big_enough = std::fabs(gain_db - last_sent_db) >= config.min_step_db;
            const bool back_to_normal = gain_db == 0.0 && last_sent_db != 0.0;
            if (connected && (big_enough || back_to_normal)) {
                const double value = reference * db_to_linear(gain_db);
                if (actuator->write(value)) {
                    last_sent_db = gain_db;
                    last_written = value;
                    last_write = now;
                    history.push(now, db_to_linear(gain_db));
                    if (config.verbose) {
                        note("[%7.2fs] source %s LUFS -> gain %5.1f dB -> volume %3.0f%%",
                             ms_since(started, now) / 1000.0, format_lufs(source).c_str(),
                             gain_db, value * 100.0);
                    }
                } else {
                    connected = false;
                    note("Could not set the volume; will reconnect.");
                }
            }
        }

        std::printf("\r%s LUFS %s  gain %5.1f dB  volume %3.0f%% %s",
                    format_lufs(source).c_str(),
                    meter_bar(source, config.controller.threshold_lufs).c_str(), last_sent_db,
                    last_written * 100.0, connected ? "" : "(waiting)");
        std::fflush(stdout);
    }

    std::printf("\n");
    if (connected && last_sent_db != 0.0) {
        if (actuator->write(reference)) {
            std::printf("Volume restored to %.0f%%.\n", reference * 100.0);
        }
    }
    capture->stop();
    return 0;
}

}  // namespace suave
