#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP

#include "protocol/Message.hpp"

namespace session {
    class SessionManager;
}

namespace protocol {

class IdentifyHandler {
public:
    void setSessionManager(session::SessionManager* session_manager);
    Message handle(const Message& message, int fd);

private:
    session::SessionManager* session_manager_ = nullptr;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_IDENTIFY_HANDLER_HPP
