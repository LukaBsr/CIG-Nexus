#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_HANDLER_SUPPORT_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_HANDLER_SUPPORT_HPP

#include "protocol/Message.hpp"
#include "protocol/MessageBuilders.hpp"
#include "session/SessionManager.hpp"

#include <string>

namespace protocol {

// Shared by every handler; found by unqualified lookup from inside a
// handler's member functions (all in namespace protocol).
inline Message makeError(const std::string& code, const std::string& msg) {
    return make_error_message(code, msg);
}

// Returns nullptr (caller returns NOT_IDENTIFIED) if fd has no session
// or hasn't completed IDENTIFY yet.
inline const session::Session* requireIdentified(const session::SessionManager* session_manager,
                                                 int fd) {
    if (!session_manager) {
        return nullptr;
    }

    const session::Session* session = session_manager->getSession(fd);
    if (!session || session->username.empty()) {
        return nullptr;
    }

    return session;
}

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_HANDLER_SUPPORT_HPP
