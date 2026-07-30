#ifndef CIG_NEXUS_UTIL_FILE_PERMISSIONS_HPP
#define CIG_NEXUS_UTIL_FILE_PERMISSIONS_HPP

#include <string>

namespace util {

// True if `path_env_var_name` names a private-key-path env var by
// convention (contains "PRIVATE_KEY") — e.g. SESSION_JWT_PRIVATE_KEY_PATH,
// but not AUTH_JWT_PUBLIC_KEY_PATH (the public half is meant to be widely
// readable, so it must never trip requireOwnerOnlyPermissions()). The
// server doesn't read a private key today — only the public key, via
// main.cpp's sole readRequiredFile() call — so this currently gates a
// no-op in production; it's convention-based rather than call-site-based
// so the same readRequiredFile() helper stays safe to reuse if that ever
// changes, without silently missing the check.
bool isPrivateKeyPathVar(const std::string& path_env_var_name);

// Throws std::runtime_error if `path` doesn't exist, can't be stat'd, or is
// group- or world-readable (docs/security-audit.md §1.3 / action item 3) —
// a private key on disk that isn't restricted to its owner defeats the
// point of keeping key material out of process env / `docker inspect`
// output in the first place.
void requireOwnerOnlyPermissions(const std::string& path);

} // namespace util

#endif // CIG_NEXUS_UTIL_FILE_PERMISSIONS_HPP
