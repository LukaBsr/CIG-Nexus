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
    // docs/social/friends-dms-design.md §3.3: users this connection's own
    // account has blocked — hydrated at IDENTIFY from the durable block
    // list (mirrors guild_ids), updated live on BLOCK_USER/UNBLOCK_USER.
    // Consulted by presence delivery (§2.5) to exclude blocked users'
    // connections from this user's PRESENCE_UPDATE broadcasts.
    std::vector<std::string> blocked_user_ids;
    // docs/social/friends-dms-design.md §3.3: this connection's own
    // account's friends — hydrated at IDENTIFY (mirrors guild_ids/
    // blocked_user_ids), updated live on FRIEND_ADDED/FRIEND_REMOVED.
    // Consulted by canSendDm() (DMHandler) as one of the two ways a DM is
    // permitted (friends OR shared guild).
    std::vector<std::string> friend_ids;
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_HPP
