#include "Server.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        uint16_t port = 4242;

        // Parse --port argument
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
                break;
            }
        }

        // Instantiate and start server
        Server server(port);
        server.start();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
