#include "Server.hpp"
#include "protocol/MessageParser.hpp"

#include <arpa/inet.h>
#include <cstddef>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace {

bool send_all(int fd, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < size) {
        const ssize_t sent = ::send(fd, bytes + total_sent, size - total_sent, 0);
        if (sent <= 0) {
            return false;
        }

        total_sent += static_cast<size_t>(sent);
    }

    return true;
}

} // namespace

Server::Server(uint16_t port)
    : port_(port), running_(false), listener_(port) {

    chat_handler_.setSessionManager(&session_manager_);
    identify_handler_.setSessionManager(&session_manager_);

    dispatcher_.registerHandler("HELLO",
        [this](const protocol::Message& msg, int /*fd*/) {
            return hello_handler_.handle(msg);
        }
    );

    dispatcher_.registerHandler("CHAT_MESSAGE",
        [this](const protocol::Message& msg, int fd) {
            return chat_handler_.handle(msg, fd);
        }
    );

    dispatcher_.registerHandler("IDENTIFY",
        [this](const protocol::Message& msg, int fd) {
            return identify_handler_.handle(msg, fd);
        }
    );
}

void Server::start() {
    if (running_) {
        throw std::runtime_error("Server is already running");
    }

    listener_.start();
    running_ = true;
    std::cout << "CIG Nexus Server starting on port " << port_ << std::endl;

    while (running_) {
        int client_fd = listener_.accept();
        if (client_fd >= 0) {
            std::cout << "Client connected (fd=" << client_fd << ")" << std::endl;
            connections_.emplace(
                client_fd,
                std::make_unique<Connection>(client_fd)
            );
        }

        for (auto it = connections_.begin(); it != connections_.end(); ) {
            auto& [fd, conn] = *it;

            if (!conn->readFromSocket()) {
                std::cout << "Client disconnected (fd=" << fd << ")" << std::endl;
                session_manager_.removeSession(fd);
                it = connections_.erase(it);
                continue;
            }

            auto frames = conn->pollFrames();
            for (const auto& frame : frames) {
                protocol::Message message;

                try {
                    message = protocol::parse_message(frame);
                } catch (const std::exception& e) {
                    std::cerr << "Protocol error: " << e.what() << std::endl;
                    continue;
                }

                const auto response = dispatcher_.dispatch(message, fd);

                switch (response.scope) {
                    case protocol::Scope::BROADCAST:
                        broadcast(response);
                        break;

                    case protocol::Scope::DIRECT:
                        sendMessage(fd, response);
                        break;
                }
            }
            ++it;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "CIG Nexus Server stopped" << std::endl;
}

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
        (void)sendMessage(fd, message);
    }
}

void Server::stop() {
    running_ = false;
}
