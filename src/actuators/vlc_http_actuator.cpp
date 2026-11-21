// Controls VLC through its built-in HTTP ("Web") interface.
//
// VLC exposes /requests/status.json; its "volume" field goes from 0 to 512,
// where 256 is 100 %. Setting it: /requests/status.json?command=volume&val=N
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "net/http_client.hpp"
#include "suave/volume_actuator.hpp"

namespace suave {

namespace {

constexpr double kVlcUnity = 256.0;
constexpr int kVlcMax = 512;
constexpr int kTimeoutMs = 800;

class VlcHttpActuator final : public VolumeActuator {
public:
    explicit VlcHttpActuator(const Config& config)
        : host_(config.vlc_host),
          port_(config.vlc_port),
          auth_("Basic " + net::base64_encode(":" + config.vlc_password)) {}

    std::string name() const override {
        return "vlc-http (" + host_ + ":" + std::to_string(port_) + ")";
    }

    bool connect(std::string& error) override {
        const auto volume = query("/requests/status.json", error);
        return volume.has_value();
    }

    std::optional<double> read() override {
        std::string error;
        const auto volume = query("/requests/status.json", error);
        if (!volume) {
            return std::nullopt;
        }
        return *volume / kVlcUnity;
    }

    bool write(double linear) override {
        const int value =
            std::clamp(static_cast<int>(std::lround(linear * kVlcUnity)), 0, kVlcMax);
        std::string error;
        return query("/requests/status.json?command=volume&val=" + std::to_string(value), error)
            .has_value();
    }

    double resolution() const override { return 1.0 / kVlcUnity; }

private:
    // Performs a request and returns VLC's raw volume (0..512) from the reply.
    std::optional<double> query(const std::string& path, std::string& error) {
        const auto response = net::http_get(host_, port_, path, auth_, kTimeoutMs, error);
        if (!response) {
            error = "VLC web interface not reachable at " + host_ + ":" + std::to_string(port_) +
                    " (" + error + "). Is VLC running with the Web interface enabled?";
            return std::nullopt;
        }
        if (response->status == 401 || response->status == 403) {
            error = "VLC rejected the password (HTTP " + std::to_string(response->status) +
                    "). Check --vlc-password.";
            return std::nullopt;
        }
        if (response->status != 200) {
            error = "VLC answered HTTP " + std::to_string(response->status);
            return std::nullopt;
        }
        const std::string key = "\"volume\":";
        const auto pos = response->body.find(key);
        if (pos == std::string::npos) {
            error = "VLC status has no volume field";
            return std::nullopt;
        }
        const char* start = response->body.c_str() + pos + key.size();
        char* end = nullptr;
        const double volume = std::strtod(start, &end);
        if (end == start) {
            error = "could not parse VLC volume";
            return std::nullopt;
        }
        return volume;
    }

    std::string host_;
    int port_;
    std::string auth_;
};

}  // namespace

std::unique_ptr<VolumeActuator> make_vlc_http_actuator(const Config& config) {
    return std::make_unique<VlcHttpActuator>(config);
}

}  // namespace suave
