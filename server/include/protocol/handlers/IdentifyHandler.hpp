#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP

#include "protocol/Message.hpp"

namespace session {
class SessionManager;
}

namespace auth {
class JwtVerifier;
class RevocationCache;
} // namespace auth

namespace protocol {

// design doc §8: IDENTIFY no longer takes a client-chosen username — it
// takes a session_token (the access JWT from §6), and identity (user_id,
// username, discord_id) is derived entirely from its verified claims.
class IdentifyHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setJwtVerifier(const auth::JwtVerifier* jwt_verifier);
    void setRevocationCache(const auth::RevocationCache* revocation_cache);

    Message handle(const Message& message, int fd);

  private:
    session::SessionManager* session_manager_ = nullptr;
    const auth::JwtVerifier* jwt_verifier_ = nullptr;
    const auth::RevocationCache* revocation_cache_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
