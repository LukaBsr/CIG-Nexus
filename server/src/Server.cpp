#include "Server.hpp"
#include "protocol/MessageBuilders.hpp"
#include "protocol/MessageParser.hpp"
#include "util/DebugFlags.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace {

// design doc §9: "polls on a short interval (e.g. 30s)".
constexpr std::chrono::seconds kRevocationPollInterval{30};

bool send_all(int fd, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < size) {
        // MSG_NOSIGNAL: a peer that RST'd the connection would otherwise
        // raise SIGPIPE on this send, and the process has no handler for
        // it — default disposition is termination. Broadcast/targeted
        // delivery routinely writes to fds that may have gone stale since
        // their membership snapshot was taken, so this can't be "just
        // don't do that"; the failure has to be a normal `false` return.
        const ssize_t sent = ::send(fd, bytes + total_sent, size - total_sent, MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;
        }

        total_sent += static_cast<size_t>(sent);
    }

    return true;
}

} // namespace

Server::Server(uint16_t port) : port_(port), running_(false), listener_(port) {

    chat_handler_.setSessionManager(&session_manager_);
    identify_handler_.setSessionManager(&session_manager_);
    // RevocationCache needs no PEM/config, so it's always safe to wire in —
    // unlike JwtVerifier (configureAuth()) and InternalApiClient
    // (setInternalApiClient()), which are constructed with runtime config
    // that isn't available yet at Server construction time.
    identify_handler_.setRevocationCache(&revocation_cache_);
    // docs/guilds/social-presence-design.md §3.4/§1.10: hydrates Session.guild_ids
    // on successful IDENTIFY from GuildManager's membership index.
    identify_handler_.setGuildManager(&guild_manager_);
    guild_handler_.setSessionManager(&session_manager_);
    guild_handler_.setGuildManager(&guild_manager_);
    channel_handler_.setSessionManager(&session_manager_);
    channel_handler_.setGuildManager(&guild_manager_);
    invite_handler_.setSessionManager(&session_manager_);
    invite_handler_.setGuildManager(&guild_manager_);
    join_request_handler_.setSessionManager(&session_manager_);
    join_request_handler_.setGuildManager(&guild_manager_);
    friend_handler_.setSessionManager(&session_manager_);
    block_handler_.setSessionManager(&session_manager_);
    dm_handler_.setSessionManager(&session_manager_);

    // shared/protocol/README.md's Rate Limits table — every handler with a
    // limited message type shares the one process-wide RateLimiter.
    chat_handler_.setRateLimiter(&rate_limiter_);
    channel_handler_.setRateLimiter(&rate_limiter_);
    dm_handler_.setRateLimiter(&rate_limiter_);
    guild_handler_.setRateLimiter(&rate_limiter_);
    invite_handler_.setRateLimiter(&rate_limiter_);
    join_request_handler_.setRateLimiter(&rate_limiter_);
    friend_handler_.setRateLimiter(&rate_limiter_);

    registerRoutes();
}

void Server::configureAuth(const std::string& jwt_public_key_pem) {
    jwt_verifier_.emplace(jwt_public_key_pem);
    identify_handler_.setJwtVerifier(&*jwt_verifier_);
}

void Server::setInternalApiClient(std::unique_ptr<http::InternalApiClient> client) {
    internal_api_client_ = std::move(client);
    guild_handler_.setInternalApiClient(internal_api_client_.get());
    channel_handler_.setInternalApiClient(internal_api_client_.get());
    invite_handler_.setInternalApiClient(internal_api_client_.get());
    join_request_handler_.setInternalApiClient(internal_api_client_.get());
    friend_handler_.setInternalApiClient(internal_api_client_.get());
    block_handler_.setInternalApiClient(internal_api_client_.get());
    dm_handler_.setInternalApiClient(internal_api_client_.get());

    // docs/guilds/social-presence-design.md §4.5: constructed here (not at Server
    // construction) because it needs internal_api_client_.get(), which
    // isn't available yet at that point. Started in start(), stopped
    // explicitly at the end of start()'s loop.
    message_worker_ =
        std::make_unique<persistence::MessagePersistenceWorker>(internal_api_client_.get());
    chat_handler_.setMessagePersistenceWorker(message_worker_.get());
    channel_handler_.setMessagePersistenceWorker(message_worker_.get());
    dm_handler_.setMessagePersistenceWorker(message_worker_.get());

    // IDENTIFY hardening (B2): same constructed-here-not-at-Server-
    // construction reasoning as message_worker_ above. IdentifyHandler no
    // longer holds an InternalApiClient at all — this worker (and
    // processHydrationResults(), which drains it) fully own the
    // post-IDENTIFY load instead.
    hydration_worker_ = std::make_unique<session::SessionHydrationWorker>(
        internal_api_client_.get(), hydration_retry_delays_);
}

void Server::setHydrationRetryDelaysForTesting(std::vector<std::chrono::milliseconds> delays) {
    hydration_retry_delays_ = std::move(delays);
}

void Server::hydrateGuildCatalog() {
    if (!internal_api_client_) {
        return;
    }

    const std::optional<http::Catalog> catalog = internal_api_client_->fetchCatalog();
    if (!catalog) {
        std::cerr << "Failed to fetch initial guild/channel catalog from internal API" << std::endl;
        return;
    }

    for (const auto& g : catalog->guilds) {
        const guild::GuildVisibility visibility =
            guild::guildVisibilityFromString(g.visibility).value_or(guild::GuildVisibility::OPEN);
        guild_manager_.upsertGuild(g.guild_id, g.name, g.owner_id, visibility);
    }
    for (const auto& c : catalog->channels) {
        const guild::ChannelType type =
            c.channel_type == "VOICE" ? guild::ChannelType::VOICE : guild::ChannelType::TEXT;
        guild_manager_.upsertChannel(c.channel_id, c.guild_id, c.name, type);
    }
    // Delivery eligibility (who receives a BROADCAST/TARGETED message) stays
    // per-connection SessionManager state established via JOIN_GUILD, not
    // hydrated from the catalog (design doc §8.1) — catalog->memberships is
    // NOT used to populate that. It IS consulted here for role_rank
    // (docs/guilds/social-presence-design.md §2.2/§2.3): a minimal predicate cache,
    // not the delivery-eligibility roster, and not the full LIST_MEMBERS
    // roster either (that stays a live read, §2.3).
    for (const auto& m : catalog->memberships) {
        guild_manager_.setMemberRank(m.guild_id, m.user_id, m.role_rank);
    }

    std::cout << "Hydrated guild catalog: " << catalog->guilds.size() << " guild(s), "
              << catalog->channels.size() << " channel(s)" << std::endl;
}

void Server::hydrateMessageSequences() {
    if (!internal_api_client_) {
        return;
    }

    const http::LastSequence last_seq = internal_api_client_->fetchLastSequence();
    chat_handler_.seedMessageCounter(last_seq.lobby_seq);
    channel_handler_.seedMessageCounter(last_seq.channel_seq);
    dm_handler_.seedMessageCounter(last_seq.dm_seq);

    std::cout << "Hydrated message sequence counters (lobby=" << last_seq.lobby_seq.value_or(0)
              << ", channel=" << last_seq.channel_seq.value_or(0)
              << ", dm=" << last_seq.dm_seq.value_or(0) << ")" << std::endl;
}

void Server::pollRevocationCache() {
    if (!internal_api_client_) {
        return;
    }

    std::string as_of;
    const std::vector<std::string> revoked =
        internal_api_client_->fetchRevokedSessionIds(revocation_poll_as_of_, as_of);
    if (!revoked.empty()) {
        revocation_cache_.merge(revoked);
    }
    if (!as_of.empty()) {
        revocation_poll_as_of_ = as_of;
    }

    disconnectRevokedSessions();
}

void Server::disconnectRevokedSessions() {
    for (auto it = connections_.begin(); it != connections_.end();) {
        const int fd = it->first;
        const session::Session* session = session_manager_.getSession(fd);

        if (session && !session->app_session_id.empty() &&
            revocation_cache_.isRevoked(session->app_session_id)) {
            std::cout << "Disconnecting revoked session (fd=" << fd << ")" << std::endl;
            removeSessionTrackingPresence(fd);
            it = connections_.erase(it);
            continue;
        }
        ++it;
    }
}

protocol::Message Server::makePresenceUpdate(const std::string& user_id, bool online) const {
    protocol::Message presence;
    presence.type = "PRESENCE_UPDATE";
    presence.scope = protocol::Scope::BROADCAST;
    presence.payload = nlohmann::json{{"type", "PRESENCE_UPDATE"},
                                      {"user_id", user_id},
                                      {"status", online ? "online" : "offline"}};
    return presence;
}

std::vector<int>
Server::computePresenceExclusionFds(const std::vector<std::string>& blocked_user_ids) const {
    std::vector<int> excluded;
    for (const auto& blocked_user_id : blocked_user_ids) {
        const std::vector<int> fds = session_manager_.getFdsForUser(blocked_user_id);
        excluded.insert(excluded.end(), fds.begin(), fds.end());
    }
    return excluded;
}

void Server::removeSessionTrackingPresence(int fd) {
    const session::Session* session = session_manager_.getSession(fd);
    const std::string user_id = session ? session->user_id : std::string();
    // docs/social/friends-dms-design.md §2.5: captured *before*
    // removeSession() below erases this connection's Session — the
    // exclusion set still needs to reflect who this user had blocked.
    const std::vector<std::string> blocked_user_ids =
        session ? session->blocked_user_ids : std::vector<std::string>();
    // IDENTIFY hardening (B2): a session whose async hydration
    // (processHydrationResults()) never completed never incremented
    // presence either — decrementing here anyway wouldn't just be a
    // harmless no-op, it would wrongly erode a *different*,
    // already-online connection's real count for this same user_id (two
    // tabs, one still pending, one fully online: the pending tab
    // disconnecting must not make the online tab look offline). Checking
    // session_context_ready, not just "did this fd ever get a user_id,"
    // is what makes that distinction.
    const bool had_presence = session && session->session_context_ready;

    util::logPresenceDebug("removeSessionTrackingPresence fd=" + std::to_string(fd) +
                           " user_id=" + (user_id.empty() ? "(never identified)" : user_id) +
                           (had_presence ? "" : " (no matching presence increment)"));

    session_manager_.removeSession(fd);

    if (had_presence && session_manager_.decrementPresence(user_id)) {
        broadcastExcluding(makePresenceUpdate(user_id, false),
                           computePresenceExclusionFds(blocked_user_ids));
    }
}

void Server::processHydrationResults() {
    if (!hydration_worker_) {
        return;
    }

    for (const auto& result : hydration_worker_->drainResults()) {
        const auto conn_it = connections_.find(result.fd);
        if (conn_it == connections_.end()) {
            continue; // Connection already gone by the time this landed.
        }

        session::Session* session = session_manager_.getSession(result.fd);
        if (!session || session->app_session_id != result.app_session_id) {
            // fd reused by an unrelated connection/IDENTIFY since this job
            // was enqueued (docs/known-issues.md's fd-reuse-race idea) —
            // matching on app_session_id, not just fd, catches that case
            // and drops the stale result instead of misapplying it to the
            // wrong session.
            continue;
        }

        if (result.succeeded) {
            for (const auto& block : result.context.blocks) {
                session->blocked_user_ids.push_back(block.user_id);
            }
            for (const auto& f : result.context.friends) {
                session->friend_ids.push_back(f.user_id);
            }
            session->display_name = result.context.profile.display_name;
            session->avatar_url = result.context.profile.avatar_url;
            session->session_context_ready = true;

            util::logPresenceDebug("hydration complete fd=" + std::to_string(result.fd) +
                                   " user_id=" + session->user_id);
            if (session_manager_.incrementPresence(session->user_id)) {
                broadcastExcluding(makePresenceUpdate(session->user_id, true),
                                   computePresenceExclusionFds(session->blocked_user_ids));
            }
        } else {
            // shared/protocol/README.md's Asynchronous IDENTIFY Hydration
            // section: fail closed, not open — an unenforced block list is
            // worse than a dropped connection the client must reconnect
            // for.
            util::logPresenceDebug("hydration failed fd=" + std::to_string(result.fd) +
                                   " user_id=" + session->user_id + " -- disconnecting");

            protocol::Message error;
            error.type = "ERROR";
            error.scope = protocol::Scope::DIRECT;
            error.payload =
                protocol::make_error("SESSION_CONTEXT_UNAVAILABLE",
                                     "Could not load account context in time; please reconnect");
            sendMessage(result.fd, error);

            removeSessionTrackingPresence(result.fd); // invalidates `session` above
            connections_.erase(conn_it);
        }
    }
}

void Server::start() {
    if (running_) {
        throw std::runtime_error("Server is already running");
    }

    listener_.start();
    running_ = true;
    std::cout << "CIG Nexus Server starting on port " << port_ << std::endl;

    hydrateGuildCatalog();
    hydrateMessageSequences();
    if (message_worker_) {
        message_worker_->start();
    }
    if (hydration_worker_) {
        hydration_worker_->start();
    }
    // docs/security-audit.md §1.5: without this, every restart opens a
    // window of up to kRevocationPollInterval where a session revoked
    // before the restart is valid again, since revocation_cache_ starts
    // empty and only the *next* interval tick would repopulate it.
    // Mirrors hydrateGuildCatalog()'s immediate-call pattern above.
    pollRevocationCache();
    last_revocation_poll_ = std::chrono::steady_clock::now();

    while (running_) {
        int client_fd = listener_.accept();
        if (client_fd >= 0) {
            std::cout << "Client connected (fd=" << client_fd << ")" << std::endl;
            connections_.emplace(client_fd, std::make_unique<Connection>(client_fd));
        }

        for (auto it = connections_.begin(); it != connections_.end();) {
            auto& [fd, conn] = *it;

            if (!conn->readFromSocket()) {
                std::cout << "Client disconnected (fd=" << fd << ")" << std::endl;
                removeSessionTrackingPresence(fd);
                it = connections_.erase(it);
                continue;
            }

            auto frames = conn->pollFrames();
            for (const auto& frame : frames) {
                protocol::Message message;

                try {
                    message = protocol::parse_message(frame);
                } catch (const std::exception& e) {
                    std::cerr << "Parse error (fd=" << fd << "): " << e.what() << std::endl;
                    continue;
                }

                std::vector<protocol::Message> responses;
                try {
                    responses = dispatcher_.dispatch(message, fd);
                } catch (const std::exception& e) {
                    std::cerr << "Dispatch error (fd=" << fd << "): " << e.what() << std::endl;
                    protocol::Message error;
                    error.type = "ERROR";
                    error.scope = protocol::Scope::DIRECT;
                    error.payload = protocol::make_error("PROTOCOL_VIOLATION",
                                                         "Unknown message type: " + message.type);
                    sendMessage(fd, error);
                    continue;
                }

                // docs/guilds/social-presence-design.md §1.9/§6 step 5: a handler
                // may now return more than one Message (different
                // recipients, different payloads) — each is delivered
                // independently by its own scope, same switch as before,
                // just run once per element instead of once total.
                bool identified = false;
                for (const auto& response : responses) {
                    switch (response.scope) {
                    case protocol::Scope::BROADCAST:
                        broadcast(response);
                        break;

                    case protocol::Scope::DIRECT:
                        sendMessage(fd, response);
                        break;

                    case protocol::Scope::TARGETED:
                        for (int target_fd : response.target_fds) {
                            sendMessage(target_fd, response);
                        }
                        break;
                    }

                    if (response.type == "IDENTIFIED") {
                        identified = true;
                    }
                }

                // IDENTIFY hardening (B2): presence is no longer
                // incremented here — it's deferred until this connection's
                // async post-IDENTIFY load actually succeeds
                // (processHydrationResults(), below the connections_ loop),
                // since the PRESENCE_UPDATE broadcast needs a real
                // blocked_user_ids to exclude correctly, not the empty
                // default a freshly-created Session starts with. This is
                // also why removeSessionTrackingPresence() no longer
                // assumes "has a user_id" implies "incremented presence."
                if (message.type == "IDENTIFY" && identified) {
                    session::Session* session = session_manager_.getSession(fd);
                    if (session && hydration_worker_) {
                        util::logPresenceDebug("identify success fd=" + std::to_string(fd) +
                                               " user_id=" + session->user_id +
                                               " (hydration enqueued)");
                        hydration_worker_->enqueue({fd, session->user_id, session->app_session_id});
                    } else if (session) {
                        // No internal API client configured at all (unlike
                        // "configured but the load is still in flight") —
                        // there is nothing to wait for, so there is nothing
                        // to gate on. Proceed exactly as IDENTIFY did before
                        // B2: vacuously ready, presence increments
                        // immediately. This is the path every test that
                        // exercises IDENTIFY without setInternalApiClient()
                        // takes, same as it always has.
                        session->session_context_ready = true;
                        util::logPresenceDebug("identify success fd=" + std::to_string(fd) +
                                               " user_id=" + session->user_id +
                                               " (no internal API client; proceeding "
                                               "synchronously)");
                        if (session_manager_.incrementPresence(session->user_id)) {
                            broadcastExcluding(
                                makePresenceUpdate(session->user_id, true),
                                computePresenceExclusionFds(session->blocked_user_ids));
                        }
                    }
                }
            }
            ++it;
        }

        processHydrationResults();

        // design doc §9: pull-based revocation cache, polled on an interval
        // rather than a network call per IDENTIFY. Also sweeps already-
        // connected sessions so an explicit revocation disconnects them
        // within one poll interval, not just blocks *new* identifies.
        const auto now = std::chrono::steady_clock::now();
        if (now - last_revocation_poll_ >= kRevocationPollInterval) {
            pollRevocationCache();
            last_revocation_poll_ = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (message_worker_) {
        message_worker_->stop();
    }
    if (hydration_worker_) {
        hydration_worker_->stop();
    }

    std::cout << "CIG Nexus Server stopped" << std::endl;
}

bool Server::sendMessage(int fd, const protocol::Message& message) {
    const std::string payload = message.payload.dump();
    const uint32_t frame_size = htonl(static_cast<uint32_t>(payload.size()));

    if (!send_all(fd, &frame_size, sizeof(frame_size))) {
        return false;
    }

    if (!send_all(fd, payload.data(), payload.size())) {
        return false;
    }

    return true;
}

void Server::broadcast(const protocol::Message& message) {
    for (const auto& [fd, conn] : connections_) {
        (void)conn;
        if (!session_manager_.hasSession(fd)) {
            continue;
        }
        (void)sendMessage(fd, message);
    }
}

void Server::broadcastExcluding(const protocol::Message& message,
                                const std::vector<int>& excluded_fds) {
    if (excluded_fds.empty()) {
        broadcast(message);
        return;
    }

    for (const auto& [fd, conn] : connections_) {
        (void)conn;
        if (!session_manager_.hasSession(fd)) {
            continue;
        }
        if (std::find(excluded_fds.begin(), excluded_fds.end(), fd) != excluded_fds.end()) {
            continue;
        }
        (void)sendMessage(fd, message);
    }
}

void Server::stop() {
    running_ = false;
}

uint16_t Server::bound_port() const {
    return listener_.bound_port();
}
