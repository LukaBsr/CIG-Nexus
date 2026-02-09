#ifndef CIG_NEXUS_TCP_LISTENER_HPP
#define CIG_NEXUS_TCP_LISTENER_HPP

#include <cstdint>

class TcpListener {
public:
    explicit TcpListener(uint16_t port);
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    void start();
    int accept();

private:
    uint16_t port_;
    int socket_fd_;
};

#endif // CIG_NEXUS_TCP_LISTENER_HPP
