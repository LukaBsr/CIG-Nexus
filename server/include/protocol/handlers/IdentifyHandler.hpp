#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP

#include "protocol/Message.hpp"

namespace session {
class SessionManager;
}

namespace guild {
class GuildManager;
}

namespace auth {
class JwtVerifier;
class RevocationCache;
} // namespace auth

namespace protocol {

// design doc §8: IDENTIFY no longer takes a client-chosen username — it
// takes a session_token (the access JWT from §6), and identity (user_id,
// username, discord_id) is derived entirely from its verified claims.
//
// IDENTIFY hardening (B2): this handler no longer makes any internal-API
// call itself. Through B1 it called InternalApiClient::fetchSessionContext
// synchronously here to hydrate blocked_user_ids/friend_ids/display_name/
// avatar_url before returning IDENTIFIED — but that meant a slow or
// unreachable internal API stalled this server's single-threaded main
// loop for every other connection, not just this one. That load is now
// Server's responsibility: on seeing this handler's IDENTIFIED response,
// Server enqueues a session::SessionHydrationJob on a background worker
// and applies the result later, off this handler entirely. See
// shared/protocol/README.md's Asynchronous IDENTIFY Hydration section.
class IdentifyHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setJwtVerifier(const auth::JwtVerifier* jwt_verifier);
    void setRevocationCache(const auth::RevocationCache* revocation_cache);
    void setGuildManager(const guild::GuildManager* guild_manager);

    Message handle(const Message& message, int fd);

  private:
    session::SessionManager* session_manager_ = nullptr;
    const auth::JwtVerifier* jwt_verifier_ = nullptr;
    const auth::RevocationCache* revocation_cache_ = nullptr;
    const guild::GuildManager* guild_manager_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
