#ifndef CIG_NEXUS_SESSION_SESSION_HYDRATION_WORKER_HPP
#define CIG_NEXUS_SESSION_SESSION_HYDRATION_WORKER_HPP

#include "http/InternalApiClient.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace session {

// One (fd, user_id, app_session_id) IDENTIFY needing its post-IDENTIFY
// blocks/friends/profile load. app_session_id (the JWT's sid claim) is
// carried through so a completed result can be matched back against the
// still-current session for that fd, not misapplied to an unrelated
// connection that happens to reuse the same fd number later — see
// SessionHydrationResult's comment.
struct SessionHydrationJob {
    int fd;
    std::string user_id;
    std::string app_session_id;
};

// The outcome of one job, drained by the main loop and applied to
// SessionManager state — never touched from the worker thread itself,
// since SessionManager isn't thread-safe. `fd`/`app_session_id` are
// carried through unchanged from the originating job so the consumer can
// verify the session this was for is still the same one (matching on both
// — not just fd — closes the specific gap
// docs/known-issues.md's fd-reuse-race idea flags: a fd freed and reused
// by an unrelated IDENTIFY before this result is drained must not have
// this result applied to it, even though the fd number matches).
// `succeeded == false` means every retry attempt failed within the
// worker's bounded retry budget — the caller is expected to disconnect,
// not to degrade and continue (shared/protocol/README.md's Asynchronous
// IDENTIFY Hydration section: an unenforced block list is worse than a
// dropped connection).
struct SessionHydrationResult {
    int fd;
    std::string app_session_id;
    bool succeeded;
    http::WireSessionContext context; // only meaningful when succeeded
};

// Runs IdentifyHandler's post-IDENTIFY internal-API load
// (InternalApiClient::fetchSessionContext) off the single-threaded main
// loop, with bounded retry — the async counterpart to
// persistence::MessagePersistenceWorker, but bidirectional: that worker
// only ever consumes (enqueue -> fire-and-forget); this one also produces
// results the main loop must apply back to SessionManager state
// (drainResults()), since presence and DM_SEND both depend on knowing
// whether that load has completed.
//
// Retry schedule sums to ~60s of backoff by default (not counting each
// attempt's own up-to-5s HTTP timeout) — long enough to ride out a brief
// internal-API blip, short enough that a sustained outage disconnects the
// caller in a bounded time rather than leaving an unenforced block list
// in place indefinitely. Deliberately not "retry forever": the web client
// has no automatic reconnect logic yet (docs/known-issues.md), so an
// unbounded retry would be the lesser evil only until that exists —
// tracked there, not solved here. Injectable for tests, which use a much
// faster schedule.
class SessionHydrationWorker {
  public:
    explicit SessionHydrationWorker(
        http::InternalApiClient* client,
        std::vector<std::chrono::milliseconds> retry_delays = defaultRetryDelays());
    ~SessionHydrationWorker();

    SessionHydrationWorker(const SessionHydrationWorker&) = delete;
    SessionHydrationWorker& operator=(const SessionHydrationWorker&) = delete;

    static std::vector<std::chrono::milliseconds> defaultRetryDelays();

    // Starts the worker thread. Safe to call at most once; a second call
    // is a no-op.
    void start();

    // Signals the worker to finish draining whatever is already queued,
    // then joins the thread. Safe to call whether or not start() was ever
    // called, and safe to call more than once.
    void stop();

    // Thread-safe. In practice only ever called from the main loop thread.
    void enqueue(SessionHydrationJob job);

    // Thread-safe. Returns and clears every result completed since the
    // last call — the main loop calls this once per tick. Never blocks;
    // returns an empty vector if nothing has completed yet.
    std::vector<SessionHydrationResult> drainResults();

  private:
    void run();

    http::InternalApiClient* client_;
    std::vector<std::chrono::milliseconds> retry_delays_;
    std::thread worker_thread_;
    bool running_ = false;

    std::mutex input_mutex_;
    std::condition_variable input_cv_;
    std::deque<SessionHydrationJob> input_queue_;
    bool stop_requested_ = false;

    std::mutex output_mutex_;
    std::deque<SessionHydrationResult> output_queue_;
};

} // namespace session

#endif // CIG_NEXUS_SESSION_SESSION_HYDRATION_WORKER_HPP
