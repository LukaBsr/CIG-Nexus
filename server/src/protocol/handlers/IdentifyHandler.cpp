#include "protocol/handlers/IdentifyHandler.hpp"

#include "auth/JwtVerifier.hpp"
#include "auth/RevocationCache.hpp"
#include "guild/GuildManager.hpp"
#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

#include <string>

namespace protocol {

namespace {

Message makeError(const std::string& code, const std::string& msg) {
    Message response;
    response.type = "ERROR";
    response.payload = make_error(code, msg);
    return response;
}

} // namespace

void IdentifyHandler::setSessionManager(session::SessionManager* session_manager) {
    session_manager_ = session_manager;
}

void IdentifyHandler::setJwtVerifier(const auth::JwtVerifier* jwt_verifier) {
    jwt_verifier_ = jwt_verifier;
}

void IdentifyHandler::setRevocationCache(const auth::RevocationCache* revocation_cache) {
    revocation_cache_ = revocation_cache;
}

void IdentifyHandler::setGuildManager(const guild::GuildManager* guild_manager) {
    guild_manager_ = guild_manager;
}

void IdentifyHandler::setInternalApiClient(http::InternalApiClient* internal_api_client) {
    internal_api_client_ = internal_api_client;
}

Message IdentifyHandler::handle(const Message& message, int fd) {
    if (message.type != "IDENTIFY") {
        return makeError("PROTOCOL_VIOLATION", "Expected IDENTIFY message");
    }

    if (!message.payload.is_object()) {
        return makeError("MALFORMED_MESSAGE", "IDENTIFY payload must be an object");
    }

    if (!message.payload.contains("session_token")) {
        return makeError("AUTH_REQUIRED", "IDENTIFY missing required field: session_token");
    }

    if (!message.payload["session_token"].is_string()) {
        return makeError("AUTH_REQUIRED", "IDENTIFY session_token must be a string");
    }

    if (!session_manager_ || !jwt_verifier_) {
        return makeError("INTERNAL_ERROR", "Auth context unavailable");
    }

    if (session_manager_->hasSession(fd)) {
        return makeError("PROTOCOL_VIOLATION", "Connection is already identified");
    }

    const std::string session_token = message.payload["session_token"].get<std::string>();
    const auth::JwtVerification verification = jwt_verifier_->verify(session_token);

    switch (verification.result) {
    case auth::JwtVerifyResult::Ok:
        break;
    case auth::JwtVerifyResult::Expired:
        return makeError("SESSION_EXPIRED", "session_token has expired");
    case auth::JwtVerifyResult::Malformed:
    case auth::JwtVerifyResult::UnsupportedAlgorithm:
    case auth::JwtVerifyResult::InvalidSignature:
    case auth::JwtVerifyResult::WrongAudience:
        return makeError("INVALID_SESSION", "session_token is invalid");
    }

    const auth::AccessJwtClaims& claims = *verification.claims;

    if (revocation_cache_ && revocation_cache_->isRevoked(claims.sid)) {
        return makeError("SESSION_REVOKED", "Session has been revoked");
    }

    session::Session& session = session_manager_->createSession(fd);
    session.user_id = claims.sub;
    session.username = claims.username;
    session.discord_id = claims.discord_id;
    session.app_session_id = claims.sid;

    // docs/guilds/social-presence-design.md §3.4/§1.10: hydrate this connection's
    // guild_ids immediately from GuildManager's durable-membership index,
    // rather than leaving it empty until the client re-issues JOIN_GUILD
    // for every guild it already belongs to. A pure in-memory lookup — no
    // new internal API call. guild_ids is freshly empty (createSession()
    // above), so a direct assignment is safe here.
    if (guild_manager_) {
        session.guild_ids = guild_manager_->getGuildIdsForUser(session.user_id);
    }

    // docs/social/friends-dms-design.md §3.3: hydrate blocked_user_ids the
    // same way, except this one is a live internal API call (no
    // process-wide block cache exists the way GuildManager caches every
    // membership) — a bounded, once-per-IDENTIFY cost, not a per-message
    // one. A failed/unset call just leaves blocked_user_ids empty rather
    // than failing IDENTIFY — presence exclusion (Server.cpp) degrades to
    // "no exclusion" in that case, not a hard error.
    if (internal_api_client_) {
        if (const auto blocks = internal_api_client_->fetchBlocks(session.user_id)) {
            for (const auto& block : *blocks) {
                session.blocked_user_ids.push_back(block.user_id);
            }
        }

        // §3.3: friend_ids hydrated the same way, for canSendDm()'s
        // in-memory friend check.
        if (const auto friends = internal_api_client_->fetchFriends(session.user_id)) {
            for (const auto& f : *friends) {
                session.friend_ids.push_back(f.user_id);
            }
        }
    }

    Message response;
    response.type = "IDENTIFIED";
    response.payload = nlohmann::json{
        {"type", "IDENTIFIED"}, {"user_id", session.user_id}, {"username", session.username}};
    return response;
}

} // namespace protocol
