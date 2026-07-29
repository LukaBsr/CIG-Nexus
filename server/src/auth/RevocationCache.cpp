#include "auth/RevocationCache.hpp"

namespace auth {

bool RevocationCache::isRevoked(const std::string& session_id) const {
    return revoked_session_ids_.find(session_id) != revoked_session_ids_.end();
}

void RevocationCache::merge(const std::vector<std::string>& revoked_session_ids) {
    for (const auto& id : revoked_session_ids) {
        revoked_session_ids_.insert(id);
    }
}

} // namespace auth
