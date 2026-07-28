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
};

struct WireMembership {
    std::string guild_id;
    std::string user_id;
};

struct WireChannel {
    std::string channel_id;
    std::string guild_id;
    std::string name;
    std::string channel_type; // "TEXT" | "VOICE"
};

struct Catalog {
    std::vector<WireGuild> guilds;
    std::vector<WireMembership> memberships;
    std::vector<WireChannel> channels;
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

    virtual std::optional<WireGuild> createGuild(const std::string& name, const std::string& owner_id) = 0;
    virtual bool deleteGuild(const std::string& guild_id) = 0;

    virtual bool createMembership(const std::string& guild_id, const std::string& user_id,
                                  const std::string& role) = 0;
    virtual bool deleteMembership(const std::string& guild_id, const std::string& user_id) = 0;

    virtual std::optional<WireChannel> createChannel(const std::string& guild_id, const std::string& name,
                                                     const std::string& channel_type) = 0;
    virtual bool deleteChannel(const std::string& channel_id) = 0;

    // since_iso8601: pass the timestamp of the previous successful poll: the
    // response's "as_of" field. Design doc §9.
    virtual std::vector<std::string> fetchRevokedSessionIds(const std::string& since_iso8601,
                                                             std::string& out_as_of) = 0;
};

} // namespace http

#endif // CIG_NEXUS_HTTP_INTERNAL_API_CLIENT_HPP
