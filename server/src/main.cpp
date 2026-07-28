#include "Server.hpp"
#include "http/CurlInternalApiClient.hpp"

#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <curl/curl.h>

static Server* g_server = nullptr;

static void handle_signal(int /*sig*/) {
    if (g_server) {
        g_server->stop();
    }
}

namespace {

// design doc §8.2: auth and internal-API config arrive via env vars, not
// flags — mirrors how the web/gateway services are already configured in
// docker-compose.yml (e.g. NEXT_PUBLIC_GATEWAY_URL). Fails fast at startup
// rather than letting IDENTIFY/CREATE_GUILD silently return INTERNAL_ERROR
// for every request because a var was missing.
std::string requireEnv(const char* name) {
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty()) {
        throw std::runtime_error(std::string("Missing required environment variable: ") + name);
    }
    return value;
}

// PEM values passed through docker-compose/.env files commonly arrive with
// literal "\n" escape sequences instead of real newlines (same issue,
// mirrored in web/lib/auth/env.ts's normalizePem for SESSION_JWT_PRIVATE_KEY).
std::string normalizePem(std::string value) {
    std::string::size_type pos = 0;
    while ((pos = value.find("\\n", pos)) != std::string::npos) {
        value.replace(pos, 2, "\n");
        pos += 1;
    }
    return value;
}

} // namespace

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

        // AUTH_JWT_PUBLIC_KEY: the RS256 public key counterpart of Next.js's
        // SESSION_JWT_PRIVATE_KEY (design doc §6) — static env-provided key,
        // not a JWKS endpoint (see docs/auth-discord-design.md's "Key
        // distribution" decision).
        const std::string jwt_public_key_pem = normalizePem(requireEnv("AUTH_JWT_PUBLIC_KEY"));
        const std::string internal_api_base_url = requireEnv("INTERNAL_API_BASE_URL");
        const std::string internal_api_shared_secret = requireEnv("INTERNAL_API_SHARED_SECRET");

        Server server(port);
        g_server = &server;

        server.configureAuth(jwt_public_key_pem);
        server.setInternalApiClient(
            std::make_unique<http::CurlInternalApiClient>(internal_api_base_url, internal_api_shared_secret));

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
