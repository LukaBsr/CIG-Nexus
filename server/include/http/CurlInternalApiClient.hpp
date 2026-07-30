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
    std::optional<WireGuild> createGuild(const std::string& name,
                                         const std::string& owner_id) override;
    bool deleteGuild(const std::string& guild_id) override;
    bool createMembership(const std::string& guild_id, const std::string& user_id,
                          const std::string& role) override;
    bool deleteMembership(const std::string& guild_id, const std::string& user_id) override;
    std::optional<WireChannel> createChannel(const std::string& guild_id, const std::string& name,
                                             const std::string& channel_type) override;
    bool deleteChannel(const std::string& channel_id) override;
    std::vector<std::string> fetchRevokedSessionIds(const std::string& since_iso8601,
                                                    std::string& out_as_of) override;

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
