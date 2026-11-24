#include "http_client.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace vinna::net {

namespace {

#ifdef _WIN32
struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockInit() {
        if (ok) WSACleanup();
    }
};

bool ensure_winsock() {
    static WinsockInit init;
    return init.ok;
}

void close_socket(socket_t s) { closesocket(s); }

void set_timeouts(socket_t s, int timeout_ms) {
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}
#else
bool ensure_winsock() { return true; }

void close_socket(socket_t s) { ::close(s); }

void set_timeouts(socket_t s, int timeout_ms) {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
#endif

bool send_all(socket_t s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

std::string base64_encode(const std::string& input) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < input.size()) {
        const unsigned v = (static_cast<unsigned char>(input[i]) << 16) |
                           (static_cast<unsigned char>(input[i + 1]) << 8) |
                           static_cast<unsigned char>(input[i + 2]);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += table[v & 63];
        i += 3;
    }
    const std::size_t rest = input.size() - i;
    if (rest == 1) {
        const unsigned v = static_cast<unsigned char>(input[i]) << 16;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += "==";
    } else if (rest == 2) {
        const unsigned v = (static_cast<unsigned char>(input[i]) << 16) |
                           (static_cast<unsigned char>(input[i + 1]) << 8);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::optional<HttpResponse> http_get(const std::string& host, int port, const std::string& path,
                                     const std::string& authorization, int timeout_ms,
                                     std::string& error) {
    if (!ensure_winsock()) {
        error = "network initialisation failed";
        return std::nullopt;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &results) != 0 || !results) {
        error = "cannot resolve host '" + host + "'";
        return std::nullopt;
    }

    socket_t s = kInvalidSocket;
    for (addrinfo* ai = results; ai; ai = ai->ai_next) {
        s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == kInvalidSocket) {
            continue;
        }
        set_timeouts(s, timeout_ms);
        if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
            break;
        }
        close_socket(s);
        s = kInvalidSocket;
    }
    freeaddrinfo(results);

    if (s == kInvalidSocket) {
        error = "connection to " + host + ":" + port_str + " refused";
        return std::nullopt;
    }

    std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\n";
    if (!authorization.empty()) {
        request += "Authorization: " + authorization + "\r\n";
    }
    request += "Connection: close\r\n\r\n";

    if (!send_all(s, request)) {
        close_socket(s);
        error = "failed to send HTTP request";
        return std::nullopt;
    }

    std::string raw;
    char buffer[4096];
    for (;;) {
        const auto n = ::recv(s, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        raw.append(buffer, static_cast<std::size_t>(n));
        if (raw.size() > (1u << 20)) {
            break;  // VLC status replies are a few KB; refuse anything huge.
        }
    }
    close_socket(s);

    // "HTTP/1.x 200 OK\r\n...\r\n\r\nbody"
    if (raw.compare(0, 5, "HTTP/") != 0) {
        error = "invalid HTTP response";
        return std::nullopt;
    }
    HttpResponse response;
    const auto space = raw.find(' ');
    if (space != std::string::npos) {
        response.status = std::atoi(raw.c_str() + space + 1);
    }
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        response.body = raw.substr(header_end + 4);
    }
    return response;
}

}  // namespace vinna::net
