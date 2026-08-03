#include <catch2/catch_test_macros.hpp>

#include "TcpListener.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int connect_loopback(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

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

// accept() is non-blocking (TcpListener::start()'s O_NONBLOCK) and this test
// doesn't run an event loop, so poll briefly for the connection to land.
int accept_with_retry(TcpListener& listener) {
    for (int i = 0; i < 100; ++i) {
        const int fd = listener.accept();
        if (fd >= 0) {
            return fd;
        }
        ::usleep(10000);
    }
    return -1;
}

} // namespace

// docs/guilds/social-presence-design.md §3.3: SO_KEEPALIVE mitigation for the
// half-open-connection gap, shipped alongside presence rather than deferred.
TEST_CASE("TcpListener enables and tunes SO_KEEPALIVE on accepted sockets") {
    TcpListener listener(0);
    listener.start();

    const int client_fd = connect_loopback(listener.bound_port());
    REQUIRE(client_fd >= 0);

    const int server_fd = accept_with_retry(listener);
    REQUIRE(server_fd >= 0);

    int keepalive = 0;
    socklen_t len = sizeof(keepalive);
    REQUIRE(::getsockopt(server_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &len) == 0);
    CHECK(keepalive != 0);

    int idle = 0;
    len = sizeof(idle);
    REQUIRE(::getsockopt(server_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, &len) == 0);
    CHECK(idle > 0);
    CHECK(idle <= 60); // tuned well below Linux's hours-long default

    int interval = 0;
    len = sizeof(interval);
    REQUIRE(::getsockopt(server_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, &len) == 0);
    CHECK(interval > 0);

    int count = 0;
    len = sizeof(count);
    REQUIRE(::getsockopt(server_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, &len) == 0);
    CHECK(count > 0);

    ::close(client_fd);
    ::close(server_fd);
}
