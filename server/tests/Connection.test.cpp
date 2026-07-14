#include <catch2/catch_test_macros.hpp>

#include "Connection.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {

// AF_UNIX SOCK_STREAM behaves like a connected TCP socket for the
// recv()/send()/close() calls Connection makes, without needing to bind a
// real port. {a, b} are two ends of the same connected pair.
//
// NOT used for the RST/ECONNRESET test below: SO_LINGER's abortive close
// doesn't reliably surface as ECONNRESET on AF_UNIX the way it does on real
// TCP (verified empirically — a socketpair-based version of that test kept
// passing even with the ECONNRESET fix reverted, because it was silently
// falling through the clean-EOF branch instead). That test uses a real
// loopback TCP pair via make_tcp_pair() instead.
std::pair<int, int> make_socket_pair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        throw std::runtime_error("socketpair failed");
    }
    return {fds[0], fds[1]};
}

// {server_fd, peer_fd}: a real, connected loopback TCP pair. server_fd is
// the accept()ed side (what Connection would wrap in the real server);
// peer_fd is the connect()ed side the test acts as.
std::pair<int, int> make_tcp_pair() {
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("socket() failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0; // OS assigns a free ephemeral port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd, 1) != 0) {
        throw std::runtime_error("bind/listen failed");
    }

    socklen_t addr_len = sizeof(addr);
    if (::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        throw std::runtime_error("getsockname failed");
    }

    const int peer_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (peer_fd < 0 || ::connect(peer_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error("connect failed");
    }

    const int server_fd = ::accept(listen_fd, nullptr, nullptr);
    ::close(listen_fd);
    if (server_fd < 0) {
        throw std::runtime_error("accept failed");
    }

    return {server_fd, peer_fd};
}

void set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void send_framed(int fd, const std::string& json) {
    const uint32_t size_be = htonl(static_cast<uint32_t>(json.size()));
    ::send(fd, &size_be, 4, 0);
    ::send(fd, json.data(), json.size(), 0);
}

std::string frame_to_string(const std::vector<uint8_t>& frame) {
    return std::string(frame.begin(), frame.end());
}

} // namespace

TEST_CASE("Connection buffers received bytes and extracts a complete frame") {
    auto [server_fd, peer_fd] = make_socket_pair();
    set_nonblocking(server_fd);
    Connection conn(server_fd); // takes ownership of server_fd, closes it on destruction

    send_framed(peer_fd, R"({"type":"HELLO"})");

    REQUIRE(conn.readFromSocket());
    auto frames = conn.pollFrames();

    REQUIRE(frames.size() == 1);
    REQUIRE(frame_to_string(frames[0]) == R"({"type":"HELLO"})");

    ::close(peer_fd);
}

TEST_CASE("Connection readFromSocket returns true with no frames when nothing is available yet") {
    auto [server_fd, peer_fd] = make_socket_pair();
    set_nonblocking(server_fd);
    Connection conn(server_fd);

    REQUIRE(conn.readFromSocket()); // EAGAIN/EWOULDBLOCK path
    REQUIRE(conn.pollFrames().empty());

    ::close(peer_fd);
}

TEST_CASE("Connection buffers a frame split across two separate writes") {
    auto [server_fd, peer_fd] = make_socket_pair();
    set_nonblocking(server_fd);
    Connection conn(server_fd);

    const std::string json = R"({"type":"PING"})";
    const uint32_t size_be = htonl(static_cast<uint32_t>(json.size()));

    // Size prefix arrives on its own; the payload hasn't been sent yet.
    ::send(peer_fd, &size_be, 4, 0);
    REQUIRE(conn.readFromSocket());
    REQUIRE(conn.pollFrames().empty()); // frame is incomplete so far

    ::send(peer_fd, json.data(), json.size(), 0);
    REQUIRE(conn.readFromSocket());

    auto frames = conn.pollFrames();
    REQUIRE(frames.size() == 1);
    REQUIRE(frame_to_string(frames[0]) == json);

    ::close(peer_fd);
}

TEST_CASE("Connection readFromSocket returns false on a clean disconnect") {
    auto [server_fd, peer_fd] = make_socket_pair();
    set_nonblocking(server_fd);
    Connection conn(server_fd);

    ::close(peer_fd); // clean close: peer sees EOF (recv() returns 0), not a reset

    REQUIRE_FALSE(conn.readFromSocket());
}

TEST_CASE("Connection readFromSocket returns false, without throwing, when the peer resets the connection") {
    auto [server_fd, peer_fd] = make_tcp_pair();
    set_nonblocking(server_fd);
    Connection conn(server_fd);

    // SO_LINGER with a zero timeout forces an abortive close (RST) instead
    // of a clean FIN, so the next recv() on server_fd fails with
    // ECONNRESET rather than returning 0.
    struct linger sl{1, 0};
    ::setsockopt(peer_fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
    ::close(peer_fd);

    // Regression test: readFromSocket() used to throw std::runtime_error on
    // any recv() error other than EAGAIN/EWOULDBLOCK, including
    // ECONNRESET. Server::start() didn't catch it, so it escaped uncaught
    // and terminated the process. It must now return false like any other
    // disconnect.
    bool result = true;
    REQUIRE_NOTHROW(result = conn.readFromSocket());
    REQUIRE_FALSE(result);
}
