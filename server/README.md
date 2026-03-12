# CIG Nexus Server

The CIG Nexus Server is the authoritative TCP backend for the platform. It accepts framed JSON messages over TCP, dispatches protocol handlers, and broadcasts chat messages to connected clients.

## Current Responsibilities

- Accept TCP connections on port `4242`
- Track active client connections
- Decode 4-byte big-endian framed messages
- Parse JSON protocol messages
- Dispatch `HELLO` and `CHAT_MESSAGE` handlers
- Return direct responses for handshake and validation errors
- Broadcast valid chat messages to all connected clients

## Implemented Protocol Features

### HELLO

Clients send a `HELLO` message after connecting.

The server validates:

- `type` is `"HELLO"`
- `version` is `"0.1"`
- `client` is either `"web"` or `"desktop"`

On success, the server returns a `WELCOME` message.

### CHAT_MESSAGE

Clients can send chat messages after connection.

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
- `from`
- `content`

## Architecture Notes

- **Transport**: raw TCP
- **Framing**: 4-byte big-endian size prefix
- **Payload format**: JSON
- **Connection model**: one socket per client
- **I/O approach**: accept loop plus per-connection polling in the main loop

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
- `CHAT_MESSAGE` validation
- server-side broadcast for valid chat messages
- Catch2 coverage for framing and chat handler behavior

Not implemented yet:

- authentication and authorization
- persistent sessions and named users
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
