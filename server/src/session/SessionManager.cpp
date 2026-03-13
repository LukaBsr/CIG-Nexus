#include "session/SessionManager.hpp"
#include <chrono>

namespace session {

Session& SessionManager::createSession(int socket_fd) {
    const auto now = std::chrono::system_clock::now();
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()
        ).count();

    Session session{
        "s_" + std::to_string(next_session_id_++),
        "u_" + std::to_string(next_user_id_++),
        "",
        static_cast<uint64_t>(timestamp),
        socket_fd
    };

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

} // namespace session
