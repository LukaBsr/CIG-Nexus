#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_CHANNEL_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_CHANNEL_HANDLER_HPP

#include "protocol/Message.hpp"

#include <atomic>

namespace session {
class SessionManager;
struct Session;
}

namespace guild {
class GuildManager;
}

namespace protocol {

// Channel lifecycle and messaging: LIST_CHANNELS, CREATE_CHANNEL,
// DELETE_CHANNEL, JOIN_CHANNEL, LEAVE_CHANNEL, CHANNEL_MESSAGE. See
// docs/rooms-spec.md for the full protocol shapes.
class ChannelHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setGuildManager(guild::GuildManager* guild_manager);

    Message handleListChannels(const Message& message, int fd) const;
    Message handleCreateChannel(const Message& message, int fd) const;
    Message handleDeleteChannel(const Message& message, int fd) const;
    Message handleJoinChannel(const Message& message, int fd) const;
    Message handleLeaveChannel(const Message& message, int fd) const;
    Message handleChannelMessage(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);

    // Returns nullptr (caller returns NOT_IDENTIFIED) if fd has no session
    // or hasn't completed IDENTIFY yet.
    const session::Session* requireIdentified(int fd) const;

    session::SessionManager* session_manager_ = nullptr;
    guild::GuildManager* guild_manager_ = nullptr;

    // Same reasoning as ChatHandler::message_counter_ and GuildManager's id
    // counters: single-threaded today, std::atomic removes a landmine for
    // whenever that stops being true. Separate counter space from
    // CHAT_MESSAGE's — message_id only needs to be unique within a message
    // kind, matching existing convention.
    mutable std::atomic<int> message_counter_{0};
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_CHANNEL_HANDLER_HPP
