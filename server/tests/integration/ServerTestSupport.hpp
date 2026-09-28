#ifndef CIG_NEXUS_TESTS_INTEGRATION_SERVER_TEST_SUPPORT_HPP
#define CIG_NEXUS_TESTS_INTEGRATION_SERVER_TEST_SUPPORT_HPP

#include "../auth/TestJwtHelper.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// ----------------------------------------------------------------------------
// TCP helpers shared by integration tests below
// ----------------------------------------------------------------------------

namespace server_test_support {

inline int tcp_connect(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv = {2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

inline void send_framed(int fd, const std::string& json) {
    uint32_t size_be = htonl(static_cast<uint32_t>(json.size()));
    ::send(fd, &size_be, 4, 0);
    ::send(fd, json.data(), json.size(), 0);
}

inline std::string recv_frame_raw(int fd) {
    uint32_t size_be = 0;
    if (::recv(fd, &size_be, 4, MSG_WAITALL) != 4)
        return "";
    uint32_t size = ntohl(size_be);
    std::string buf(size, '\0');
    if (::recv(fd, buf.data(), size, MSG_WAITALL) != static_cast<ssize_t>(size))
        return "";
    return buf;
}

// docs/guilds/social-presence-design.md §3.2: PRESENCE_UPDATE broadcasts to every
// open connection, including the one whose own IDENTIFY just triggered it
// (same delivery model CHAT_MESSAGE already uses) and every other already-
// connected socket. The tests below that predate presence, and every test
// that doesn't specifically assert on presence, use this instead of the raw
// primitive above so an interleaved PRESENCE_UPDATE never gets mistaken for
// the response a test is actually waiting for. Tests that DO want to
// observe a PRESENCE_UPDATE call recv_frame_raw() directly.
inline std::string recv_framed(int fd) {
    while (true) {
        std::string frame = recv_frame_raw(fd);
        if (frame.empty()) {
            return frame;
        }
        const auto parsed = nlohmann::json::parse(frame, nullptr, false);
        if (parsed.is_discarded() || parsed.value("type", "") != "PRESENCE_UPDATE") {
            return frame;
        }
    }
}

inline void set_recv_timeout(int fd, long seconds, long microseconds) {
    struct timeval tv {
        seconds, microseconds
    };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

inline uint64_t now_seconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

// Signs a session_token for IDENTIFY (design doc §6/§8) — every caller in
// this file shares one server-wide keypair (configured via
// Server::configureAuth) but gets a distinct sub/username/sid per identity.
inline std::string make_session_token(EVP_PKEY* key, const std::string& user_id,
                                      const std::string& username) {
    nlohmann::json header{{"alg", "RS256"}, {"typ", "JWT"}};
    nlohmann::json payload{{"sub", user_id},         {"discord_id", user_id},
                           {"username", username},   {"sid", user_id + "-sid"},
                           {"iat", now_seconds()},   {"exp", now_seconds() + 900},
                           {"iss", "cig-nexus-web"}, {"aud", "cig-nexus-server"}};
    return test_helpers::signTestJwt(key, header, payload);
}

} // namespace server_test_support

#endif // CIG_NEXUS_TESTS_INTEGRATION_SERVER_TEST_SUPPORT_HPP
