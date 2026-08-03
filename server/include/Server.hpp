#ifndef CIG_NEXUS_SERVER_HPP
#define CIG_NEXUS_SERVER_HPP

#include "Connection.hpp"
#include "TcpListener.hpp"

#include "protocol/MessageDispatcher.hpp"
#include "protocol/handlers/ChannelHandler.hpp"
#include "protocol/handlers/ChatHandler.hpp"
#include "protocol/handlers/GuildHandler.hpp"
#include "protocol/handlers/HelloHandler.hpp"
#include "protocol/handlers/IdentifyHandler.hpp"
#include "protocol/handlers/InviteHandler.hpp"
#include "protocol/handlers/JoinRequestHandler.hpp"

#include "auth/JwtVerifier.hpp"
#include "auth/RevocationCache.hpp"
#include "guild/GuildManager.hpp"
#include "http/InternalApiClient.hpp"
#include "persistence/MessagePersistenceWorker.hpp"
#include "session/SessionManager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

class Server {
  public:
    explicit Server(uint16_t port);

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Constructs the RS256 JwtVerifier IdentifyHandler needs (design doc
    // §6/§8/§9). Must be called before start() for IDENTIFY to ever
    // succeed — without it, jwt_verifier_ stays unset and IdentifyHandler
    // returns INTERNAL_ERROR for every IDENTIFY, matching how an unset
    // GuildManager/InternalApiClient already behaves elsewhere.
    void configureAuth(const std::string& jwt_public_key_pem);

    // Takes ownership. Wires the client into GuildHandler/ChannelHandler
    // (design doc §8.1) and enables catalog hydration at start() and the
    // periodic revocation-cache poll (§9). Tests inject a
    // test_helpers::FakeInternalApiClient here instead of a real
    // CurlInternalApiClient.
    void setInternalApiClient(std::unique_ptr<http::InternalApiClient> client);

    void start();
    void stop();
    uint16_t bound_port() const;

  private:
    // Network send helpers
    bool sendMessage(int fd, const protocol::Message& message);
    void broadcast(const protocol::Message& message);

    // Startup catalog hydration and the periodic revocation poll/sweep
    // (design doc §8.1, §9) — no-ops if internal_api_client_ is unset.
    void hydrateGuildCatalog();
    void pollRevocationCache();
    void disconnectRevokedSessions();

    // docs/guilds/social-presence-design.md §4.3: seeds both handlers' message
    // counters from Postgres's durable high-water mark at startup — no-op
    // if internal_api_client_ is unset.
    void hydrateMessageSequences();

    // docs/guilds/social-presence-design.md §3.2/§3.3: presence transitions are
    // orchestrated here, not inside IdentifyHandler — this is the one place
    // that already owns both connect (via a successful IDENTIFY dispatch)
    // and disconnect (both the clean/RST readFromSocket() failure path and
    // the revocation sweep), and already has broadcast() available. A
    // handler could compute the transition but has no way to also emit a
    // second, unrelated broadcast message through today's one-Message-per-
    // dispatch return path (that's Step 5's job, and it's scoped to a
    // different problem — two *different* payloads to two different
    // audiences, not one extra uniform broadcast).
    protocol::Message makePresenceUpdate(const std::string& user_id, bool online) const;
    // Wraps SessionManager::removeSession() so every disconnect path
    // consistently checks the 1->0 presence transition — never call
    // session_manager_.removeSession() directly outside this.
    void removeSessionTrackingPresence(int fd);

    // Server runtime state
    uint16_t port_;
    std::atomic<bool> running_;
    TcpListener listener_;

    // Protocol dispatch and handlers
    protocol::MessageDispatcher dispatcher_;
    protocol::HelloHandler hello_handler_;
    protocol::ChatHandler chat_handler_;
    protocol::IdentifyHandler identify_handler_;
    protocol::GuildHandler guild_handler_;
    protocol::ChannelHandler channel_handler_;
    protocol::InviteHandler invite_handler_;
    protocol::JoinRequestHandler join_request_handler_;

    // In-memory connection/session/guild state
    session::SessionManager session_manager_;
    guild::GuildManager guild_manager_;
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;

    // Auth (design doc §6/§9) and the internal catalog API (§8.1) — both
    // optional/unset until configureAuth()/setInternalApiClient() are
    // called, so existing tests that only exercise unauthenticated
    // dispatch paths (e.g. HELLO/unknown-message-type) don't need either.
    std::optional<auth::JwtVerifier> jwt_verifier_;
    auth::RevocationCache revocation_cache_;
    std::unique_ptr<http::InternalApiClient> internal_api_client_;
    std::string revocation_poll_as_of_; // empty = "since the beginning" for the first poll
    std::chrono::steady_clock::time_point last_revocation_poll_;

    // Declared after internal_api_client_ so it destructs (and stops its
    // thread) first — it holds a raw pointer into internal_api_client_ and
    // must never outlive it (docs/guilds/social-presence-design.md §4.5).
    std::unique_ptr<persistence::MessagePersistenceWorker> message_worker_;
};

#endif // CIG_NEXUS_SERVER_HPP
