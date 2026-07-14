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

Guild and channel actions (see [Guilds and Channels](#guilds-and-channels)) are available to any identified client independent of `CHAT_MESSAGE` — there is no additional handshake step for them, and using them has no effect on the `CHAT_MESSAGE` global-broadcast behavior above.

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
  "server_version": "0.5"
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

### Guilds and Channels

A **guild** is a container owned by its creator, holding a set of **channels**. A **channel** belongs to exactly one guild and has a `channel_type` of `TEXT` or `VOICE`; only `TEXT` channels are functional today (see [Security and Limits](#security-and-limits)). All guild/channel actions below require the connection to be identified first (`ERROR` / `NOT_IDENTIFIED` otherwise), independent of `CHAT_MESSAGE`.

A connection can be a member of multiple guilds at once, but has at most one **active channel** at a time across all guilds — joining a channel implicitly leaves whichever channel was previously active. Guild membership alone does not deliver channel messages; a connection must explicitly `JOIN_CHANNEL` to receive them.

Only the guild owner can create or delete channels in this iteration. There is no mechanism yet to delegate that to other members, and no privacy model — `LIST_GUILDS` returns every guild that exists, and any identified client can `JOIN_GUILD` any of them by id.

#### CREATE_GUILD

Client to server:

```json
{
  "type": "CREATE_GUILD",
  "name": "My Guild"
}
```

Validation:

- payload must be an object
- `name` must exist, be a string, be non-empty, and be at most `64` characters

On success, the server creates the guild, adds the creator as a member, and returns:

```json
{
  "type": "GUILD_CREATED",
  "guild_id": "g_1",
  "name": "My Guild",
  "owner_id": "u_1"
}
```

#### LIST_GUILDS

Client to server:

```json
{ "type": "LIST_GUILDS" }
```

Response, listing every guild that exists:

```json
{
  "type": "GUILD_LIST",
  "guilds": [
    { "guild_id": "g_1", "name": "My Guild", "owner_id": "u_1" }
  ]
}
```

#### JOIN_GUILD

Client to server:

```json
{
  "type": "JOIN_GUILD",
  "guild_id": "g_1"
}
```

Validation:

- `guild_id` must exist and be a string
- the guild must exist (`ERROR` / `GUILD_NOT_FOUND` otherwise)
- the connection must not already be a member (`ERROR` / `PROTOCOL_VIOLATION` otherwise)

On success:

```json
{
  "type": "GUILD_JOINED",
  "guild_id": "g_1",
  "name": "My Guild",
  "owner_id": "u_1",
  "channels": [
    { "channel_id": "c_1", "name": "general", "channel_type": "TEXT" }
  ]
}
```

#### LEAVE_GUILD

Client to server:

```json
{
  "type": "LEAVE_GUILD",
  "guild_id": "g_1"
}
```

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be a member (`NOT_GUILD_MEMBER`)
- the connection must not be the guild owner (`PROTOCOL_VIOLATION` — owners must use `DELETE_GUILD` instead)

On success, the server removes the membership, clears the connection's active channel if it belonged to this guild, and broadcasts to every member who was in the guild **including the leaver**:

```json
{
  "type": "MEMBER_LEFT",
  "guild_id": "g_1",
  "user_id": "u_2"
}
```

#### DELETE_GUILD

Client to server:

```json
{
  "type": "DELETE_GUILD",
  "guild_id": "g_1"
}
```

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be the guild owner (`NOT_GUILD_OWNER` otherwise)

On success, the server deletes the guild and all of its channels, clears membership and any active channel for every affected connection, and broadcasts to every former member:

```json
{
  "type": "GUILD_DELETED",
  "guild_id": "g_1"
}
```

#### LIST_CHANNELS

Client to server:

```json
{
  "type": "LIST_CHANNELS",
  "guild_id": "g_1"
}
```

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be a member of the guild (`NOT_GUILD_MEMBER` otherwise)

Response:

```json
{
  "type": "CHANNEL_LIST",
  "guild_id": "g_1",
  "channels": [
    { "channel_id": "c_1", "name": "general", "channel_type": "TEXT" }
  ]
}
```

#### CREATE_CHANNEL

Client to server:

```json
{
  "type": "CREATE_CHANNEL",
  "guild_id": "g_1",
  "name": "general",
  "channel_type": "TEXT"
}
```

Validation:

- `name` must exist, be a string, be non-empty, and be at most `64` characters
- `channel_type` must exist and be exactly `"TEXT"` or `"VOICE"` (`MALFORMED_MESSAGE` otherwise)
- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be the guild owner (`NOT_GUILD_OWNER` otherwise — this is the only authorization check today; see [Security and Limits](#security-and-limits))

On success, the server broadcasts to **every current guild member**, including the owner:

```json
{
  "type": "CHANNEL_CREATED",
  "guild_id": "g_1",
  "channel_id": "c_2",
  "name": "general",
  "channel_type": "TEXT"
}
```

#### DELETE_CHANNEL

Client to server:

```json
{
  "type": "DELETE_CHANNEL",
  "guild_id": "g_1",
  "channel_id": "c_2"
}
```

Validation:

- the channel must exist and belong to the given guild (`CHANNEL_NOT_FOUND` otherwise)
- the connection must be the guild owner (`NOT_GUILD_OWNER` otherwise)

On success, the server clears the active channel for any connection that had it active and broadcasts to every current guild member:

```json
{
  "type": "CHANNEL_DELETED",
  "guild_id": "g_1",
  "channel_id": "c_2"
}
```

#### JOIN_CHANNEL

Client to server:

```json
{
  "type": "JOIN_CHANNEL",
  "channel_id": "c_2"
}
```

Validation:

- the channel must exist (`CHANNEL_NOT_FOUND`)
- the connection must be a member of the channel's guild (`NOT_GUILD_MEMBER`)
- the channel must be `TEXT` (`PROTOCOL_VIOLATION` for `VOICE` — not joinable this iteration)

On success, this implicitly replaces any previously active channel:

```json
{
  "type": "CHANNEL_JOINED",
  "guild_id": "g_1",
  "channel_id": "c_2"
}
```

#### LEAVE_CHANNEL

Client to server:

```json
{ "type": "LEAVE_CHANNEL" }
```

No `channel_id` — this operates on the connection's current active channel.

Validation:

- the connection must have an active channel (`NOT_IN_CHANNEL` otherwise)

Response:

```json
{
  "type": "CHANNEL_LEFT",
  "channel_id": "c_2"
}
```

#### CHANNEL_MESSAGE

Client to server:

```json
{
  "type": "CHANNEL_MESSAGE",
  "content": "hello"
}
```

No `channel_id` — the server targets the sender's current active channel rather than trusting a client-supplied id, so a connection cannot message a channel it hasn't joined.

Validation:

- the connection must have an active channel (`NOT_IN_CHANNEL` otherwise)
- `content` must exist, be a string, be non-empty, and be at most `500` characters

On success, the server broadcasts to every connection whose active channel matches, **including the sender**:

```json
{
  "type": "CHANNEL_MESSAGE",
  "channel_id": "c_2",
  "guild_id": "g_1",
  "message_id": 1,
  "timestamp": 1741104000,
  "user_id": "u_1",
  "username": "web_user",
  "content": "hello"
}
```

`message_id` is generated from its own counter, independent of `CHAT_MESSAGE`'s.

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
| `GUILD_NOT_FOUND` | referenced `guild_id` does not exist |
| `CHANNEL_NOT_FOUND` | referenced `channel_id` does not exist, or does not belong to the given guild |
| `NOT_GUILD_MEMBER` | action requires guild membership the caller doesn't have |
| `NOT_GUILD_OWNER` | action is owner-only and the caller isn't the owner |
| `NOT_IN_CHANNEL` | `CHANNEL_MESSAGE` or `LEAVE_CHANNEL` sent with no active channel |

## Behavior Notes

- The server is authoritative.
- The gateway is transport-only and does not validate protocol payloads.
- Valid `CHAT_MESSAGE` responses are broadcast to all connected clients.
- Non-chat responses are returned only to the originating client.
- Guild/channel responses that need to reach more than one connection but not literally everyone (`MEMBER_LEFT`, `GUILD_DELETED`, `CHANNEL_CREATED`, `CHANNEL_DELETED`, `CHANNEL_MESSAGE`) use a third delivery mode, `TARGETED`: the handler computes the exact set of recipient connections (e.g. "current members of this guild," or "connections with this channel active") and the server delivers only to that set. This is distinct from `BROADCAST`, which always means every connected client.

## Security and Limits

Current implementation limitations:

- no authentication
- no authorization — guild ownership is the only access control that exists, and it is not delegable yet (see `docs/rooms-spec.md`, "Future Permission Hook")
- no guild privacy — `LIST_GUILDS` returns every guild, and any identified client can `JOIN_GUILD` any of them (see `docs/rooms-spec.md`, "Deferred: Guild Privacy")
- `VOICE` channels are metadata-only: the type is modeled and validated, but there is no audio transport or voice presence
- no TLS
- no rate limiting
- no persistent identity, guilds, or channels — all in-memory, wiped on restart

Do not treat the current protocol as production-ready for untrusted environments.

## Related Documentation

- See [../../gateway/README.md](../../gateway/README.md) for gateway transport behavior.
- See [../../server/README.md](../../server/README.md) for current server implementation details.
- See [../../docs/rooms-spec.md](../../docs/rooms-spec.md) for the guild/channel feature's design rationale, data model, and deferred work (permissions, privacy).
