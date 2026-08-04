#ifndef CIG_NEXUS_PROTOCOL_HANDLERS_DM_HANDLER_HPP
#define CIG_NEXUS_PROTOCOL_HANDLERS_DM_HANDLER_HPP

#include "protocol/Message.hpp"

#include <atomic>
#include <optional>

namespace session {
class SessionManager;
struct Session;
} // namespace session

namespace http {
class InternalApiClient;
}

namespace persistence {
class MessagePersistenceWorker;
}

namespace protocol {

// Direct messages: DM_SEND, FETCH_HISTORY's peer_id branch (dispatched
// here from Server.cpp based on payload shape — see
// docs/social/friends-dms-design.md §3.5), LIST_DM_CONVERSATIONS. See §3
// for the full design, including §3.3's canSendDm resolution.
class DMHandler {
  public:
    void setSessionManager(session::SessionManager* session_manager);
    void setInternalApiClient(http::InternalApiClient* internal_api_client);
    void setMessagePersistenceWorker(persistence::MessagePersistenceWorker* worker);

    // §3.4: seeds the counter from the durable high-water mark at startup,
    // instead of always starting at 0 — same treatment as
    // ChannelHandler::seedMessageCounter.
    void seedMessageCounter(std::optional<int> last_seq);

    Message handleDmSend(const Message& message, int fd) const;
    Message handleFetchHistory(const Message& message, int fd) const;
    Message handleListDmConversations(const Message& message, int fd) const;

  private:
    static Message makeError(const std::string& code, const std::string& msg);
    const session::Session* requireIdentified(int fd) const;

    // §3.2/§3.3: friends OR shared guild, AND NOT blocked (either
    // direction) — re-checked fresh on every call, never cached beyond
    // the in-memory Session state already hydrated at IDENTIFY and kept
    // live. Falls back to a live internal API call only for the guild-
    // membership and block checks' peer-side half, and only when the peer
    // has zero active connections (§3.3's resolved live-fallback).
    bool canSendDm(const session::Session* session, const std::string& peer_user_id) const;

    session::SessionManager* session_manager_ = nullptr;
    http::InternalApiClient* internal_api_client_ = nullptr;
    persistence::MessagePersistenceWorker* message_worker_ = nullptr;

    // §3.4: one counter shared across every DM conversation (not
    // per-conversation), mirroring ChannelHandler::message_counter_'s
    // "shared, not per-entity" shape exactly — a third, separate id-space
    // from the lobby's and every channel's.
    mutable std::atomic<int> message_counter_{0};
};

} // namespace protocol

#endif // CIG_NEXUS_PROTOCOL_HANDLERS_DM_HANDLER_HPP
