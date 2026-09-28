#include "Server.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <vector>

namespace {

bool send_all(int fd, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < size) {
        // MSG_NOSIGNAL: a peer that RST'd the connection would otherwise
        // raise SIGPIPE on this send, and the process has no handler for
        // it — default disposition is termination. Broadcast/targeted
        // delivery routinely writes to fds that may have gone stale since
        // their membership snapshot was taken, so this can't be "just
        // don't do that"; the failure has to be a normal `false` return.
        const ssize_t sent = ::send(fd, bytes + total_sent, size - total_sent, MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;
        }

        total_sent += static_cast<size_t>(sent);
    }

    return true;
}

} // namespace

bool Server::sendMessage(int fd, const protocol::Message& message) {
    const std::string payload = message.payload.dump();
    const uint32_t frame_size = htonl(static_cast<uint32_t>(payload.size()));

    if (!send_all(fd, &frame_size, sizeof(frame_size))) {
        return false;
    }

    if (!send_all(fd, payload.data(), payload.size())) {
        return false;
    }

    return true;
}

void Server::broadcast(const protocol::Message& message) {
    for (const auto& [fd, conn] : connections_) {
        (void)conn;
        if (!session_manager_.hasSession(fd)) {
            continue;
        }
        (void)sendMessage(fd, message);
    }
}

void Server::broadcastExcluding(const protocol::Message& message,
                                const std::vector<int>& excluded_fds) {
    if (excluded_fds.empty()) {
        broadcast(message);
        return;
    }

    for (const auto& [fd, conn] : connections_) {
        (void)conn;
        if (!session_manager_.hasSession(fd)) {
            continue;
        }
        if (std::find(excluded_fds.begin(), excluded_fds.end(), fd) != excluded_fds.end()) {
            continue;
        }
        (void)sendMessage(fd, message);
    }
}
