#include "persistence/MessagePersistenceWorker.hpp"

#include "http/InternalApiClient.hpp"

#include <array>
#include <chrono>
#include <iostream>

namespace persistence {

namespace {
constexpr int kMaxAttempts = 3;
constexpr std::array<std::chrono::milliseconds, 2> kRetryDelays{std::chrono::milliseconds(500),
                                                                std::chrono::milliseconds(1000)};
} // namespace

MessagePersistenceWorker::MessagePersistenceWorker(http::InternalApiClient* client)
    : client_(client) {}

MessagePersistenceWorker::~MessagePersistenceWorker() {
    stop();
}

void MessagePersistenceWorker::start() {
    if (running_) {
        return;
    }
    running_ = true;
    stop_requested_ = false;
    worker_thread_ = std::thread(&MessagePersistenceWorker::run, this);
}

void MessagePersistenceWorker::stop() {
    if (!running_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    running_ = false;
}

void MessagePersistenceWorker::enqueue(PendingMessage message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(message));
    }
    cv_.notify_one();
}

void MessagePersistenceWorker::run() {
    while (true) {
        PendingMessage message;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_requested_ || !queue_.empty(); });
            if (queue_.empty()) {
                // Only reachable with stop_requested_ true (the wait
                // predicate) and nothing left to drain.
                return;
            }
            message = std::move(queue_.front());
            queue_.pop_front();
        }

        bool persisted = false;
        for (int attempt = 0; attempt < kMaxAttempts && !persisted; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(kRetryDelays[static_cast<size_t>(attempt - 1)]);
            }
            if (client_ && client_->createMessage(message.channel_id, message.dm_peer_id,
                                                  message.user_id, message.content, message.seq)) {
                persisted = true;
            }
        }

        if (!persisted) {
            std::cerr << "Failed to persist message (seq=" << message.seq << ") after "
                      << kMaxAttempts << " attempts" << std::endl;
        }
    }
}

} // namespace persistence
