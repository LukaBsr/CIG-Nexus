# CIG-Nexus

A real-time communication platform with a WebSocket gateway, TCP server backend, and multi-client support.

## Architecture

```
Clients (Web, Desktop)
       ↓
   WebSocket Gateway (WsServer v0.1)
       ↓
   CIG Nexus Server (TCP backend)
```

## Components

### Gateway (WebSocket ↔ TCP Bridge)
Transport layer gateway that bridges WebSocket clients to the TCP backend server.
- **Version:** v0.1
- **Language:** TypeScript (Node.js)
- **Technology:** `ws` library for WebSocket server
- **Port:** 8080 (configurable via `WS_PORT`)
- **Features:**
  - 1 WebSocket connection = 1 TCP connection (1:1 mapping)
  - JSON string messages over WebSocket
  - Binary framing (4-byte big-endian size prefix) for TCP
  - Automatic connection management and error handling
  - No protocol validation or business logic

See [gateway/README.md](gateway/README.md) for details.

### Server (TCP Backend)
Authoritative backend server that owns state and coordinates message flow.
- **Language:** C++
- **Storage:** In-memory message state
- **Protocol:** Binary-framed JSON messages
- **Port:** 4242 (default)

See [server/README.md](server/README.md) for details.

### Shared Protocol
Common protocol definitions and message types.

See [shared/protocol/README.md](shared/protocol/README.md) for specification.

## Quick Start

### Start Server
```bash
cd server/build
./cig-nexus-server
# Server listening on port 4242
```

### Start Gateway
```bash
cd gateway
npm install
npm run build
WS_PORT=8080 TCP_HOST=localhost TCP_PORT=4242 npm start
# WebSocket gateway listening on port 8080
```

### Connect Client
```javascript
const ws = new WebSocket("ws://localhost:8080");

ws.onopen = () => {
  ws.send(JSON.stringify({
    type: "HELLO",
    version: "0.1",
    client: "web"
  }));
};

ws.onmessage = (msg) => {
  console.log("Received:", msg.data);
};
```

## Project Structure

```
.
├── gateway/           # WebSocket gateway (TypeScript)
│   ├── src/
│   │   ├── index.ts  # Entry point
│   │   ├── ws/       # WebSocket server
│   │   ├── tcp/      # TCP client wrapper
│   │   ├── protocol/ # Binary framing
│   │   └── types.ts  # TypeScript types
│   ├── package.json
│   └── tsconfig.json
├── server/            # TCP backend (C++)
│   ├── src/           # Implementation
│   ├── include/       # Headers
│   ├── tests/         # Unit tests
│   └── CMakeLists.txt
├── shared/            # Shared protocol definitions
│   └── protocol/
├── web/               # Web client (placeholder)
├── desktop/           # Desktop client (placeholder)
└── docs/              # Architecture documentation
```

## Development Status

**Gateway v0.1:** ✓ Complete
- WebSocket server
- TCP client with framing
- 1:1 connection mapping
- Error handling and graceful shutdown

**Server:** In development
- [x] TCP listener
- [ ] Message processing pipeline
- [ ] Protocol handlers
- [ ] Session management

**Clients:** Placeholder
- [ ] Web client
- [ ] Desktop client