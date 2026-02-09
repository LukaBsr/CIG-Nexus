#ifndef CIG_NEXUS_SERVER_HPP
#define CIG_NEXUS_SERVER_HPP

#include "TcpListener.hpp"
#include "protocol/handlers/HelloHandler.hpp"
#include "protocol/MessageDispatcher.hpp"
#include <cstdint>

class Server {
public:
    explicit Server(uint16_t port);

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start();
    void stop();

private:
    uint16_t port_;
    bool running_;
    TcpListener listener_;
    protocol::MessageDispatcher dispatcher_;
    protocol::HelloHandler hello_handler_;
};

#endif // CIG_NEXUS_SERVER_HPP
