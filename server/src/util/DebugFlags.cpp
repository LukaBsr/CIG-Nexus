#include "util/DebugFlags.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace util {

bool presenceDebugLoggingEnabled() {
    const char* value = std::getenv("CIG_NEXUS_DEBUG_PRESENCE");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

void logPresenceDebug(const std::string& message) {
    if (!presenceDebugLoggingEnabled()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::cerr << "[presence-debug] t=" << seconds << " " << message << std::endl;
}

} // namespace util
