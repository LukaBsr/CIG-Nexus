#include "session/SessionHydrationWorker.hpp"

#include "http/InternalApiClient.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace {

// A small, purpose-built fake, mirroring
// tests/persistence/MessagePersistenceWorker.test.cpp's
// RecordingInternalApiClient — per-call control (fail N times, then
// succeed) to actually exercise the retry loop, plus thread-safe state
// since the worker calls it from its own thread. Every method besides
// fetchSessionContext is a stub; nothing else is exercised here.
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
    bool createMessage(const std::optional<std::string>&, const std::optional<std::string>&,
                       const std::string&, const std::string&, int) override {
        return false;
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
        return {};
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
        return {};
    }
    http::SendFriendRequestResult addFriendByCode(const std::string&, const std::string&) override {
        return {};
    }
    http::AcceptFriendRequestResult acceptFriendRequest(const std::string&,
                                                        const std::string&) override {
        return {};
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

    std::optional<http::WireSessionContext>
    fetchSessionContext(const std::string& user_id) override {
        const int attempt = ++call_count_;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            calls.push_back(user_id);
        }
        if (attempt <= fail_first_n_calls) {
            return std::nullopt;
        }
        http::WireSessionContext context = context_to_return;
        context.profile.user_id = user_id;
        return context;
    }

    // How many leading calls should fail before succeeding — set before
    // enqueueing to exercise the worker's retry loop. A value >= the
    // worker's retry-delay-count-plus-one means "always fail."
    std::atomic<int> fail_first_n_calls{0};
    http::WireSessionContext context_to_return;

    int callCount() { return call_count_; }

    std::vector<std::string> callsSnapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls;
    }

  private:
    std::atomic<int> call_count_{0};
    std::mutex mutex_;
    std::vector<std::string> calls;
};

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

// Fast, deterministic schedule for tests — the real ~60s default would
// make these tests unusable.
std::vector<std::chrono::milliseconds> fastRetryDelays(size_t count) {
    return std::vector<std::chrono::milliseconds>(count, std::chrono::milliseconds(5));
}

} // namespace

TEST_CASE("SessionHydrationWorker succeeds on the first attempt and reports the result",
          "[SessionHydrationWorker]") {
    RecordingInternalApiClient client;
    client.context_to_return.profile.username = "alice";
    session::SessionHydrationWorker worker(&client, fastRetryDelays(3));
    worker.start();

    worker.enqueue({42, "u_1", "sid-1"});

    std::vector<session::SessionHydrationResult> results;
    REQUIRE(waitUntil(
        [&] {
            auto drained = worker.drainResults();
            if (!drained.empty()) {
                results = std::move(drained);
            }
            return !results.empty();
        },
        std::chrono::seconds(2)));

    worker.stop();

    REQUIRE(results.size() == 1);
    CHECK(results[0].fd == 42);
    CHECK(results[0].app_session_id == "sid-1");
    CHECK(results[0].succeeded);
    CHECK(results[0].context.profile.user_id == "u_1");
    CHECK(results[0].context.profile.username == "alice");
    CHECK(client.callCount() == 1);
}

TEST_CASE("SessionHydrationWorker retries a failed load and eventually succeeds",
          "[SessionHydrationWorker]") {
    RecordingInternalApiClient client;
    client.fail_first_n_calls = 2; // fails attempts 1 and 2, succeeds on attempt 3
    session::SessionHydrationWorker worker(&client, fastRetryDelays(5));
    worker.start();

    worker.enqueue({7, "u_2", "sid-2"});

    std::vector<session::SessionHydrationResult> results;
    REQUIRE(waitUntil(
        [&] {
            auto drained = worker.drainResults();
            if (!drained.empty()) {
                results = std::move(drained);
            }
            return !results.empty();
        },
        std::chrono::seconds(2)));

    worker.stop();

    REQUIRE(results.size() == 1);
    CHECK(results[0].succeeded);
    CHECK(client.callCount() == 3);
}

TEST_CASE("SessionHydrationWorker reports failure once the retry budget is exhausted",
          "[SessionHydrationWorker]") {
    RecordingInternalApiClient client;
    client.fail_first_n_calls = 1000; // never succeeds
    const auto delays = fastRetryDelays(3);
    session::SessionHydrationWorker worker(&client, delays);
    worker.start();

    worker.enqueue({9, "u_3", "sid-3"});

    std::vector<session::SessionHydrationResult> results;
    REQUIRE(waitUntil(
        [&] {
            auto drained = worker.drainResults();
            if (!drained.empty()) {
                results = std::move(drained);
            }
            return !results.empty();
        },
        std::chrono::seconds(2)));

    worker.stop();

    REQUIRE(results.size() == 1);
    CHECK_FALSE(results[0].succeeded);
    // One initial attempt plus one per configured delay.
    CHECK(client.callCount() == static_cast<int>(delays.size()) + 1);
}

TEST_CASE("SessionHydrationWorker processes jobs in order and keeps their identity separate",
          "[SessionHydrationWorker]") {
    RecordingInternalApiClient client;
    session::SessionHydrationWorker worker(&client, fastRetryDelays(2));
    worker.start();

    worker.enqueue({1, "u_a", "sid-a"});
    worker.enqueue({2, "u_b", "sid-b"});

    std::vector<session::SessionHydrationResult> results;
    REQUIRE(waitUntil(
        [&] {
            auto drained = worker.drainResults();
            results.insert(results.end(), drained.begin(), drained.end());
            return results.size() >= 2;
        },
        std::chrono::seconds(2)));

    worker.stop();

    REQUIRE(results.size() == 2);
    CHECK(results[0].fd == 1);
    CHECK(results[0].app_session_id == "sid-a");
    CHECK(results[0].context.profile.user_id == "u_a");
    CHECK(results[1].fd == 2);
    CHECK(results[1].app_session_id == "sid-b");
    CHECK(results[1].context.profile.user_id == "u_b");
}

TEST_CASE("SessionHydrationWorker::drainResults returns empty when nothing has completed",
          "[SessionHydrationWorker]") {
    RecordingInternalApiClient client;
    session::SessionHydrationWorker worker(&client, fastRetryDelays(2));

    CHECK(worker.drainResults().empty());
}

TEST_CASE("SessionHydrationWorker::defaultRetryDelays sums to about a minute",
          "[SessionHydrationWorker]") {
    const auto delays = session::SessionHydrationWorker::defaultRetryDelays();
    std::chrono::milliseconds total{0};
    for (const auto& delay : delays) {
        total += delay;
    }
    CHECK(total == std::chrono::seconds(60));
}
