#include "Server.hpp"
#include "protocol/MessageParser.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <vector>

Server::Server(uint16_t port)
    : port_(port), running_(false), listener_(port) {

    dispatcher_.registerHandler("HELLO",
        [this](const protocol::Message& msg) {
            return hello_handler_.handle(msg);
        }
    );

    dispatcher_.registerHandler("CHAT_MESSAGE",
        [this](const protocol::Message& msg) {
            return chat_handler_.handle(msg);
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
                it = connections_.erase(it);
                continue;
            }

            auto frames = conn->pollFrames();
            for (const auto& frame : frames) {
                auto message = protocol::parse_message(frame);
                auto response = dispatcher_.dispatch(message);

                if (response.type == "CHAT_MESSAGE") {
                    broadcast(response);
                } else {
                    std::string response_json = response.payload.dump();

                    uint32_t size = htonl(static_cast<uint32_t>(response_json.size()));
                    ::send(fd, &size, 4, 0);
                    ::send(fd, response_json.data(), response_json.size(), 0);
                }
            }

            ++it;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "CIG Nexus Server stopped" << std::endl;
}

void Server::broadcast(const protocol::Message& message) {
    std::string message_json = message.payload.dump();
    uint32_t size = htonl(static_cast<uint32_t>(message_json.size()));

    for (const auto& [fd, conn] : connections_) {
        ::send(fd, &size, 4, 0);
        ::send(fd, message_json.data(), message_json.size(), 0);
    }
}

void Server::stop() {
    running_ = false;
}
