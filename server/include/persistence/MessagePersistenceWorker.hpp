#ifndef CIG_NEXUS_PERSISTENCE_MESSAGE_PERSISTENCE_WORKER_HPP
#define CIG_NEXUS_PERSISTENCE_MESSAGE_PERSISTENCE_WORKER_HPP

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace http {
class InternalApiClient;
}

namespace persistence {

struct PendingMessage {
    std::optional<std::string> channel_id;
    std::string user_id;
    std::string content;
    int seq;
};

// docs/guilds/social-presence-design.md §4.5: the fire-and-forget persistence
// path for CHAT_MESSAGE/CHANNEL_MESSAGE — ChatHandler/ChannelHandler call
// enqueue() right after broadcasting, from the single-threaded
// Server::start() loop, so the hot path never waits on this. This worker's
// own thread drains the queue and calls InternalApiClient::createMessage()
// with bounded retry. A single worker thread draining a FIFO queue
// preserves persistence order matching broadcast order — the ordering
// detail §4.5 flags as required, not optional.
class MessagePersistenceWorker {
  public:
    explicit MessagePersistenceWorker(http::InternalApiClient* client);
    ~MessagePersistenceWorker();

    MessagePersistenceWorker(const MessagePersistenceWorker&) = delete;
    MessagePersistenceWorker& operator=(const MessagePersistenceWorker&) = delete;

    // Starts the worker thread. Safe to call at most once; a second call
    // is a no-op.
    void start();

    // Signals the worker to finish draining whatever is already queued,
    // then joins the thread. Safe to call whether or not start() was ever
    // called, and safe to call more than once (idempotent) — in
    // particular, safe to rely on being called again by the destructor
    // after an explicit stop() already ran.
    void stop();

    // Thread-safe. In practice only ever called from the main loop thread,
    // but makes no assumption about that.
    void enqueue(PendingMessage message);

  private:
    void run();

    http::InternalApiClient* client_;
    std::thread worker_thread_;
    bool running_ = false;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<PendingMessage> queue_;
    bool stop_requested_ = false;
};

} // namespace persistence

#endif // CIG_NEXUS_PERSISTENCE_MESSAGE_PERSISTENCE_WORKER_HPP
