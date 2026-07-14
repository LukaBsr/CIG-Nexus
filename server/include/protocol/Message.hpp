#ifndef CIG_NEXUS_PROTOCOL_MESSAGE_HPP
#define CIG_NEXUS_PROTOCOL_MESSAGE_HPP

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace protocol {

// TARGETED delivers to an explicit fd list rather than "everyone" (BROADCAST)
// or "just the sender" (DIRECT). The handler that builds the response owns
// the domain knowledge to compute that list (e.g. "members of this guild"),
// so it populates target_fds; Server just iterates it. This keeps guild/
// channel membership logic out of Server, which otherwise only knows about
// connection I/O and dispatch.
enum class Scope { DIRECT, BROADCAST, TARGETED };

struct Message {
    std::string type;
    nlohmann::json payload;
    Scope scope = Scope::DIRECT;
    std::vector<int> target_fds; // populated by the handler when scope == TARGETED
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_MESSAGE_HPP
