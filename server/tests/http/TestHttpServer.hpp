#ifndef CIG_NEXUS_TESTS_HTTP_TEST_HTTP_SERVER_HPP
#define CIG_NEXUS_TESTS_HTTP_TEST_HTTP_SERVER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace test_helpers {

// A real, minimal HTTP/1.1 server bound to 127.0.0.1 on an OS-assigned
// port, used to exercise CurlInternalApiClient's actual request/response
// handling — the same "real sockets over mocks" approach already used by
// Connection.test.cpp's make_tcp_pair. Every accepted request gets the same
// canned status/body; requests are recorded for assertions.
class TestHttpServer {
  public:
    struct RecordedRequest {
        std::string method;
        std::string path;
        std::string body;
        std::string internal_secret_header; // empty if not sent
        bool has_content_type_json = false;
    };

    TestHttpServer(int response_status, std::string response_body);
    ~TestHttpServer();

    TestHttpServer(const TestHttpServer&) = delete;
    TestHttpServer& operator=(const TestHttpServer&) = delete;

    uint16_t port() const {
        return port_;
    }

    std::string baseUrl() const;

    // Blocks until a request has been handled (or a short timeout elapses),
    // then returns it. std::nullopt on timeout.
    std::optional<RecordedRequest> waitForRequest();

  private:
    void run();

    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> running_{true};
    std::thread thread_;

    int response_status_;
    std::string response_body_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<RecordedRequest> requests_;
};

} // namespace test_helpers

#endif // CIG_NEXUS_TESTS_HTTP_TEST_HTTP_SERVER_HPP
