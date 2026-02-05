# CIG Nexus Architecture

A real-time communication platform architected for clarity, control, and extensibility.

## System Overview

CIG Nexus is composed of three primary components:

- **C++ Server** - Central authoritative service for state management and coordination
- **Web Application** - Next.js-based frontend for browser clients
- **Desktop Application** - Electron-based cross-platform client

```
┌─────────────────────────────────────────────┐
│           C++ Server (Authoritative)        │
│  • Connection Management                    │
│  • State Synchronization                    │
│  • Message Broadcasting                     │
└──────────┬──────────────────────┬───────────┘
           │                      │
      TCP Protocol             TCP Protocol
    (Custom Binary)           (Custom Binary)
           │                      │
    ┌──────▼────────┐      ┌──────▼──────────┐
    │  Web Client   │      │ Desktop Client  │
    │   (Next.js)   │      │  (Electron)     │
    └───────────────┘      └─────────────────┘
```

## Server Responsibilities

The server is the authoritative source of truth and is responsible for:

- **Connection Management** - Establishing and maintaining TCP connections with clients
- **Authentication** - Verifying client identity (planned for v0.2)
- **Session Management** - Tracking user sessions and maintaining connection state
- **State Synchronization** - Ensuring all clients reflect the current server state
- **Message Broadcasting** - Distributing messages to relevant clients
- **Validation & Authorization** - All critical decisions and state changes validated server-side

### Server Design Principles

- **Authoritative**: Single source of truth for all application state
- **Stateless on Startup**: State reconstructed from persistent storage
- **Non-blocking I/O**: Handles multiple concurrent connections efficiently

## Network Communication

### Why a Custom TCP Protocol

Rather than using existing solutions like WebSockets or gRPC, we implement a custom TCP protocol for:

- **Full Control**: Complete ownership of protocol behavior and evolution
- **Predictable Performance**: Optimized framing without feature bloat
- **Learning Goals**: Deep networking and low-level systems integration
- **C++ Integration**: Seamless integration with C++ networking libraries

### Message Framing Strategy

Binary framing provides:

- **Clear Boundaries**: Unambiguous message separation
- **Resilience**: Handles partial reads and network delays gracefully
- **Separation of Concerns**: Transport layer independent from application logic
- **Efficiency**: Minimal overhead for high-frequency messages

For protocol details, see [../shared/protocol/README.md](../shared/protocol/README.md).

## Client Architecture

### Design Philosophy

Clients follow a simple, independent model:

- **No Critical Decisions**: Clients do not validate state or make authoritative decisions
- **Intent-Based**: Clients send intentions to the server; the server decides execution
- **State Reflection**: Clients display and interact with server state
- **Stateless Operations**: Each client operates independently without awareness of others

### Client Types

| Client | Technology | Role |
|--------|-----------|------|
| **Web** | Next.js | Browser-based interface |
| **Desktop** | Electron | Cross-platform application |
| **Bot** | Custom | Programmatic access (future) |

## Scalability & Future Work

The current v0.1 release focuses on core functionality. Future versions will address scalability:

- **Horizontal Scaling** (v1.0+) - Multiple server instances
- **Sharding** (v1.0+) - Data partitioning across servers
- **WebSocket Gateway** (v1.0+) - Browser clients without raw TCP
- **Load Balancing** (v1.0+) - Distributed traffic handling
- **Clustering** (v1.0+) - Server-to-server coordination

These features are intentionally deferred to ensure core stability first.

## Protocol Roadmap

| Version | Focus | Status |
|---------|-------|--------|
| **v0.1** | Framing & Handshake | In Progress |
| **v0.2** | Authentication | Planned |
| **v0.3** | Messaging System | Planned |
| **v1.0** | Stability & Security | Planned |

## Directory Structure

```
CIG-Nexus/
├── server/              # C++ server implementation
│   ├── src/            # Server source code
│   ├── include/        # Header files
│   ├── tests/          # Server tests
│   └── CMakeLists.txt  # CMake configuration
├── web/                # Next.js web application
│   ├── app/            # Application code
│   └── public/         # Static assets
├── desktop/            # Electron desktop application
│   └── src/            # Application source
├── shared/             # Shared resources
│   └── protocol/       # Protocol documentation
└── docs/               # Documentation
```

## Design Principles

The CIG Nexus architecture is built on four core principles:

### 1. Simplicity
- Minimalist protocol design
- Clear separation of concerns
- Easy to understand and extend

### 2. Robustness
- Authoritative server model prevents inconsistency
- Binary framing handles edge cases
- Explicit error handling

### 3. Extensibility
- Protocol versioning for evolution
- Message types easily added without breaking clients
- Modular server design

### 4. Future-Proof
- TCP foundation for long-term viability
- Migration path to binary payloads
- Scalability considerations baked in

## Security Considerations

**Current Release (v0.1)**: Unencrypted, unauthenticated communication

Security roadmap:

- **v0.2**: Authentication layer
- **v1.0**: TLS/SSL encryption
- **Post-v1.0**: Advanced security features

⚠️ Do not use this system for production without proper security measures.

## Development Guidelines

### Server Development
- Implement in C++ with focus on performance and reliability
- Use non-blocking I/O patterns
- Validate all client input

### Client Development
- Never trust local client state
- Always request server validation
- Handle connection failures gracefully
- Implement exponential backoff for reconnection

### Protocol Changes
- Never remove message fields (only add)
- Version changes carefully
- Support multiple protocol versions for compatibility

## Conclusion

CIG Nexus is architected as a modern, extensible real-time communication platform with a strong foundation for growth. The authoritative server model, custom protocol, and clear separation of concerns provide both immediate functionality and a clear path to sophisticated features in future releases.
