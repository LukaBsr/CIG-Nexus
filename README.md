<h1 align="center">CIG-Nexus</h1>

<p align="center">
	Real-time chat platform with a Next.js web client, a transport-only WebSocket gateway, and a C++ TCP server.
</p>

<p align="center">
	<img alt="Web" src="https://img.shields.io/badge/Web-Next.js%2016-111111" />
	<img alt="Gateway" src="https://img.shields.io/badge/Gateway-Node.js%20%2B%20TypeScript-3178C6" />
	<img alt="Server" src="https://img.shields.io/badge/Server-C%2B%2B-00599C" />
	<img alt="Protocol" src="https://img.shields.io/badge/Protocol-WebSocket%20%E2%86%94%20TCP-0F766E" />
  <img alt="Status" src="https://img.shields.io/badge/Status-v0.7%20-D97706" />
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
| Auth | Discord OAuth2 (PKCE), signed RS256 session JWTs |
| Persistence | PostgreSQL + Drizzle ORM, Redis |
| Tests | Catch2 |
| Local orchestration | Docker Compose |

## Features

### Web

- Next.js App Router client, Discord OAuth2 login
- Session-token `IDENTIFY` — no client-chosen username
- Global lobby chat, plus a Guilds tab: create/join guilds (open, application, or private visibility), invites, join requests, officer-gated channel creation, per-channel chat
- Guild member roster with rank-based roles (Crew / Officer / Captain) and live online/offline presence
- Friends tab: friend requests (direct or by code), blocking, and 1:1 direct messages with history
- Settings modal: customizable profile (display name, avatar), Appearance theme switching, blocked-users list

### Gateway

- One WebSocket connection maps to one TCP connection
- WebSocket messages are JSON strings
- TCP messages use 4-byte big-endian framing
- No business logic, no validation, no auth, no session management

### Server

- TCP listener and connection tracking
- Frame decoding and message parsing
- `HELLO` / `WELCOME` handshake
- Session-token `IDENTIFY` / `IDENTIFIED` identity flow (Discord OAuth2, backed by Postgres via Next.js's internal API)
- Session creation on `IDENTIFY` (not on raw connect)
- `CHAT_MESSAGE` / `CHANNEL_MESSAGE` handling, with scope-based broadcast/targeted routing
- Guild/channel lifecycle (create, join, leave, delete), invites, visibility, join requests, and rank-based roles
- Presence tracking (online/offline, connection-count based)
- Message persistence and history retrieval (`FETCH_HISTORY`)
- Friends, blocking, and direct messages
- Protocol handlers covered by Catch2 tests

## Quick Start

### 1. Prerequisites

- Docker
- Docker Compose
- A Discord OAuth2 application (client ID + secret) — required for login; see [`docs/auth/discord-design.md`](docs/auth/discord-design.md)

### 2. Configure environment

```bash
cp .env.example .env
```

Fill in the values `.env.example` documents inline: Postgres credentials, the
Discord client ID/secret, a generated RS256 keypair under `secrets/`, and two
random secrets. `web` and `server` both fail fast at startup if anything
required is missing or unreadable.

### 3. Start the stack

```bash
docker compose up --build
```

Available services:

- Web: `http://localhost:3000`
- Gateway WebSocket: `ws://localhost:8080`
- Server TCP: `localhost:4242`

Postgres and Redis also run as part of the stack but aren't published to the
host — only reachable from within the Compose network.

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

After `WELCOME`, the web client identifies with a session token obtained from
Discord OAuth2 login (`GET /api/auth/session-token`), not a chosen username:

```json
{
  "type": "IDENTIFY",
  "session_token": "<JWT>"
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
  "content": "hello",
  "display_name": null,
  "avatar_url": null
}
```

## Structure

```text
.
├── web/                # Next.js web client
├── gateway/            # WebSocket <-> TCP gateway
├── server/             # C++ TCP server and protocol handlers
├── desktop/            # Not the active development path (see CLAUDE.md)
├── shared/             # Shared protocol documentation
├── docs/               # Architecture and design-record notes
└── docker-compose.yml  # Local orchestration
```

## Documentation

- See [gateway/README.md](gateway/README.md) for gateway details.
- See [server/README.md](server/README.md) for server details.
- See [shared/protocol/README.md](shared/protocol/README.md) for shared protocol documentation.
- See [docs/architecture/overview.md](docs/architecture/overview.md) for system-level architecture.
- See [docs/architecture/gateway-transport.md](docs/architecture/gateway-transport.md) for the gateway's transport contract.
- See [docs/guilds/design.md](docs/guilds/design.md) for the guilds/channels feature's design rationale.
- See [docs/guilds/social-presence-design.md](docs/guilds/social-presence-design.md) for guild invites, roster/roles, presence, and message persistence.
- See [docs/auth/discord-design.md](docs/auth/discord-design.md) for the Discord OAuth2 authentication design.
- See [docs/settings/appearance-design.md](docs/settings/appearance-design.md) for the settings shell and theme system.
- See [docs/social/friends-dms-design.md](docs/social/friends-dms-design.md) for friends, blocking, direct messages, and profiles.
- See [docs/frontend-rebuild-plan.md](docs/frontend-rebuild-plan.md) for the web client's component/hook structure and its live wire-casing convention.
- See [docs/known-issues.md](docs/known-issues.md) for a living list of found-but-not-reliably-reproduced bugs.
- See [docs/architecture-audit.md](docs/architecture-audit.md), [docs/ci-audit.md](docs/ci-audit.md), and [docs/security-audit.md](docs/security-audit.md) for the repository's standing audits.

## Status

Current project state:

- Discord OAuth2 login; Postgres-backed users, sessions, guilds, channels, and memberships
- Web client: global lobby, a Guilds tab (create/join, invites, visibility, join requests, roster/roles), a Friends tab (requests, blocking, DMs), and a Settings modal (profile, appearance, blocked users)
- Server handles the full protocol: session-token `HELLO`/`IDENTIFY`, `CHAT_MESSAGE`/`CHANNEL_MESSAGE`, guild/channel lifecycle, invites/join requests, rank-based roles, presence, message persistence/history, friends/blocking/DMs
- Broadcast flow implemented through `Message.scope` (`DIRECT`, `BROADCAST`, `TARGETED`)
- Dockerized local stack (web, gateway, server, Postgres, Redis) is available

Still pending or intentionally out of scope:

- Functional voice channels (metadata-only today — see [`docs/guilds/design.md`](docs/guilds/design.md))
- A fully general, configurable permission system beyond the three fixed guild roles (Crew/Officer/Captain)
- Native WebSocket support in the C++ server (the gateway remains the WS↔TCP bridge)
- Production-grade event loop / scaling concerns
- TLS and deployment hardening
- Desktop client (`desktop/` exists in the repo but isn't the active development path)

---

Project CIG-Nexus - an experimental real-time chat platform with a clear separation between client, gateway, and server.
