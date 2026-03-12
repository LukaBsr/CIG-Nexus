# CIG Nexus Gateway

The gateway is a minimal WebSocket-to-TCP bridge for browser clients. It accepts JSON messages over WebSocket, frames them for the TCP server, and forwards server responses back to the browser.

## Current Role

The gateway is intentionally transport-only.

It does:

- accept browser WebSocket connections
- open one TCP connection per WebSocket connection
- encode outgoing TCP frames
- decode incoming TCP frames
- forward messages between browser and server
- close both sides when either side disconnects

It does not:

- validate protocol fields
- inspect business meaning of messages
- authenticate users
- manage sessions
- transform message payloads

## Runtime Behavior

### Browser to Server

1. Browser sends a JSON string over WebSocket.
2. The gateway converts it to a `Buffer`.
3. The gateway prefixes it with a 4-byte big-endian payload size.
4. The framed message is written to the TCP socket.

### Server to Browser

1. The gateway reads raw TCP chunks.
2. It accumulates partial data in an internal buffer.
3. It extracts complete frames.
4. Each frame payload is converted back to a string.
5. The JSON string is sent to the browser over WebSocket.

## Current Configuration

Environment variables:

- `WS_PORT` default `8080`
- `TCP_HOST` default `localhost`
- `TCP_PORT` default `4242`

## Development

Install dependencies:

```bash
npm install
```

Build:

```bash
npm run build
```

Run:

```bash
WS_PORT=8080 TCP_HOST=localhost TCP_PORT=4242 npm start
```

Example startup logs:

```text
Starting WebSocket gateway...
WS_PORT=8080 TCP_HOST=localhost TCP_PORT=4242
WebSocket gateway listening on port 8080
```

## Current Implementation

```text
src/
├── index.ts
├── tcp/
│   └── TcpClient.ts
├── ws/
│   └── WsServer.ts
└── protocol/
    └── frame.ts
```

### Key Modules

`src/ws/WsServer.ts`

- creates the WebSocket server
- creates one `TcpClient` per browser connection
- forwards WebSocket messages to TCP
- forwards TCP responses back to WebSocket

`src/tcp/TcpClient.ts`

- wraps a single Node.js TCP socket
- manages an internal receive buffer
- extracts complete framed messages

`src/protocol/frame.ts`

- encodes payloads with a 4-byte big-endian length prefix
- extracts multiple complete frames from a buffer
- enforces a maximum frame size of 1 MiB

## Current Limitations

- no reconnection logic
- no backpressure handling
- no TLS
- no rate limiting
- no gateway-side metrics or structured logging

## Docker

The gateway is included in the root Docker Compose stack.

From the repository root:

```bash
docker compose up --build
```

The gateway will be reachable at `ws://localhost:8080`.

## Related Documentation

- See [../docs/gateway-api.md](../docs/gateway-api.md) for gateway transport behavior.
- See [../shared/protocol/README.md](../shared/protocol/README.md) for message framing and protocol payloads.
