#ifndef CIG_NEXUS_CONNECTION_HPP
#define CIG_NEXUS_CONNECTION_HPP

#include <cstdint>
#include <vector>

class FrameDecoder;

class Connection {
public:
    explicit Connection(int socket_fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const;
    bool readFromSocket();
    std::vector<std::vector<uint8_t>> pollFrames();

private:
    int socket_fd_;
    std::vector<uint8_t> buffer_;
};

#endif // CIG_NEXUS_CONNECTION_HPP
