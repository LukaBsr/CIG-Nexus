#include "Server.hpp"
#include "protocol/MessageBuilders.hpp"
#include "protocol/MessageParser.hpp"
#include "util/DebugFlags.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

// design doc §9: "polls on a short interval (e.g. 30s)".
constexpr std::chrono::seconds kRevocationPollInterval{30};

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
                    sendMessage(
                        fd, protocol::make_error_message("PROTOCOL_VIOLATION",
                                                         "Unknown message type: " + message.type));
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

void Server::stop() {
    running_ = false;
}

uint16_t Server::bound_port() const {
    return listener_.bound_port();
}
