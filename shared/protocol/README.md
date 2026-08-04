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
4. Client sends `IDENTIFY` with a session token.
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
  "session_token": "<JWT>"
}
```

`session_token` is the short-lived RS256 access JWT issued by the web
client's `/api/auth/session-token` endpoint after a completed Discord
OAuth2 login — not a client-chosen value. See
`docs/auth/discord-design.md` §6/§8 for the full issuance flow and claim
shape.

Validation:

- payload must be an object
- `session_token` must exist (`AUTH_REQUIRED` otherwise)
- `session_token` must be a string (`AUTH_REQUIRED` otherwise)
- `session_token` must be a well-formed JWT with a valid signature and the
  expected audience, signed with the algorithm this server is configured
  to verify (`INVALID_SESSION` otherwise — see error codes below)
- `session_token` must not be expired (`SESSION_EXPIRED` otherwise)
- `session_token` must reference a session that has not been revoked
  (`SESSION_REVOKED` otherwise)

On success, the server creates a session for that socket — with `user_id`
and `username` taken from the token's verified claims, not supplied by the
client — and returns:

```json
{
  "type": "IDENTIFIED",
  "user_id": "u_1",
  "username": "web_user"
}
```

The new session's guild memberships are also hydrated immediately from durable state (`docs/guilds/social-presence-design.md` §3.4/§1.10) — a client does not need to re-issue `JOIN_GUILD` for every guild it already belongs to just because it reconnected. `LIST_CHANNELS`/`LIST_MEMBERS`/`CHANNEL_MESSAGE` etc. against a guild the identified user already belongs to work immediately after `IDENTIFY`, without an intervening `JOIN_GUILD` call.

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

### PRESENCE_UPDATE

Server to client only — there is no client-sent message for this; presence is derived entirely from connection lifecycle events the server already observes.

```json
{ "type": "PRESENCE_UPDATE", "user_id": "u_1", "status": "online" }
```

`status` is `"online"` or `"offline"`. Presence is tracked as a connection count per `user_id`, not per connection — a user can have multiple simultaneous connections (multiple tabs/devices), and only the transitions matter:

- `"online"`: broadcast when a user's connection count goes `0 → 1` (their first connection completes `IDENTIFY`).
- `"offline"`: broadcast when it goes `1 → 0` (their last connection disconnects, whether a clean close or a detected reset).

A second or third connection for the same user connecting or disconnecting emits nothing — no visible state change occurred. Delivery is `Scope::BROADCAST` (`docs/guilds/social-presence-design.md` §3.4) — every connected client receives every `PRESENCE_UPDATE`, including the user whose own status just changed and regardless of shared guild membership; there is no per-guild-scoped variant. A client wanting a per-guild "who's online" view computes it locally by intersecting the globally-received online set against the roster it already has for that guild (`LIST_MEMBERS`). One exception: a user who has blocked at least one other user excludes that user's connections specifically — see [Presence exclusion mechanism](#presence-exclusion-mechanism) under [Blocking](#blocking).

**Known limitation**: detecting a peer that stops responding without a clean close (network partition, laptop sleep) relies on TCP keepalive (`SO_KEEPALIVE`, tuned to a ~30s idle timeout / 10s probe interval / 3 probes — well under Linux's default of several hours), not an application-level heartbeat. A user can appear `"online"` for up to roughly that keepalive window after actually going dark.

### Guilds and Channels

A **guild** is a container owned by its creator, holding a set of **channels**. A **channel** belongs to exactly one guild and has a `channel_type` of `TEXT` or `VOICE`; only `TEXT` channels are functional today (see [Security and Limits](#security-and-limits)). All guild/channel actions below require the connection to be identified first (`ERROR` / `NOT_IDENTIFIED` otherwise), independent of `CHAT_MESSAGE`.

A connection can be a member of multiple guilds at once, but has at most one **active channel** at a time across all guilds — joining a channel implicitly leaves whichever channel was previously active. Guild membership alone does not deliver channel messages; a connection must explicitly `JOIN_CHANNEL` to receive them.

Every guild member has a `role_rank` (integer, `0` = crew/member, `1` = officer, `2` = owner — see [Roles](#roles)). Creating a channel or an invite requires officer-or-above; deleting a channel or the guild itself, changing another member's role, and changing guild visibility, stay owner-only.

Every guild also has a `visibility` (`"open"` | `"application"` | `"private"`, defaults to `"open"` — see [Guild Visibility](#guild-visibility)) controlling discovery and how a non-member can join.

#### CREATE_GUILD

Client to server:

```json
{
  "type": "CREATE_GUILD",
  "name": "My Guild",
  "visibility": "open"
}
```

`visibility` is optional, defaulting to `"open"` — see [Guild Visibility](#guild-visibility).

Validation:

- payload must be an object
- `name` must exist, be a string, be non-empty, and be at most `64` characters
- `visibility`, if present, must be `"open"`, `"application"`, or `"private"` (`MALFORMED_MESSAGE` otherwise)

On success, the server creates the guild, adds the creator as a member at the owner rank, and returns:

```json
{
  "type": "GUILD_CREATED",
  "guild_id": "g_1",
  "name": "My Guild",
  "owner_id": "u_1",
  "visibility": "open"
}
```

#### LIST_GUILDS

Client to server:

```json
{ "type": "LIST_GUILDS" }
```

Response, listing every guild visible to the caller — every `open`/`application` guild, plus any `private` guild the caller is already a member of; `private` guilds the caller isn't a member of are silently omitted, not flagged as inaccessible (see [Guild Visibility](#guild-visibility)):

```json
{
  "type": "GUILD_LIST",
  "guilds": [
    { "guild_id": "g_1", "name": "My Guild", "owner_id": "u_1", "visibility": "open" }
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
- the guild must exist (`ERROR` / `GUILD_NOT_FOUND` otherwise) — a `private` guild the caller isn't a member of returns this same code, indistinguishable from a nonexistent id (see [Guild Visibility](#guild-visibility))
- the connection must not already be a member (`ERROR` / `PROTOCOL_VIOLATION` otherwise)
- the guild must not be `application`-visibility (`ERROR` / `GUILD_REQUIRES_APPROVAL` otherwise — use `REQUEST_JOIN` instead)

On success:

```json
{
  "type": "GUILD_JOINED",
  "guild_id": "g_1",
  "name": "My Guild",
  "owner_id": "u_1",
  "visibility": "open",
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

#### Guild Visibility

`visibility` is one of:

| Mode | Listed in `LIST_GUILDS`? | `JOIN_GUILD`-by-id | `JOIN_VIA_INVITE` |
|---|---|---|---|
| `open` (default) | Yes, to everyone | Works directly | Creates membership directly |
| `application` | Yes, to everyone | Rejected — `GUILD_REQUIRES_APPROVAL`, use `REQUEST_JOIN` instead | Creates a **join request**, not direct membership — the invite is still consumed, but an officer must still approve (see [Join Requests](#join-requests)) |
| `private` | No — omitted from `LIST_GUILDS` for non-members; members still see it | Rejected — `GUILD_NOT_FOUND`, indistinguishable from a nonexistent id | Creates membership directly (this is the mode's only door) |

`private` is deliberately information-hiding: probing a private guild's id (via `JOIN_GUILD`, `REQUEST_JOIN`, `LIST_MEMBERS`, or `LIST_CHANNELS`) returns the same error a nonexistent id would, never a distinct "this exists but you can't see it" response.

#### SET_GUILD_VISIBILITY

Client to server:

```json
{ "type": "SET_GUILD_VISIBILITY", "guild_id": "g_1", "visibility": "private" }
```

Validation:

- `guild_id`/`visibility` must exist and be strings; `visibility` must be `"open"`, `"application"`, or `"private"` (`MALFORMED_MESSAGE` otherwise)
- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be the guild owner (`NOT_GUILD_OWNER` otherwise — owner-only, not widened to officer-or-above, since this changes who can discover/join the guild at all)

If the guild is leaving `application` mode, every pending join request for it is deleted as part of the same change — an officer's earlier silence on a request doesn't retroactively become approval just because the mode changed.

On success, broadcasts to every current guild member:

```json
{ "type": "GUILD_VISIBILITY_CHANGED", "guild_id": "g_1", "visibility": "private" }
```

#### Guild Invites

An invite is a shareable code that lets a user join a guild without already knowing its id (or, for `application`/`private` guilds, without discovering it via `LIST_GUILDS` at all). For an `open` guild this is pure convenience, not a security boundary — the guild is joinable by id regardless. For `application`/`private` guilds it's part of the actual access-control surface.

#### CREATE_INVITE

Client to server:

```json
{ "type": "CREATE_INVITE", "guild_id": "g_1", "max_uses": null, "expires_in_seconds": null }
```

`max_uses` and `expires_in_seconds` are both optional, defaulting to unlimited uses and no expiry (reusable, permanent link, matching Discord's own default). `expires_in_seconds` is relative to when the server processes the request; the server stores an absolute expiry.

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be an officer or above (`NOT_GUILD_OFFICER` otherwise)
- `max_uses`, if present, must be a positive integer
- `expires_in_seconds`, if present, must be a positive integer

On success:

```json
{
  "type": "INVITE_CREATED",
  "guild_id": "g_1",
  "code": "aB3kD9qP2x",
  "max_uses": null,
  "use_count": 0,
  "expires_at": null,
  "created_at": "2026-08-01T12:00:00Z"
}
```

No `invite_url` field — building a shareable link is a web-client concern, not a protocol one.

#### LIST_INVITES

Client to server:

```json
{ "type": "LIST_INVITES", "guild_id": "g_1" }
```

Validation: same as `CREATE_INVITE` (officer-or-above — if you can create invites, you can see the ones that exist).

Response:

```json
{
  "type": "INVITE_LIST",
  "guild_id": "g_1",
  "invites": [
    { "code": "aB3kD9qP2x", "max_uses": null, "use_count": 3, "expires_at": null, "revoked_at": null, "created_at": "2026-08-01T12:00:00Z" }
  ]
}
```

#### REVOKE_INVITE

Client to server:

```json
{ "type": "REVOKE_INVITE", "guild_id": "g_1", "code": "aB3kD9qP2x" }
```

Validation:

- the invite must exist and belong to `guild_id` (`INVITE_NOT_FOUND` otherwise)
- the connection must be an officer or above (`NOT_GUILD_OFFICER` otherwise)

On success:

```json
{ "type": "INVITE_REVOKED", "guild_id": "g_1", "code": "aB3kD9qP2x" }
```

#### JOIN_VIA_INVITE

Client to server:

```json
{ "type": "JOIN_VIA_INVITE", "code": "aB3kD9qP2x" }
```

Validated, in order: the invite must exist (`INVITE_NOT_FOUND`), not be revoked (`INVITE_REVOKED`), not be expired (`INVITE_EXPIRED`), be under `max_uses` if set (`INVITE_MAX_USES_REACHED`), and the caller must not already be a member of the invite's guild (`PROTOCOL_VIOLATION`, reusing the same code `JOIN_GUILD` uses for "already a member").

On success against an `open` or `private` guild, the response is `GUILD_JOINED` — the exact same shape `JOIN_GUILD` returns:

```json
{
  "type": "GUILD_JOINED",
  "guild_id": "g_1",
  "name": "My Guild",
  "owner_id": "u_1",
  "visibility": "open",
  "channels": [ { "channel_id": "c_1", "name": "general", "channel_type": "TEXT" } ]
}
```

On success against an `application` guild, the invite is still consumed (its `use_count` increments), but membership is **not** granted directly — a join request is created instead, and the response is `JOIN_REQUESTED` (the same shape `REQUEST_JOIN` returns, see [Join Requests](#join-requests)) rather than `GUILD_JOINED`. Every currently-connected officer-or-above member of the guild also receives `JOIN_REQUEST_RECEIVED`, exactly as if the requester had called `REQUEST_JOIN` directly.

#### Join Requests

For an `application`-visibility guild, joining is a two-step flow: a non-member calls `REQUEST_JOIN` (or redeems an invite, see above), and an officer-or-above member later calls `APPROVE_JOIN_REQUEST` or `REJECT_JOIN_REQUEST`.

#### REQUEST_JOIN

Client to server:

```json
{ "type": "REQUEST_JOIN", "guild_id": "g_1" }
```

Validation:

- the guild must exist and not be `private` (`GUILD_NOT_FOUND` for both — see [Guild Visibility](#guild-visibility))
- the guild must not be `open` (`PROTOCOL_VIOLATION` — "guild is open, use `JOIN_GUILD` instead")
- the connection must not already be a member (`PROTOCOL_VIOLATION`)
- the connection must not already have a pending request for this guild (`JOIN_REQUEST_ALREADY_PENDING`)

On success, the requester receives:

```json
{ "type": "JOIN_REQUESTED", "guild_id": "g_1" }
```

...and every currently-connected officer-or-above member of the guild also receives, so officers don't have to poll `LIST_JOIN_REQUESTS` to notice a new one:

```json
{ "type": "JOIN_REQUEST_RECEIVED", "guild_id": "g_1", "user_id": "u_2", "username": "web_user" }
```

#### LIST_JOIN_REQUESTS

Client to server:

```json
{ "type": "LIST_JOIN_REQUESTS", "guild_id": "g_1" }
```

Validation: the guild must exist (`GUILD_NOT_FOUND`); the connection must be an officer or above (`NOT_GUILD_OFFICER` otherwise).

Response:

```json
{
  "type": "JOIN_REQUEST_LIST",
  "guild_id": "g_1",
  "requests": [
    { "user_id": "u_2", "username": "web_user", "requested_at": "2026-08-01T12:00:00Z" }
  ]
}
```

#### APPROVE_JOIN_REQUEST / REJECT_JOIN_REQUEST

Client to server:

```json
{ "type": "APPROVE_JOIN_REQUEST", "guild_id": "g_1", "user_id": "u_2" }
{ "type": "REJECT_JOIN_REQUEST", "guild_id": "g_1", "user_id": "u_2" }
```

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be an officer or above (`NOT_GUILD_OFFICER` otherwise)
- a pending request for `user_id` must exist (`JOIN_REQUEST_NOT_FOUND` otherwise)

`APPROVE_JOIN_REQUEST` creates the membership (at the crew/member rank) and deletes the request; the approver receives:

```json
{ "type": "JOIN_REQUEST_APPROVED", "guild_id": "g_1", "user_id": "u_2" }
```

...and every connection currently identified as `u_2` (there may be more than one — multiple tabs/devices) receives `GUILD_JOINED`, the same shape `JOIN_GUILD`/`JOIN_VIA_INVITE` return, even though `u_2` never sent a `JOIN_GUILD` themselves. Nothing is sent to `u_2` if they aren't currently connected — they'll see the new membership via `LIST_GUILDS`/`LIST_MEMBERS` next time they connect.

`REJECT_JOIN_REQUEST` just deletes the request; the rejecter receives:

```json
{ "type": "JOIN_REQUEST_REJECTED", "guild_id": "g_1", "user_id": "u_2" }
```

...and every connection currently identified as `u_2`, if any, receives the same payload.

#### Roles

Roles are rank-based, not string-based: `guild_memberships.role_rank` is an integer, and every authorization check compares it against a named threshold (`kMemberRank = 0`, `kOfficerRank = 1`, `kOwnerRank = 2`) rather than a literal. `role_label` (e.g. `"Crew"`, `"Officer"`, `"Captain"`) is a cosmetic, per-guild display string resolved server-side from `role_rank` and never consulted by any authorization check — a client should gate UI on `role_rank`, not on the label's text.

#### LIST_MEMBERS

Client to server:

```json
{ "type": "LIST_MEMBERS", "guild_id": "g_1" }
```

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be a member of the guild (`NOT_GUILD_MEMBER` otherwise) — same precedent as `LIST_CHANNELS`

Response:

```json
{
  "type": "MEMBER_LIST",
  "guild_id": "g_1",
  "members": [
    { "user_id": "u_1", "username": "web_user", "role_rank": 2, "role_label": "Captain", "joined_at": "2026-07-14T18:00:00Z" }
  ]
}
```

This is a live read on every request — the roster is never cached by the server, unlike the guild/channel catalog.

#### SET_MEMBER_ROLE

Client to server:

```json
{ "type": "SET_MEMBER_ROLE", "guild_id": "g_1", "user_id": "u_2", "role_rank": 1 }
```

Validation:

- the guild must exist (`GUILD_NOT_FOUND`)
- the connection must be the guild owner (`role_rank >= 2`; `NOT_GUILD_OWNER` otherwise — promoting/demoting is owner-only, same tier as `DELETE_GUILD`/`SET_GUILD_VISIBILITY`)
- `role_rank` must be an integer `>= 0` and `< 2` (`MALFORMED_MESSAGE` otherwise) — the owner rank itself is never a valid target for this message
- `user_id` must not be the guild's owner (`PROTOCOL_VIOLATION` otherwise) — ownership isn't reassignable through this message; see `LEAVE_GUILD`
- `user_id` must be a member of the guild (`NOT_GUILD_MEMBER` otherwise)

On success, the server broadcasts to every current guild member:

```json
{
  "type": "MEMBER_ROLE_UPDATED",
  "guild_id": "g_1",
  "user_id": "u_2",
  "role_rank": 1,
  "role_label": "Officer"
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
- the connection must be an officer or above in the guild (`role_rank >= 1`; `NOT_GUILD_OFFICER` otherwise — see [Roles](#roles) and [Security and Limits](#security-and-limits))

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

#### FETCH_HISTORY

Client to server:

```json
{
  "type": "FETCH_HISTORY",
  "channel_id": "c_2",
  "before_seq": null,
  "limit": 50
}
```

`channel_id` is optional; omit it or send JSON `null` to fetch lobby (`CHAT_MESSAGE`) history instead of a channel's. `before_seq` is optional; omit it or send `null` to fetch the most recent page. `limit` is optional, defaults to `50`, and must be between `1` and `100`.

Validation:

- the client must be identified (`NOT_IDENTIFIED` otherwise)
- when `channel_id` is given: it must reference an existing channel (`CHANNEL_NOT_FOUND` otherwise), and the caller must be a member of that channel's guild (`NOT_GUILD_MEMBER` otherwise) — reading history requires the same membership `CHANNEL_MESSAGE` already requires to send, so reading is never looser than writing. The lobby has no such check, matching `CHAT_MESSAGE`'s fully-open model.
- `before_seq`, if present, must be an integer
- `limit`, if present, must be an integer in `[1, 100]`

Server to client:

```json
{
  "type": "MESSAGE_HISTORY",
  "channel_id": "c_2",
  "messages": [
    {
      "message_id": 41,
      "timestamp": 1741104000,
      "user_id": "u_1",
      "username": "web_user",
      "content": "hello"
    }
  ],
  "has_more": true
}
```

`channel_id` in the response echoes the request (`null` for the lobby). `messages` is chronological (oldest first). `has_more` is `true` when older messages exist beyond this page — pass the oldest returned message's `message_id` as the next request's `before_seq` to page further back (keyset pagination, not offset-based).

This is a live read-through call to the internal API on every request — results are never cached by the server. See `docs/guilds/social-presence-design.md` §4 for the persistence design (write-side: `CHAT_MESSAGE`/`CHANNEL_MESSAGE` persist asynchronously, fire-and-forget with bounded retry, after the broadcast — a client can in principle receive a message before it's durably persisted).

### Friends

Not guild-scoped — a friendship is a relationship between two accounts, independent of any guild either is in. See `docs/social/friends-dms-design.md` §1 for the full design (data model, the invite-redemption-pattern reuse for friend codes, and the reverse-pending auto-accept case below). All require the connection to be identified first.

#### SEND_FRIEND_REQUEST

Client to server:

```json
{ "type": "SEND_FRIEND_REQUEST", "user_id": "u_2" }
```

Validation: `user_id` must reference an existing user (`USER_NOT_FOUND` otherwise); must not be the caller's own id (`PROTOCOL_VIOLATION`); caller and target must not already be friends (`PROTOCOL_VIOLATION`).

On success, two messages: `Scope::DIRECT` to the caller and `Scope::TARGETED` to every connection identified as the target (only sent if at least one exists):

```json
{ "type": "FRIEND_REQUEST_SENT", "user_id": "u_2" }
```

```json
{ "type": "FRIEND_REQUEST_RECEIVED", "user_id": "u_1" }
```

**Reverse-pending auto-accept**: if the target already has a pending request to the caller (i.e. the two were about to cross), no second request is created — the two become friends immediately instead, and both messages above are `FRIEND_ADDED` (see `ACCEPT_FRIEND_REQUEST` below) rather than `FRIEND_REQUEST_SENT`/`FRIEND_REQUEST_RECEIVED`.

#### ADD_FRIEND_BY_CODE

Client to server:

```json
{ "type": "ADD_FRIEND_BY_CODE", "code": "aB3kD9qP2x" }
```

Resolves `code` to a target user, then behaves exactly like `SEND_FRIEND_REQUEST` against that user — same validation, same response shapes (including the reverse-pending auto-accept case), plus `FRIEND_CODE_NOT_FOUND` if the code doesn't belong to any account.

#### ACCEPT_FRIEND_REQUEST

Client to server (`user_id` is the original requester):

```json
{ "type": "ACCEPT_FRIEND_REQUEST", "user_id": "u_1" }
```

Validation: a pending request from `user_id` to the caller must exist (`FRIEND_REQUEST_NOT_FOUND` otherwise).

On success, `Scope::TARGETED` to each participant's connections, same shape from each side:

```json
{ "type": "FRIEND_ADDED", "user_id": "u_2" }
```

#### REJECT_FRIEND_REQUEST / CANCEL_FRIEND_REQUEST

Client to server — `REJECT_FRIEND_REQUEST` is sent by the recipient of a pending request (`user_id` is the original requester); `CANCEL_FRIEND_REQUEST` is sent by the requester who sent it (`user_id` is the recipient):

```json
{ "type": "REJECT_FRIEND_REQUEST", "user_id": "u_1" }
```

```json
{ "type": "CANCEL_FRIEND_REQUEST", "user_id": "u_2" }
```

Validation: a matching pending request must exist (`FRIEND_REQUEST_NOT_FOUND` otherwise) — `REJECT_FRIEND_REQUEST` and `CANCEL_FRIEND_REQUEST` both delete the same row; only which party must be which distinguishes them.

On success, delivered to both participants (`Scope::DIRECT` to the caller, `Scope::TARGETED` to the other party):

```json
{ "type": "FRIEND_REQUEST_REJECTED", "user_id": "u_2" }
```

```json
{ "type": "FRIEND_REQUEST_CANCELED", "user_id": "u_1" }
```

#### REMOVE_FRIEND

Client to server:

```json
{ "type": "REMOVE_FRIEND", "user_id": "u_2" }
```

Validation: caller and `user_id` must currently be friends (`FRIEND_NOT_FOUND` otherwise).

On success, delivered to both participants:

```json
{ "type": "FRIEND_REMOVED", "user_id": "u_2" }
```

#### LIST_FRIENDS

Client to server: `{ "type": "LIST_FRIENDS" }`. Response (`Scope::DIRECT`):

```json
{ "type": "FRIEND_LIST", "friends": [ { "user_id": "u_2", "username": "web_user" } ] }
```

No online/offline status embedded — a client intersects this against the `PRESENCE_UPDATE` stream it already receives, the same way a per-guild "who's online" view is computed from `LIST_MEMBERS`.

#### LIST_FRIEND_REQUESTS

Client to server: `{ "type": "LIST_FRIEND_REQUESTS" }`. Response (`Scope::DIRECT`):

```json
{
  "type": "FRIEND_REQUEST_LIST",
  "incoming": [ { "user_id": "u_3", "username": "web_user", "created_at": "2026-08-01T12:00:00Z" } ],
  "outgoing": []
}
```

#### FETCH_FRIEND_CODE / REGENERATE_FRIEND_CODE

Client to server: `{ "type": "FETCH_FRIEND_CODE" }` or `{ "type": "REGENERATE_FRIEND_CODE" }`. Response (`Scope::DIRECT`) either way:

```json
{ "type": "FRIEND_CODE", "code": "aB3kD9qP2x" }
```

`REGENERATE_FRIEND_CODE` immediately invalidates the previous code — there is no grace period, and no `revoked_at`/`use_count` the way guild invites have, since a friend code has no multi-use-tracking concept: exactly one code is live per account at a time.

### Blocking

See `docs/social/friends-dms-design.md` §2 for the full design (what blocking does, the silent-failure rationale below, and the presence-exclusion mechanism). All three require the connection to be identified first.

#### BLOCK_USER

Client to server:

```json
{ "type": "BLOCK_USER", "user_id": "u_2" }
```

Validation: `user_id` must not be the caller's own id (`PROTOCOL_VIOLATION`); must reference an existing user (`USER_NOT_FOUND`). Blocking an already-blocked user is idempotent success, not an error.

On success, in the same transaction: any pending friend request between the two (either direction) is deleted, and an existing friendship is removed. Response (`Scope::DIRECT`, to the caller only — **the target receives no notification**, silent per the recommendation below):

```json
{ "type": "USER_BLOCKED", "user_id": "u_2" }
```

From this point on, the blocked user (`u_2` here) cannot send the blocker a new friend request, cannot start or continue a DM with them, and stops receiving the blocker's `PRESENCE_UPDATE`s — all silently (see below).

#### UNBLOCK_USER

Client to server: `{ "type": "UNBLOCK_USER", "user_id": "u_2" }`. Validation: an active block from the caller to `user_id` must exist (`USER_NOT_FOUND` otherwise — reused, not a distinct "no such block" code). Response (`Scope::DIRECT`, silent to the target, same as `BLOCK_USER`):

```json
{ "type": "USER_UNBLOCKED", "user_id": "u_2" }
```

Does **not** restore any friendship or pending request that existed before the block — both parties start from "no relationship."

#### LIST_BLOCKS

Client to server: `{ "type": "LIST_BLOCKS" }`. Response (`Scope::DIRECT`):

```json
{ "type": "BLOCK_LIST", "blocked": [ { "user_id": "u_2", "username": "web_user", "blocked_at": "2026-08-01T12:00:00Z" } ] }
```

#### Silent failure, not an explicit error

A blocked user reaching for the blocker gets exactly the response an unrelated, ordinary failure would produce — never a distinguishing `BLOCKED`-style code:

- `SEND_FRIEND_REQUEST`/`ADD_FRIEND_BY_CODE` against a blocked pair returns `USER_NOT_FOUND`, identical to targeting a `user_id` that doesn't exist.
- `DM_SEND` against a blocked pair returns `DM_NOT_PERMITTED`, identical to an ordinary stranger with no shared friendship or guild (see [Direct Messages](#direct-messages)).
- Presence: the blocked user simply never receives the blocker's `PRESENCE_UPDATE` — no error, no signal, indistinguishable from the blocker never having been online.
- Profile: `GET /api/users/:id/profile` (a REST endpoint, not part of this WebSocket protocol) returns the same 404 a nonexistent user would.

No code path anywhere reveals "you're blocked" as distinct from "this wouldn't have worked anyway" — deliberately, to avoid the harassment feedback loop an explicit signal would enable (a blocked user probing whether a specific action's failure means "blocked" vs. "ordinary" would otherwise get a confirming answer).

#### Presence exclusion mechanism

`PRESENCE_UPDATE` is normally `Scope::BROADCAST` (see [PRESENCE_UPDATE](#presence_update)) — every connected client gets every transition. When the presence subject has blocked at least one user, their `PRESENCE_UPDATE` is instead delivered to every currently-connected connection *except* those identified as a user they've blocked — still effectively broadcast-shaped for everyone else, just with that one exclusion. A user with no active blocks is entirely unaffected; this never applies in the other direction (a blocked user's own presence is still broadcast normally, including to the person who blocked them — see the Scope note above on why full bidirectional invisibility isn't built).

### Direct Messages

1:1 conversations only — no group DMs (see `docs/social/friends-dms-design.md`'s Scope section for that as a future extension). A DM conversation is addressed by the other participant's `user_id` on every message here — never a separate conversation id; the server resolves/creates the underlying conversation record internally. See `docs/social/friends-dms-design.md` §3 for the full design, including the permission model's re-check-on-every-send resolution and how this reuses (not reinvents) `docs/guilds/social-presence-design.md` §4's message-persistence architecture. All three require the connection to be identified first.

#### DM_SEND

Client to server:

```json
{ "type": "DM_SEND", "user_id": "u_2", "content": "hello" }
```

Validation: `content` must be non-empty and at most 500 characters (`MALFORMED_MESSAGE`, same limit as `CHAT_MESSAGE`); `user_id` must not be the caller's own id (`PROTOCOL_VIOLATION`); the caller and `user_id` must be friends or share at least one guild membership, and neither must have blocked the other (`DM_NOT_PERMITTED` otherwise — this single code covers both "no shared context" and "blocked," deliberately indistinguishable, same reasoning as [Blocking](#blocking)'s silent-failure section). This permission check runs fresh on **every** `DM_SEND`, not once when a conversation starts — a thread that was legitimately opened while the two shared a guild goes silently inert (new sends rejected) if that guild is later left and the two never became friends; existing history stays fully readable regardless.

On success, `Scope::TARGETED` to every connection identified as either participant:

```json
{ "type": "DM_MESSAGE", "message_id": 7, "timestamp": 1741104000, "user_id": "u_1", "content": "hello" }
```

Same shape `CHAT_MESSAGE`'s broadcast already uses — no separate `recipient_id`/`conversation_id` field. A receiving client determines which conversation this belongs to exactly the way it already determines "is this my own echo or someone else's" for `CHAT_MESSAGE`: compare `user_id` to its own identified id.

Persisted fire-and-forget with bounded retry, reusing `docs/guilds/social-presence-design.md` §4.5's Option B directly — nothing about DMs changes that tradeoff.

#### FETCH_HISTORY (DM scope)

[FETCH_HISTORY](#fetch_history) gains a `peer_id` field, mutually exclusive with `channel_id` (send at most one; both omitted still means the lobby):

```json
{ "type": "FETCH_HISTORY", "peer_id": "u_2", "before_seq": null, "limit": 50 }
```

No permission check beyond identification — reading history never depends on `DM_SEND`'s permission check being currently true, only on the caller actually being a participant in the resolved conversation, which is automatic (the conversation is always resolved from the caller's own id and `peer_id`, never a client-supplied conversation id). A `peer_id` with no conversation yet yields an empty page, not an error — same "empty is a valid, distinct state" reasoning `LIST_INVITES` already uses.

Response, extended with a `peer_id` echo alongside the existing `channel_id` (exactly one of the two is non-null):

```json
{ "type": "MESSAGE_HISTORY", "channel_id": null, "peer_id": "u_2", "messages": [], "has_more": false }
```

#### LIST_DM_CONVERSATIONS

Client to server: `{ "type": "LIST_DM_CONVERSATIONS" }`. Response (`Scope::DIRECT`):

```json
{ "type": "DM_CONVERSATION_LIST", "conversations": [ { "peer_id": "u_2", "last_message_at": "2026-08-01T12:00:00Z" } ] }
```

Deliberately minimal — no unread counts or message previews.

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
| `AUTH_REQUIRED` | `IDENTIFY` sent without a `session_token` |
| `INVALID_SESSION` | `session_token` is malformed, has an invalid signature, or was issued for a different audience |
| `SESSION_EXPIRED` | `session_token` has passed its expiry |
| `SESSION_REVOKED` | `session_token` references a session that has been revoked |
| `GUILD_NOT_FOUND` | referenced `guild_id` does not exist |
| `CHANNEL_NOT_FOUND` | referenced `channel_id` does not exist, or does not belong to the given guild |
| `NOT_GUILD_MEMBER` | action requires guild membership the caller doesn't have |
| `NOT_GUILD_OWNER` | action is owner-only and the caller isn't the owner |
| `NOT_GUILD_OFFICER` | action requires officer-or-above (`role_rank >= 1`) and the caller doesn't have it |
| `NOT_IN_CHANNEL` | `CHANNEL_MESSAGE` or `LEAVE_CHANNEL` sent with no active channel |
| `INVITE_NOT_FOUND` | referenced invite `code` does not exist, or does not belong to the given guild (`REVOKE_INVITE`) |
| `INVITE_EXPIRED` | invite's `expires_at` has passed |
| `INVITE_REVOKED` | invite has been revoked |
| `INVITE_MAX_USES_REACHED` | invite's `use_count` has reached its `max_uses` |
| `GUILD_REQUIRES_APPROVAL` | `JOIN_GUILD` attempted against an `application`-visibility guild — use `REQUEST_JOIN` instead |
| `JOIN_REQUEST_ALREADY_PENDING` | `REQUEST_JOIN` sent while a request for that guild is already pending |
| `JOIN_REQUEST_NOT_FOUND` | `APPROVE_JOIN_REQUEST`/`REJECT_JOIN_REQUEST` referenced a user with no pending request |
| `USER_NOT_FOUND` | referenced `user_id` does not exist |
| `FRIEND_REQUEST_NOT_FOUND` | `ACCEPT_FRIEND_REQUEST`/`REJECT_FRIEND_REQUEST`/`CANCEL_FRIEND_REQUEST` referenced no matching pending request |
| `FRIEND_NOT_FOUND` | `REMOVE_FRIEND` referenced a user the caller isn't currently friends with |
| `FRIEND_CODE_NOT_FOUND` | `ADD_FRIEND_BY_CODE`'s `code` does not belong to any account |
| `DM_NOT_PERMITTED` | `DM_SEND` sent to a user who isn't a friend and shares no guild with the caller, or where either party has blocked the other (deliberately indistinguishable — see [Blocking](#blocking)) |

## Behavior Notes

- The server is authoritative.
- The gateway is transport-only and does not validate protocol payloads.
- Valid `CHAT_MESSAGE` responses are broadcast to all connected clients.
- Non-chat responses are returned only to the originating client.
- `PRESENCE_UPDATE` is also broadcast to all connected clients, but unlike `CHAT_MESSAGE` it isn't triggered by any client-sent message — it's emitted by the server's own connection-lifecycle handling (a successful `IDENTIFY`, or a detected disconnect) on a 0↔1 connection-count transition (see [PRESENCE_UPDATE](#presence_update)).
- Guild/channel responses that need to reach more than one connection but not literally everyone (`MEMBER_LEFT`, `GUILD_DELETED`, `CHANNEL_CREATED`, `CHANNEL_DELETED`, `CHANNEL_MESSAGE`, `MEMBER_ROLE_UPDATED`, `GUILD_VISIBILITY_CHANGED`, `JOIN_REQUEST_RECEIVED`, and the approval-path copies of `GUILD_JOINED`/`JOIN_REQUEST_REJECTED`) use a third delivery mode, `TARGETED`: the handler computes the exact set of recipient connections (e.g. "current members of this guild," "connections with this channel active," or "every connection currently identified as this specific user_id") and the server delivers only to that set. This is distinct from `BROADCAST`, which always means every connected client. The Friends messages (`FRIEND_REQUEST_RECEIVED`, `FRIEND_ADDED`, and the copies of `FRIEND_REQUEST_REJECTED`/`FRIEND_REQUEST_CANCELED`/`FRIEND_REMOVED` delivered to the other party) all use the "every connection currently identified as this specific user_id" variant, not a guild-membership-based one, since friendship isn't guild-scoped.
- A single client action can trigger more than one outgoing message to different recipients with different payloads (`JOIN_VIA_INVITE`'s `application`-mode diversion, `REQUEST_JOIN`, `APPROVE_JOIN_REQUEST`, `REJECT_JOIN_REQUEST`, and every Friends action in the previous bullet) — the dispatcher returns a list of messages per incoming message, not just one, and each is delivered independently per its own `scope`.

## Security and Limits

Current implementation limitations:

- authorization is rank-based (`role_rank`, [Roles](#roles)) with exactly three reachable tiers today (crew/officer/owner) — no general, delegable permission system yet (see `docs/guilds/design.md`, "Future Permission Hook")
- guild privacy exists (`visibility`: `open`/`application`/`private` — see [Guild Visibility](#guild-visibility)) but only at the guild level, not per-channel or per-message; an `open` guild (the default) still has none
- invite codes are the only access-control boundary for `application`/`private` guilds — a leaked/forwarded code grants whatever that guild's mode allows (direct membership for `private`, a join request for `application`); there is no per-invite audience restriction
- a leaked/forwarded friend code (`ADD_FRIEND_BY_CODE`) lets anyone send its owner a friend request — unlike a guild invite this never grants anything unilaterally (friendship still requires the recipient to accept), so the exposure is a request, not membership
- presence (`PRESENCE_UPDATE`) leaks online/offline status across guild boundaries — every connected client learns it for every other identified user, regardless of shared guild membership. Consistent with, not a regression from, the existing baseline above (guild existence and membership-by-id are already visible to every identified client with no privacy model)
- `VOICE` channels are metadata-only: the type is modeled and validated, but there is no audio transport or voice presence
- no TLS
- no rate limiting
- no persistent identity, guilds, or channels — all in-memory, wiped on restart

Do not treat the current protocol as production-ready for untrusted environments.

## Related Documentation

- See [../../gateway/README.md](../../gateway/README.md) for gateway transport behavior.
- See [../../server/README.md](../../server/README.md) for current server implementation details.
- See [../../docs/guilds/design.md](../../docs/guilds/design.md) for the guild/channel feature's design rationale, data model, and deferred work (permissions, privacy).
