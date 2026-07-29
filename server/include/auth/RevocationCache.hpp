#ifndef CIG_NEXUS_AUTH_REVOCATION_CACHE_HPP
#define CIG_NEXUS_AUTH_REVOCATION_CACHE_HPP

#include <string>
#include <unordered_set>
#include <vector>

namespace auth {

// Design doc §9: local, poll-refreshed cache of revoked session ids (a
// JWT's `sid` claim). Checked on IDENTIFY and by the periodic sweep of
// already-connected sessions (Server.cpp) — never a network call itself;
// something else (Server.cpp, via InternalApiClient::fetchRevokedSessionIds)
// owns actually polling and calling merge().
class RevocationCache {
  public:
    bool isRevoked(const std::string& session_id) const;
    void merge(const std::vector<std::string>& revoked_session_ids);

  private:
    std::unordered_set<std::string> revoked_session_ids_;
};

} // namespace auth

#endif // CIG_NEXUS_AUTH_REVOCATION_CACHE_HPP
