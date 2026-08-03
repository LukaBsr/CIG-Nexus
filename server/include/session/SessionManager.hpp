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

    // Guild/channel membership (see docs/guilds/design.md). SessionManager owns
    // this because it's per-connection state, the same way username is.
    void addGuildMembership(int socket_fd, const std::string& guild_id);
    void removeGuildMembership(int socket_fd, const std::string& guild_id);
    bool isMemberOfGuild(int socket_fd, const std::string& guild_id) const;

    void setActiveChannel(int socket_fd, const std::string& channel_id);
    void clearActiveChannel(int socket_fd);

    // docs/guilds/social-presence-design.md §3.1: presence is derived from
    // connection count, not "does a Session exist for this fd" — a user can
    // have multiple simultaneous connections (multiple tabs/devices), and a
    // second/third one connecting or closing must not flicker online/offline
    // when nothing about the user's actual presence changed. Callers use the
    // returned bool to decide whether to broadcast PRESENCE_UPDATE — this
    // class only tracks the count, it never sends anything itself.
    // Returns true iff this call caused a 0->1 transition (just came online).
    bool incrementPresence(const std::string& user_id);
    // Returns true iff this call caused a 1->0 transition (just went offline).
    bool decrementPresence(const std::string& user_id);
    bool isOnline(const std::string& user_id) const;

    // Recipient lists for Scope::TARGETED delivery.
    std::vector<int> getFdsInGuild(const std::string& guild_id) const;
    std::vector<int> getFdsWithActiveChannel(const std::string& channel_id) const;
    // docs/guilds/social-presence-design.md §1.9: every fd currently identified as
    // user_id (handles multiple tabs/devices, same reasoning as presence's
    // connection-count tracking, §3.1). Needed for APPROVE_JOIN_REQUEST to
    // reach the approved user's connection(s) even though they're not yet
    // reflected as a guild member in anyone's Session state. May be empty —
    // Scope::TARGETED with an empty target_fds is already a no-op elsewhere.
    std::vector<int> getFdsForUser(const std::string& user_id) const;

    // Cascading cleanup for guild/channel deletion.
    void purgeGuildMembership(const std::string& guild_id,
                              const std::vector<std::string>& channel_ids);
    void clearActiveChannelEverywhere(const std::string& channel_id);

  private:
    std::unordered_map<int, Session> sessions_;
    // user_id -> number of currently-open connections identified as that user.
    std::unordered_map<std::string, int> presence_counts_;

    // user_id is no longer generated here — it comes from the verified
    // access JWT's `sub` claim (design doc §8), set by IdentifyHandler
    // after createSession() the same way it already sets username.
    std::uint64_t next_session_id_ = 1;
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_MANAGER_HPP
