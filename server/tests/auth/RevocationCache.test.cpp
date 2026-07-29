#include "auth/RevocationCache.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("RevocationCache reports unknown session ids as not revoked", "[RevocationCache]") {
    auth::RevocationCache cache;
    CHECK_FALSE(cache.isRevoked("some-session-id"));
}

TEST_CASE("RevocationCache reports merged ids as revoked", "[RevocationCache]") {
    auth::RevocationCache cache;
    cache.merge({"session-a", "session-b"});

    CHECK(cache.isRevoked("session-a"));
    CHECK(cache.isRevoked("session-b"));
    CHECK_FALSE(cache.isRevoked("session-c"));
}

TEST_CASE("RevocationCache accumulates across multiple merges", "[RevocationCache]") {
    auth::RevocationCache cache;
    cache.merge({"session-a"});
    cache.merge({"session-b"});

    CHECK(cache.isRevoked("session-a"));
    CHECK(cache.isRevoked("session-b"));
}
