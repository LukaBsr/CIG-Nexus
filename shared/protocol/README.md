# CIG Nexus Protocol

This document describes the protocol currently used between clients and the CIG Nexus TCP server.

## Overview

The protocol is based on:

- TCP transport
- 4-byte big-endian binary framing
- JSON payloads
- server-side validation and dispatch

Browser clients do not speak TCP directly. They reach the server through the WebSocket gateway, but the payloads remain the same JSON messages.

## Transport and Framing

Each TCP message is sent as:

```text
+--------------------+----------------------+
| uint32_t size (4B) | JSON payload (size) |
+--------------------+----------------------+
```

Framing rules:

- size is a 32-bit unsigned big-endian integer
- size must be greater than `0`
- size must be less than or equal to `1 MiB`
- payload bytes are UTF-8 JSON
- multiple frames may arrive in a single socket read
- partial frames must remain buffered until complete

## Connection Lifecycle

1. Client opens a TCP connection.
2. Client sends a `HELLO` message.
3. Server returns `WELCOME` or `ERROR`.
4. Client sends `IDENTIFY` with a username.
5. Server returns `IDENTIFIED` or `ERROR`.
6. Identified clients may send `CHAT_MESSAGE` messages.
7. Valid chat messages are broadcast to all active connections.

## Message Types

### HELLO

Client to server:

```json
{
  "type": "HELLO",
  "version": "0.1",
  "client": "web"
}
```

Validation:

- `type` must be `"HELLO"`
- `version` must be `"0.1"`
- `client` must be `"web"` or `"desktop"`

### WELCOME

Server to client:

```json
{
  "type": "WELCOME",
  "server_version": "0.3"
}
```

Notes:

- `server_version` currently reflects the server/application version, not the client protocol version field

### IDENTIFY

Client to server:

```json
{
  "type": "IDENTIFY",
  "username": "web_user"
}
```

Validation:

- payload must be an object
- `username` must exist
- `username` must be a string
- `username` must not be empty
- `username` length must be at most `32`

On success, the server creates a session for that socket and returns:

```json
{
  "type": "IDENTIFIED",
  "user_id": "u_1",
  "username": "web_user"
}
```

### CHAT_MESSAGE

Client to server:

```json
{
  "type": "CHAT_MESSAGE",
  "content": "hello"
}
```

Validation:

- payload must be an object
- `type` must exist, be a string, and equal `"CHAT_MESSAGE"`
- `content` must exist
- `content` must be a string
- `content` must not be empty
- `content` length must be at most `500`

On success, the server emits and broadcasts:

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

Field meanings:

- `message_id`: generated server-side
- `timestamp`: UNIX timestamp in seconds
- `user_id`: server-assigned per identified connection
- `username`: current username for the identified connection
- `content`: validated chat message content

If a non-identified client sends `CHAT_MESSAGE`, server returns `ERROR` with code `NOT_IDENTIFIED`.

### ERROR

Server to client:

```json
{
  "type": "ERROR",
  "code": "MALFORMED_MESSAGE",
  "message": "CHAT_MESSAGE content must not be empty"
}
```

Current error codes used by the implementation:

| Code | Meaning |
|---|---|
| `UNSUPPORTED_VERSION` | `HELLO.version` is not supported |
| `PROTOCOL_VIOLATION` | message type or sequencing is invalid |
| `MALFORMED_MESSAGE` | required fields are missing or invalid |
| `NOT_IDENTIFIED` | client attempted chat before successful `IDENTIFY` |
| `INTERNAL_ERROR` | missing internal context for request processing |

## Behavior Notes

- The server is authoritative.
- The gateway is transport-only and does not validate protocol payloads.
- Valid `CHAT_MESSAGE` responses are broadcast to all connected clients.
- Non-chat responses are returned only to the originating client.

## Security and Limits

Current implementation limitations:

- no authentication
- no authorization
- no TLS
- no rate limiting
- no persistent identity

Do not treat the current protocol as production-ready for untrusted environments.

## Related Documentation

- See [../../gateway/README.md](../../gateway/README.md) for gateway transport behavior.
- See [../../server/README.md](../../server/README.md) for current server implementation details.
