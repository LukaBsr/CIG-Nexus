# CIG Nexus Gateway API

This document describes the current transport contract of the WebSocket gateway used by browser clients.

## Purpose

Browsers cannot open raw TCP sockets, so the gateway provides a WebSocket entry point and forwards those messages to the TCP server.

The gateway remains transport-only:

- no business logic
- no protocol validation
- no authentication
- no session management
- no message transformation

## Connection Model

- one WebSocket connection maps to one TCP connection
- the TCP connection is opened when the WebSocket client connects
- if either side closes, the gateway closes the other side

## WebSocket Side

Client-facing transport:

- protocol: WebSocket
- payload type: JSON string
- expected browser endpoint: `ws://localhost:8080`

Examples:

Handshake request:

```json
{
  "type": "HELLO",
  "version": "0.1",
  "client": "web"
}
```

Identify request:

```json
{
  "type": "IDENTIFY",
  "username": "web_user"
}
```

Chat request:

```json
{
  "type": "CHAT_MESSAGE",
  "content": "hello"
}
```

## TCP Side

Server-facing transport:

- protocol: TCP
- framing: 4-byte big-endian payload size prefix
- payload type: JSON bytes
- default server endpoint: `localhost:4242`

The gateway encodes outgoing frames and extracts incoming frames using the same framing rules documented in the shared protocol README.

## Forwarding Rules

- WebSocket text messages are forwarded to TCP as framed bytes
- TCP framed payloads are forwarded to WebSocket as text
- payload contents are not modified by the gateway
- message ordering is preserved

## Lifecycle

For each browser connection:

1. WebSocket client connects.
2. Gateway opens a TCP connection to the backend server.
3. Browser sends `HELLO` then `IDENTIFY`.
4. Browser chat messages are forwarded to TCP.
5. TCP responses (`WELCOME`, `IDENTIFIED`, `CHAT_MESSAGE`, `ERROR`) are forwarded back to the browser.
6. If one side closes or errors, the gateway closes the other side.

## Gateway Error Message

If the gateway cannot connect to the TCP server, it sends:

```json
{
  "type": "GATEWAY_ERROR",
  "message": "Unable to connect to server"
}
```

After that, it closes the WebSocket connection.

## Current Limits

- no reconnection logic
- no multiplexing
- no gateway-side rate limiting
- no TLS termination
- no structured retry or buffering strategy beyond in-memory partial frame accumulation

## Design Intent

The gateway is intentionally thin so the server owns protocol semantics and the browser can still participate in the system using standard WebSocket APIs.
