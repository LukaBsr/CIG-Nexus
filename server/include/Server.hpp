#ifndef CIG_NEXUS_SERVER_HPP
#define CIG_NEXUS_SERVER_HPP

#include "Connection.hpp"
#include "TcpListener.hpp"

#include "protocol/MessageDispatcher.hpp"
#include "protocol/handlers/BlockHandler.hpp"
#include "protocol/handlers/ChannelHandler.hpp"
#include "protocol/handlers/ChatHandler.hpp"
#include "protocol/handlers/DMHandler.hpp"
#include "protocol/handlers/FriendHandler.hpp"
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
#include "session/SessionHydrationWorker.hpp"
#include "session/SessionManager.hpp"
#include "util/RateLimiter.hpp"

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

    // Test-only hook: overrides the ~60s-total default retry schedule
    // session::SessionHydrationWorker uses for the post-IDENTIFY load.
    // Must be called before setInternalApiClient(), since that's where the
    // worker is actually constructed — a call after that point has no
    // effect on the already-constructed worker.
    void setHydrationRetryDelaysForTesting(std::vector<std::chrono::milliseconds> delays);

    void start();
    void stop();
    uint16_t bound_port() const;

  private:
    // Network send helpers
    bool sendMessage(int fd, const protocol::Message& message);
    // Delivers to every *identified* connection only. A socket that hasn't
    // completed IDENTIFY has no authenticated session, so it must not see
    // lobby chat or presence (user ids, online status) either.
    void broadcast(const protocol::Message& message);
    // docs/social/friends-dms-design.md §2.5: broadcast() minus a set of
    // fds to skip — the mechanism presence delivery uses to exclude a
    // blocked user's connections. Same identified-only delivery rule.
    void broadcastExcluding(const protocol::Message& message, const std::vector<int>& excluded_fds);
    // Every fd currently identified as any user in blocked_user_ids — the
    // exclusion set for the presence-subject's own PRESENCE_UPDATE
    // broadcasts. Takes the list directly (not a user_id to look up)
    // because the offline call site must capture it *before*
    // SessionManager::removeSession() erases the subject's own Session.
    // Empty input returns empty output — the common case costs one
    // no-op loop, not a lookup.
    std::vector<int>
    computePresenceExclusionFds(const std::vector<std::string>& blocked_user_ids) const;

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
    // IDENTIFY hardening (B2): drains session::SessionHydrationWorker's
    // completed jobs and applies each to the still-matching session (fd +
    // app_session_id both still current — see SessionHydrationResult's
    // comment on why both). A successful result populates
    // blocked_user_ids/friend_ids/display_name/avatar_url and only then
    // performs the presence increment/broadcast this connection's
    // IDENTIFY deferred. A failed result (retry budget exhausted) sends
    // SESSION_CONTEXT_UNAVAILABLE and force-closes the connection, per
    // shared/protocol/README.md's Asynchronous IDENTIFY Hydration section
    // — called once per main-loop tick, unconditionally (unlike the
    // interval-gated revocation poll, draining an empty queue costs
    // nothing).
    void processHydrationResults();

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
    protocol::FriendHandler friend_handler_;
    protocol::BlockHandler block_handler_;
    protocol::DMHandler dm_handler_;

    // In-memory connection/session/guild state
    session::SessionManager session_manager_;
    guild::GuildManager guild_manager_;
    // shared/protocol/README.md's Rate Limits table — one process-wide
    // limiter, injected into every handler that needs it; see the
    // constructor. Same in-memory, reset-on-restart posture as
    // SessionManager/GuildManager.
    util::RateLimiter rate_limiter_;
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
    // Same ownership/lifetime reasoning as message_worker_ above, for
    // IDENTIFY hardening (B2)'s post-IDENTIFY hydration load.
    std::vector<std::chrono::milliseconds> hydration_retry_delays_ =
        session::SessionHydrationWorker::defaultRetryDelays();
    std::unique_ptr<session::SessionHydrationWorker> hydration_worker_;
};

#endif // CIG_NEXUS_SERVER_HPP
