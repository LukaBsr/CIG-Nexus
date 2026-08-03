#include "TcpListener.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// docs/guilds/social-presence-design.md §3.3: mitigates the half-open-connection
// gap (a peer that stops responding without the OS ever surfacing a read
// failure — network partition, laptop sleep). Not a full fix: a user can
// show "online" for up to ~kKeepaliveIdleSeconds +
// kKeepaliveIntervalSeconds * kKeepaliveProbeCount after actually going
// dark. Explicitly tuned rather than left at OS defaults, which are often
// hours on Linux — that would make presence's offline transition
// practically unusable for anything but a clean close.
constexpr int kKeepaliveIdleSeconds = 30;     // idle time before the first probe
constexpr int kKeepaliveIntervalSeconds = 10; // time between probes
constexpr int kKeepaliveProbeCount = 3;       // failed probes before the OS reports the fd dead

// Best-effort: a failure to enable/tune keepalive shouldn't fail accept()
// itself — it's a mitigation for a gap that already existed, not a
// correctness requirement for the connection to function.
void enableKeepalive(int fd) {
    int enable = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));

    int idle = kKeepaliveIdleSeconds;
    int interval = kKeepaliveIntervalSeconds;
    int count = kKeepaliveProbeCount;
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

} // namespace

TcpListener::TcpListener(uint16_t port) : port_(port), socket_fd_(-1) {}

TcpListener::~TcpListener() {
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void TcpListener::start() {
    if (socket_fd_ != -1) {
        throw std::runtime_error("TcpListener already started");
    }

    // Create TCP socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == -1) {
        throw std::runtime_error(std::string("Failed to create socket: ") + strerror(errno));
    }

    // Set SO_REUSEADDR
    int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        close(socket_fd_);
        socket_fd_ = -1;
        throw std::runtime_error(std::string("Failed to set SO_REUSEADDR: ") + strerror(errno));
    }

    // Set non-blocking
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags == -1) {
        close(socket_fd_);
        socket_fd_ = -1;
        throw std::runtime_error(std::string("Failed to get socket flags: ") + strerror(errno));
    }

    if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(socket_fd_);
        socket_fd_ = -1;
        throw std::runtime_error(std::string("Failed to set non-blocking: ") + strerror(errno));
    }

    // Bind to port on INADDR_ANY
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(socket_fd_);
        socket_fd_ = -1;
        throw std::runtime_error(std::string("Failed to bind to port: ") + strerror(errno));
    }

    // Listen with backlog
    if (listen(socket_fd_, SOMAXCONN) == -1) {
        close(socket_fd_);
        socket_fd_ = -1;
        throw std::runtime_error(std::string("Failed to listen: ") + strerror(errno));
    }
}

uint16_t TcpListener::bound_port() const {
    if (socket_fd_ < 0)
        return 0;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
        return 0;
    return ntohs(addr.sin_port);
}

int TcpListener::accept() {
    int client_fd = ::accept(socket_fd_, nullptr, nullptr);
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1; // No pending connections (non-blocking)
        }
        throw std::runtime_error(std::string("Failed to accept connection: ") + strerror(errno));
    }

    // Set client socket to non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags == -1) {
        close(client_fd);
        throw std::runtime_error(std::string("Failed to get client flags: ") + strerror(errno));
    }

    if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(client_fd);
        throw std::runtime_error(std::string("Failed to set client non-blocking: ") +
                                 strerror(errno));
    }

    enableKeepalive(client_fd);

    return client_fd;
}
