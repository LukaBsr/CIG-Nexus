#include "Server.hpp"
#include "protocol/MessageBuilders.hpp"
#include "protocol/MessageParser.hpp"

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

    // docs/guilds/social-presence-design.md §1.9/§6 step 5: most registrations below
    // wrap their handler's single Message in a one-element vector — the
    // dispatcher contract is std::vector<Message>, but only the handlers
    // that actually need to notify two different recipients with two
    // different payloads (JOIN_VIA_INVITE's application-mode diversion,
    // REQUEST_JOIN, APPROVE_JOIN_REQUEST, REJECT_JOIN_REQUEST) return the
    // vector directly instead of wrapping.
    dispatcher_.registerHandler("HELLO", [this](const protocol::Message& msg, int /*fd*/) {
        return std::vector<protocol::Message>{hello_handler_.handle(msg)};
    });

    dispatcher_.registerHandler("CHAT_MESSAGE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{chat_handler_.handle(msg, fd)};
    });

    dispatcher_.registerHandler("IDENTIFY", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{identify_handler_.handle(msg, fd)};
    });

    dispatcher_.registerHandler("CREATE_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleCreateGuild(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_GUILDS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleListGuilds(msg, fd)};
    });

    dispatcher_.registerHandler("JOIN_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleJoinGuild(msg, fd)};
    });

    dispatcher_.registerHandler("LEAVE_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleLeaveGuild(msg, fd)};
    });

    dispatcher_.registerHandler("DELETE_GUILD", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleDeleteGuild(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_MEMBERS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleListMembers(msg, fd)};
    });

    dispatcher_.registerHandler("SET_MEMBER_ROLE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleSetMemberRole(msg, fd)};
    });

    dispatcher_.registerHandler("SET_GUILD_VISIBILITY", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{guild_handler_.handleSetGuildVisibility(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_CHANNELS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleListChannels(msg, fd)};
    });

    dispatcher_.registerHandler("CREATE_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleCreateChannel(msg, fd)};
    });

    dispatcher_.registerHandler("DELETE_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleDeleteChannel(msg, fd)};
    });

    dispatcher_.registerHandler("JOIN_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleJoinChannel(msg, fd)};
    });

    dispatcher_.registerHandler("LEAVE_CHANNEL", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleLeaveChannel(msg, fd)};
    });

    dispatcher_.registerHandler("CHANNEL_MESSAGE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleChannelMessage(msg, fd)};
    });

    dispatcher_.registerHandler("FETCH_HISTORY", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{channel_handler_.handleFetchHistory(msg, fd)};
    });

    dispatcher_.registerHandler("CREATE_INVITE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{invite_handler_.handleCreateInvite(msg, fd)};
    });

    dispatcher_.registerHandler("LIST_INVITES", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{invite_handler_.handleListInvites(msg, fd)};
    });

    dispatcher_.registerHandler("REVOKE_INVITE", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{invite_handler_.handleRevokeInvite(msg, fd)};
    });

    dispatcher_.registerHandler("JOIN_VIA_INVITE", [this](const protocol::Message& msg, int fd) {
        return invite_handler_.handleJoinViaInvite(msg, fd);
    });

    dispatcher_.registerHandler("REQUEST_JOIN", [this](const protocol::Message& msg, int fd) {
        return join_request_handler_.handleRequestJoin(msg, fd);
    });

    dispatcher_.registerHandler("LIST_JOIN_REQUESTS", [this](const protocol::Message& msg, int fd) {
        return std::vector<protocol::Message>{join_request_handler_.handleListJoinRequests(msg, fd)};
    });

    dispatcher_.registerHandler("APPROVE_JOIN_REQUEST", [this](const protocol::Message& msg, int fd) {
        return join_request_handler_.handleApproveJoinRequest(msg, fd);
    });

    dispatcher_.registerHandler("REJECT_JOIN_REQUEST", [this](const protocol::Message& msg, int fd) {
        return join_request_handler_.handleRejectJoinRequest(msg, fd);
    });
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

    // docs/guilds/social-presence-design.md §4.5: constructed here (not at Server
    // construction) because it needs internal_api_client_.get(), which
    // isn't available yet at that point. Started in start(), stopped
    // explicitly at the end of start()'s loop.
    message_worker_ =
        std::make_unique<persistence::MessagePersistenceWorker>(internal_api_client_.get());
    chat_handler_.setMessagePersistenceWorker(message_worker_.get());
    channel_handler_.setMessagePersistenceWorker(message_worker_.get());
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

    std::cout << "Hydrated message sequence counters (lobby=" << last_seq.lobby_seq.value_or(0)
              << ", channel=" << last_seq.channel_seq.value_or(0) << ")" << std::endl;
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

void Server::removeSessionTrackingPresence(int fd) {
    const session::Session* session = session_manager_.getSession(fd);
    const std::string user_id = session ? session->user_id : std::string();

    session_manager_.removeSession(fd);

    // user_id is empty for a connection that disconnected before ever
    // completing IDENTIFY — it never incremented presence, so there's
    // nothing to decrement or announce.
    if (!user_id.empty() && session_manager_.decrementPresence(user_id)) {
        broadcast(makePresenceUpdate(user_id, false));
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

                // docs/guilds/social-presence-design.md §3.2: emitted from here,
                // after IDENTIFY's own IDENTIFIED response has already been
                // sent above — a separate, unrelated broadcast to everyone
                // else, not a replacement for it. Checked by message type
                // rather than inside IdentifyHandler itself: see
                // Server::makePresenceUpdate's doc comment for why.
                if (message.type == "IDENTIFY" && identified) {
                    const session::Session* session = session_manager_.getSession(fd);
                    if (session && session_manager_.incrementPresence(session->user_id)) {
                        broadcast(makePresenceUpdate(session->user_id, true));
                    }
                }
            }
            ++it;
        }

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
        (void)sendMessage(fd, message);
    }
}

void Server::stop() {
    running_ = false;
}

uint16_t Server::bound_port() const {
    return listener_.bound_port();
}
