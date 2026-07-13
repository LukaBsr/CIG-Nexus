#include <catch2/catch_test_macros.hpp>

#include "Server.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// ----------------------------------------------------------------------------
// TCP helpers shared by integration tests below
// ----------------------------------------------------------------------------

namespace {

constexpr uint16_t PORT_STOP_TEST = 19242;
constexpr uint16_t PORT_PROTOCOL_VIOLATION_TEST = 19243;

int tcp_connect(uint16_t port) {
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

void send_framed(int fd, const std::string& json) {
    uint32_t size_be = htonl(static_cast<uint32_t>(json.size()));
    ::send(fd, &size_be, 4, 0);
    ::send(fd, json.data(), json.size(), 0);
}

std::string recv_framed(int fd) {
    uint32_t size_be = 0;
    if (::recv(fd, &size_be, 4, MSG_WAITALL) != 4)
        return "";
    uint32_t size = ntohl(size_be);
    std::string buf(size, '\0');
    if (::recv(fd, buf.data(), size, MSG_WAITALL) != static_cast<ssize_t>(size))
        return "";
    return buf;
}

} // namespace

// ----------------------------------------------------------------------------
// Fix #2: stop() causes start() to return (exercises the atomic running_ flag
// and verifies the signal-handler target works correctly).
// ----------------------------------------------------------------------------

TEST_CASE("Server exits start() when stop() is called externally") {
    Server server(PORT_STOP_TEST);
    std::thread t([&server] { server.start(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.stop();
    t.join(); // would hang indefinitely if stop() had no effect

    REQUIRE(true);
}

// ----------------------------------------------------------------------------
// Fix #3: server sends PROTOCOL_VIOLATION ERROR for unknown message type.
// ----------------------------------------------------------------------------

TEST_CASE("Server returns PROTOCOL_VIOLATION for unknown message type") {
    Server server(PORT_PROTOCOL_VIOLATION_TEST);
    std::thread t([&server] { server.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int fd = tcp_connect(PORT_PROTOCOL_VIOLATION_TEST);
    REQUIRE(fd >= 0);

    // Complete the HELLO handshake so the server is in a normal state
    send_framed(fd, R"({"type":"HELLO","version":"0.1","client":"web"})");
    std::string welcome = recv_framed(fd);
    REQUIRE(!welcome.empty());
    REQUIRE(nlohmann::json::parse(welcome)["type"] == "WELCOME");

    // Send a message type the dispatcher has no handler for
    send_framed(fd, R"({"type":"POKE","data":"test"})");
    std::string response = recv_framed(fd);
    REQUIRE(!response.empty());

    ::close(fd);
    server.stop();
    t.join();

    auto parsed = nlohmann::json::parse(response);
    REQUIRE(parsed["type"] == "ERROR");
    REQUIRE(parsed["code"] == "PROTOCOL_VIOLATION");
}
