#ifndef CIG_NEXUS_HTTP_CURL_INTERNAL_API_CLIENT_HPP
#define CIG_NEXUS_HTTP_CURL_INTERNAL_API_CLIENT_HPP

#include "http/InternalApiClient.hpp"

#include <optional>
#include <string>

namespace http {

// Concrete InternalApiClient backed by libcurl. base_url has no trailing
// slash, e.g. "http://web-internal:3001" (design doc §8.1/§8.2: this is
// the only outbound HTTP the C++ server makes).
class CurlInternalApiClient : public InternalApiClient {
  public:
    CurlInternalApiClient(std::string base_url, std::string shared_secret);
    ~CurlInternalApiClient() override = default;

    CurlInternalApiClient(const CurlInternalApiClient&) = delete;
    CurlInternalApiClient& operator=(const CurlInternalApiClient&) = delete;

    std::optional<Catalog> fetchCatalog() override;
    std::optional<WireGuild> createGuild(const std::string& name, const std::string& owner_id,
                                         const std::string& visibility) override;
    bool deleteGuild(const std::string& guild_id) override;
    std::optional<std::string> setGuildVisibility(const std::string& guild_id,
                                                   const std::string& visibility) override;
    std::optional<int> createMembership(const std::string& guild_id, const std::string& user_id,
                                        int role_rank) override;
    bool deleteMembership(const std::string& guild_id, const std::string& user_id) override;
    std::optional<std::vector<WireMember>> fetchGuildMembers(const std::string& guild_id) override;
    std::optional<std::string> setMemberRole(const std::string& guild_id, const std::string& user_id,
                                             int role_rank) override;
    std::optional<WireChannel> createChannel(const std::string& guild_id, const std::string& name,
                                             const std::string& channel_type) override;
    bool deleteChannel(const std::string& channel_id) override;
    std::vector<std::string> fetchRevokedSessionIds(const std::string& since_iso8601,
                                                    std::string& out_as_of) override;
    bool createMessage(const std::optional<std::string>& channel_id, const std::optional<std::string>& dm_peer_id,
                       const std::string& user_id, const std::string& content, int seq) override;
    std::optional<HistoryPage> fetchMessages(const std::optional<std::string>& channel_id,
                                             const std::optional<std::string>& dm_peer_id,
                                             const std::string& requester_user_id, std::optional<int> before_seq,
                                             int limit) override;
    LastSequence fetchLastSequence() override;
    std::optional<std::vector<WireDmConversation>> fetchDmConversations(const std::string& user_id) override;
    std::optional<std::vector<std::string>> fetchGuildIdsForUser(const std::string& user_id) override;

    std::optional<WireInvite> createInvite(const std::string& guild_id, const std::string& created_by,
                                           std::optional<int> max_uses,
                                           std::optional<int> expires_in_seconds) override;
    std::optional<std::vector<WireInvite>> fetchInvites(const std::string& guild_id) override;
    bool revokeInvite(const std::string& guild_id, const std::string& code) override;
    RedeemInviteResult redeemInvite(const std::string& code, const std::string& user_id) override;

    CreateJoinRequestResult createJoinRequest(const std::string& guild_id,
                                              const std::string& user_id) override;
    std::optional<std::vector<WireJoinRequest>> fetchJoinRequests(const std::string& guild_id) override;
    std::optional<int> approveJoinRequest(const std::string& guild_id,
                                          const std::string& user_id) override;
    bool rejectJoinRequest(const std::string& guild_id, const std::string& user_id) override;

    SendFriendRequestResult sendFriendRequest(const std::string& requester_id,
                                              const std::string& recipient_id) override;
    SendFriendRequestResult addFriendByCode(const std::string& requester_id,
                                            const std::string& code) override;
    AcceptFriendRequestResult acceptFriendRequest(const std::string& requester_id,
                                                  const std::string& recipient_id) override;
    bool deleteFriendRequest(const std::string& requester_id, const std::string& recipient_id) override;
    bool removeFriend(const std::string& user_id_a, const std::string& user_id_b) override;
    std::optional<std::vector<WireFriend>> fetchFriends(const std::string& user_id) override;
    std::optional<FriendRequestList> fetchFriendRequests(const std::string& user_id) override;
    std::optional<std::string> fetchFriendCode(const std::string& user_id) override;
    std::optional<std::string> regenerateFriendCode(const std::string& user_id) override;

    bool blockUser(const std::string& blocker_id, const std::string& blocked_id) override;
    bool unblockUser(const std::string& blocker_id, const std::string& blocked_id) override;
    std::optional<std::vector<WireBlock>> fetchBlocks(const std::string& user_id) override;

    std::optional<WireUserProfile> fetchUserProfile(const std::string& user_id) override;

  private:
    struct HttpResponse {
        long status = 0;
        std::string body;
    };

    std::optional<HttpResponse> request(const std::string& method, const std::string& path,
                                        const std::string& body) const;

    std::string base_url_;
    std::string shared_secret_;
};

} // namespace http

#endif // CIG_NEXUS_HTTP_CURL_INTERNAL_API_CLIENT_HPP
