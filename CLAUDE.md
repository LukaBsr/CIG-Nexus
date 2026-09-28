# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CIG-Nexus is a real-time chat platform with three runtime services:

- **Web** (`web/`) — Next.js 16 / React 19 frontend, App Router, Tailwind CSS
- **Gateway** (`gateway/`) — TypeScript Node.js WebSocket-to-TCP bridge (transport-only, no business logic)
- **Server** (`server/`) — C++20 TCP backend, CMake build, Catch2 tests

## Commands

### Full Stack (Docker)

```bash
docker compose up --build
```

Services: Web `localhost:3000`, Gateway WS `localhost:8080`, Server TCP `localhost:4242`.

### Server (C++)

```bash
cd server && mkdir -p build && cd build && cmake .. && cmake --build .
./cig-nexus-server          # runs on TCP port 4242
```

This builds the test suite too (`BUILD_TESTS` defaults to `ON`). To build
just the app, skipping the test suite entirely:

```bash
cd server && mkdir -p build && cd build && cmake .. -DBUILD_TESTS=OFF && cmake --build .
./cig-nexus-server          # runs on TCP port 4242
```

Run tests:

```bash
cd server/build && ctest
# or to see output:
./cig-nexus-tests
```

Run a single test (Catch2):

```bash
./cig-nexus-tests "[HelloHandler]"
```

Check formatting the way CI does. CI pins `clang-format==18.1.8`
(`.github/workflows/server-ci.yml`); a different local version can disagree
with it on existing lines, so install the pinned one in a venv rather than
using a system `clang-format` or running `clang-format -i` with it:

```bash
python3 -m venv .venv-clang-format && .venv-clang-format/bin/pip install clang-format==18.1.8
find server/include server/src server/tests -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 .venv-clang-format/bin/clang-format --dry-run --Werror
```

### Gateway (TypeScript)

```bash
cd gateway && npm install && npm run build
WS_PORT=8080 TCP_HOST=localhost TCP_PORT=4242 npm start
```

### Web (Next.js)

```bash
cd web && npm install && npm run dev   # dev server on port 3000
npm run lint                           # ESLint
npm run build                          # production build
npm test                               # Vitest; needs a running Docker daemon (testcontainers starts Postgres/Redis and builds the web image)
```

## Architecture

### Data Flow

```
Browser (Next.js)
    | WebSocket / JSON strings
    v
Gateway (Node.js + TypeScript)
    | TCP / 4-byte big-endian framed JSON
    v
Server (C++)
```

### TCP Framing

All Server ↔ Gateway communication uses: `[uint32_t big-endian size][JSON payload]`. Max frame size is 1 MiB. The gateway's `src/protocol/frame.ts` handles encoding/decoding; the server's `FrameDecoder` handles the C++ side.

### Protocol Lifecycle

Clients must follow this sequence:
1. Connect → send `HELLO` → receive `WELCOME`
2. Send `IDENTIFY` → receive `IDENTIFIED` (session is created here, not on connect)
3. Send `CHAT_MESSAGE` → server validates, broadcasts enriched message to all clients

Sessions are keyed by socket fd and held in memory only (no persistence across restarts).

### Message Routing

The server's `Message` type carries a `Scope` field:
- `Scope::DIRECT` — response goes only to sender
- `Scope::BROADCAST` — response goes to all identified connections (never to a socket that hasn't completed `IDENTIFY`)
- `Scope::TARGETED` — response goes to an explicit fd list the handler computes (e.g. current guild members, connections with a channel active, or every connection identified as one `user_id`)

`CHAT_MESSAGE` responses are always `BROADCAST`; handshake and error responses are `DIRECT`; guild/channel notifications, `CHANNEL_MESSAGE`, friend events and `DM_MESSAGE` are `TARGETED`. A handler may return several `Message`s from one request (e.g. approving a join request notifies the approver and the approved user separately).

### Server Layout

```
server/
├── include/            # Headers: Server, TcpListener, Connection, auth/, guild/, http/, persistence/, protocol/, session/, util/
├── src/
│   ├── Server.cpp      # Server class core: construction/wiring, start() main loop, stop()
│   ├── ServerRoutes.cpp  # Server::registerRoutes() — the message-type -> handler dispatch table
│   ├── ServerDelivery.cpp  # Server socket delivery: sendMessage, broadcast, broadcastExcluding
│   ├── ServerSessionLifecycle.cpp  # Presence transitions, removeSessionTrackingPresence, applying async hydration results
│   ├── ServerBackgroundSync.cpp  # Startup hydration (guild catalog, message sequences) and the revocation poll/sweep
│   ├── auth/           # JwtVerifier, RevocationCache — RS256 session-token verification
│   ├── guild/           # GuildManager — guild/channel catalog write-through cache, role-rank predicates
│   ├── http/            # CurlInternalApiClient (+ internal WireParsers) — calls to Next.js's /internal/* API (server never touches Postgres directly)
│   ├── persistence/     # MessagePersistenceWorker — chat/channel message persistence
│   ├── protocol/
│   │   ├── handlers/   # BlockHandler, ChannelHandler, ChatHandler, DMHandler, FriendHandler,
│   │   │               # GuildHandler, HelloHandler, IdentifyHandler, InviteHandler, JoinRequestHandler
│   │   ├── MessageDispatcher.cpp
│   │   └── MessageParser.cpp
│   ├── session/         # SessionManager (per-fd sessions, presence counts), SessionHydrationWorker (async post-IDENTIFY load)
│   └── util/            # FilePermissions (startup checks, e.g. private-key file mode), RateLimiter, DebugFlags
└── tests/               # Catch2 tests mirroring src/: auth/, guild/, http/, integration/, persistence/, protocol/, session/, util/
```

### Web Layout

```
web/
├── app/                 # Next.js App Router — page.tsx is the chat UI; api/ (OAuth/session/appearance
│                        # routes) and internal/ (the Postgres-backed API the C++ server calls) live here too
├── components/          # Extracted UI components (MessageList, FriendsView, GuildRail, SettingsModal, ...)
├── hooks/
│   └── useGatewayConnection.ts  # Owns the WS connection + protocol switch; wire snake_case -> camelCase boundary
└── lib/
    ├── gateway.ts       # WebSocket client wrapper, handles HELLO/IDENTIFY/CHAT_MESSAGE/... send helpers
    ├── types.ts         # Wire* / camelCase type pairs and mappers
    ├── auth/            # Session-token issuance/verification (web side)
    ├── internal/        # Postgres access backing app/internal/* routes (catalog, friends, blocks, DMs, ...)
    ├── settings/         # Settings modal section registry
    ├── appearance/        # Theme tokens/registry
    └── user/             # Profile display-name/avatar resolution (§4.5)
```

### Gateway Layout

```
gateway/src/
├── index.ts
├── ws/WsServer.ts     # WebSocket server, creates one TcpClient per connection
├── tcp/TcpClient.ts   # Wraps a single TCP socket with a receive buffer
└── protocol/frame.ts  # Frame encode/decode
```

## Key Constraints

- The gateway must remain transport-only — no validation, no business logic.
- All protocol validation lives in the C++ server handlers.
- The web client sends `HELLO` on connect and `IDENTIFY` automatically after `WELCOME`.
- `NEXT_PUBLIC_GATEWAY_URL` env var controls the WebSocket endpoint in the web client.
- The desktop client (`desktop/`) exists in the repo but is not the active development path.

## Git Workflow

- **Never commit directly to `main`.** All changes go through a feature branch and a pull request.
- Branch naming: match the existing convention seen in commit history 
  (e.g. `feat/`, `fix/`, `infra/` prefixes — mirrors Conventional Commits style 
  already used in commit messages).
- Create a new branch before making any code change: 
  `git checkout -b fix/identify-payload-type` (example).
- Do not merge or push to `main` — open a PR and leave it for review.

## Reference Documentation

### shared/protocol/ — wire protocol source of truth

`shared/protocol/README.md` is the canonical specification for every message type, field constraint, error code, and framing rule. When adding or changing a message type:

1. Update `shared/protocol/README.md` first.
2. Keep `server/src/protocol/handlers/` consistent with it (validation rules, field names, error codes).
3. Keep `gateway/src/protocol/frame.ts` consistent with the framing rules (max size, byte order).

If the spec and the code disagree, the spec is intentional — treat it as a bug in the code, not the doc.

### docs/ — architecture and transport reference

| File | What it covers | When to read it |
|---|---|---|
| `docs/architecture/overview.md` | Full system design: service responsibilities, data flow, message routing, design principles | Before adding a new service or changing where validation/routing lives |
| `docs/architecture/gateway-transport.md` | Gateway transport contract: WebSocket side, TCP side, forwarding rules, lifecycle | Before touching gateway framing, connection handling, or the browser↔server message path |
| `docs/auth/discord-design.md` | Discord OAuth2 + Postgres persistence design record | Before touching auth, sessions, or the internal API between the C++ server and Next.js |
| `docs/guilds/design.md` | Guilds/channels feature design record | Before touching guild/channel protocol handlers or the write-through cache |
| `docs/guilds/social-presence-design.md` | Guild invites, visibility, roster/roles, presence, message persistence design record | Before touching invites, join requests, roles/roster, presence, or message persistence/history |
| `docs/settings/appearance-design.md` | Settings shell + theme system design record | Before touching the settings modal, theme tokens/registry, or appearance sync |
| `docs/known-issues.md` | Living list of found-but-not-reliably-reproduced bugs (currently one open: a presence connection-count leak; resolved entries are kept below it) | Before touching `SessionManager` presence tracking, or if you notice presence looking wrong |
| `docs/social/friends-dms-design.md` | Friends, blocking, 1:1 DMs, and customizable profiles design record | Before touching friend requests/codes, blocking, direct messages, or the profile/settings-Profile-section work |
| `docs/security-audit.md` | Standing security audit: verified findings, dependency-advisory exposure traces, open hardening items | Before touching auth, `/internal/*`, cookies, rate limiting, or dependency versions |
| `docs/architecture-audit.md` | Repo-structure/docs-organization audit (mostly executed; kept as a record) | Before reorganizing docs or moving files |
| `docs/ci-audit.md` | CI workflow audit (mostly executed; kept as a record) | Before changing `.github/workflows/` |
| `CHANGELOG.md` | Per-release change history | When cutting a release or checking what shipped in a version |

### Frontend rebuild

The `web/` rebuild (session-token `IDENTIFY`, extracted `components/`/`hooks/`,
Tailwind, CIG Nexus visual identity) is complete —
[`docs/frontend-rebuild-plan.md`](docs/frontend-rebuild-plan.md) is kept
as a reference for its still-live
[wire-message casing convention](docs/frontend-rebuild-plan.md#wire-message-casing-convention)
(inbound `Wire*` types stay snake_case; `useGatewayConnection` is the only
place that maps them to camelCase) — mirror it for any new wire message
type.