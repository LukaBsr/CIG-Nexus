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

namespace http {
class InternalApiClient;
}

namespace protocol {

// design doc §8: IDENTIFY no longer takes a client-chosen username — it
// takes a session_token (the access JWT from §6), and identity (user_id,
// username, discord_id) is derived entirely from its verified claims.
class IdentifyHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setJwtVerifier(const auth::JwtVerifier* jwt_verifier);
    void setRevocationCache(const auth::RevocationCache* revocation_cache);
    void setGuildManager(const guild::GuildManager* guild_manager);
    // docs/social/friends-dms-design.md §3.3: unlike guild_ids (a pure
    // in-memory GuildManager lookup), blocked_user_ids has no equivalent
    // process-wide cache — hydrating it needs one live internal API call
    // per IDENTIFY. Optional the same way internal_api_client_ is
    // elsewhere: unset means blocked_user_ids just stays empty, not an
    // IDENTIFY failure.
    void setInternalApiClient(http::InternalApiClient* internal_api_client);

    Message handle(const Message& message, int fd);

  private:
    session::SessionManager* session_manager_ = nullptr;
    const auth::JwtVerifier* jwt_verifier_ = nullptr;
    const auth::RevocationCache* revocation_cache_ = nullptr;
    const guild::GuildManager* guild_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
