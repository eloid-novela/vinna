#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "miniaudio_config.h"
#include "suave/audio_capture.hpp"

namespace suave {

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

bool looks_like_monitor(const std::string& name) {
    return contains_ci(name, "monitor");
}

#ifdef _WIN32
constexpr bool kHasLoopback = true;  // WASAPI loopback.
#else
constexpr bool kHasLoopback = false;  // Use PulseAudio/PipeWire monitor sources.
#endif

struct DeviceLists {
    std::vector<ma_device_info> playback;
    std::vector<ma_device_info> capture;
};

// RAII wrapper around ma_context.
class Context {
public:
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context() {
        if (ok_) {
            ma_context_uninit(&ctx_);
        }
    }

    bool init(std::string& error) {
        if (ma_context_init(nullptr, 0, nullptr, &ctx_) != MA_SUCCESS) {
            error = "could not initialise the audio system";
            return false;
        }
        ok_ = true;
        return true;
    }

    std::optional<DeviceLists> devices(std::string& error) {
        ma_device_info* playback = nullptr;
        ma_device_info* capture = nullptr;
        ma_uint32 playback_count = 0;
        ma_uint32 capture_count = 0;
        if (ma_context_get_devices(&ctx_, &playback, &playback_count, &capture, &capture_count) !=
            MA_SUCCESS) {
            error = "could not enumerate audio devices";
            return std::nullopt;
        }
        DeviceLists lists;
        lists.playback.assign(playback, playback + playback_count);
        lists.capture.assign(capture, capture + capture_count);
        return lists;
    }

    ma_context* get() { return &ctx_; }
    const char* backend_name() const { return ma_get_backend_name(ctx_.backend); }

private:
    ma_context ctx_{};
    bool ok_ = false;
};

class MiniaudioCapture final : public AudioCapture {
public:
    explicit MiniaudioCapture(std::string filter) : filter_(std::move(filter)) {}

    ~MiniaudioCapture() override { stop(); }

    bool start(std::string& error) override {
        if (!context_.init(error)) {
            return false;
        }
        auto lists = context_.devices(error);
        if (!lists) {
            return false;
        }

        ma_device_type type = ma_device_type_capture;
        std::optional<ma_device_info> chosen;

        if (kHasLoopback) {
            // Windows: record an output device through WASAPI loopback.
            type = ma_device_type_loopback;
            if (!filter_.empty()) {
                for (const auto& d : lists->playback) {
                    if (contains_ci(d.name, filter_)) {
                        chosen = d;
                        break;
                    }
                }
                if (!chosen) {
                    // Allow recording a capture device (e.g. "Stereo Mix") by name too.
                    for (const auto& d : lists->capture) {
                        if (contains_ci(d.name, filter_)) {
                            chosen = d;
                            type = ma_device_type_capture;
                            break;
                        }
                    }
                }
                if (!chosen) {
                    error = "no audio device matches '" + filter_ + "' (see `suave devices`)";
                    return false;
                }
            }
            // No filter: null device id = default output.
        } else {
            if (!filter_.empty()) {
                for (const auto& d : lists->capture) {
                    if (contains_ci(d.name, filter_)) {
                        chosen = d;
                        break;
                    }
                }
                if (!chosen) {
                    error = "no capture device matches '" + filter_ + "' (see `suave devices`)";
                    return false;
                }
            } else {
                chosen = pick_default_monitor(*lists);
                if (!chosen) {
                    error =
                        "no monitor source found. suave needs PulseAudio or PipeWire "
                        "(pipewire-pulse) to listen to what is playing. "
                        "Run `suave devices` and pass one with --device.";
                    return false;
                }
            }
        }

        ma_device_config cfg = ma_device_config_init(type);
        cfg.capture.format = ma_format_f32;
        cfg.capture.channels = 0;  // Native channel count.
        cfg.sampleRate = 0;        // Native sample rate.
        cfg.capture.pDeviceID = chosen ? &chosen->id : nullptr;
        cfg.dataCallback = &MiniaudioCapture::on_data;
        cfg.pUserData = this;
        cfg.periodSizeInMilliseconds = 10;

        if (ma_device_init(context_.get(), &cfg, &device_) != MA_SUCCESS) {
            error = std::string("could not open the audio device (backend: ") +
                    context_.backend_name() + ")";
            return false;
        }
        device_ok_ = true;

        name_ = chosen ? std::string(chosen->name) : std::string("default output (loopback)");
        if (device_.capture.name[0] != '\0') {
            name_ = device_.capture.name;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            filter_state_ = std::make_unique<KWeightingFilter>(device_.sampleRate,
                                                               device_.capture.channels);
        }

        if (ma_device_start(&device_) != MA_SUCCESS) {
            error = "could not start the audio device";
            stop();
            return false;
        }
        return true;
    }

    void stop() override {
        if (device_ok_) {
            ma_device_uninit(&device_);
            device_ok_ = false;
        }
    }

    KWeightingFilter::Block take() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return filter_state_ ? filter_state_->take() : KWeightingFilter::Block{};
    }

    unsigned sample_rate() const override { return device_ok_ ? device_.sampleRate : 0; }
    unsigned channels() const override { return device_ok_ ? device_.capture.channels : 0; }
    std::string device_name() const override { return name_; }

private:
    static void on_data(ma_device* device, void* /*output*/, const void* input,
                        ma_uint32 frames) {
        auto* self = static_cast<MiniaudioCapture*>(device->pUserData);
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->filter_state_) {
            self->filter_state_->process(static_cast<const float*>(input), frames);
        }
    }

    // Prefers "Monitor of <default output>", then any monitor.
    static std::optional<ma_device_info> pick_default_monitor(const DeviceLists& lists) {
        std::string default_output;
        for (const auto& d : lists.playback) {
            if (d.isDefault) {
                default_output = d.name;
                break;
            }
        }
        if (!default_output.empty()) {
            for (const auto& d : lists.capture) {
                if (looks_like_monitor(d.name) && contains_ci(d.name, default_output)) {
                    return d;
                }
            }
        }
        for (const auto& d : lists.capture) {
            if (looks_like_monitor(d.name)) {
                return d;
            }
        }
        return std::nullopt;
    }

    std::string filter_;
    Context context_;
    ma_device device_{};
    bool device_ok_ = false;
    std::string name_;
    std::mutex mutex_;
    std::unique_ptr<KWeightingFilter> filter_state_;
};

}  // namespace

std::unique_ptr<AudioCapture> make_capture(const std::string& device_filter) {
    return std::make_unique<MiniaudioCapture>(device_filter);
}

std::vector<CaptureDeviceInfo> list_capture_devices(std::string& error) {
    std::vector<CaptureDeviceInfo> out;
    Context ctx;
    if (!ctx.init(error)) {
        return out;
    }
    auto lists = ctx.devices(error);
    if (!lists) {
        return out;
    }
    if (kHasLoopback) {
        for (const auto& d : lists->playback) {
            out.push_back({d.name, d.isDefault != 0, true});
        }
    }
    for (const auto& d : lists->capture) {
        out.push_back({d.name, d.isDefault != 0, looks_like_monitor(d.name)});
    }
    return out;
}

}  // namespace suave
