#ifndef CIG_NEXUS_TESTS_HTTP_FAKE_INTERNAL_API_CLIENT_HPP
#define CIG_NEXUS_TESTS_HTTP_FAKE_INTERNAL_API_CLIENT_HPP

#include "http/InternalApiClient.hpp"

namespace test_helpers {

// In-memory stand-in for the internal catalog API (design doc §8.1), used
// by GuildHandler/ChannelHandler-level tests so they can inject a working
// InternalApiClient without a real Next.js/Postgres running.
// CurlInternalApiClient has its own dedicated tests against a real HTTP
// server (tests/http/CurlInternalApiClient.test.cpp) — this fake exists so
// handler tests stay focused on handler logic (validation, authorization,
// cache/session updates), not HTTP mechanics.
class FakeInternalApiClient : public http::InternalApiClient {
  public:
    std::optional<http::Catalog> fetchCatalog() override { return catalog_to_return; }

    std::optional<http::WireGuild> createGuild(const std::string& name, const std::string& owner_id,
                                               const std::string& visibility) override {
        if (fail_create_guild) {
            return std::nullopt;
        }
        return http::WireGuild{"g_fake_" + std::to_string(next_guild_id_++), name, owner_id,
                               visibility};
    }

    bool deleteGuild(const std::string&) override { return !fail_delete_guild; }

    std::optional<std::string> setGuildVisibility(const std::string&,
                                                  const std::string& visibility) override {
        if (fail_set_guild_visibility) {
            return std::nullopt;
        }
        return visibility;
    }

    std::optional<int> createMembership(const std::string&, const std::string&,
                                        int role_rank) override {
        if (fail_create_membership) {
            return std::nullopt;
        }
        return create_membership_returns_rank.value_or(role_rank);
    }

    bool deleteMembership(const std::string&, const std::string&) override {
        return !fail_delete_membership;
    }

    std::optional<std::vector<http::WireMember>> fetchGuildMembers(const std::string&) override {
        if (fail_fetch_guild_members) {
            return std::nullopt;
        }
        return guild_members_to_return;
    }

    std::optional<std::string> setMemberRole(const std::string&, const std::string&, int) override {
        if (fail_set_member_role) {
            return std::nullopt;
        }
        return set_member_role_label_to_return;
    }

    std::optional<http::WireChannel> createChannel(const std::string& guild_id,
                                                   const std::string& name,
                                                   const std::string& channel_type) override {
        if (fail_create_channel) {
            return std::nullopt;
        }
        return http::WireChannel{"c_fake_" + std::to_string(next_channel_id_++), guild_id, name,
                                 channel_type};
    }

    bool deleteChannel(const std::string&) override { return !fail_delete_channel; }

    std::vector<std::string> fetchRevokedSessionIds(const std::string&,
                                                    std::string& out_as_of) override {
        out_as_of = "fake-as-of";
        return revoked_ids_to_return;
    }

    bool createMessage(const std::optional<std::string>& channel_id,
                       const std::optional<std::string>& dm_peer_id, const std::string& user_id,
                       const std::string& content, int seq) override {
        if (fail_create_message) {
            return false;
        }
        created_messages.push_back(http::WireMessage{seq, channel_id, 0, user_id, "", content,
                                                     std::nullopt, std::nullopt});
        last_created_dm_peer_id = dm_peer_id;
        return true;
    }

    std::optional<http::HistoryPage> fetchMessages(const std::optional<std::string>&,
                                                   const std::optional<std::string>&,
                                                   const std::string&, std::optional<int>,
                                                   int) override {
        if (fail_fetch_messages) {
            return std::nullopt;
        }
        return history_page_to_return;
    }

    http::LastSequence fetchLastSequence() override { return last_sequence_to_return; }

    std::optional<std::vector<http::WireDmConversation>>
    fetchDmConversations(const std::string&) override {
        if (fail_fetch_dm_conversations) {
            return std::nullopt;
        }
        return dm_conversations_to_return;
    }

    std::optional<std::vector<std::string>> fetchGuildIdsForUser(const std::string&) override {
        if (fail_fetch_guild_ids_for_user) {
            return std::nullopt;
        }
        return guild_ids_for_user_to_return;
    }

    std::optional<http::WireInvite> createInvite(const std::string&, const std::string&,
                                                 std::optional<int> max_uses,
                                                 std::optional<int>) override {
        if (fail_create_invite) {
            return std::nullopt;
        }
        http::WireInvite invite;
        invite.code = create_invite_returns_code.empty()
                          ? "fake-code-" + std::to_string(next_invite_id_++)
                          : create_invite_returns_code;
        invite.max_uses = max_uses;
        invite.use_count = 0;
        invite.created_at = "2026-01-01T00:00:00Z";
        return invite;
    }

    std::optional<std::vector<http::WireInvite>> fetchInvites(const std::string&) override {
        if (fail_fetch_invites) {
            return std::nullopt;
        }
        return invites_to_return;
    }

    bool revokeInvite(const std::string&, const std::string&) override {
        return !fail_revoke_invite;
    }

    http::RedeemInviteResult redeemInvite(const std::string&, const std::string&) override {
        return redeem_invite_returns;
    }

    http::CreateJoinRequestResult createJoinRequest(const std::string&,
                                                    const std::string&) override {
        return create_join_request_returns;
    }

    std::optional<std::vector<http::WireJoinRequest>>
    fetchJoinRequests(const std::string&) override {
        if (fail_fetch_join_requests) {
            return std::nullopt;
        }
        return join_requests_to_return;
    }

    std::optional<int> approveJoinRequest(const std::string&, const std::string&) override {
        if (fail_approve_join_request) {
            return std::nullopt;
        }
        return approve_join_request_returns_rank;
    }

    bool rejectJoinRequest(const std::string&, const std::string&) override {
        return !fail_reject_join_request;
    }

    http::SendFriendRequestResult sendFriendRequest(const std::string&,
                                                    const std::string&) override {
        return send_friend_request_returns;
    }

    http::SendFriendRequestResult addFriendByCode(const std::string&, const std::string&) override {
        return add_friend_by_code_returns;
    }

    http::AcceptFriendRequestResult acceptFriendRequest(const std::string&,
                                                        const std::string&) override {
        return accept_friend_request_returns;
    }

    bool deleteFriendRequest(const std::string&, const std::string&) override {
        return !fail_delete_friend_request;
    }

    bool removeFriend(const std::string&, const std::string&) override {
        return !fail_remove_friend;
    }

    std::optional<std::vector<http::WireFriend>> fetchFriends(const std::string&) override {
        if (fail_fetch_friends) {
            return std::nullopt;
        }
        return friends_to_return;
    }

    std::optional<http::FriendRequestList> fetchFriendRequests(const std::string&) override {
        if (fail_fetch_friend_requests) {
            return std::nullopt;
        }
        return friend_requests_to_return;
    }

    std::optional<std::string> fetchFriendCode(const std::string&) override {
        if (fail_fetch_friend_code) {
            return std::nullopt;
        }
        return friend_code_to_return;
    }

    std::optional<std::string> regenerateFriendCode(const std::string&) override {
        if (fail_regenerate_friend_code) {
            return std::nullopt;
        }
        return friend_code_to_return;
    }

    bool blockUser(const std::string&, const std::string&) override { return !fail_block_user; }
    bool unblockUser(const std::string&, const std::string&) override { return !fail_unblock_user; }
    std::optional<std::vector<http::WireBlock>> fetchBlocks(const std::string&) override {
        if (fail_fetch_blocks) {
            return std::nullopt;
        }
        return blocks_to_return;
    }

    std::optional<http::WireUserProfile> fetchUserProfile(const std::string& user_id) override {
        if (fail_fetch_user_profile) {
            return std::nullopt;
        }
        if (user_profile_to_return) {
            return user_profile_to_return;
        }
        return http::WireUserProfile{user_id, "fake-username", std::nullopt, std::nullopt};
    }

    // Test control: flip one of these to exercise a handler's "internal API
    // call failed" path (should become INTERNAL_ERROR without mutating any
    // local cache/session state).
    bool fail_create_guild = false;
    bool fail_delete_guild = false;
    bool fail_set_guild_visibility = false;
    bool fail_create_membership = false;
    bool fail_delete_membership = false;
    bool fail_create_channel = false;
    bool fail_delete_channel = false;
    bool fail_create_message = false;
    bool fail_fetch_messages = false;
    bool fail_fetch_guild_members = false;
    bool fail_set_member_role = false;
    bool fail_create_invite = false;
    bool fail_fetch_invites = false;
    bool fail_revoke_invite = false;
    bool fail_fetch_join_requests = false;
    bool fail_approve_join_request = false;
    bool fail_reject_join_request = false;
    bool fail_delete_friend_request = false;
    bool fail_remove_friend = false;
    bool fail_fetch_friends = false;
    bool fail_fetch_friend_requests = false;
    bool fail_fetch_friend_code = false;
    bool fail_regenerate_friend_code = false;
    bool fail_block_user = false;
    bool fail_unblock_user = false;
    bool fail_fetch_blocks = false;
    bool fail_fetch_user_profile = false;
    bool fail_fetch_dm_conversations = false;
    bool fail_fetch_guild_ids_for_user = false;
    http::Catalog catalog_to_return;
    std::vector<std::string> revoked_ids_to_return;
    std::vector<http::WireMessage> created_messages;
    // Set by the most recent createMessage() call — lets a test assert
    // whether a persisted message was scoped to a DM peer.
    std::optional<std::string> last_created_dm_peer_id;
    http::HistoryPage history_page_to_return;
    http::LastSequence last_sequence_to_return;
    std::vector<http::WireDmConversation> dm_conversations_to_return;
    std::vector<std::string> guild_ids_for_user_to_return;
    std::vector<http::WireMember> guild_members_to_return;
    std::string set_member_role_label_to_return = "Officer";
    // If unset, createMembership echoes back whatever role_rank it was
    // called with — override to simulate the idempotent-rejoin case where
    // Postgres already had a different rank for this row.
    std::optional<int> create_membership_returns_rank;
    std::string create_invite_returns_code; // empty = auto-generate a fake one
    std::vector<http::WireInvite> invites_to_return;
    http::RedeemInviteResult redeem_invite_returns;
    http::CreateJoinRequestResult create_join_request_returns =
        http::CreateJoinRequestResult::CREATED;
    std::vector<http::WireJoinRequest> join_requests_to_return;
    int approve_join_request_returns_rank = 0;
    http::SendFriendRequestResult send_friend_request_returns;
    http::SendFriendRequestResult add_friend_by_code_returns;
    http::AcceptFriendRequestResult accept_friend_request_returns;
    std::vector<http::WireFriend> friends_to_return;
    http::FriendRequestList friend_requests_to_return;
    std::string friend_code_to_return = "fake-friend-code";
    std::vector<http::WireBlock> blocks_to_return;
    // Unset = fetchUserProfile() synthesizes a minimal fake profile from
    // the requested user_id, so existing IDENTIFY-flow tests that don't
    // care about display_name/avatar_url don't need to configure this.
    std::optional<http::WireUserProfile> user_profile_to_return;

  private:
    int next_guild_id_ = 1;
    int next_channel_id_ = 1;
    int next_invite_id_ = 1;
};

} // namespace test_helpers

#endif // CIG_NEXUS_TESTS_HTTP_FAKE_INTERNAL_API_CLIENT_HPP
