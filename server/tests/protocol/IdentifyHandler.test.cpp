#include <catch2/catch_test_macros.hpp>

#include "protocol/handlers/IdentifyHandler.hpp"
#include "session/SessionManager.hpp"

#include "auth/JwtVerifier.hpp"
#include "auth/RevocationCache.hpp"

#include "../auth/TestJwtHelper.hpp"

#include <chrono>

namespace {

uint64_t nowSeconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

nlohmann::json validClaims(const std::string& sub = "u_11111111-1111-1111-1111-111111111111",
                          const std::string& sid = "22222222-2222-2222-2222-222222222222") {
    return nlohmann::json{{"sub", sub},
                          {"discord_id", "999"},
                          {"username", "web_user"},
                          {"sid", sid},
                          {"iat", nowSeconds()},
                          {"exp", nowSeconds() + 900},
                          {"iss", "cig-nexus-web"},
                          {"aud", "cig-nexus-server"}};
}

nlohmann::json rs256Header() {
    return nlohmann::json{{"alg", "RS256"}, {"typ", "JWT"}};
}

protocol::Message make_identify(const std::string& token) {
    protocol::Message message;
    message.type = "IDENTIFY";
    message.payload = {{"type", "IDENTIFY"}, {"session_token", token}};
    return message;
}

} // namespace

TEST_CASE("IdentifyHandler accepts a valid session_token and populates the session from its claims") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);

    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), validClaims());
    const auto response = handler.handle(make_identify(token), 10);

    REQUIRE(response.type == "IDENTIFIED");
    REQUIRE(response.payload["type"] == "IDENTIFIED");
    REQUIRE(response.payload["username"] == "web_user");
    REQUIRE(response.payload["user_id"] == "u_11111111-1111-1111-1111-111111111111");
    REQUIRE(sessions.hasSession(10));

    const auto* session = sessions.getSession(10);
    REQUIRE(session != nullptr);
    REQUIRE(session->username == "web_user");
    REQUIRE(session->user_id == "u_11111111-1111-1111-1111-111111111111");
    REQUIRE(session->discord_id == "999");
    REQUIRE(session->app_session_id == "22222222-2222-2222-2222-222222222222");
}

TEST_CASE("IdentifyHandler rejects second IDENTIFY on same connection") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);

    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), validClaims());
    const auto first = handler.handle(make_identify(token), 10);
    REQUIRE(first.type == "IDENTIFIED");

    const auto second = handler.handle(make_identify(token), 10);
    REQUIRE(second.type == "ERROR");
    REQUIRE(second.payload["code"] == "PROTOCOL_VIOLATION");
}

TEST_CASE("IdentifyHandler rejects a missing session_token") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);

    protocol::Message message;
    message.type = "IDENTIFY";
    message.payload = {{"type", "IDENTIFY"}};

    const auto response = handler.handle(message, 12);
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "AUTH_REQUIRED");
}

TEST_CASE("IdentifyHandler rejects a non-string session_token") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);

    protocol::Message message;
    message.type = "IDENTIFY";
    message.payload = {{"type", "IDENTIFY"}, {"session_token", 42}};

    const auto response = handler.handle(message, 12);
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "AUTH_REQUIRED");
}

TEST_CASE("IdentifyHandler rejects an expired session_token") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);

    nlohmann::json claims = validClaims();
    claims["exp"] = nowSeconds() - 60;
    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), claims);

    const auto response = handler.handle(make_identify(token), 13);
    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "SESSION_EXPIRED");
    REQUIRE_FALSE(sessions.hasSession(13));
}

TEST_CASE("IdentifyHandler rejects a token signed by an unrecognized key") {
    test_helpers::TestRsaKeyPair signing_keys;
    test_helpers::TestRsaKeyPair server_keys;
    auth::JwtVerifier verifier(server_keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);

    const std::string token = test_helpers::signTestJwt(signing_keys.key, rs256Header(), validClaims());
    const auto response = handler.handle(make_identify(token), 14);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "INVALID_SESSION");
}

TEST_CASE("IdentifyHandler rejects a revoked session") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());
    auth::RevocationCache revocation_cache;
    revocation_cache.merge({"22222222-2222-2222-2222-222222222222"});

    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);
    handler.setJwtVerifier(&verifier);
    handler.setRevocationCache(&revocation_cache);

    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), validClaims());
    const auto response = handler.handle(make_identify(token), 15);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "SESSION_REVOKED");
    REQUIRE_FALSE(sessions.hasSession(15));
}

TEST_CASE("IdentifyHandler returns INTERNAL_ERROR when session manager is missing") {
    test_helpers::TestRsaKeyPair keys;
    auth::JwtVerifier verifier(keys.publicKeyPem());

    protocol::IdentifyHandler handler;
    handler.setJwtVerifier(&verifier);

    const std::string token = test_helpers::signTestJwt(keys.key, rs256Header(), validClaims());
    const auto response = handler.handle(make_identify(token), 16);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
}

TEST_CASE("IdentifyHandler returns INTERNAL_ERROR when the JWT verifier is missing") {
    protocol::IdentifyHandler handler;
    session::SessionManager sessions;
    handler.setSessionManager(&sessions);

    const auto response = handler.handle(make_identify("irrelevant"), 17);

    REQUIRE(response.type == "ERROR");
    REQUIRE(response.payload["code"] == "INTERNAL_ERROR");
}
