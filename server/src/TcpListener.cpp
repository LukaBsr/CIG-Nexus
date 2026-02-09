#include "TcpListener.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

TcpListener::TcpListener(uint16_t port)
    : port_(port), socket_fd_(-1) {}

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

int TcpListener::accept() {
    int client_fd = ::accept(socket_fd_, nullptr, nullptr);
    if (client_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;  // No pending connections (non-blocking)
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
        throw std::runtime_error(std::string("Failed to set client non-blocking: ") + strerror(errno));
    }

    return client_fd;
}
