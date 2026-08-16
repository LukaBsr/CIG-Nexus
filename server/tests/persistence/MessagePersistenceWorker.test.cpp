#include "persistence/MessagePersistenceWorker.hpp"

#include "http/InternalApiClient.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// A small, purpose-built fake distinct from test_helpers::FakeInternalApiClient
// (tests/http/FakeInternalApiClient.hpp) — that one's fail_create_message is a
// static on/off switch; this test needs per-call control (e.g. "fail twice,
// then succeed") to actually exercise the retry loop, plus thread-safe
// recording since the worker calls it from its own thread.
class RecordingInternalApiClient : public http::InternalApiClient {
  public:
    std::optional<http::Catalog> fetchCatalog() override { return std::nullopt; }
    std::optional<http::WireGuild> createGuild(const std::string&, const std::string&,
                                               const std::string&) override {
        return std::nullopt;
    }
    bool deleteGuild(const std::string&) override { return false; }
    std::optional<std::string> setGuildVisibility(const std::string&, const std::string&) override {
        return std::nullopt;
    }
    std::optional<int> createMembership(const std::string&, const std::string&, int) override {
        return std::nullopt;
    }
    bool deleteMembership(const std::string&, const std::string&) override { return false; }
    std::optional<std::vector<http::WireMember>> fetchGuildMembers(const std::string&) override {
        return std::nullopt;
    }
    std::optional<std::string> setMemberRole(const std::string&, const std::string&, int) override {
        return std::nullopt;
    }
    std::optional<http::WireChannel> createChannel(const std::string&, const std::string&,
                                                   const std::string&) override {
        return std::nullopt;
    }
    bool deleteChannel(const std::string&) override { return false; }
    std::vector<std::string> fetchRevokedSessionIds(const std::string&, std::string&) override {
        return {};
    }
    std::optional<http::HistoryPage> fetchMessages(const std::optional<std::string>&,
                                                   const std::optional<std::string>&,
                                                   const std::string&, std::optional<int>,
                                                   int) override {
        return std::nullopt;
    }
    http::LastSequence fetchLastSequence() override { return {}; }
    std::optional<std::vector<http::WireDmConversation>>
    fetchDmConversations(const std::string&) override {
        return std::nullopt;
    }
    std::optional<std::vector<std::string>> fetchGuildIdsForUser(const std::string&) override {
        return std::nullopt;
    }

    std::optional<http::WireInvite> createInvite(const std::string&, const std::string&,
                                                 std::optional<int>, std::optional<int>) override {
        return std::nullopt;
    }
    std::optional<std::vector<http::WireInvite>> fetchInvites(const std::string&) override {
        return std::nullopt;
    }
    bool revokeInvite(const std::string&, const std::string&) override { return false; }
    http::RedeemInviteResult redeemInvite(const std::string&, const std::string&) override {
        return http::RedeemInviteResult{};
    }
    http::CreateJoinRequestResult createJoinRequest(const std::string&,
                                                    const std::string&) override {
        return http::CreateJoinRequestResult::FAILED;
    }
    std::optional<std::vector<http::WireJoinRequest>>
    fetchJoinRequests(const std::string&) override {
        return std::nullopt;
    }
    std::optional<int> approveJoinRequest(const std::string&, const std::string&) override {
        return std::nullopt;
    }
    bool rejectJoinRequest(const std::string&, const std::string&) override { return false; }

    http::SendFriendRequestResult sendFriendRequest(const std::string&,
                                                    const std::string&) override {
        return http::SendFriendRequestResult{};
    }
    http::SendFriendRequestResult addFriendByCode(const std::string&, const std::string&) override {
        return http::SendFriendRequestResult{};
    }
    http::AcceptFriendRequestResult acceptFriendRequest(const std::string&,
                                                        const std::string&) override {
        return http::AcceptFriendRequestResult{};
    }
    bool deleteFriendRequest(const std::string&, const std::string&) override { return false; }
    bool removeFriend(const std::string&, const std::string&) override { return false; }
    std::optional<std::vector<http::WireFriend>> fetchFriends(const std::string&) override {
        return std::nullopt;
    }
    std::optional<http::FriendRequestList> fetchFriendRequests(const std::string&) override {
        return std::nullopt;
    }
    std::optional<std::string> fetchFriendCode(const std::string&) override { return std::nullopt; }
    std::optional<std::string> regenerateFriendCode(const std::string&) override {
        return std::nullopt;
    }

    bool blockUser(const std::string&, const std::string&) override { return false; }
    bool unblockUser(const std::string&, const std::string&) override { return false; }
    std::optional<std::vector<http::WireBlock>> fetchBlocks(const std::string&) override {
        return std::nullopt;
    }
    std::optional<http::WireUserProfile> fetchUserProfile(const std::string&) override {
        return std::nullopt;
    }

    bool createMessage(const std::optional<std::string>& channel_id,
                       const std::optional<std::string>& dm_peer_id, const std::string& user_id,
                       const std::string& content, int seq) override {
        const int attempt = ++call_count_;

        std::lock_guard<std::mutex> lock(mutex_);
        calls.push_back({channel_id, dm_peer_id, user_id, content, seq});

        return attempt > fail_first_n_calls;
    }

    // How many *total* calls (across all messages, since this fake doesn't
    // distinguish by message) should fail before succeeding — set before
    // enqueueing to exercise the worker's retry loop.
    std::atomic<int> fail_first_n_calls{0};

    std::vector<persistence::PendingMessage> callsSnapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls;
    }

  private:
    std::atomic<int> call_count_{0};
    std::mutex mutex_;
    std::vector<persistence::PendingMessage> calls;
};

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

} // namespace

TEST_CASE("MessagePersistenceWorker persists an enqueued message", "[MessagePersistenceWorker]") {
    RecordingInternalApiClient client;
    persistence::MessagePersistenceWorker worker(&client);
    worker.start();

    worker.enqueue({std::nullopt, std::nullopt, "u_1", "hello", 1});

    REQUIRE(waitUntil([&] { return client.callsSnapshot().size() == 1; },
                      std::chrono::milliseconds(2000)));
    const auto calls = client.callsSnapshot();
    REQUIRE(calls[0].content == "hello");
    REQUIRE(calls[0].seq == 1);
    REQUIRE_FALSE(calls[0].channel_id.has_value());

    worker.stop();
}

TEST_CASE("MessagePersistenceWorker preserves enqueue order", "[MessagePersistenceWorker]") {
    RecordingInternalApiClient client;
    persistence::MessagePersistenceWorker worker(&client);
    worker.start();

    for (int i = 1; i <= 5; ++i) {
        worker.enqueue({std::nullopt, std::nullopt, "u_1", "msg-" + std::to_string(i), i});
    }

    REQUIRE(waitUntil([&] { return client.callsSnapshot().size() == 5; },
                      std::chrono::milliseconds(2000)));
    worker.stop();

    const auto calls = client.callsSnapshot();
    for (int i = 0; i < 5; ++i) {
        REQUIRE(calls[static_cast<size_t>(i)].seq == i + 1);
    }
}

TEST_CASE("MessagePersistenceWorker retries a failed persist and eventually succeeds",
          "[MessagePersistenceWorker]") {
    RecordingInternalApiClient client;
    client.fail_first_n_calls = 2; // fails attempts 1 and 2, succeeds on attempt 3

    persistence::MessagePersistenceWorker worker(&client);
    worker.start();

    worker.enqueue({std::nullopt, std::nullopt, "u_1", "eventually", 1});

    // 3 attempts with 500ms/1000ms backoff between them — generous timeout.
    REQUIRE(waitUntil([&] { return client.callsSnapshot().size() == 3; },
                      std::chrono::milliseconds(5000)));
    worker.stop();

    const auto calls = client.callsSnapshot();
    REQUIRE(calls.size() == 3);
    for (const auto& call : calls) {
        REQUIRE(call.seq == 1);
        REQUIRE(call.content == "eventually");
    }
}

TEST_CASE("MessagePersistenceWorker::stop drains the queue before returning",
          "[MessagePersistenceWorker]") {
    RecordingInternalApiClient client;
    persistence::MessagePersistenceWorker worker(&client);
    worker.start();

    for (int i = 1; i <= 3; ++i) {
        worker.enqueue({std::nullopt, std::nullopt, "u_1", "msg-" + std::to_string(i), i});
    }
    worker.stop(); // should block until the queue is fully drained

    REQUIRE(client.callsSnapshot().size() == 3);
}

TEST_CASE("MessagePersistenceWorker::stop is safe to call without start",
          "[MessagePersistenceWorker]") {
    RecordingInternalApiClient client;
    persistence::MessagePersistenceWorker worker(&client);
    worker.stop(); // must not hang or crash
    REQUIRE(true);
}
