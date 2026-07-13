#include "Server.hpp"
#include <csignal>
#include <iostream>

static Server* g_server = nullptr;

static void handle_signal(int /*sig*/) {
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    try {
        uint16_t port = 4242;

        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
                break;
            }
        }

        Server server(port);
        g_server = &server;

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        server.start();

        g_server = nullptr;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
