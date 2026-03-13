#ifndef CIG_NEXUS_SERVER_HPP
#define CIG_NEXUS_SERVER_HPP

#include "Connection.hpp"
#include "TcpListener.hpp"

#include "protocol/handlers/ChatHandler.hpp"
#include "protocol/handlers/IdentifyHandler.hpp"
#include "protocol/handlers/HelloHandler.hpp"
#include "protocol/MessageDispatcher.hpp"

#include "session/SessionManager.hpp"

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
    // Network send helpers
    bool sendMessage(int fd, const protocol::Message& message);
    void broadcast(const protocol::Message& message);

    // Server runtime state
    uint16_t port_;
    bool running_;
    TcpListener listener_;

    // Protocol dispatch and handlers
    protocol::MessageDispatcher dispatcher_;
    protocol::HelloHandler hello_handler_;
    protocol::ChatHandler chat_handler_;
    protocol::IdentifyHandler identify_handler_;

    // In-memory connection/session state
    session::SessionManager session_manager_;
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
};

#endif // CIG_NEXUS_SERVER_HPP
