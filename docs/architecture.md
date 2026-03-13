# CIG Nexus Architecture

This document describes the architecture as it exists today in the repository.

## System Overview

The application is currently composed of three active runtime pieces:

- **Web client** built with Next.js
- **Gateway** built with Node.js and TypeScript
- **Authoritative server** built with C++

```text
Browser (Next.js)
      |
      | WebSocket / JSON strings
      v
Gateway (Node.js + TypeScript)
      |
      | TCP / 4-byte big-endian framed JSON
      v
Authoritative Server (C++)
```

## Current Separation of Concerns

### Web Client

Responsible for:

- opening a browser WebSocket connection
- sending `HELLO` on connect
- sending `IDENTIFY` after `WELCOME` (currently default username)
- sending `CHAT_MESSAGE` payloads from the UI
- displaying connection status and received messages

Not responsible for:

- protocol validation
- authentication
- message routing
- server-side state decisions

### Gateway

Responsible for:

- WebSocket termination for browsers
- one WebSocket to one TCP connection mapping
- TCP frame encoding and decoding
- forwarding messages without modifying their meaning

Not responsible for:

- business logic
- protocol validation
- session management
- authorization

### Server

Responsible for:

- accepting TCP clients
- tracking active connections
- parsing framed JSON messages
- validating protocol payloads
- dispatching handlers
- creating in-memory identity sessions on `IDENTIFY`
- routing responses by semantic scope (`DIRECT` vs `BROADCAST`)
- broadcasting valid chat messages to all connected clients

## Current Implemented Flow

### Handshake

1. The web client connects to the gateway.
2. The gateway opens a TCP connection to the server.
3. The web client sends `HELLO`.
4. The server validates it.
5. The server returns `WELCOME` or `ERROR`.

### Identity

1. After `WELCOME`, the client sends `IDENTIFY` with a username.
2. The server validates username constraints.
3. The server creates a session for that socket.
4. The server returns `IDENTIFIED` with `user_id` and `username`.

### Chat

1. The web client sends `CHAT_MESSAGE` with `content`.
2. The gateway forwards it unchanged apart from transport framing.
3. The server validates payload and identity state.
4. The server creates a normalized chat message with identity metadata.
5. The message is marked `Scope::BROADCAST`.
6. The server broadcasts that message to every active connection.
7. The gateway forwards the resulting JSON message back to browsers.

## Protocol Transport

TCP framing is shared across all non-browser server communication:

- 4-byte big-endian unsigned payload size
- JSON payload bytes immediately after the size prefix
- maximum frame size currently enforced at `1 MiB`

The browser side uses plain WebSocket text messages carrying JSON.

## Runtime Topology

In local development, the expected stack is:

- web client on port `3000`
- gateway on port `8080`
- TCP server on port `4242`

This topology is also represented in the root `docker-compose.yml`.

## Current Project Status

Implemented:

- browser chat UI
- gateway transport bridge
- TCP server connection tracking
- `HELLO` / `WELCOME`
- `IDENTIFY` / `IDENTIFIED`
- `CHAT_MESSAGE` validation and normalization
- in-memory session manager keyed by socket fd
- broadcast to connected clients

Not implemented yet:

- authentication
- persistence for sessions/users across process restarts
- persistence layer
- native WebSocket support in the C++ server
- production-grade scalability and hardening

## Desktop Client Status

The repository contains a `desktop/` area, but the current documented and working end-to-end flow is centered on:

- Next.js web client
- gateway
- C++ server

Desktop integration is not the primary active path at this stage.

## Design Principles

- **Explicit transport boundaries**: browser transport and TCP transport are separated cleanly
- **Authoritative backend**: validation and routing decisions happen in the server
- **Minimal gateway**: the gateway remains disposable and transport-only
- **Incremental evolution**: features are added without collapsing transport and protocol responsibilities together

## Related Documentation

- See [../shared/protocol/README.md](../shared/protocol/README.md) for wire-level protocol behavior.
- See [../gateway/README.md](../gateway/README.md) for gateway implementation notes.
- See [../server/README.md](../server/README.md) for backend implementation details.
