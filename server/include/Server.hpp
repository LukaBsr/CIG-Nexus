#ifndef CIG_NEXUS_SERVER_HPP
#define CIG_NEXUS_SERVER_HPP

#include "Connection.hpp"
#include "TcpListener.hpp"
#include "protocol/handlers/HelloHandler.hpp"
#include "protocol/MessageDispatcher.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>

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
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
};

#endif // CIG_NEXUS_SERVER_HPP
