#include "protocol/handlers/IdentifyHandler.hpp"

#include "auth/JwtVerifier.hpp"
#include "auth/RevocationCache.hpp"
#include "guild/GuildManager.hpp"
#include "http/InternalApiClient.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"
#include "util/DebugFlags.hpp"

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
    util::logPresenceDebug("IdentifyHandler::handle entry fd=" + std::to_string(fd));

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

    // docs/known-issues.md's presence connection-count leak: this log line
    // marks the point where a Session now exists for this fd but IDENTIFIED
    // has not been sent yet. If anything below throws, the "identify
    // success" log in Server.cpp never appears for this fd/user_id pair —
    // a visible gap between this line and that one is the signal to look
    // for, not just each line in isolation.
    util::logPresenceDebug("IdentifyHandler session created fd=" + std::to_string(fd) +
                           " user_id=" + session.user_id);

    // docs/guilds/social-presence-design.md §3.4/§1.10: hydrate this connection's
    // guild_ids immediately from GuildManager's durable-membership index,
    // rather than leaving it empty until the client re-issues JOIN_GUILD
    // for every guild it already belongs to. A pure in-memory lookup — no
    // new internal API call. guild_ids is freshly empty (createSession()
    // above), so a direct assignment is safe here.
    if (guild_manager_) {
        session.guild_ids = guild_manager_->getGuildIdsForUser(session.user_id);
    }

    // docs/social/friends-dms-design.md §3.3/§4.5: hydrate
    // blocked_user_ids, friend_ids, and display_name/avatar_url from one
    // combined internal-API call (WireSessionContext) rather than three
    // sequential ones — IDENTIFY hardening B1. This server is
    // single-threaded, so three round trips here previously meant every
    // other connection's traffic waited behind up to three back-to-back
    // 5s-timeout HTTP calls on a slow/unreachable web service; one call
    // halves that worst case. A failed/unset call leaves all three at
    // their empty defaults rather than failing IDENTIFY — presence
    // exclusion (Server.cpp) degrades to "no exclusion," canSendDm() to
    // "no cached friendship," and sent messages carry no display_name/
    // avatar_url, none of which block identification itself.
    if (internal_api_client_) {
        if (const auto context = internal_api_client_->fetchSessionContext(session.user_id)) {
            for (const auto& block : context->blocks) {
                session.blocked_user_ids.push_back(block.user_id);
            }
            for (const auto& f : context->friends) {
                session.friend_ids.push_back(f.user_id);
            }
            session.display_name = context->profile.display_name;
            session.avatar_url = context->profile.avatar_url;
        }
    }

    Message response;
    response.type = "IDENTIFIED";
    response.payload = nlohmann::json{
        {"type", "IDENTIFIED"}, {"user_id", session.user_id}, {"username", session.username}};
    return response;
}

} // namespace protocol
