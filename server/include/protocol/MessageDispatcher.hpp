#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_DISPATCHER_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_DISPATCHER_HPP

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Message.hpp"

namespace protocol {

// docs/social-presence-design.md §1.9's ARBITRATION: widened from a single
// Message to a vector so a handler can notify two different recipients with
// two different payloads from one client action (e.g. a future
// APPROVE_JOIN_REQUEST: "done" to the approver, "you're in" to the approved
// user) — something no single Scope/target_fds combination can express.
// Every handler registered today still returns exactly one Message; this is
// the mechanism landing alone, before anything needs the extra element.
class MessageDispatcher {
  public:
    using Handler = std::function<std::vector<Message>(const Message&, int)>;

    void registerHandler(const std::string& type, Handler handler);
    std::vector<Message> dispatch(const Message& message, int fd) const;

  private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_DISPATCHER_HPP
