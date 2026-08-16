#ifndef CIG_NEXUS_HTTP_INTERNAL_API_CLIENT_HPP
#define CIG_NEXUS_HTTP_INTERNAL_API_CLIENT_HPP

#include <optional>
#include <string>
#include <vector>

namespace http {

struct WireGuild {
    std::string guild_id;
    std::string name;
    std::string owner_id;
    std::string visibility; // "open" | "application" | "private" (docs/guilds/social-presence-design.md §1.7)
};

struct WireMembership {
    std::string guild_id;
    std::string user_id;
    int role_rank;
};

// docs/guilds/social-presence-design.md §2.1 (LIST_MEMBERS). role_label is
// pre-resolved server-side (Next.js) against the guild's role_theme — C++
// never resolves it itself. joined_at is relayed as an opaque ISO 8601
// string; unlike WireMessage's timestamp, C++ never assigns or interprets
// this value, only passes it through.
struct WireMember {
    std::string user_id;
    std::string username;
    int role_rank;
    std::string role_label;
    std::string joined_at;
    // docs/social/friends-dms-design.md §4.5: resolved server-side
    // (Next.js) the same way every other profile-bearing field here is —
    // display_name is unset only if the internal API response omits it
    // (it shouldn't, in practice — resolveDisplayName never returns null),
    // avatar_url is unset when the user has no avatar at all.
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

struct WireChannel {
    std::string channel_id;
    std::string guild_id;
    std::string name;
    std::string channel_type; // "TEXT" | "VOICE"
};

// docs/guilds/social-presence-design.md §1.2/§1.4. Timestamps relayed as opaque
// ISO 8601 strings, same treatment as WireMember's joined_at — C++ never
// assigns or interprets them.
struct WireInvite {
    std::string code;
    std::optional<int> max_uses;
    int use_count = 0;
    std::optional<std::string> expires_at;
    std::optional<std::string> revoked_at;
    std::string created_at;
};

// §1.9 (LIST_JOIN_REQUESTS).
struct WireJoinRequest {
    std::string user_id;
    std::string username;
    std::string requested_at;
    // docs/social/friends-dms-design.md §4.5.
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

// §1.9 (REQUEST_JOIN). FAILED covers malformed/unreachable — distinct from
// ALREADY_PENDING, which is a normal, expected outcome the handler maps to
// JOIN_REQUEST_ALREADY_PENDING rather than INTERNAL_ERROR.
enum class CreateJoinRequestResult { CREATED, ALREADY_PENDING, FAILED };

// §1.3/§1.5 (JOIN_VIA_INVITE). Mirrors web/lib/internal/invites.ts's
// RedeemInviteResult discriminated union: on success, is_join_request
// distinguishes §1.8's "invite still requires approval" diversion
// (`application`-visibility guilds) from ordinary direct membership.
enum class RedeemInviteError { NOT_FOUND, REVOKED, EXPIRED, MAX_USES_REACHED, ALREADY_MEMBER, FAILED };

struct RedeemInviteResult {
    bool ok = false;
    bool is_join_request = false; // only meaningful when ok
    std::string guild_id;         // only meaningful when ok
    std::optional<int> role_rank; // only set when ok && !is_join_request
    RedeemInviteError error = RedeemInviteError::FAILED; // only meaningful when !ok
};

struct Catalog {
    std::vector<WireGuild> guilds;
    std::vector<WireMembership> memberships;
    std::vector<WireChannel> channels;
};

// design doc (docs/guilds/social-presence-design.md) §4: channel_id is unset for
// the global lobby (CHAT_MESSAGE), set for a specific channel
// (CHANNEL_MESSAGE) — mirrors the Postgres schema's nullable channel_id.
struct WireMessage {
    int message_id;
    std::optional<std::string> channel_id;
    long timestamp;
    std::string user_id;
    std::string username;
    std::string content;
    // docs/social/friends-dms-design.md §4.5.
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

struct HistoryPage {
    std::vector<WireMessage> messages;
    bool has_more = false;
};

// docs/social/friends-dms-design.md §1. WireFriend/WireFriendRequest mirror
// WireMember's shape (user_id + username, nothing cached/joined further).
struct WireFriend {
    std::string user_id;
    std::string username;
    // docs/social/friends-dms-design.md §4.5.
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

struct WireFriendRequest {
    std::string user_id; // the other party
    std::string username;
    std::string created_at;
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

// §1.4's ordered validation, all evaluated server-side (Next.js) in one
// call — mirrors RedeemInviteResult's shape (a discriminated outcome, not
// a boolean plus a separate error path). CODE_NOT_FOUND is only ever
// produced by addFriendByCode(); SELF/USER_NOT_FOUND/ALREADY_FRIENDS are
// shared with sendFriendRequest() since both run the same underlying
// transaction (§1.3).
enum class SendFriendRequestOutcome {
    REQUEST_CREATED,
    FRIENDS_ADDED, // §1.4 step 5: a reverse-pending request already existed
    ALREADY_FRIENDS,
    SELF,
    USER_NOT_FOUND,
    CODE_NOT_FOUND,
    FAILED
};

struct SendFriendRequestResult {
    SendFriendRequestOutcome outcome = SendFriendRequestOutcome::FAILED;
    // Only meaningful when outcome is REQUEST_CREATED/FRIENDS_ADDED — the
    // target's wire id/username, needed to build the response payloads.
    std::string user_id;
    std::string username;
};

struct AcceptFriendRequestResult {
    bool ok = false;
    std::string user_id; // only meaningful when ok — the original requester
    std::string username;
};

struct FriendRequestList {
    std::vector<WireFriendRequest> incoming;
    std::vector<WireFriendRequest> outgoing;
};

// docs/social/friends-dms-design.md §2. Directed — blocker_id/blocked_id
// aren't symmetric the way friendships are.
struct WireBlock {
    std::string user_id; // the blocked user
    std::string username;
    std::string blocked_at;
    // docs/social/friends-dms-design.md §4.5.
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

// §4.3: the durable high-water mark for each of the two message id-spaces
// (lobby, all channels combined) — fetched once at server startup to seed
// ChatHandler's/ChannelHandler's message_id counters. unset means no
// messages have ever been persisted in that scope yet.
struct LastSequence {
    std::optional<int> lobby_seq;
    std::optional<int> channel_seq;
    // docs/social/friends-dms-design.md §3.4: a third, shared id-space
    // across every DM conversation (not per-conversation) — mirrors the
    // channel id-space's "shared, not per-entity" shape exactly.
    std::optional<int> dm_seq;
};

// docs/social/friends-dms-design.md §4.5, revised at implementation: the
// backing shape for Session::display_name/avatar_url's IDENTIFY-time
// hydration (IdentifyHandler.cpp) — a single-user lookup, mirroring
// fetchBlocks/fetchFriends' role for blocked_user_ids/friend_ids. Needed
// because CHAT_MESSAGE/CHANNEL_MESSAGE/DM_MESSAGE are built directly from
// Session's cached identity with no per-message internal API call, unlike
// LIST_MEMBERS/LIST_FRIENDS/FETCH_HISTORY which are already live reads and
// get these fields for free by extending their existing query.
struct WireUserProfile {
    std::string user_id;
    std::string username;
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
};

// docs/social/friends-dms-design.md §3.5 (LIST_DM_CONVERSATIONS). Revised
// at implementation (§4.5): the original shape carried only peer_id/
// last_message_at, unrenderable as a conversation list without a name —
// username is mandatory here, the same as every other roster/list entry.
struct WireDmConversation {
    std::string peer_id;
    std::string username;
    std::optional<std::string> display_name;
    std::optional<std::string> avatar_url;
    std::optional<std::string> last_message_at;
};

// Design doc §8.1: the seam GuildHandler/ChannelHandler call through for
// every catalog mutation, and that GuildManager's write-through cache is
// hydrated from at startup. An abstract interface so handler-level tests
// can inject a fake instead of making real HTTP calls — CurlInternalApiClient
// is the concrete implementation, exercised directly by its own tests
// against a real embedded test HTTP server.
class InternalApiClient {
  public:
    virtual ~InternalApiClient() = default;

    virtual std::optional<Catalog> fetchCatalog() = 0;

    virtual std::optional<WireGuild> createGuild(const std::string& name, const std::string& owner_id,
                                                 const std::string& visibility) = 0;
    virtual bool deleteGuild(const std::string& guild_id) = 0;

    // §1.10 (SET_GUILD_VISIBILITY). Returns the new visibility on success,
    // nullopt if the guild doesn't exist.
    virtual std::optional<std::string> setGuildVisibility(const std::string& guild_id,
                                                           const std::string& visibility) = 0;

    // Returns the *actual* resulting role_rank, not necessarily role_rank:
    // idempotent-rejoin (an existing row from a prior session) is a no-op
    // against Postgres, so the response reports back whatever the row
    // already carries — see web/lib/internal/catalog.ts's createMembership
    // for why blindly trusting the requested role_rank here would risk
    // silently demoting an already-promoted member's cached rank.
    virtual std::optional<int> createMembership(const std::string& guild_id,
                                                const std::string& user_id, int role_rank) = 0;
    virtual bool deleteMembership(const std::string& guild_id, const std::string& user_id) = 0;

    // docs/guilds/social-presence-design.md §2.3: a live read, never cached here —
    // LIST_MEMBERS calls this directly on every request.
    virtual std::optional<std::vector<WireMember>> fetchGuildMembers(const std::string& guild_id) = 0;

    // §2.4 (SET_MEMBER_ROLE). Returns the resolved role_label on success,
    // nullopt if the membership doesn't exist.
    virtual std::optional<std::string> setMemberRole(const std::string& guild_id,
                                                      const std::string& user_id, int role_rank) = 0;

    virtual std::optional<WireChannel> createChannel(const std::string& guild_id,
                                                     const std::string& name,
                                                     const std::string& channel_type) = 0;
    virtual bool deleteChannel(const std::string& channel_id) = 0;

    // since_iso8601: pass the timestamp of the previous successful poll: the
    // response's "as_of" field. Design doc §9.
    virtual std::vector<std::string> fetchRevokedSessionIds(const std::string& since_iso8601,
                                                            std::string& out_as_of) = 0;

    // docs/guilds/social-presence-design.md §4.5: called fire-and-forget from
    // MessagePersistenceWorker, after the message has already been
    // broadcast — not on the hot dispatch path itself. channel_id unset =
    // the global lobby. docs/social/friends-dms-design.md §3.4: dm_peer_id
    // (mutually exclusive with channel_id) resolves/creates the DM
    // conversation as part of this same call — the conversation row is an
    // implementation detail of persisting the message, not a prerequisite
    // checked before broadcasting it.
    virtual bool createMessage(const std::optional<std::string>& channel_id,
                               const std::optional<std::string>& dm_peer_id, const std::string& user_id,
                               const std::string& content, int seq) = 0;

    // §4.4: the first read-through (not write-through) internal API call —
    // never cached in GuildManager. before_seq unset fetches the most
    // recent page. dm_peer_id (mutually exclusive with channel_id) and
    // requester_user_id (only meaningful alongside dm_peer_id, to resolve
    // which conversation) are docs/social/friends-dms-design.md §3.5's DM
    // scope addition — a peer with no conversation yet yields an empty
    // page, not an error.
    virtual std::optional<HistoryPage> fetchMessages(const std::optional<std::string>& channel_id,
                                                      const std::optional<std::string>& dm_peer_id,
                                                      const std::string& requester_user_id,
                                                      std::optional<int> before_seq, int limit) = 0;

    // §4.3/§3.4: called once at server startup, alongside fetchCatalog().
    virtual LastSequence fetchLastSequence() = 0;

    // docs/social/friends-dms-design.md §3.6 (LIST_DM_CONVERSATIONS).
    virtual std::optional<std::vector<WireDmConversation>> fetchDmConversations(const std::string& user_id) = 0;

    // §3.3: canSendDm()'s live fallback for the shared-guild-membership
    // check when the peer has zero active connections (no in-memory
    // Session::guild_ids to intersect against) — mirrors fetchBlocks'
    // equivalent role for the blocked-check's same fallback case.
    virtual std::optional<std::vector<std::string>> fetchGuildIdsForUser(const std::string& user_id) = 0;

    // §1.4/§1.5 (CREATE_INVITE/LIST_INVITES/REVOKE_INVITE/JOIN_VIA_INVITE).
    // Not cached in GuildManager — same "live read/write, not write-through"
    // treatment §1.5 gives invites and §4.4 gives message history.
    virtual std::optional<WireInvite> createInvite(const std::string& guild_id,
                                                   const std::string& created_by,
                                                   std::optional<int> max_uses,
                                                   std::optional<int> expires_in_seconds) = 0;
    virtual std::optional<std::vector<WireInvite>> fetchInvites(const std::string& guild_id) = 0;
    virtual bool revokeInvite(const std::string& guild_id, const std::string& code) = 0;
    virtual RedeemInviteResult redeemInvite(const std::string& code, const std::string& user_id) = 0;

    // §1.9 (REQUEST_JOIN/LIST_JOIN_REQUESTS/APPROVE_JOIN_REQUEST/REJECT_JOIN_REQUEST).
    virtual CreateJoinRequestResult createJoinRequest(const std::string& guild_id,
                                                      const std::string& user_id) = 0;
    virtual std::optional<std::vector<WireJoinRequest>> fetchJoinRequests(const std::string& guild_id) = 0;
    // Returns the new member's role_rank on success, nullopt if no such
    // request exists.
    virtual std::optional<int> approveJoinRequest(const std::string& guild_id,
                                                  const std::string& user_id) = 0;
    virtual bool rejectJoinRequest(const std::string& guild_id, const std::string& user_id) = 0;

    // §1.6 (SEND_FRIEND_REQUEST / ADD_FRIEND_BY_CODE / ACCEPT_FRIEND_REQUEST /
    // REJECT_FRIEND_REQUEST / CANCEL_FRIEND_REQUEST / REMOVE_FRIEND /
    // LIST_FRIENDS / LIST_FRIEND_REQUESTS / FETCH_FRIEND_CODE /
    // REGENERATE_FRIEND_CODE). Not cached in GuildManager — same "live
    // read/write, not write-through" treatment as invites (§1.5) and join
    // requests.
    virtual SendFriendRequestResult sendFriendRequest(const std::string& requester_id,
                                                       const std::string& recipient_id) = 0;
    virtual SendFriendRequestResult addFriendByCode(const std::string& requester_id,
                                                     const std::string& code) = 0;
    virtual AcceptFriendRequestResult acceptFriendRequest(const std::string& requester_id,
                                                           const std::string& recipient_id) = 0;
    // Used for both REJECT_FRIEND_REQUEST and CANCEL_FRIEND_REQUEST — same
    // deletion either way (docs/social/friends-dms-design.md §1.4); the
    // handler decides which wire response to send based on which message
    // came in, not this call's return shape.
    virtual bool deleteFriendRequest(const std::string& requester_id, const std::string& recipient_id) = 0;
    virtual bool removeFriend(const std::string& user_id_a, const std::string& user_id_b) = 0;
    virtual std::optional<std::vector<WireFriend>> fetchFriends(const std::string& user_id) = 0;
    virtual std::optional<FriendRequestList> fetchFriendRequests(const std::string& user_id) = 0;
    virtual std::optional<std::string> fetchFriendCode(const std::string& user_id) = 0;
    virtual std::optional<std::string> regenerateFriendCode(const std::string& user_id) = 0;

    // §2.8 (BLOCK_USER / UNBLOCK_USER / LIST_BLOCKS) — also what
    // IdentifyHandler calls to hydrate a new connection's
    // Session::blocked_user_ids (§3.3), since the same "who has this user
    // blocked" list serves both.
    virtual bool blockUser(const std::string& blocker_id, const std::string& blocked_id) = 0;
    virtual bool unblockUser(const std::string& blocker_id, const std::string& blocked_id) = 0;
    virtual std::optional<std::vector<WireBlock>> fetchBlocks(const std::string& user_id) = 0;

    // §4.5: called once at IDENTIFY to hydrate Session::display_name/
    // avatar_url — see WireUserProfile's comment for why this is a
    // separate call rather than reusing an existing live-join query.
    virtual std::optional<WireUserProfile> fetchUserProfile(const std::string& user_id) = 0;
};

} // namespace http

#endif // CIG_NEXUS_HTTP_INTERNAL_API_CLIENT_HPP
