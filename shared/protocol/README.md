# CIG Nexus TCP Protocol

A custom TCP-based protocol designed for real-time communication with full control over framing, parsing, and evolution.

## Overview

The CIG Nexus protocol provides a lightweight, frame-based messaging system built on:

- **TCP transport** for reliable delivery
- **Binary framing** for robust message boundaries
- **JSON payloads** for flexibility and debugging
- **Authoritative server model** for single source of truth

## Transport

- **Protocol**: TCP
- **Connection**: One persistent connection per client
- **I/O Model**: Non-blocking on the server side
- **Reliability**: TCP's built-in reliability guarantees

## Message Framing

Each message follows a simple length-prefixed frame format:

```
+--------------------+----------------------+
| uint32_t size (4B) | JSON payload (size) |
+--------------------+----------------------+
```

### Frame Structure

| Component | Details |
|-----------|---------|
| **Size** | 32-bit unsigned integer, network byte order (big-endian), exact size of JSON payload in bytes (minimum 1, maximum `MAX_FRAME_SIZE`) |
| **Payload** | UTF-8 encoded JSON, no delimiter or terminator, exactly one message per frame |

#### Size Constraints and Validation

- **Minimum size**: `size` MUST be at least `1`. A frame with `size == 0` is invalid and MUST be rejected.
- **Maximum size**: Implementations MUST enforce a configured `MAX_FRAME_SIZE` (recommended default: 1 MiB). Frames with `size > MAX_FRAME_SIZE` MUST be treated as a protocol error and SHOULD cause the connection to be closed.
- **Truncated frames**: If fewer than `size` bytes are received for the payload (e.g., the peer closes the connection or a read timeout occurs), the frame is invalid and MUST NOT be processed. The connection SHOULD be closed.
- **Overlong payloads**: If more than `size` bytes are received before processing, only the first `size` bytes belong to the current frame; any additional bytes belong to subsequent frames and MUST NOT be consumed as part of the current payload.
### Example Frame

```
00 00 00 3C
{ "type": "HELLO", "version": "0.1", "client": "desktop" }
```

## Design Philosophy

- **Server Authority**: The server is the authoritative source of truth
- **Client Intentions**: Clients only send intentions; the server validates and executes
- **Centralized Logic**: All validation and state changes are handled server-side

## Connection Lifecycle

1. TCP connection is established
2. Client sends `HELLO` message
3. Server responds with `WELCOME` or `ERROR`
4. Session becomes active

## Common Messages

### HELLO (Client → Server)

Initial handshake message sent by the client after connecting.

```json
{
  "type": "HELLO",
  "version": "0.1",
  "client": "desktop"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"HELLO"` |
| `version` | string | Protocol version (MAJOR.MINOR) |
| `client` | string | Client type: `web`, `desktop`, or `bot` |

### WELCOME (Server → Client)

Positive response to a valid `HELLO` message.

```json
{
  "type": "WELCOME",
  "session_id": "abc123",
  "server_version": "0.1"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"WELCOME"` |
| `session_id` | string | Unique session identifier for this connection |
| `server_version` | string | Protocol version supported by the server |

### ERROR (Server → Client)

Error response sent when a message cannot be processed or connection is rejected.

```json
{
  "type": "ERROR",
  "code": "UNSUPPORTED_VERSION",
  "message": "Protocol version not supported"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"ERROR"` |
| `code` | string | Machine-readable error code |
| `message` | string | Human-readable error description |

**Error codes (v0.1)**

The following error codes may be returned by the server in v0.1:

| Code | Description |
|------|-------------|
| `UNSUPPORTED_VERSION` | The client requested a protocol version not supported by the server. |
| `PROTOCOL_VIOLATION` | The client sent a message that violates the protocol (e.g., wrong type, invalid sequence). |
| `MALFORMED_MESSAGE` | The client sent a message that could not be parsed (e.g., invalid JSON, missing required fields). |
| `ABUSIVE_BEHAVIOR` | The client exhibited abusive or disallowed behavior and the server rejected the request. |
| `INACTIVITY_TIMEOUT` | The client was inactive for too long and the server timed out the connection. |
| `INTERNAL_ERROR` | The server encountered an unexpected internal error while processing the request. |
## Connection Termination

The server may close the connection in the following cases:

- Protocol violations detected
- Malformed messages received
- Client version is unsupported
- Abusive behavior detected
- Inactivity timeout (if configured)

## Versioning

- **Format**: `MAJOR.MINOR`
- **Incompatibility**: Incompatible versions are rejected during the handshake
- **Backward Compatibility**: Optional and explicit; not guaranteed between versions
- **Evolution**: New message types and fields can be added with version increments

## Security (v0.1)

⚠️ **Current implementation is unencrypted and unauthenticated**

- ❌ No authentication
- ❌ No encryption
- ❌ No rate limiting

**Security features will be added in later versions.** Do not use this protocol for sensitive data in production without additional security measures.

## Future Roadmap

- **v0.2+**: Authentication and authorization
- **v1.0**: TLS/SSL encryption
- **Post-v1.0**: Binary payload format migration for performance

## Notes

- JSON is intentionally used for readability and debugging in early versions
- Each connection is independent; no state is shared between clients
- The protocol is designed to be easily extended with new message types
- Migration to a binary payload format is planned for later versions to improve performance
