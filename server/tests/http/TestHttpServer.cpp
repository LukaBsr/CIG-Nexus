#include "TestHttpServer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace test_helpers {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Reads until the connection closes or a reasonable request has been read;
// good enough for the small, well-formed requests CurlInternalApiClient
// sends in tests.
std::string readAll(int fd) {
    std::string data;
    char buffer[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        data.append(buffer, static_cast<size_t>(n));

        const size_t header_end = data.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            continue;
        }

        size_t content_length = 0;
        std::istringstream header_stream(data.substr(0, header_end));
        std::string line;
        while (std::getline(header_stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            if (toLower(line.substr(0, colon)) == "content-length") {
                content_length = static_cast<size_t>(std::stoul(line.substr(colon + 1)));
            }
        }

        const size_t body_so_far = data.size() - (header_end + 4);
        if (body_so_far >= content_length) {
            break;
        }
    }
    return data;
}

TestHttpServer::RecordedRequest parseRequest(const std::string& raw) {
    TestHttpServer::RecordedRequest request;

    const size_t header_end = raw.find("\r\n\r\n");
    const std::string head = header_end == std::string::npos ? raw : raw.substr(0, header_end);
    request.body = header_end == std::string::npos ? "" : raw.substr(header_end + 4);

    std::istringstream stream(head);
    std::string request_line;
    std::getline(stream, request_line);
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    std::string http_version;
    request_line_stream >> request.method >> request.path >> http_version;

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = toLower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        // Trim leading space.
        size_t start = value.find_first_not_of(' ');
        value = start == std::string::npos ? "" : value.substr(start);

        if (name == "x-internal-secret") {
            request.internal_secret_header = value;
        } else if (name == "content-type" && value.find("application/json") != std::string::npos) {
            request.has_content_type_json = true;
        }
    }

    return request;
}

} // namespace

TestHttpServer::TestHttpServer(int response_status, std::string response_body)
    : response_status_(response_status), response_body_(std::move(response_body)) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    socklen_t addr_len = sizeof(addr);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    port_ = ntohs(addr.sin_port);

    ::listen(listen_fd_, 16);

    thread_ = std::thread(&TestHttpServer::run, this);
}

TestHttpServer::~TestHttpServer() {
    running_ = false;
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::string TestHttpServer::baseUrl() const {
    return "http://127.0.0.1:" + std::to_string(port_);
}

void TestHttpServer::run() {
    while (running_) {
        const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            break; // listen_fd_ was closed by the destructor
        }

        const std::string raw = readAll(client_fd);
        const RecordedRequest request = parseRequest(raw);

        std::string response = "HTTP/1.1 " + std::to_string(response_status_) + " OK\r\n";
        response += "Content-Type: application/json\r\n";
        response += "Content-Length: " + std::to_string(response_body_.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += response_body_;

        ::send(client_fd, response.data(), response.size(), 0);
        ::close(client_fd);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.push(request);
        }
        cv_.notify_one();
    }
}

std::optional<TestHttpServer::RecordedRequest> TestHttpServer::waitForRequest() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::seconds(2), [this] { return !requests_.empty(); })) {
        return std::nullopt;
    }
    RecordedRequest request = requests_.front();
    requests_.pop();
    return request;
}

} // namespace test_helpers
