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
    std::optional<http::Catalog> fetchCatalog() override {
        return catalog_to_return;
    }

    std::optional<http::WireGuild> createGuild(const std::string& name, const std::string& owner_id) override {
        if (fail_create_guild) {
            return std::nullopt;
        }
        return http::WireGuild{"g_fake_" + std::to_string(next_guild_id_++), name, owner_id};
    }

    bool deleteGuild(const std::string&) override {
        return !fail_delete_guild;
    }

    bool createMembership(const std::string&, const std::string&, const std::string&) override {
        return !fail_create_membership;
    }

    bool deleteMembership(const std::string&, const std::string&) override {
        return !fail_delete_membership;
    }

    std::optional<http::WireChannel> createChannel(const std::string& guild_id, const std::string& name,
                                                   const std::string& channel_type) override {
        if (fail_create_channel) {
            return std::nullopt;
        }
        return http::WireChannel{"c_fake_" + std::to_string(next_channel_id_++), guild_id, name,
                                 channel_type};
    }

    bool deleteChannel(const std::string&) override {
        return !fail_delete_channel;
    }

    std::vector<std::string> fetchRevokedSessionIds(const std::string&, std::string& out_as_of) override {
        out_as_of.clear();
        return {};
    }

    // Test control: flip one of these to exercise a handler's "internal API
    // call failed" path (should become INTERNAL_ERROR without mutating any
    // local cache/session state).
    bool fail_create_guild = false;
    bool fail_delete_guild = false;
    bool fail_create_membership = false;
    bool fail_delete_membership = false;
    bool fail_create_channel = false;
    bool fail_delete_channel = false;
    http::Catalog catalog_to_return;

  private:
    int next_guild_id_ = 1;
    int next_channel_id_ = 1;
};

} // namespace test_helpers

#endif // CIG_NEXUS_TESTS_HTTP_FAKE_INTERNAL_API_CLIENT_HPP
