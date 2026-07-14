#ifndef CIG_NEXUS_SESSION_SESSION_MANAGER_HPP
#define CIG_NEXUS_SESSION_SESSION_MANAGER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Guild/channel membership (see docs/rooms-spec.md). SessionManager owns
    // this because it's per-connection state, the same way username is.
    void addGuildMembership(int socket_fd, const std::string& guild_id);
    void removeGuildMembership(int socket_fd, const std::string& guild_id);
    bool isMemberOfGuild(int socket_fd, const std::string& guild_id) const;

    void setActiveChannel(int socket_fd, const std::string& channel_id);
    void clearActiveChannel(int socket_fd);

    // Recipient lists for Scope::TARGETED delivery.
    std::vector<int> getFdsInGuild(const std::string& guild_id) const;
    std::vector<int> getFdsWithActiveChannel(const std::string& channel_id) const;

    // Cascading cleanup for guild/channel deletion.
    void purgeGuildMembership(const std::string& guild_id,
                              const std::vector<std::string>& channel_ids);
    void clearActiveChannelEverywhere(const std::string& channel_id);

  private:
    std::unordered_map<int, Session> sessions_;

    std::uint64_t next_session_id_ = 1;
    std::uint64_t next_user_id_ = 1;
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_MANAGER_HPP
