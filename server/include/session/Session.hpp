#ifndef CIG_NEXUS_SESSION_SESSION_HPP
#define CIG_NEXUS_SESSION_SESSION_HPP

#include <string>

namespace session {

struct Session {
    std::string session_id;
    std::string user_id;
    std::string username;
    uint64_t connected_at;
    int socket_fd;
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_HPP
