#include "Connection.hpp"
#include "FrameDecoder.hpp"

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

static constexpr std::size_t READ_BUFFER_SIZE = 4096;

Connection::Connection(int socket_fd) : socket_fd_(socket_fd), buffer_() {}

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

        // Any other recv() error (e.g. ECONNRESET from a peer that RST the
        // connection rather than closing cleanly) is treated as a
        // disconnect, not an exceptional condition: Server::start() already
        // has a well-defined disconnect path (session/connection cleanup)
        // that owns this. Throwing here used to escape uncaught out of
        // Server::start() and terminate the process.
        return false;
    }
}

std::vector<std::vector<uint8_t>> Connection::pollFrames() {
    FrameDecoder decoder(buffer_);
    return decoder.extract_frames();
}
