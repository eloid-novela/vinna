// Controls the per-application volume on Windows (the sliders in the Volume
// Mixer) through the Core Audio session API.
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "vinna/volume_actuator.hpp"

namespace vinna {

namespace {

// Tiny COM smart pointer (avoids depending on WRL / ATL).
template <typename T>
class Com {
public:
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    Com& operator=(Com&& other) noexcept {
        if (this != &other) {
            reset();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }
    ~Com() { reset(); }

    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T** put() {
        reset();
        return &p_;
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

std::string to_utf8(const wchar_t* text) {
    if (!text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// File name (e.g. "vlc.exe") of a process, or empty if it cannot be queried.
std::string process_name(DWORD pid) {
    if (pid == 0) {
        return {};
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    std::string name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        name = to_utf8(path);
        const auto slash = name.find_last_of("\\/");
        if (slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
    }
    CloseHandle(process);
    return name;
}

class WasapiActuator final : public VolumeActuator {
public:
    explicit WasapiActuator(const Config& config) : app_(config.app) {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_initialised_ = SUCCEEDED(hr);
    }

    ~WasapiActuator() override {
        volumes_.clear();
        if (com_initialised_) {
            CoUninitialize();
        }
    }

    std::string name() const override { return "wasapi (app: " + app_ + ")"; }

    bool connect(std::string& error) override {
        volumes_.clear();

        Com<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(enumerator.put())))) {
            error = "could not create the Windows audio device enumerator";
            return false;
        }
        Com<IMMDevice> device;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()))) {
            error = "no default audio output device";
            return false;
        }
        Com<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(manager.put())))) {
            error = "could not open the audio session manager";
            return false;
        }
        Com<IAudioSessionEnumerator> sessions;
        if (FAILED(manager->GetSessionEnumerator(sessions.put()))) {
            error = "could not list audio sessions";
            return false;
        }
        int count = 0;
        sessions->GetCount(&count);

        const std::string needle = lower(app_);
        for (int i = 0; i < count; ++i) {
            Com<IAudioSessionControl> control;
            if (FAILED(sessions->GetSession(i, control.put()))) {
                continue;
            }
            Com<IAudioSessionControl2> control2;
            if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                               reinterpret_cast<void**>(control2.put())))) {
                continue;
            }
            DWORD pid = 0;
            control2->GetProcessId(&pid);
            const std::string exe = lower(process_name(pid));
            if (exe.empty() || exe.find(needle) == std::string::npos) {
                continue;
            }
            Com<ISimpleAudioVolume> volume;
            if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                  reinterpret_cast<void**>(volume.put())))) {
                volumes_.push_back(std::move(volume));
            }
        }

        if (volumes_.empty()) {
            error = "no audio session from '" + app_ + "' found (is it playing?)";
            return false;
        }
        return true;
    }

    std::optional<double> read() override {
        if (volumes_.empty()) {
            std::string error;
            if (!connect(error)) {
                return std::nullopt;
            }
        }
        float level = 0.0f;
        if (FAILED(volumes_.front()->GetMasterVolume(&level))) {
            volumes_.clear();
            return std::nullopt;
        }
        return static_cast<double>(level);
    }

    bool write(double linear) override {
        if (volumes_.empty()) {
            return false;
        }
        const float level = static_cast<float>(std::clamp(linear, 0.0, 1.0));
        bool ok = true;
        for (auto& volume : volumes_) {
            ok = SUCCEEDED(volume->SetMasterVolume(level, nullptr)) && ok;
        }
        if (!ok) {
            volumes_.clear();
        }
        return ok;
    }

    double resolution() const override { return 0.002; }

private:
    std::string app_;
    bool com_initialised_ = false;
    std::vector<Com<ISimpleAudioVolume>> volumes_;
};

}  // namespace

std::unique_ptr<VolumeActuator> make_wasapi_actuator(const Config& config) {
    return std::make_unique<WasapiActuator>(config);
}

}  // namespace vinna

#endif  // _WIN32
