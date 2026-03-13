#ifndef CIG_NEXUS_SESSION_SESSION_MANAGER_HPP
#define CIG_NEXUS_SESSION_SESSION_MANAGER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>

#include "session/Session.hpp"

namespace session {

class SessionManager {
public:
    Session& createSession(int socket_fd);
    void removeSession(int socket_fd);
    
    bool hasSession(int socket_fd) const noexcept {
        return sessions_.find(socket_fd) != sessions_.end();
    }

    Session* getSession(int socket_fd);
    const Session* getSession(int socket_fd) const;
    
    Session* getSessionByUserId(const std::string& user_id);
    const Session* getSessionByUserId(const std::string& user_id) const;

    bool updateUsername(int socket_fd, const std::string& username);

private:
    std::unordered_map<int, Session> sessions_;

    std::uint64_t next_session_id_ = 1;
    std::uint64_t next_user_id_ = 1;
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_MANAGER_HPP
