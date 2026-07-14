# CIG Nexus Server

The CIG Nexus Server is the authoritative TCP backend for the platform. It accepts framed JSON messages over TCP, dispatches protocol handlers, and broadcasts chat messages to connected clients.

## Current Responsibilities

- Accept TCP connections on port `4242`
- Track active client connections
- Decode 4-byte big-endian framed messages
- Parse JSON protocol messages
- Dispatch `HELLO`, `IDENTIFY`, `CHAT_MESSAGE`, and the guild/channel handlers
- Manage in-memory sessions keyed by socket fd, including guild membership and active-channel state
- Manage the in-memory guild/channel catalog (`GuildManager`)
- Return direct responses for handshake and validation errors
- Broadcast valid chat messages to all connected clients using `Message.scope`
- Deliver guild/channel responses to a targeted subset of connections (e.g. "current guild members") using `Scope::TARGETED`

## Implemented Protocol Features

### HELLO

Clients send a `HELLO` message after connecting.

The server validates:

- `type` is `"HELLO"`
- `version` is `"0.1"`
- `client` is either `"web"` or `"desktop"`

On success, the server returns a `WELCOME` message.

`WELCOME` currently includes:

- `type`
- `server_version`

`WELCOME` no longer includes `session_id` because sessions are created on `IDENTIFY`.

### IDENTIFY

Clients identify after receiving `WELCOME`.

The server validates:

- payload is a JSON object
- `username` exists
- `username` is a string
- `username` is not empty
- `username` length is at most `32` characters
- connection is not already identified

On success:

- the server creates a new in-memory session for the socket
- the session is assigned `session_id` and `user_id`
- the server returns `IDENTIFIED` with `user_id` and `username`

### CHAT_MESSAGE

Clients can send chat messages after successful `IDENTIFY`.

The server validates:

- payload is a JSON object
- payload `type` is `"CHAT_MESSAGE"`
- `content` exists
- `content` is a string
- `content` is not empty
- `content` length is at most `500` characters

On success, the server returns a normalized chat message and broadcasts it to every active connection.

Current chat payloads include:

- `type`
- `message_id`
- `timestamp`
- `user_id`
- `username`
- `content`

If a client sends `CHAT_MESSAGE` before identify, the server returns:

- `type = "ERROR"`
- `code = "NOT_IDENTIFIED"`

### Guilds and Channels

Identified clients can create, list, join, leave, and (owner only) delete
guilds, and — owner only — create/delete channels within a guild. A
connection can be a member of multiple guilds but has at most one active
channel at a time; `JOIN_CHANNEL` implicitly leaves whichever channel was
previously active.

Message types: `CREATE_GUILD`, `LIST_GUILDS`, `JOIN_GUILD`, `LEAVE_GUILD`,
`DELETE_GUILD`, `LIST_CHANNELS`, `CREATE_CHANNEL`, `DELETE_CHANNEL`,
`JOIN_CHANNEL`, `LEAVE_CHANNEL`, `CHANNEL_MESSAGE`. `CHANNEL_MESSAGE` targets
the sender's own active channel rather than a client-supplied id.

Guild-wide notifications (channel created/deleted, a member leaving, a guild
being deleted) and `CHANNEL_MESSAGE` are delivered via `Scope::TARGETED` to
the relevant subset of connections, not a full broadcast.

See [../shared/protocol/README.md](../shared/protocol/README.md) for the
full request/response shapes and validation rules, and
[../docs/rooms-spec.md](../docs/rooms-spec.md) for the feature's design
rationale.

## Architecture Notes

- **Transport**: raw TCP
- **Framing**: 4-byte big-endian size prefix
- **Payload format**: JSON
- **Connection model**: one socket per client
- **I/O approach**: accept loop plus per-connection polling in the main loop
- **Routing model**: `Message.scope` drives response behavior:
	- `Scope::DIRECT`: response sent only to sender
	- `Scope::BROADCAST`: response sent to all active connections
	- `Scope::TARGETED`: response sent to an explicit fd list the handler computes (e.g. "current guild members")

## Directory Layout

- `include/` - public headers, server types, protocol handler headers
- `src/` - server implementation and protocol handlers
- `tests/` - Catch2 unit tests for framing and handlers

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Run

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

## Current Status

Implemented now:

- TCP listener
- framed message decoding
- JSON message parsing
- dispatcher-based protocol handling
- `HELLO` / `WELCOME`
- `IDENTIFY` / `IDENTIFIED`
- `CHAT_MESSAGE` validation
- server-side broadcast for valid chat messages
- deferred session creation on `IDENTIFY`
- guild/channel lifecycle and channel messaging, with `Scope::TARGETED` delivery
- Catch2 coverage for framing, dispatcher, handlers, session manager, guild manager, and connection I/O (including a peer-reset regression test)

Not implemented yet:

- authentication and authorization
- persistent sessions, guilds, channels, and named users across restarts
- database or message history persistence
- production-grade event loop and backpressure handling
- TLS or encryption
- reconnection or delivery guarantees above TCP
- guild privacy and channel-creation permission delegation (see [../docs/rooms-spec.md](../docs/rooms-spec.md))

## Related Documentation

- See [../shared/protocol/README.md](../shared/protocol/README.md) for wire protocol details.
- See [../docs/architecture.md](../docs/architecture.md) for system-level architecture.

## Notes

Design goals remain:

- clarity over cleverness
- explicit protocol handling
- simple transport boundaries
- incremental evolution through small, testable steps
