#include "Server.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

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
