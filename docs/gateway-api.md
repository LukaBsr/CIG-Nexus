# CIG Nexus WebSocket Gateway API

This document defines the API and responsibilities of the WebSocket gateway
used to connect browser clients to the CIG Nexus C++ server.

The gateway is a transport bridge only. It does not implement protocol logic
or business rules.

---

## Purpose

Web browsers cannot open raw TCP connections.  
The WebSocket gateway enables browser clients to communicate with the CIG Nexus
server by translating between WebSocket and the custom TCP protocol.

The gateway exists to:
- Enable web clients without modifying the C++ server
- Preserve the existing TCP protocol and handlers
- Keep transport concerns isolated from business logic

---

## Scope (v0.1)

### In Scope
- WebSocket ↔ TCP transport bridging
- Binary framing on the TCP side
- JSON messages on the WebSocket side
- One WebSocket connection mapped to one TCP connection
- Transparent message forwarding

### Out of Scope
- Protocol validation
- Authentication
- Authorization
- Session management
- Message routing or handling
- Business logic of any kind

---

## Architecture Overview

```
    Browser (Next.js)
           │   
           │  WebSocket (JSON)
           │
           ▼
    WebSocket Gateway
           │  
           │  TCP (Binary Framing
           │         + JSON)
           ▼
  CIG Nexus C++ Server
```

---

## Connection Lifecycle

For each WebSocket client:

1. Browser establishes a WebSocket connection to the gateway
2. Gateway opens a TCP connection to the CIG Nexus server
3. Browser sends protocol messages as JSON over WebSocket
4. Gateway frames and forwards messages to the TCP server
5. Server responses are unframed and forwarded to the browser
6. If either side closes the connection, the gateway closes the other side

### Connection Mapping

- **1 WebSocket connection = 1 TCP connection**
- No multiplexing in v0.1

---

## Message Format

### WebSocket Messages

- One WebSocket message corresponds to one protocol message
- Payload is a UTF-8 encoded JSON string
- No additional envelope or metadata

#### Example (Browser → Gateway)

```json
{
  "type": "HELLO",
  "version": "0.1",
  "client": "web"
}
```
---

### TCP Messages

Messages are framed using the existing binary framing protocol:

- 4-byte big-endian unsigned integer size
- Followed by the JSON payload bytes

The gateway applies framing before sending messages to the server and removes
framing when receiving messages from the server.

### Message Forwarding Rules

- Messages are forwarded as-is
- The gateway does not modify message contents
- The gateway does not inspect or validate protocol fields
- Message ordering is preserved

### Server Responses

Responses from the CIG Nexus server are forwarded directly to the browser.

### Example (Server → Gateway → Browser)
```json
{
  "type": "WELCOME",
  "session_id": "abc123",
  "server_version": "0.1"
}
```

## Error Handling

The gateway distinguishes between protocol errors and gateway errors.

### Protocol Errors

Originating from the CIG Nexus server

Represented by ERROR messages

Forwarded directly to the browser without modification

### Example
```json
{
  "type": "ERROR",
  "code": "UNSUPPORTED_VERSION",
  "message": "Protocol version not supported"
}
```

### Gateway Errors

Errors caused by transport or infrastructure issues, such as:

- TCP server unreachable
- TCP connection closed unexpectedly
- Invalid framing
- Internal gateway failures

Gateway errors use a distinct message type to avoid confusion with protocol
errors.

### Gateway Error Format
```json
{
  "type": "GATEWAY_ERROR",
  "message": "Unable to connect to server"
}
```

## Connection Failure Behavior

- If the TCP connection fails during WebSocket lifetime:
    - Send a GATEWAY_ERROR to the browser
    - Close the WebSocket connection

- If the WebSocket connection closes:
    - Immediately close the TCP connection

## Security Considerations (v0.1)

- The gateway performs no authentication or authorization
- Communication is unencrypted
- The gateway must not expose internal server details in error messages

⚠️ This setup is intended for development and testing only.

## Future Evolution

### Native WebSocket Support

In a future version, WebSocket support may be integrated directly into the
CIG Nexus C++ server.

This would allow:

- Direct browser connections
- Removal of the external gateway
- Unified transport handling

The current design intentionally keeps transport abstraction isolated so that
this evolution can occur without refactoring protocol or business logic.

## Design Principles

- **Transport-only**: No business logic
- **Transparent**: Forward messages without modification
- **Minimal**: Small surface area, easy to reason about
- **Disposable**: Can be replaced by native WebSocket support later

## Summary

The WebSocket gateway is a thin, intentional bridge between browser clients and
the CIG Nexus TCP server.

By isolating transport concerns, it enables rapid web development while
preserving the integrity, simplicity, and testability of the core server.
