<h1 align="center">CIG-Nexus</h1>

<p align="center">
	Real-time chat platform with a Next.js web client, a transport-only WebSocket gateway, and a C++ TCP server.
</p>

<p align="center">
	<img alt="Web" src="https://img.shields.io/badge/Web-Next.js%2016-111111" />
	<img alt="Gateway" src="https://img.shields.io/badge/Gateway-Node.js%20%2B%20TypeScript-3178C6" />
	<img alt="Server" src="https://img.shields.io/badge/Server-C%2B%2B-00599C" />
	<img alt="Protocol" src="https://img.shields.io/badge/Protocol-WebSocket%20%E2%86%94%20TCP-0F766E" />
  <img alt="Status" src="https://img.shields.io/badge/Status-v0.4%20-D97706" />
</p>

## Vision

**CIG-Nexus** is a lightweight real-time messaging platform built to keep transport concerns, protocol handling, and client UI clearly separated.

Current focus:

- Deliver a working end-to-end chat flow from browser to TCP backend
- Keep the gateway transport-only with no business logic
- Keep protocol handling explicit and testable in the server
- Make local development simple with Docker Compose and isolated services

## Architecture

```text
Next.js Web Client
        |
        | WebSocket / JSON strings
        v
Gateway (Node.js + TypeScript)
        |
        | TCP / 4-byte big-endian framed JSON
        v
CIG Nexus Server (C++)
```

## Stack

| Domain | Technology |
|---|---|
| Web client | Next.js 16, React 19, TypeScript |
| Gateway | Node.js, TypeScript, ws |
| Backend server | C++, CMake |
| Tests | Catch2 |
| Local orchestration | Docker Compose |

## Features

### Web

- Next.js App Router client
- Browser-only WebSocket connection
- Automatic `HELLO` on connect
- Automatic `IDENTIFY` on `WELCOME` (default username: `web_user`)
- Simple chat UI with live message list

### Gateway

- One WebSocket connection maps to one TCP connection
- WebSocket messages are JSON strings
- TCP messages use 4-byte big-endian framing
- No business logic, no validation, no auth, no session management

### Server

- TCP listener and connection tracking
- Frame decoding and message parsing
- `HELLO` / `WELCOME` handshake
- `IDENTIFY` / `IDENTIFIED` identity flow
- Session creation on `IDENTIFY` (not on raw connect)
- `CHAT_MESSAGE` handling and scope-based broadcast routing
- Protocol handlers covered by Catch2 tests

## Quick Start

### 1. Prerequisites

- Docker
- Docker Compose

### 2. Start the stack

```bash
docker compose up --build
```

Available services:

- Web: `http://localhost:3000`
- Gateway WebSocket: `ws://localhost:8080`
- Server TCP: `localhost:4242`

## Local Development

### Server

```bash
cd server
mkdir -p build
cd build
cmake ..
cmake --build .
./cig-nexus-server
```

### Gateway

```bash
cd gateway
npm install
npm run build
WS_PORT=8080 TCP_HOST=localhost TCP_PORT=4242 npm start
```

### Web

```bash
cd web
npm install
npm run dev
```

## Example Flow

The browser connects to the gateway and sends:

```json
{
  "type": "HELLO",
  "version": "0.1",
  "client": "web"
}
```

After `WELCOME`, the web client automatically identifies:

```json
{
  "type": "IDENTIFY",
  "username": "web_user"
}
```

Then chat messages are sent as:

```json
{
  "type": "CHAT_MESSAGE",
  "content": "hello"
}
```

Successful chat responses currently include metadata such as:

```json
{
  "type": "CHAT_MESSAGE",
  "message_id": 1,
  "timestamp": 1741104000,
  "user_id": "u_1",
  "username": "web_user",
  "content": "hello"
}
```

## Structure

```text
.
├── web/                # Next.js web client
├── gateway/            # WebSocket <-> TCP gateway
├── server/             # C++ TCP server and protocol handlers
├── shared/             # Shared protocol documentation
├── docs/               # Architecture notes
└── docker-compose.yml  # Local orchestration
```

## Documentation

- See [gateway/README.md](gateway/README.md) for gateway details.
- See [server/README.md](server/README.md) for server details.
- See [shared/protocol/README.md](shared/protocol/README.md) for shared protocol documentation.

## Status

Current project state:

- Web client is functional and can send chat messages
- Gateway is functional as a transport bridge
- Server handles `HELLO`, `IDENTIFY`, and `CHAT_MESSAGE`
- Broadcast flow is implemented through `Message.scope`
- Dockerized local stack is available

Still pending or intentionally out of scope:

- Authentication and authorization
- Persistent sessions and identities across restarts
- Database persistence
- Production-grade event loop / scaling concerns
- TLS and deployment hardening

---

Project CIG-Nexus - an experimental real-time chat platform with a clear separation between client, gateway, and server.
