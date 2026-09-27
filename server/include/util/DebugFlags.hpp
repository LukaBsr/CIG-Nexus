#ifndef CIG_NEXUS_UTIL_DEBUG_FLAGS_HPP
#define CIG_NEXUS_UTIL_DEBUG_FLAGS_HPP

#include <string>

namespace util {

// Temporary instrumentation for docs/known-issues.md's presence
// connection-count leak — off by default, enabled by setting
// CIG_NEXUS_DEBUG_PRESENCE=1 in the environment before starting the
// server. Not meant to stay forever: remove the call sites (and this
// file) once that issue is fixed or confirmed no longer reproducible.
//
// Reads the environment on every call rather than caching the result, so
// tests can set/unset the variable and observe the effect immediately —
// this is only called around presence transitions and IDENTIFY, not on
// any per-message hot path, so the repeated getenv() cost doesn't matter.
bool presenceDebugLoggingEnabled();

// No-op unless presenceDebugLoggingEnabled(). Prints a timestamped
// "[presence-debug] " line to stderr, so it interleaves without being
// mixed into the normal stdout connection-lifecycle logging by default.
void logPresenceDebug(const std::string& message);

} // namespace util

#endif // CIG_NEXUS_UTIL_DEBUG_FLAGS_HPP
