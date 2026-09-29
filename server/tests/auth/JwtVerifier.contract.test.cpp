#include "auth/JwtVerifier.hpp"

#include "TestJwtHelper.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

// Contract with the real web signer (web/lib/auth/jwt.ts). The fixture is one
// token that signer produced, frozen, with its public key — see
// tests/fixtures/jwt/README.md. The verifier's clock is injected so the token
// verifies at a fixed instant instead of "expiring" once real time passes
// iat + 15 minutes. web/lib/auth/jwt.test.ts keeps the fixture in step with
// what the signer emits today.

namespace {

constexpr uint64_t kFixtureIat = 1790000000;
constexpr uint64_t kFixtureExp = 1790000900;

std::string readFixture(const std::string& name) {
    std::ifstream file(std::string(CIG_NEXUS_TEST_FIXTURES_DIR) + "/jwt/" + name, std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream contents;
    contents << file.rdbuf();
    std::string text = contents.str();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

auth::JwtVerifier fixtureVerifier(uint64_t now, const std::string& issuer = "cig-nexus-web") {
    return auth::JwtVerifier(readFixture("public.pem") + "\n", issuer, [now] { return now; });
}

} // namespace

TEST_CASE("JwtVerifier accepts a token issued by the real web signer", "[JwtVerifier][contract]") {
    const std::string token = readFixture("web-signer-token.txt");
    const auto verification = fixtureVerifier(kFixtureIat + 60).verify(token);

    REQUIRE(verification.result == auth::JwtVerifyResult::Ok);
    REQUIRE(verification.claims.has_value());
    CHECK(verification.claims->sub == "u_00000000-0000-4000-8000-000000000001");
    CHECK(verification.claims->discord_id == "123456789012345678");
    CHECK(verification.claims->username == "fixture_user");
    CHECK(verification.claims->sid == "00000000-0000-4000-8000-0000000000aa");
}

TEST_CASE("JwtVerifier treats the real web signer's token as expired once its exp passes",
          "[JwtVerifier][contract]") {
    const std::string token = readFixture("web-signer-token.txt");

    CHECK(fixtureVerifier(kFixtureExp - 1).verify(token).result == auth::JwtVerifyResult::Ok);
    CHECK(fixtureVerifier(kFixtureExp).verify(token).result == auth::JwtVerifyResult::Expired);
}

TEST_CASE("JwtVerifier rejects the real web signer's token under a different expected issuer",
          "[JwtVerifier][contract]") {
    const std::string token = readFixture("web-signer-token.txt");

    CHECK(fixtureVerifier(kFixtureIat + 60, "someone-else").verify(token).result ==
          auth::JwtVerifyResult::WrongIssuer);
}

TEST_CASE("JwtVerifier rejects the real web signer's token under a different key",
          "[JwtVerifier][contract]") {
    const std::string token = readFixture("web-signer-token.txt");
    test_helpers::TestRsaKeyPair other_keys;
    auth::JwtVerifier verifier(other_keys.publicKeyPem(), "cig-nexus-web",
                               [] { return kFixtureIat + 60; });

    CHECK(verifier.verify(token).result == auth::JwtVerifyResult::InvalidSignature);
}
