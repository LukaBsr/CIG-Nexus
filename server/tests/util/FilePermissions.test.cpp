#include "util/FilePermissions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// mkstemp fills a real, unique path from the template and creates the file;
// tests then chmod it to whatever mode they want to exercise.
std::string writeTempFile(mode_t mode) {
    std::string path_template = "/tmp/cig-nexus-perm-test-XXXXXX";
    int fd = ::mkstemp(path_template.data());
    REQUIRE(fd >= 0);
    ::close(fd);
    REQUIRE(::chmod(path_template.c_str(), mode) == 0);
    return path_template;
}

} // namespace

TEST_CASE("isPrivateKeyPathVar matches env var names by *PRIVATE_KEY* convention",
          "[FilePermissions]") {
    REQUIRE(util::isPrivateKeyPathVar("SESSION_JWT_PRIVATE_KEY_PATH"));
    REQUIRE_FALSE(util::isPrivateKeyPathVar("AUTH_JWT_PUBLIC_KEY_PATH"));
    REQUIRE_FALSE(util::isPrivateKeyPathVar("INTERNAL_API_BASE_URL"));
}

TEST_CASE("requireOwnerOnlyPermissions accepts an owner-only-readable file", "[FilePermissions]") {
    const std::string path = writeTempFile(0600);
    REQUIRE_NOTHROW(util::requireOwnerOnlyPermissions(path));
    ::remove(path.c_str());
}

TEST_CASE("requireOwnerOnlyPermissions rejects a group-readable file", "[FilePermissions]") {
    const std::string path = writeTempFile(0640);
    REQUIRE_THROWS_AS(util::requireOwnerOnlyPermissions(path), std::runtime_error);
    ::remove(path.c_str());
}

TEST_CASE("requireOwnerOnlyPermissions rejects a world-readable file", "[FilePermissions]") {
    const std::string path = writeTempFile(0644);
    REQUIRE_THROWS_AS(util::requireOwnerOnlyPermissions(path), std::runtime_error);
    ::remove(path.c_str());
}

TEST_CASE("requireOwnerOnlyPermissions throws when the file doesn't exist", "[FilePermissions]") {
    REQUIRE_THROWS_AS(util::requireOwnerOnlyPermissions("/nonexistent/private.pem"),
                      std::runtime_error);
}
