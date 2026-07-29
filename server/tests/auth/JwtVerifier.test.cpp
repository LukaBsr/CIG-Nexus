#include "auth/JwtVerifier.hpp"

#include "TestJwtHelper.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace {

uint64_t nowSeconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

nlohmann::json validPayload() {
    return nlohmann::json{{"sub", "u_11111111-1111-1111-1111-111111111111"},
                          {"discord_id", "999"},
                          {"username", "web_user"},
                          {"sid", "22222222-2222-2222-2222-222222222222"},
                          {"iat", nowSeconds()},
                          {"exp", nowSeconds() + 900},
                          {"iss", "cig-nexus-web"},
                          {"aud", "cig-nexus-server"}};
}

nlohmann::json rs256Header() {
    return nlohmann::json{{"alg", "RS256"}, {"typ", "JWT"}};
}

} // namespace

TEST_CASE("JwtVerifier accepts a well-formed RS256 token and returns its claims",
          "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), validPayload());
    const auto verification = verifier.verify(token);

    REQUIRE(verification.result == auth::JwtVerifyResult::Ok);
    REQUIRE(verification.claims.has_value());
    CHECK(verification.claims->sub == "u_11111111-1111-1111-1111-111111111111");
    CHECK(verification.claims->discord_id == "999");
    CHECK(verification.claims->username == "web_user");
    CHECK(verification.claims->sid == "22222222-2222-2222-2222-222222222222");
}

TEST_CASE("JwtVerifier rejects a token declaring a different algorithm", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    // Signed the same way (RS256, real signature) but the header claims a
    // different algorithm — this must be rejected on the header check
    // alone, not accepted because the underlying bytes happen to verify.
    nlohmann::json header = rs256Header();
    header["alg"] = "HS256";
    const std::string token = test_helpers::signTestJwt(keys.key, header, validPayload());

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::UnsupportedAlgorithm);
    CHECK_FALSE(verification.claims.has_value());
}

TEST_CASE("JwtVerifier rejects alg: none", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    nlohmann::json header = rs256Header();
    header["alg"] = "none";
    const std::string token = test_helpers::signTestJwt(keys.key, header, validPayload());

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::UnsupportedAlgorithm);
}

TEST_CASE("JwtVerifier rejects a token signed by a different key", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair signing_keys;
    test_helpers::TestRsaKeyPair other_keys;
    auth::JwtVerifier verifier(other_keys.publicKeyPem());

    const std::string token = test_helpers::signTestJwt(signing_keys.key, rs256Header(), validPayload());

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::InvalidSignature);
}

TEST_CASE("JwtVerifier rejects a tampered payload", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), validPayload());
    // Flip a character in the payload segment without re-signing.
    const size_t first_dot = token.find('.');
    const size_t second_dot = token.find('.', first_dot + 1);
    token[first_dot + 2] = (token[first_dot + 2] == 'A') ? 'B' : 'A';
    (void)second_dot;

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::InvalidSignature);
}

TEST_CASE("JwtVerifier rejects an expired token", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    nlohmann::json payload = validPayload();
    payload["exp"] = nowSeconds() - 60;
    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), payload);

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::Expired);
}

TEST_CASE("JwtVerifier rejects the wrong audience", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    nlohmann::json payload = validPayload();
    payload["aud"] = "someone-else";
    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), payload);

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::WrongAudience);
}

TEST_CASE("JwtVerifier rejects a token missing a required claim", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    nlohmann::json payload = validPayload();
    payload.erase("sid");
    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), payload);

    const auto verification = verifier.verify(token);
    CHECK(verification.result == auth::JwtVerifyResult::Malformed);
}

TEST_CASE("JwtVerifier rejects a string that isn't a JWT at all", "[JwtVerifier]") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    const auto verification = verifier.verify("not-a-jwt");
    CHECK(verification.result == auth::JwtVerifyResult::Malformed);
}
