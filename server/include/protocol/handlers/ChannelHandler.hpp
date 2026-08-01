#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_CHANNEL_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_CHANNEL_HANDLER_HPP

#include "protocol/Message.hpp"

#include <atomic>
#include <optional>

namespace session {
class SessionManager;
struct Session;
} // namespace session

namespace guild {
class GuildManager;
}

namespace http {
class InternalApiClient;
}

namespace persistence {
class MessagePersistenceWorker;
}

namespace protocol {

// Channel lifecycle and messaging: LIST_CHANNELS, CREATE_CHANNEL,
// DELETE_CHANNEL, JOIN_CHANNEL, LEAVE_CHANNEL, CHANNEL_MESSAGE,
// FETCH_HISTORY. See docs/guilds/design.md for the guild/channel protocol
// shapes and docs/social-presence-design.md §4 for FETCH_HISTORY.
// CREATE_CHANNEL/DELETE_CHANNEL go through InternalApiClient first (design
// doc §8.1, write-through cache) — LIST_CHANNELS, JOIN/LEAVE_CHANNEL, and
// CHANNEL_MESSAGE's broadcast are cache reads or purely per-connection
// SessionManager state; FETCH_HISTORY is the exception (§4.4) — a live
// read-through internal API call, never cached here.
class ChannelHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);
    void setMessagePersistenceWorker(persistence::MessagePersistenceWorker* worker);

    // docs/social-presence-design.md §4.3: seeds the counter from the
    // durable high-water mark at startup, instead of always starting at 0.
    void seedMessageCounter(std::optional<int> last_seq);

    Message handleListChannels(const Message& message, int fd) const;
    Message handleCreateChannel(const Message& message, int fd) const;
    Message handleDeleteChannel(const Message& message, int fd) const;
    Message handleJoinChannel(const Message& message, int fd) const;
    Message handleLeaveChannel(const Message& message, int fd) const;
    Message handleChannelMessage(const Message& message, int fd) const;
    Message handleFetchHistory(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);

    // Returns nullptr (caller returns NOT_IDENTIFIED) if fd has no session
    // or hasn't completed IDENTIFY yet.
    const session::Session* requireIdentified(int fd) const;

    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
    persistence::MessagePersistenceWorker* message_worker_ = nullptr;

    // Same reasoning as ChatHandler::message_counter_ and GuildManager's id
    // counters: single-threaded today, std::atomic removes a landmine for
    // whenever that stops being true. Separate counter space from
    // CHAT_MESSAGE's — message_id only needs to be unique within a message
    // kind, matching existing convention. One counter shared across every
    // channel (not per-channel) — matches the persisted schema's two
    // id-spaces (docs/social-presence-design.md §4.2).
    mutable std::atomic<int> message_counter_{0};
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_CHANNEL_HANDLER_HPP
