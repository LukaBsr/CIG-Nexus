# CIG Nexus Server

The CIG Nexus Server is the authoritative TCP backend for the CIG Nexus real-time communication platform. It owns state, validates client intent, and coordinates message flow across clients.

## Architecture Overview

- **TCP server (non-blocking)** for scalable connection handling on Linux
- **Binary framing** for clear message boundaries
- **JSON-based protocol** for readable payloads and early-stage iteration
- **Message dispatcher and handlers** to route protocol messages by type

## Directory Structure

- `include/` - Public headers and protocol types
- `src/` - Server implementation
- `tests/` - Unit tests for protocol and core logic

## Protocol Overview

Protocol documentation is defined in [../shared/protocol/README.md](../shared/protocol/README.md).

At a high level, clients connect over TCP and send a `HELLO` message. The server validates the protocol version and client type, then responds with `WELCOME` or `ERROR`.

## Build

CMake is used for configuration and builds.

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

## Run

Start the server from the build output:

```bash
./cig-nexus-server
```

Default port: `4242`.

## Testing

Unit tests are built with CMake and Catch2.

```bash
cd build
cmake ..
cmake --build .
ctest
```

## Project Status

**Current (v0.1):**
- HELLO / WELCOME handshake implemented
- Message parsing and dispatching
- Frame decoding for binary-framed messages

**Not implemented yet:**
- Authentication and authorization
- Persistent sessions and user management
- Message broadcasting
- Production event loop (poll/epoll integration)
- Connection lifecycle management (timeouts, limits, etc.)
- TLS or encryption

## Notes

Design goals:
- Clarity over cleverness
- Correctness in protocol handling
- Testability and incremental evolution
