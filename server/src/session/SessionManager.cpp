#include "session/SessionManager.hpp"
#include <algorithm>
#include <chrono>

namespace session {

Session& SessionManager::createSession(int socket_fd) {
    const auto now = std::chrono::system_clock::now();
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // user_id/username/discord_id/app_session_id start empty and are filled
    // in by IdentifyHandler from the verified access JWT's claims — this
    // function only establishes the local per-connection bookkeeping.
    Session session{"s_" + std::to_string(next_session_id_++),
                    "",
                    "",
                    "",
                    "",
                    static_cast<uint64_t>(timestamp),
                    socket_fd,
                    {},
                    "",
                    {},
                    {},
                    std::nullopt,
                    std::nullopt};

    auto [it, inserted] = sessions_.insert_or_assign(socket_fd, session);

    return it->second;
}

void SessionManager::removeSession(int socket_fd) {
    sessions_.erase(socket_fd);
}

Session* SessionManager::getSession(int socket_fd) {
    const auto it = sessions_.find(socket_fd);
    if (it == sessions_.end()) {
        return nullptr;
    }

    return &it->second;
}

const Session* SessionManager::getSession(int socket_fd) const {
    const auto it = sessions_.find(socket_fd);
    if (it == sessions_.end()) {
        return nullptr;
    }

    return &it->second;
}

bool SessionManager::updateUsername(int socket_fd, const std::string& username) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return false;
    }

    session->username = username;
    return true;
}

Session* SessionManager::getSessionByUserId(const std::string& user_id) {
    for (auto& [fd, session] : sessions_) {
        if (session.user_id == user_id) {
            return &session;
        }
    }
    return nullptr;
}

const Session* SessionManager::getSessionByUserId(const std::string& user_id) const {
    for (const auto& [fd, session] : sessions_) {
        if (session.user_id == user_id) {
            return &session;
        }
    }
    return nullptr;
}

void SessionManager::addGuildMembership(int socket_fd, const std::string& guild_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    auto& ids = session->guild_ids;
    if (std::find(ids.begin(), ids.end(), guild_id) == ids.end()) {
        ids.push_back(guild_id);
    }
}

void SessionManager::removeGuildMembership(int socket_fd, const std::string& guild_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    auto& ids = session->guild_ids;
    ids.erase(std::remove(ids.begin(), ids.end(), guild_id), ids.end());
}

void SessionManager::addBlockedUser(int socket_fd, const std::string& blocked_user_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    auto& ids = session->blocked_user_ids;
    if (std::find(ids.begin(), ids.end(), blocked_user_id) == ids.end()) {
        ids.push_back(blocked_user_id);
    }
}

void SessionManager::removeBlockedUser(int socket_fd, const std::string& blocked_user_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    auto& ids = session->blocked_user_ids;
    ids.erase(std::remove(ids.begin(), ids.end(), blocked_user_id), ids.end());
}

void SessionManager::addFriend(int socket_fd, const std::string& friend_user_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    auto& ids = session->friend_ids;
    if (std::find(ids.begin(), ids.end(), friend_user_id) == ids.end()) {
        ids.push_back(friend_user_id);
    }
}

void SessionManager::removeFriend(int socket_fd, const std::string& friend_user_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    auto& ids = session->friend_ids;
    ids.erase(std::remove(ids.begin(), ids.end(), friend_user_id), ids.end());
}

bool SessionManager::isMemberOfGuild(int socket_fd, const std::string& guild_id) const {
    const Session* session = getSession(socket_fd);
    if (!session) {
        return false;
    }

    const auto& ids = session->guild_ids;
    return std::find(ids.begin(), ids.end(), guild_id) != ids.end();
}

void SessionManager::setActiveChannel(int socket_fd, const std::string& channel_id) {
    Session* session = getSession(socket_fd);
    if (!session) {
        return;
    }

    session->active_channel_id = channel_id;
}

void SessionManager::clearActiveChannel(int socket_fd) {
    setActiveChannel(socket_fd, "");
}

std::vector<int> SessionManager::getFdsInGuild(const std::string& guild_id) const {
    std::vector<int> fds;
    for (const auto& [fd, session] : sessions_) {
        const auto& ids = session.guild_ids;
        if (std::find(ids.begin(), ids.end(), guild_id) != ids.end()) {
            fds.push_back(fd);
        }
    }
    return fds;
}

std::vector<int> SessionManager::getFdsWithActiveChannel(const std::string& channel_id) const {
    std::vector<int> fds;
    for (const auto& [fd, session] : sessions_) {
        if (session.active_channel_id == channel_id) {
            fds.push_back(fd);
        }
    }
    return fds;
}

std::vector<int> SessionManager::getFdsForUser(const std::string& user_id) const {
    std::vector<int> fds;
    for (const auto& [fd, session] : sessions_) {
        if (session.user_id == user_id) {
            fds.push_back(fd);
        }
    }
    return fds;
}

void SessionManager::purgeGuildMembership(const std::string& guild_id,
                                          const std::vector<std::string>& channel_ids) {
    for (auto& [fd, session] : sessions_) {
        auto& ids = session.guild_ids;
        ids.erase(std::remove(ids.begin(), ids.end(), guild_id), ids.end());

        const auto& deleted = channel_ids;
        if (std::find(deleted.begin(), deleted.end(), session.active_channel_id) != deleted.end()) {
            session.active_channel_id.clear();
        }
    }
}

void SessionManager::clearActiveChannelEverywhere(const std::string& channel_id) {
    for (auto& [fd, session] : sessions_) {
        if (session.active_channel_id == channel_id) {
            session.active_channel_id.clear();
        }
    }
}

bool SessionManager::incrementPresence(const std::string& user_id) {
    const int count = ++presence_counts_[user_id];
    return count == 1;
}

bool SessionManager::decrementPresence(const std::string& user_id) {
    const auto it = presence_counts_.find(user_id);
    if (it == presence_counts_.end()) {
        return false; // defensive: decrement without a matching increment
    }

    if (--it->second <= 0) {
        presence_counts_.erase(it);
        return true;
    }
    return false;
}

bool SessionManager::isOnline(const std::string& user_id) const {
    return presence_counts_.find(user_id) != presence_counts_.end();
}

} // namespace session
