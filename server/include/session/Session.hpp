#ifndef CIG_NEXUS_SESSION_SESSION_HPP
#define CIG_NEXUS_SESSION_SESSION_HPP

#include <string>
#include <vector>

namespace session {

struct Session {
    std::string session_id;
    std::string user_id;
    std::string username;
    uint64_t connected_at;
    int socket_fd;
    std::vector<std::string> guild_ids; // guilds this connection is a member of
    std::string active_channel_id;      // "" means no active channel
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_HPP
