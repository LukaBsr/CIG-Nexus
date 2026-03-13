# CIG Nexus Server

The CIG Nexus Server is the authoritative TCP backend for the platform. It accepts framed JSON messages over TCP, dispatches protocol handlers, and broadcasts chat messages to connected clients.

## Current Responsibilities

- Accept TCP connections on port `4242`
- Track active client connections
- Decode 4-byte big-endian framed messages
- Parse JSON protocol messages
- Dispatch `HELLO`, `IDENTIFY`, and `CHAT_MESSAGE` handlers
- Manage in-memory sessions keyed by socket fd
- Return direct responses for handshake and validation errors
- Broadcast valid chat messages to all connected clients using `Message.scope`

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

## Architecture Notes

- **Transport**: raw TCP
- **Framing**: 4-byte big-endian size prefix
- **Payload format**: JSON
- **Connection model**: one socket per client
- **I/O approach**: accept loop plus per-connection polling in the main loop
- **Routing model**: `Message.scope` drives response behavior:
	- `Scope::DIRECT`: response sent only to sender
	- `Scope::BROADCAST`: response sent to all active connections

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
- Catch2 coverage for framing, dispatcher, handlers, and session manager

Not implemented yet:

- authentication and authorization
- persistent sessions and named users across restarts
- database or message history persistence
- production-grade event loop and backpressure handling
- TLS or encryption
- reconnection or delivery guarantees above TCP

## Related Documentation

- See [../shared/protocol/README.md](../shared/protocol/README.md) for wire protocol details.
- See [../docs/architecture.md](../docs/architecture.md) for system-level architecture.

## Notes

Design goals remain:

- clarity over cleverness
- explicit protocol handling
- simple transport boundaries
- incremental evolution through small, testable steps
