#include <catch2/catch_test_macros.hpp>

#include "util/DebugFlags.hpp"

#include <cstdlib>

namespace {

// Guards against leaking CIG_NEXUS_DEBUG_PRESENCE into other test cases in
// this same process — util::presenceDebugLoggingEnabled() reads the
// environment fresh on every call (deliberately, so it's testable at all;
// see DebugFlags.hpp), so a test that sets it must also unset it.
struct EnvVarGuard {
    explicit EnvVarGuard(const char* value) { ::setenv("CIG_NEXUS_DEBUG_PRESENCE", value, 1); }
    ~EnvVarGuard() { ::unsetenv("CIG_NEXUS_DEBUG_PRESENCE"); }
};

} // namespace

TEST_CASE("presenceDebugLoggingEnabled is false when the env var is unset") {
    ::unsetenv("CIG_NEXUS_DEBUG_PRESENCE");
    REQUIRE_FALSE(util::presenceDebugLoggingEnabled());
}

TEST_CASE("presenceDebugLoggingEnabled is true only for the exact value \"1\"") {
    {
        EnvVarGuard guard("1");
        REQUIRE(util::presenceDebugLoggingEnabled());
    }
    REQUIRE_FALSE(util::presenceDebugLoggingEnabled()); // guard's destructor unset it

    for (const char* value : {"0", "true", "yes", "TRUE", ""}) {
        EnvVarGuard guard(value);
        REQUIRE_FALSE(util::presenceDebugLoggingEnabled());
    }
}

TEST_CASE("logPresenceDebug does not throw or crash regardless of the flag") {
    ::unsetenv("CIG_NEXUS_DEBUG_PRESENCE");
    REQUIRE_NOTHROW(util::logPresenceDebug("disabled case"));

    EnvVarGuard guard("1");
    REQUIRE_NOTHROW(util::logPresenceDebug("enabled case"));
}
