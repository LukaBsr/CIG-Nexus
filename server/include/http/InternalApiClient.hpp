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
    std::string visibility; // "open" | "application" | "private" (docs/social-presence-design.md §1.7)
};

struct WireMembership {
    std::string guild_id;
    std::string user_id;
    int role_rank;
};

// docs/social-presence-design.md §2.1 (LIST_MEMBERS). role_label is
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
};

struct WireChannel {
    std::string channel_id;
    std::string guild_id;
    std::string name;
    std::string channel_type; // "TEXT" | "VOICE"
};

// docs/social-presence-design.md §1.2/§1.4. Timestamps relayed as opaque
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

// design doc (docs/social-presence-design.md) §4: channel_id is unset for
// the global lobby (CHAT_MESSAGE), set for a specific channel
// (CHANNEL_MESSAGE) — mirrors the Postgres schema's nullable channel_id.
struct WireMessage {
    int message_id;
    std::optional<std::string> channel_id;
    long timestamp;
    std::string user_id;
    std::string username;
    std::string content;
};

struct HistoryPage {
    std::vector<WireMessage> messages;
    bool has_more = false;
};

// §4.3: the durable high-water mark for each of the two message id-spaces
// (lobby, all channels combined) — fetched once at server startup to seed
// ChatHandler's/ChannelHandler's message_id counters. unset means no
// messages have ever been persisted in that scope yet.
struct LastSequence {
    std::optional<int> lobby_seq;
    std::optional<int> channel_seq;
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

    // docs/social-presence-design.md §2.3: a live read, never cached here —
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

    // docs/social-presence-design.md §4.5: called fire-and-forget from
    // MessagePersistenceWorker, after the message has already been
    // broadcast — not on the hot dispatch path itself. channel_id unset =
    // the global lobby.
    virtual bool createMessage(const std::optional<std::string>& channel_id, const std::string& user_id,
                               const std::string& content, int seq) = 0;

    // §4.4: the first read-through (not write-through) internal API call —
    // never cached in GuildManager. before_seq unset fetches the most
    // recent page.
    virtual std::optional<HistoryPage> fetchMessages(const std::optional<std::string>& channel_id,
                                                      std::optional<int> before_seq, int limit) = 0;

    // §4.3: called once at server startup, alongside fetchCatalog().
    virtual LastSequence fetchLastSequence() = 0;

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
};

} // namespace http

#endif // CIG_NEXUS_HTTP_INTERNAL_API_CLIENT_HPP
