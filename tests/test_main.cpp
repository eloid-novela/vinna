// Small self-contained test runner (no external framework needed).
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "actuators/pactl_parse.hpp"
#include "net/http_client.hpp"
#include "vinna/config.hpp"
#include "vinna/controller.hpp"
#include "vinna/level_meter.hpp"
#include "vinna/restore_state.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                          \
    do {                                                                               \
        const double va_ = (a), vb_ = (b);                                             \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                                        \
            std::printf("    FAILED %s:%d: %s = %f, expected %f (+/- %f)\n", __FILE__, \
                        __LINE__, #a, va_, vb_, static_cast<double>(tol));             \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

constexpr double kPi = 3.14159265358979323846;

// Loudness of a stereo sine measured with the K-weighting filter.
double sine_loudness(double freq, double peak_dbfs, unsigned rate, double seconds) {
    const unsigned channels = 2;
    const double amp = std::pow(10.0, peak_dbfs / 20.0);
    vinna::KWeightingFilter filter(rate, channels);
    vinna::LoudnessWindow window(400.0);

    const std::size_t block = rate / 100;  // 10 ms blocks.
    std::vector<float> buffer(block * channels);
    const auto total = static_cast<std::size_t>(seconds * rate);
    for (std::size_t start = 0; start < total; start += block) {
        for (std::size_t f = 0; f < block; ++f) {
            const auto s = static_cast<float>(amp * std::sin(2.0 * kPi * freq * double(start + f) / rate));
            buffer[f * 2] = s;
            buffer[f * 2 + 1] = s;
        }
        filter.process(buffer.data(), block);
        window.push(filter.take(), rate);
    }
    return window.lufs();
}

void test_k_weighting_reference() {
    // EBU Tech 3341: a stereo 1 kHz sine at -23 dBFS reads -23 LUFS.
    CHECK_NEAR(sine_loudness(1000.0, -23.0, 48000, 2.0), -23.0, 0.1);
    CHECK_NEAR(sine_loudness(1000.0, -23.0, 44100, 2.0), -23.0, 0.1);
    CHECK_NEAR(sine_loudness(1000.0, -33.0, 96000, 2.0), -33.0, 0.1);
}

void test_k_weighting_low_frequencies_count_less() {
    const double at_1k = sine_loudness(1000.0, -20.0, 48000, 2.0);
    const double at_30 = sine_loudness(30.0, -20.0, 48000, 2.0);
    CHECK(at_30 < at_1k - 1.0);
}

void test_silence_is_minus_infinity() {
    vinna::KWeightingFilter filter(48000, 2);
    vinna::LoudnessWindow window;
    std::vector<float> zeros(480 * 2, 0.0f);
    filter.process(zeros.data(), 480);
    window.push(filter.take(), 48000);
    CHECK(!std::isfinite(window.lufs()));
}

void test_controller_target_gain() {
    vinna::ControllerSettings s;
    s.threshold_lufs = -20.0;
    s.ratio = 4.0;
    s.max_cut_db = 20.0;
    vinna::Controller c(s);
    CHECK_NEAR(c.target_gain_db(-25.0), 0.0, 1e-9);
    CHECK_NEAR(c.target_gain_db(-10.0), -7.5, 1e-9);  // 10 dB over, ratio 4 -> cut 7.5
    CHECK_NEAR(c.target_gain_db(+40.0), -20.0, 1e-9);  // Capped by max_cut.
}

void test_controller_attack_is_fast() {
    vinna::ControllerSettings s;
    s.threshold_lufs = -20.0;
    s.ratio = 4.0;
    s.attack_ms = 150.0;
    vinna::Controller c(s);
    double g = 0.0;
    for (int i = 0; i < 10; ++i) {  // 500 ms of loud input.
        g = c.update(-10.0, 50.0);
    }
    CHECK_NEAR(g, -7.5, 0.3);
}

void test_controller_holds_then_releases_slowly() {
    vinna::ControllerSettings s;
    s.threshold_lufs = -20.0;
    s.ratio = 4.0;
    s.attack_ms = 100.0;
    s.hold_ms = 1000.0;
    s.release_ms = 2000.0;
    vinna::Controller c(s);
    for (int i = 0; i < 40; ++i) c.update(-10.0, 50.0);
    const double cut = c.gain_db();
    CHECK(cut < -7.0);

    // Quiet dialogue for 0.8 s: still inside the hold time, nothing changes.
    for (int i = 0; i < 16; ++i) c.update(-30.0, 50.0);
    CHECK_NEAR(c.gain_db(), cut, 1e-9);

    // After the hold, the volume comes back up, but not instantly.
    for (int i = 0; i < 10; ++i) c.update(-30.0, 50.0);
    CHECK(c.gain_db() > cut);
    CHECK(c.gain_db() < -2.0);

    // Eventually it is back at the user's volume.
    for (int i = 0; i < 400; ++i) c.update(-30.0, 50.0);
    CHECK_NEAR(c.gain_db(), 0.0, 1e-9);
}

void test_controller_freezes_in_silence() {
    vinna::ControllerSettings s;
    s.gate_lufs = -55.0;
    s.hold_ms = 0.0;
    vinna::Controller c(s);
    for (int i = 0; i < 40; ++i) c.update(-5.0, 50.0);
    const double cut = c.gain_db();
    for (int i = 0; i < 400; ++i) c.update(-70.0, 50.0);
    CHECK_NEAR(c.gain_db(), cut, 1e-9);
    for (int i = 0; i < 400; ++i) c.update(-std::numeric_limits<double>::infinity(), 50.0);
    CHECK_NEAR(c.gain_db(), cut, 1e-9);
}

void test_controller_never_boosts() {
    vinna::Controller c;
    for (int i = 0; i < 100; ++i) {
        CHECK(c.update(-40.0, 50.0) <= 0.0);
    }
}

void test_durations() {
    CHECK_NEAR(*vinna::parse_duration_ms("150"), 150.0, 1e-9);
    CHECK_NEAR(*vinna::parse_duration_ms("150ms"), 150.0, 1e-9);
    CHECK_NEAR(*vinna::parse_duration_ms("3s"), 3000.0, 1e-9);
    CHECK_NEAR(*vinna::parse_duration_ms("1.5S"), 1500.0, 1e-9);
    CHECK_NEAR(*vinna::parse_duration_ms("2m"), 120000.0, 1e-9);
    CHECK(!vinna::parse_duration_ms("fast"));
    CHECK(!vinna::parse_duration_ms("-1s"));
}

void test_apply_setting() {
    vinna::Config c;
    CHECK(vinna::apply_setting(c, "threshold", "-22").empty());
    CHECK_NEAR(c.controller.threshold_lufs, -22.0, 1e-9);
    CHECK(vinna::apply_setting(c, "max-cut", "12").empty());
    CHECK_NEAR(c.controller.max_cut_db, 12.0, 1e-9);
    CHECK(vinna::apply_setting(c, "release", "\"4s\"").empty());
    CHECK_NEAR(c.controller.release_ms, 4000.0, 1e-9);
    CHECK(vinna::apply_setting(c, "backend", "VLC-HTTP").empty());
    CHECK(c.backend == "vlc-http");
    CHECK(vinna::apply_setting(c, "vlc_port", "9090").empty());
    CHECK(c.vlc_port == 9090);
    CHECK(vinna::apply_setting(c, "dry-run", "true").empty());
    CHECK(c.dry_run);
    CHECK(!vinna::apply_setting(c, "threshold", "loud").empty());
    CHECK(!vinna::apply_setting(c, "ratio", "0.5").empty());
    CHECK(!vinna::apply_setting(c, "backend", "alsa").empty());
    CHECK(!vinna::apply_setting(c, "nonsense", "1").empty());
}

void test_pactl_parsing() {
    const std::string sample = R"PACTL(Sink Input #12
	Driver: protocol-native.c
	Owner Module: 9
	Client: 40
	Sink: 0
	Sample Specification: float32le 2ch 48000Hz
	Channel Map: front-left,front-right
	Format: pcm, format.sample_format = "\"float32le\""  format.rate = "48000"
	Corked: no
	Mute: no
	Volume: front-left: 52429 / 80% / -5.81 dB,   front-right: 52429 / 80% / -5.81 dB
	        balance 0.00
	Properties:
		application.name = "VLC media player (LibVLC 3.0.20)"
		application.process.binary = "vlc"
		media.role = "video"

Sink Input #15
	Driver: protocol-native.c
	Volume: mono: 65536 / 100% / 0.00 dB
	Properties:
		application.name = "Firefox"
		application.process.binary = "firefox"
)PACTL";
    const auto inputs = vinna::pulse::parse_sink_inputs(sample);
    CHECK(inputs.size() == 2);
    if (inputs.size() == 2) {
        CHECK(inputs[0].index == 12);
        CHECK(inputs[0].raw_volume == 52429);
        CHECK(inputs[0].process_binary == "vlc");
        CHECK(vinna::pulse::matches(inputs[0], "VLC"));
        CHECK(!vinna::pulse::matches(inputs[1], "vlc"));
        CHECK(inputs[1].index == 15);
        CHECK(inputs[1].raw_volume == 65536);
    }
}

void test_pulse_volume_curve() {
    CHECK_NEAR(vinna::pulse::raw_to_linear(65536), 1.0, 1e-9);
    CHECK(vinna::pulse::linear_to_raw(1.0) == 65536);
    CHECK(vinna::pulse::linear_to_raw(0.0) == 0);
    CHECK_NEAR(vinna::pulse::raw_to_linear(vinna::pulse::linear_to_raw(0.5)), 0.5, 1e-3);
}

void test_base64() {
    CHECK(vinna::net::base64_encode("") == "");
    CHECK(vinna::net::base64_encode("f") == "Zg==");
    CHECK(vinna::net::base64_encode("fo") == "Zm8=");
    CHECK(vinna::net::base64_encode("foo") == "Zm9v");
    CHECK(vinna::net::base64_encode(":secret") == "OnNlY3JldA==");
}

void test_restore_state() {
    const vinna::RestoreState state{"wasapi (app: vlc)", 0.8, 0.25};
    const auto parsed = vinna::parse_restore_state(vinna::format_restore_state(state));
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->target == "wasapi (app: vlc)");
        CHECK_NEAR(parsed->reference, 0.8, 1e-9);
        CHECK_NEAR(parsed->written, 0.25, 1e-9);
    }
    CHECK(!vinna::parse_restore_state("").has_value());
    CHECK(!vinna::parse_restore_state("target = x\nreference = 1\n").has_value());
    CHECK(!vinna::parse_restore_state("target = x\nreference = abc\nwritten = 1\n").has_value());
    CHECK(!vinna::parse_restore_state("target = x\nreference = -1\nwritten = 1\n").has_value());
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"k-weighting reference level", test_k_weighting_reference},
        {"k-weighting low frequencies", test_k_weighting_low_frequencies_count_less},
        {"silence", test_silence_is_minus_infinity},
        {"controller target gain", test_controller_target_gain},
        {"controller attack", test_controller_attack_is_fast},
        {"controller hold and release", test_controller_holds_then_releases_slowly},
        {"controller silence gate", test_controller_freezes_in_silence},
        {"controller never boosts", test_controller_never_boosts},
        {"durations", test_durations},
        {"settings", test_apply_setting},
        {"pactl parsing", test_pactl_parsing},
        {"pulse volume curve", test_pulse_volume_curve},
        {"base64", test_base64},
        {"restore state", test_restore_state},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::printf("%s %s\n", g_failures == before ? "[ ok ]" : "[FAIL]", name);
    }
    if (g_failures) {
        std::printf("\n%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("\nall tests passed\n");
    return 0;
}
