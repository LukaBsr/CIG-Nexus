#include "Server.hpp"
#include "http/CurlInternalApiClient.hpp"
#include "util/FilePermissions.hpp"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
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

// AUTH_JWT_PUBLIC_KEY_PATH points at a real .pem file on disk (bind-mounted
// from secrets/, see docker-compose.yml) rather than holding key content
// directly in the env var — no escaped-newline normalization needed, since
// a real file already has real newlines.
std::string readRequiredFile(const std::string& path_env_var_name) {
    const std::string path = requireEnv(path_env_var_name.c_str());

    // docs/security-audit.md §1.3 / action item 3: refuse to start rather
    // than read a private key that's group- or world-readable on disk.
    if (util::isPrivateKeyPathVar(path_env_var_name)) {
        util::requireOwnerOnlyPermissions(path);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to read file at " + path_env_var_name + "=" + path +
                                 ": could not open file");
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
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

        // AUTH_JWT_PUBLIC_KEY_PATH: the RS256 public key counterpart of
        // Next.js's SESSION_JWT_PRIVATE_KEY_PATH (design doc §6) — a static
        // env-provided key file, not a JWKS endpoint (see
        // docs/auth/discord-design.md's "Key distribution" decision).
        const std::string jwt_public_key_pem = readRequiredFile("AUTH_JWT_PUBLIC_KEY_PATH");
        const std::string internal_api_base_url = requireEnv("INTERNAL_API_BASE_URL");
        const std::string internal_api_shared_secret = requireEnv("INTERNAL_API_SHARED_SECRET");

        Server server(port);
        g_server = &server;

        server.configureAuth(jwt_public_key_pem);
        server.setInternalApiClient(std::make_unique<http::CurlInternalApiClient>(
            internal_api_base_url, internal_api_shared_secret));

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
