#include "Server.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

Server::Server(uint16_t port)
    : port_(port), running_(false), listener_(port) {

    dispatcher_.registerHandler("HELLO",
        [this](const protocol::Message& msg) {
            return hello_handler_.handle(msg);
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
        }

        std::vector<protocol::Message> received_messages;
        for (const auto& message : received_messages) {
            (void)dispatcher_.dispatch(message);
            // TODO(protocol): integrate Connection -> FrameDecoder -> MessageParser -> Dispatcher
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "CIG Nexus Server stopped" << std::endl;
}

void Server::stop() {
    running_ = false;
}
