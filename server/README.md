# CIG Nexus Server

The CIG Nexus Server is the authoritative TCP backend for the platform. It accepts framed JSON messages over TCP, authenticates connections with signed session tokens, dispatches protocol handlers, and delivers responses by scope (direct, broadcast, or targeted).

It never opens a database connection itself: durable state (users, guilds, channels, memberships, messages, friends, blocks) lives in Postgres behind Next.js's internal-only HTTP API, and the server reaches it through `CurlInternalApiClient` (see [../docs/architecture/overview.md](../docs/architecture/overview.md)).

## Current Responsibilities

- Accept TCP connections on port `4242` (`--port` overrides)
- Track active client connections
- Decode 4-byte big-endian framed messages
- Parse JSON protocol messages
- Dispatch every protocol message type (see [../shared/protocol/README.md](../shared/protocol/README.md) for the full list): `HELLO`, `IDENTIFY`, `CHAT_MESSAGE`, guild/channel, invite, join-request, role, friend, blocking, direct-message, and `FETCH_HISTORY` handlers
- Verify RS256 session tokens (`JwtVerifier`) and reject revoked sessions (`RevocationCache`, polled from Next.js every 30 seconds and once synchronously at startup; live connections whose session is revoked are disconnected)
- Manage in-memory sessions keyed by socket fd, including guild membership and active-channel (hydrated synchronously at `IDENTIFY`), plus friend/block/profile state (hydrated asynchronously after `IDENTIFY` — see [shared/protocol/README.md](../shared/protocol/README.md)'s Asynchronous IDENTIFY Hydration section)
- Rate limit a subset of protocol actions in-memory, per-user (`util::RateLimiter`) — see [../shared/protocol/README.md](../shared/protocol/README.md)'s Rate Limits section for which types and thresholds
- Manage the guild/channel catalog (`GuildManager`) as a write-through cache over the internal API, hydrated at startup
- Track online/offline presence per user (connection-count transitions) and broadcast `PRESENCE_UPDATE`
- Persist chat, channel, and DM messages asynchronously with bounded retry (`MessagePersistenceWorker`), and serve paginated history
- Return direct responses for handshake and validation errors
- Deliver by `Message.scope`: `DIRECT` (sender only), `BROADCAST` (all identified connections), `TARGETED` (an explicit fd list the handler computes)

## Startup Configuration

The server refuses to start without these environment variables:

- `AUTH_JWT_PUBLIC_KEY_PATH` — file path of the RS256 public key used to verify session tokens (the private key never reaches this service)
- `INTERNAL_API_BASE_URL` — base URL of Next.js's internal-only API
- `INTERNAL_API_SHARED_SECRET` — shared secret sent with every internal API call

At startup it hydrates the guild catalog and message sequence counters from the internal API (once, without retry — start `web` first, as `docker-compose.yml` does), polls the revocation cache, and only then enters the accept loop.

Optional: `CIG_NEXUS_DEBUG_PRESENCE=1` enables temporary stderr logging of every presence increment/decrement and `IDENTIFY` entry/session-creation, for diagnosing [`docs/known-issues.md`](../docs/known-issues.md)'s presence-count leak. Off by default, zero output otherwise.

## Protocol Lifecycle

1. Client connects and sends `HELLO` (`version` must be `"0.1"`, `client` `"web"` or `"desktop"`); the server replies `WELCOME` (with `server_version`).
2. Client sends `IDENTIFY` with a `session_token` (an RS256 JWT issued by Next.js after Discord OAuth2 login). The server checks the signature, algorithm (pinned to `RS256`), expiry, and audience, and that the session isn't revoked; on success it creates the in-memory session, hydrates guild membership (in-memory, immediate), and replies `IDENTIFIED` right away. Blocks/friends/profile load asynchronously afterward — see [shared/protocol/README.md](../shared/protocol/README.md)'s Asynchronous IDENTIFY Hydration section for what that means for `DM_SEND` and presence in the meantime.
3. An identified client may send any other message type. Before `IDENTIFY`, they return `NOT_IDENTIFIED`.

Errors reuse the codes documented in the protocol spec (`AUTH_REQUIRED`, `INVALID_SESSION`, `SESSION_EXPIRED`, `SESSION_REVOKED`, `NOT_IDENTIFIED`, `PROTOCOL_VIOLATION`, `MALFORMED_MESSAGE`, `INTERNAL_ERROR`, and the guild/friend/DM codes).

## Guilds, Roles, and Delivery

Guild membership is durable; a connection can belong to several guilds but has at most one active channel at a time. Authorization is rank-based (`role_rank`, named constants in `include/guild/RoleRank.hpp`): officer-or-above can create channels, create invites, and approve join requests; only the owner can delete channels or guilds, change visibility, and assign roles. Visibility (`open` / `application` / `private`) controls listing and how a guild can be joined.

Guild-wide notifications, `CHANNEL_MESSAGE`, friend events, and `DM_MESSAGE` use `Scope::TARGETED`. A single request can produce several messages for different recipients (the dispatcher returns a `std::vector<Message>`).

## Architecture Notes

- **Transport**: raw TCP
- **Framing**: 4-byte big-endian size prefix (max 1 MiB)
- **Payload format**: JSON
- **Connection model**: one socket per client; `SO_KEEPALIVE` is enabled, but there is no application-level heartbeat
- **I/O approach**: single-threaded accept loop plus per-connection polling (100 ms tick); most internal API calls are synchronous (5 s timeout each) on this loop, so a slow web service delays every connection's traffic. Two exceptions run on their own worker thread instead: message persistence, and `IDENTIFY`'s post-`IDENTIFY` blocks/friends/profile load (`fetchSessionContext`, combined into one call and moved off this loop — see [shared/protocol/README.md](../shared/protocol/README.md)'s Asynchronous IDENTIFY Hydration section), applied back to the session once complete.
- **Routing model**: `Message.scope` drives response behavior:
	- `Scope::DIRECT`: response sent only to sender
	- `Scope::BROADCAST`: response sent to all identified connections (never to a connection that hasn't completed `IDENTIFY`)
	- `Scope::TARGETED`: response sent to an explicit fd list the handler computes (e.g. "current guild members")

## Directory Layout

- `include/` - public headers: server types, `auth/`, `guild/`, `http/`, `persistence/`, `protocol/` (handlers, dispatcher, parser), `session/`, `util/`
- `src/` - server implementation, mirroring `include/`
- `tests/` - Catch2 tests mirroring `src/` (`auth/`, `guild/`, `http/`, `integration/`, `persistence/`, `protocol/`, `util/`)

## Build

Requires CMake >= 3.20 and OpenSSL + libcurl development headers
(`find_package(OpenSSL REQUIRED)` / `find_package(CURL REQUIRED)` in
`CMakeLists.txt`, used for RS256 JWT verification and the internal-API
HTTP client):

- Debian/Ubuntu: `sudo apt install libssl-dev libcurl4-openssl-dev`
- Fedora: `sudo dnf install openssl-devel libcurl-devel`
- macOS: `brew install openssl curl`

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Run

The environment variables above must be set; the easiest way to run the full stack is `docker compose up --build` from the repository root.

```bash
./cig-nexus-server
```

Default TCP port: `4242`.

## Test

```bash
cd build
cmake ..
cmake --build .
ctest
```

### Formatting

CI checks formatting with a pinned `clang-format==18.1.8`
(`.github/workflows/server-ci.yml`). A different local version can disagree with it on
existing lines, so use the pinned one in a venv (from the repo root) instead of a system
`clang-format`:

```bash
python3 -m venv .venv-clang-format && .venv-clang-format/bin/pip install clang-format==18.1.8
find server/include server/src server/tests -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 .venv-clang-format/bin/clang-format --dry-run --Werror
```

## Current Status

Implemented now:

- TCP listener, framed message decoding, JSON parsing, dispatcher-based protocol handling
- `HELLO` / `WELCOME` and session-token `IDENTIFY` / `IDENTIFIED` (RS256 verification, revocation cache)
- Lobby chat, channel messaging, and direct messages, with durable history (`FETCH_HISTORY`, keyset pagination) and startup-seeded message ids
- Guild/channel lifecycle, invites, visibility, join requests, and rank-based roles
- Presence tracking and broadcast
- Friends, friend codes, blocking (including presence exclusion), and profile display fields on rosters/messages
- Catch2 coverage across auth, guild manager, internal API client, message persistence, every protocol handler, session manager, connection I/O, and end-to-end integration tests

Not implemented yet:

- TLS or encryption (the gateway and server speak plain TCP)
- Application-level heartbeat for half-open connections
- Production-grade event loop, backpressure handling, and horizontal scaling (the guild cache assumes a single server instance)
- Retry on startup hydration if the internal API is unreachable
- Functional voice channels (metadata only)
- A general, configurable permission system beyond the three fixed guild ranks

## Related Documentation

- See [../shared/protocol/README.md](../shared/protocol/README.md) for wire protocol details.
- See [../docs/architecture/overview.md](../docs/architecture/overview.md) for system-level architecture.
- See [../docs/known-issues.md](../docs/known-issues.md) for open server-side issues (presence count).

## Notes

Design goals remain:

- clarity over cleverness
- explicit protocol handling
- simple transport boundaries
- incremental evolution through small, testable steps
