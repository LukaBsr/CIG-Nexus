#include "Server.hpp"
#include <csignal>
#include <iostream>

#include <curl/curl.h>

static Server* g_server = nullptr;

static void handle_signal(int /*sig*/) {
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    // Required once before any CurlInternalApiClient use (http/CurlInternalApiClient.cpp).
    // curl_easy_init() would otherwise do this lazily, but doing it explicitly
    // here — before the signal handlers and any threads exist — is the
    // documented-safe way to call it.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int exit_code = 0;
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

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        exit_code = 1;
    }

    curl_global_cleanup();
    return exit_code;
}
