#pragma once

#include <optional>
#include <string>

namespace suave::net {

struct HttpResponse {
    int status = 0;
    std::string body;
};

// Minimal blocking HTTP/1.0 GET, enough to talk to VLC's local web interface.
// `authorization` is the full header value (e.g. "Basic ...") or empty.
std::optional<HttpResponse> http_get(const std::string& host, int port, const std::string& path,
                                     const std::string& authorization, int timeout_ms,
                                     std::string& error);

std::string base64_encode(const std::string& input);

}  // namespace suave::net
