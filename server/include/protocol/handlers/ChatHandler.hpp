#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_CHAT_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_CHAT_HANDLER_HPP

#include "protocol/Message.hpp"

#include <atomic>
#include <optional>

namespace session {
class SessionManager;
}

namespace persistence {
class MessagePersistenceWorker;
}

namespace protocol {

class ChatHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setMessagePersistenceWorker(persistence::MessagePersistenceWorker* worker);

    // docs/social-presence-design.md §4.3: seeds the counter from the
    // durable high-water mark at startup, instead of always starting at 0.
    // std::nullopt (nothing ever persisted yet) leaves it at the default.
    void seedMessageCounter(std::optional<int> last_seq);

    Message handle(const Message& message, int fd) const;

  private:
    session::SessionManager* session_manager_ = nullptr;
    persistence::MessagePersistenceWorker* message_worker_ = nullptr;
    mutable std::atomic<int> message_counter_{0};
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_CHAT_HANDLER_HPP
