#include "Connection.hpp"
#include "FrameDecoder.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

static constexpr std::size_t READ_BUFFER_SIZE = 4096;

Connection::Connection(int socket_fd)
    : socket_fd_(socket_fd), buffer_() {}

Connection::~Connection() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
    }
}

int Connection::fd() const {
    return socket_fd_;
}

bool Connection::readFromSocket() {
    uint8_t temp[READ_BUFFER_SIZE];

    while (true) {
        errno = 0;
        const ssize_t bytes_read = recv(socket_fd_, temp, sizeof(temp), 0);

        if (bytes_read > 0) {
            buffer_.insert(buffer_.end(), temp, temp + bytes_read);
            continue;
        }

        if (bytes_read == 0) {
            return false;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        throw std::runtime_error(
            std::string("Failed to read from socket: ") + strerror(errno)
        );
    }
}

std::vector<std::vector<uint8_t>> Connection::pollFrames() {
    FrameDecoder decoder(buffer_);
    return decoder.extract_frames();
}
