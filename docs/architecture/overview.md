# CIG Nexus Architecture

This document describes the architecture as it exists today in the repository.

## System Overview

The application is currently composed of three active runtime services, plus
two supporting data stores:

- **Web client** built with Next.js — also the only piece with direct
  Postgres/Redis access, and the host of the Discord OAuth2 login flow (see
  [Internal API](#internal-api-next-js-owns-postgres) below)
- **Gateway** built with Node.js and TypeScript
- **Authoritative server** built with C++
- **PostgreSQL** — durable storage for users, sessions, guilds/channels,
  memberships, messages, friends, blocks, and DM conversations
- **Redis** — OAuth rate limiting and the session-revocation cache

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

The diagram above is the live chat-message path only. Durable state doesn't
flow through it — see [Internal API](#internal-api-next-js-owns-postgres).

## Current Separation of Concerns

### Web Client

Responsible for:

- opening a browser WebSocket connection
- sending `HELLO` on connect
- sending `IDENTIFY` after `WELCOME` with a signed session token obtained via
  Discord OAuth2 login — not a chosen username
- sending `CHAT_MESSAGE`/`CHANNEL_MESSAGE` payloads and guild/friends/
  blocking/DM actions from the UI
- displaying connection status, presence, and received messages

Not responsible for:

- protocol validation
- issuing or verifying its own session token — Discord OAuth2 login and JWT
  issuance happen in Next.js's separate `/api/auth/*` routes, not in the
  WebSocket client logic itself
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
- creating in-memory identity sessions on `IDENTIFY`, authenticated via a
  signed (RS256) session token rather than a raw username
- routing responses by semantic scope (`DIRECT`, `BROADCAST`, or `TARGETED`)
- broadcasting valid chat messages to all connected clients, and persisting
  chat/channel messages with history retrieval (`FETCH_HISTORY`)
- managing the guild/channel catalog (`GuildManager`) and per-connection guild membership / active channel state (`SessionManager`)
- guild invites, visibility (open/application/private), join requests, and
  rank-based roles (Crew/Officer/Captain)
- tracking online/offline presence per user
- friends, blocking, and direct messages

## Internal API: Next.js Owns Postgres

A fourth boundary exists that the diagram above doesn't show, since it isn't
part of the live chat-message path: **the C++ server never opens a database
connection itself.** Durable state lives in PostgreSQL, and Postgres is only
ever reached through Next.js's internal-only HTTP API
(`web/app/internal/*`), isolated onto a second, unpublished port not exposed
outside the Docker network (`docs/security-audit.md`).

```text
Server (C++)  --  HTTP, shared-secret authenticated  -->  Next.js /internal/*  -->  PostgreSQL
```

`GuildManager` (and the equivalent in-process state for friends/blocks/DM
conversations) holds an in-memory, write-through cache: reads are served
from memory on hot, frequent paths (e.g. every `CREATE_GUILD`), while writes
go to Next.js's internal API first and only update the in-memory cache on
success. Cold, infrequent, UI-driven reads (e.g. `LIST_MEMBERS`) skip the
cache entirely and call the internal API live instead. See
`docs/auth/discord-design.md` §8.1 for the pattern's original design and
`docs/guilds/social-presence-design.md` §2.3 for the reasoning behind which
reads get cached and which don't.

Redis is used by the Next.js side only — OAuth rate limiting (an atomic
sliding-window Lua script) and the session-revocation cache — the C++
server has no direct dependency on it.

## Current Implemented Flow

### Handshake

1. The web client connects to the gateway.
2. The gateway opens a TCP connection to the server.
3. The web client sends `HELLO`.
4. The server validates it.
5. The server returns `WELCOME` or `ERROR`.

### Identity

1. After `WELCOME`, the client sends `IDENTIFY` with a signed session token
   (a short-lived RS256 JWT obtained from Discord OAuth2 login via Next.js's
   `/api/auth/session-token`).
2. The server verifies the token's signature and expiry.
3. On success, the server creates a session for that socket, hydrating
   profile fields (`display_name`/`avatar_url`) and guild membership from
   Next.js's internal API.
4. The server returns `IDENTIFIED` with `user_id` and `username`.

### Chat

1. The web client sends `CHAT_MESSAGE` with `content`.
2. The gateway forwards it unchanged apart from transport framing.
3. The server validates payload and identity state.
4. The server creates a normalized chat message with identity metadata.
5. The message is marked `Scope::BROADCAST`.
6. The server broadcasts that message to every active connection.
7. The gateway forwards the resulting JSON message back to browsers.

### Guilds and Channels

1. An identified client can create/list/join/leave a guild, and (owner only)
   create/delete its channels — independent of the chat flow above.
2. A connection joins at most one channel at a time; joining a new one
   implicitly leaves the previous one.
3. `CHANNEL_MESSAGE` targets the sender's own active channel rather than a
   client-supplied id, and is delivered only to connections with that
   channel active — this is `Scope::TARGETED`, not `Scope::BROADCAST`.
4. Guild-wide notifications (a channel being created/deleted, a member
   leaving, a guild being deleted) are also `Scope::TARGETED`, delivered to
   every current member rather than every connection.

See [../../shared/protocol/README.md](../../shared/protocol/README.md) for the
full message-by-message protocol and [../guilds/design.md](../guilds/design.md) for
the feature's design rationale.

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

- browser chat UI, Discord OAuth2 login
- gateway transport bridge
- TCP server connection tracking
- `HELLO` / `WELCOME`
- session-token `IDENTIFY` / `IDENTIFIED`
- `CHAT_MESSAGE`/`CHANNEL_MESSAGE` validation, normalization, and persistence with history retrieval
- in-memory session manager keyed by socket fd, backed by Postgres via Next.js's internal API (see [Internal API](#internal-api-next-js-owns-postgres))
- broadcast/targeted delivery to connected clients
- guild/channel lifecycle (create, list, join, leave, delete) and channel messaging, with `Scope::TARGETED` delivery
- guild invites, visibility (open/application/private), join requests, and rank-based roles/roster
- online/offline presence tracking
- friends, blocking, and direct messages

Not implemented yet:

- native WebSocket support in the C++ server (the gateway remains the WS↔TCP bridge)
- production-grade scalability and hardening
- a fully general, configurable permission system beyond the three fixed guild roles (see [../guilds/social-presence-design.md](../guilds/social-presence-design.md) §2)
- functional voice channels (metadata-only today, see [../guilds/design.md](../guilds/design.md))

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

- See [../../shared/protocol/README.md](../../shared/protocol/README.md) for wire-level protocol behavior.
- See [../../gateway/README.md](../../gateway/README.md) for gateway implementation notes.
- See [../../server/README.md](../../server/README.md) for backend implementation details.
- See [gateway-transport.md](gateway-transport.md) for the gateway's transport contract.
- See [../guilds/design.md](../guilds/design.md) for the guilds/channels feature's design rationale.
- See [../guilds/social-presence-design.md](../guilds/social-presence-design.md) for guild invites, roster/roles, presence, and message persistence.
- See [../auth/discord-design.md](../auth/discord-design.md) for the Discord OAuth2 + Postgres persistence design, including the internal-API write-through cache pattern.
- See [../social/friends-dms-design.md](../social/friends-dms-design.md) for friends, blocking, direct messages, and profiles.
