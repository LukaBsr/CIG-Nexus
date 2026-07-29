#ifndef CIG_NEXUS_SESSION_SESSION_HPP
#define CIG_NEXUS_SESSION_SESSION_HPP

#include <string>
#include <vector>

namespace session {

struct Session {
    std::string session_id; // local per-connection id, unrelated to Postgres
    std::string user_id;
    std::string username;
    std::string discord_id;     // from the access JWT's discord_id claim (design doc §6/§8)
    std::string app_session_id; // from the JWT's sid claim (Postgres sessions.id) — what the
                                // periodic revocation sweep checks against auth::RevocationCache
    uint64_t connected_at;
    int socket_fd;
    std::vector<std::string> guild_ids; // guilds this connection is a member of
    std::string active_channel_id;      // "" means no active channel
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_HPP
