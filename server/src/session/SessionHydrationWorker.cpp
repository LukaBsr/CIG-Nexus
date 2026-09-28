#include "session/SessionHydrationWorker.hpp"

namespace session {

std::vector<std::chrono::milliseconds> SessionHydrationWorker::defaultRetryDelays() {
    // Sums to 60s. Doubling-ish, not exact powers of two, so the schedule
    // reads as deliberate round numbers rather than an arbitrary formula.
    return {std::chrono::seconds(1), std::chrono::seconds(2),  std::chrono::seconds(4),
            std::chrono::seconds(8), std::chrono::seconds(15), std::chrono::seconds(30)};
}

SessionHydrationWorker::SessionHydrationWorker(http::InternalApiClient* client,
                                               std::vector<std::chrono::milliseconds> retry_delays)
    : client_(client), retry_delays_(std::move(retry_delays)) {}

SessionHydrationWorker::~SessionHydrationWorker() {
    stop();
}

void SessionHydrationWorker::start() {
    if (running_) {
        return;
    }
    running_ = true;
    stop_requested_ = false;
    worker_thread_ = std::thread(&SessionHydrationWorker::run, this);
}

void SessionHydrationWorker::stop() {
    if (!running_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        stop_requested_ = true;
    }
    input_cv_.notify_one();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    running_ = false;
}

void SessionHydrationWorker::enqueue(SessionHydrationJob job) {
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_queue_.push_back(std::move(job));
    }
    input_cv_.notify_one();
}

std::vector<SessionHydrationResult> SessionHydrationWorker::drainResults() {
    std::lock_guard<std::mutex> lock(output_mutex_);
    std::vector<SessionHydrationResult> results(output_queue_.begin(), output_queue_.end());
    output_queue_.clear();
    return results;
}

void SessionHydrationWorker::run() {
    while (true) {
        SessionHydrationJob job;
        {
            std::unique_lock<std::mutex> lock(input_mutex_);
            input_cv_.wait(lock, [this] { return stop_requested_ || !input_queue_.empty(); });
            if (input_queue_.empty()) {
                // Only reachable with stop_requested_ true (the wait
                // predicate) and nothing left to drain.
                return;
            }
            job = std::move(input_queue_.front());
            input_queue_.pop_front();
        }

        std::optional<http::WireSessionContext> context;
        if (client_) {
            context = client_->fetchSessionContext(job.user_id);
            for (size_t attempt = 0; !context && attempt < retry_delays_.size(); ++attempt) {
                std::this_thread::sleep_for(retry_delays_[attempt]);
                context = client_->fetchSessionContext(job.user_id);
            }
        }

        SessionHydrationResult result{job.fd, job.app_session_id, context.has_value(),
                                      context.value_or(http::WireSessionContext{})};
        {
            std::lock_guard<std::mutex> lock(output_mutex_);
            output_queue_.push_back(std::move(result));
        }
    }
}

} // namespace session
