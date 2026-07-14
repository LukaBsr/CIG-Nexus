# Guilds & Channels — Feature Spec

> File is named `rooms-spec.md` per the original request; the feature itself
> settled on a Discord-shaped model (**Guild → Channel**), not flat "rooms."
> See [Terminology](#terminology) for why "Guild" was chosen over "Server."

**Status: implemented.** Server-side protocol landed in
[PR #11](https://github.com/LukaBsr/CIG-Nexus/pull/11); the web client landed
in [PR #12](https://github.com/LukaBsr/CIG-Nexus/pull/12). Both are merged
into `main`. The rest of this document is left in its original planning-time
future tense — it's a design record of what was decided and why, not a
changelog, so it isn't rewritten to describe the shipped state after the
fact.

## Terminology

| Term | Meaning |
|---|---|
| **Guild** | Top-level container, owned by its creator. Holds channels and a member list. Discord calls this a "server"; we don't, because `server/` and `Server.cpp` already mean the C++ TCP process in this codebase. Using "Guild" avoids that collision everywhere (code, protocol, docs). |
| **Channel** | Belongs to exactly one guild. Has a `type` of `TEXT` or `VOICE`. Only `TEXT` is functional this iteration (see [Decisions From Requirements Interview](#decisions-from-requirements-interview), item 3). |
| **Member** | A user who has joined a guild. Tracked per-connection (see [Session Changes](#sessionmanager-changes)). |
| **Owner** | The user who created a guild. Currently the only one who can create/delete channels or delete the guild. |

## Decisions From Requirements Interview

These were confirmed explicitly; nothing below is assumed:

1. **Channel creation**: explicit `CREATE_CHANNEL` action, **owner-only** for this iteration. A future permission system will let owners delegate this — see [Future Permission Hook](#future-permission-hook).
2. **Guild creation**: any identified client, dynamically, via `CREATE_GUILD`. Creator becomes owner and is auto-joined as a member (no separate `JOIN_GUILD` needed for your own guild).
3. **Voice channels**: metadata-only placeholder. The `type` field exists and is validated, but `JOIN_CHANNEL` rejects `VOICE` channels this iteration — no audio transport, no voice presence.
4. **Delivery model**: explicit per-channel `JOIN_CHANNEL` required. Guild membership alone does not deliver channel messages — only the channel a connection has explicitly joined does.
5. **Membership scope**: a client can belong to **multiple guilds** simultaneously, but only **one active channel per connection** at any time, across all guilds. Joining a new channel implicitly leaves the previous one (mirrors how `IDENTIFY` is a single per-connection identity today).
6. **Message type**: channel chat is a **new `CHANNEL_MESSAGE` type**, not an extension of `CHAT_MESSAGE`. `CHAT_MESSAGE`'s wire format and global-broadcast behavior are unchanged and untouched by this feature — it remains a fully separate, orthogonal capability.
7. **IDENTIFY**: no changes. Guild/channel membership is tracked as additional session state, established via separate actions after identification.
8. **Guild discovery**: `LIST_GUILDS` (lists all guilds that exist — no privacy/invite system yet) + `JOIN_GUILD` by id.
9. **Guild deletion**: owner-only, explicit `DELETE_GUILD`. Guilds are not auto-deleted when empty.
10. **Channel deletion**: owner-only, explicit `DELETE_CHANNEL`. Members who had it as their active channel are dropped to "no active channel" and notified.
11. **Persistence**: in-memory only, exactly like `SessionManager` today. Wiped on restart. No database, no file persistence.

## Decisions Made While Filling In Gaps

The interview covered the load-bearing product decisions; these are the natural protocol/implementation completions needed to make the above actually work end-to-end. Flagging them explicitly rather than silently baking them in:

- **Owner leaving their own guild**: disallowed. `LEAVE_GUILD` returns an error if the caller is the owner — they must `DELETE_GUILD` instead. (Ownership transfer is out of scope; see [Future Permission Hook](#future-permission-hook).)
- **`LIST_CHANNELS`**: requires guild membership. You see a guild's channel list only after joining it; `LIST_GUILDS` gives you enough (id, name, owner) to decide whether to join.
- **Guild-wide notifications**: `CHANNEL_CREATED`, `CHANNEL_DELETED`, `GUILD_DELETED`, and `MEMBER_LEFT` are broadcast to every current guild member (not just the actor), so a client's guild/channel list state can stay in sync without polling `LIST_CHANNELS`/`LIST_GUILDS` after every action.
- **`CHANNEL_MESSAGE` doesn't carry `channel_id` from the client**: the server derives the target channel from the sending session's active channel. This matches "server is authoritative" and avoids a client being able to inject messages into a channel it hasn't joined by just naming its id.
- **Self-delivery on leave/delete**: when a client leaves a guild or a guild is deleted, the fd snapshot used for the notification broadcast is captured *before* membership is mutated, so the acting client still receives its own confirmation — this mirrors how `CHAT_MESSAGE`'s `Scope::BROADCAST` already includes the sender today.

## Data Model Changes

### New: `Guild` and `Channel` structs

Mirrors the existing `session/Session.hpp` plain-struct style.

```cpp
// server/include/guild/Guild.hpp
namespace guild {
struct Guild {
    std::string id;         // "g_1", "g_2", ...
    std::string name;
    std::string owner_id;   // user_id of creator
    uint64_t created_at;
};
}
```

```cpp
// server/include/guild/Channel.hpp
namespace guild {
enum class ChannelType { TEXT, VOICE };

struct Channel {
    std::string id;         // "c_1", "c_2", ...
    std::string guild_id;
    std::string name;
    ChannelType type;
    uint64_t created_at;
};
}
```

### New: `GuildManager`

Mirrors `SessionManager`'s role: owns the catalog, not per-connection state.

```cpp
// server/include/guild/GuildManager.hpp
namespace guild {
class GuildManager {
  public:
    Guild& createGuild(const std::string& name, const std::string& owner_id);
    bool deleteGuild(const std::string& guild_id); // cascades to its channels

    Channel& createChannel(const std::string& guild_id, const std::string& name, ChannelType type);
    bool deleteChannel(const std::string& channel_id);

    bool hasGuild(const std::string& guild_id) const;
    Guild* getGuild(const std::string& guild_id);
    std::vector<Guild> listGuilds() const;

    bool hasChannel(const std::string& channel_id) const;
    Channel* getChannel(const std::string& channel_id);
    std::vector<Channel> listChannels(const std::string& guild_id) const;

    bool isOwner(const std::string& guild_id, const std::string& user_id) const;
    // Authorization choke point — see Future Permission Hook.
    bool canCreateChannel(const std::string& guild_id, const std::string& user_id) const;
    bool canDeleteChannel(const std::string& guild_id, const std::string& user_id) const;

  private:
    std::unordered_map<std::string, Guild> guilds_;
    std::unordered_map<std::string, Channel> channels_;
    std::uint64_t next_guild_id_ = 1;
    std::uint64_t next_channel_id_ = 1;
};
}
```

`GuildManager` does **not** track which fd/session belongs to which guild —
that's per-connection state and belongs on `Session`, consistent with how
`username` already lives there instead of in a separate manager.

### `SessionManager` changes

`Session` (`server/include/session/Session.hpp`) gains two fields:

```cpp
struct Session {
    std::string session_id;
    std::string user_id;
    std::string username;
    uint64_t connected_at;
    int socket_fd;
    std::vector<std::string> guild_ids;     // NEW: guilds this connection is a member of
    std::string active_channel_id;          // NEW: "" means no active channel
};
```

`SessionManager` gains membership mutation + query helpers:

```cpp
void addGuildMembership(int fd, const std::string& guild_id);
void removeGuildMembership(int fd, const std::string& guild_id);
bool isMemberOfGuild(int fd, const std::string& guild_id) const;

void setActiveChannel(int fd, const std::string& channel_id);
void clearActiveChannel(int fd);

// Used to compute Scope::TARGETED recipient lists (see below).
std::vector<int> getFdsInGuild(const std::string& guild_id) const;
std::vector<int> getFdsWithActiveChannel(const std::string& channel_id) const;

// Cleanup for cascading deletes.
void purgeGuildMembership(const std::string& guild_id, const std::vector<std::string>& channel_ids);
void clearActiveChannelEverywhere(const std::string& channel_id);
```

The `getFds*` helpers do an O(n) scan over `sessions_`, same cost class as
today's `Server::broadcast()`, which already loops every connection. Fine at
this scale; revisit only if connection counts stop being trivial.

### `Message` / `Scope` changes

Two new scope values were considered:

- **Rejected**: add `Scope::GUILD` and `Scope::CHANNEL` as distinct enum
  values, with `Server` holding a `GuildManager*` to resolve recipients
  itself. Rejected because it pulls guild/channel domain knowledge into
  `Server.cpp`, which today only knows about connection I/O and dispatch —
  every existing manager (`SessionManager`) is owned by handlers, not by
  `Server` directly.
- **Chosen**: add a single `Scope::TARGETED`, and let the *handler* (which
  already holds `SessionManager*`/`GuildManager*` via the existing
  dependency-injection pattern) compute the recipient fd list and attach it
  to the `Message`. `Server` stays domain-agnostic — it just iterates
  whatever fd list it's given.

```cpp
// server/include/protocol/Message.hpp
enum class Scope { DIRECT, BROADCAST, TARGETED };

struct Message {
    std::string type;
    nlohmann::json payload;
    Scope scope = Scope::DIRECT;
    std::vector<int> target_fds; // populated by the handler when scope == TARGETED
};
```

`Server::start()`'s scope switch gains one case:

```cpp
case protocol::Scope::TARGETED:
    for (int target_fd : response.target_fds) {
        sendMessage(target_fd, response);
    }
    break;
```

This one mechanism serves both guild-wide notifications (fd list from
`getFdsInGuild`) and channel-message delivery (fd list from
`getFdsWithActiveChannel`) — no need for the enum to know which "kind" of
targeting it is.

### `MessageParser`

No changes. It only extracts `type` and stores the full payload; per-type
field validation already lives in handlers, and that pattern continues here.

## Protocol Changes

All new message types require the connection to be identified first
(`ERROR` / `NOT_IDENTIFIED` otherwise), same as `CHAT_MESSAGE` today.

### New error codes

| Code | Meaning |
|---|---|
| `GUILD_NOT_FOUND` | referenced `guild_id` doesn't exist |
| `CHANNEL_NOT_FOUND` | referenced `channel_id` doesn't exist |
| `NOT_GUILD_MEMBER` | action requires guild membership the caller doesn't have |
| `NOT_GUILD_OWNER` | action is owner-only and the caller isn't the owner |
| `NOT_IN_CHANNEL` | `CHANNEL_MESSAGE` sent with no active channel |

Existing codes (`PROTOCOL_VIOLATION`, `MALFORMED_MESSAGE`, `NOT_IDENTIFIED`,
`INTERNAL_ERROR`) are reused wherever the failure shape already matches —
e.g. "already a member of this guild" is `PROTOCOL_VIOLATION`, matching how
"already identified" is handled today.

### `CREATE_GUILD`

Client → server:

```json
{ "type": "CREATE_GUILD", "name": "My Guild" }
```

Validation: `name` required, string, non-empty, ≤ 64 characters.

Response (`Scope::DIRECT`, to creator only):

```json
{ "type": "GUILD_CREATED", "guild_id": "g_1", "name": "My Guild", "owner_id": "u_1" }
```

Side effect: creator is auto-added to `guild_ids`.

### `LIST_GUILDS`

Client → server:

```json
{ "type": "LIST_GUILDS" }
```

Response (`Scope::DIRECT`):

```json
{
  "type": "GUILD_LIST",
  "guilds": [
    { "guild_id": "g_1", "name": "My Guild", "owner_id": "u_1" }
  ]
}
```

Lists every guild that exists — there's no invite/privacy model yet.

### `JOIN_GUILD`

Client → server:

```json
{ "type": "JOIN_GUILD", "guild_id": "g_1" }
```

Validation: guild exists (`GUILD_NOT_FOUND`), caller not already a member
(`PROTOCOL_VIOLATION`).

Response (`Scope::DIRECT`, to joiner):

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

### `LEAVE_GUILD`

Client → server:

```json
{ "type": "LEAVE_GUILD", "guild_id": "g_1" }
```

Validation: caller is a member (`NOT_GUILD_MEMBER`); caller is **not** the
owner (`PROTOCOL_VIOLATION` — owners must `DELETE_GUILD`).

Response (`Scope::TARGETED`, to all members *including the leaver*, fd
snapshot captured before membership is removed):

```json
{ "type": "MEMBER_LEFT", "guild_id": "g_1", "user_id": "u_2" }
```

If the leaver's active channel belonged to this guild, it's cleared
(no separate message — the client can infer this from `MEMBER_LEFT`
referencing its own `user_id`, or simply from no longer receiving
`CHANNEL_MESSAGE` for that channel).

### `DELETE_GUILD`

Client → server:

```json
{ "type": "DELETE_GUILD", "guild_id": "g_1" }
```

Validation: caller is the owner (`NOT_GUILD_OWNER`).

Response (`Scope::TARGETED`, to all members, fd snapshot captured before
guild/membership state is torn down):

```json
{ "type": "GUILD_DELETED", "guild_id": "g_1" }
```

Side effects: all channels in the guild are deleted; every member's
`guild_ids` drops the guild; any member whose active channel was in this
guild has it cleared.

### `LIST_CHANNELS`

Client → server:

```json
{ "type": "LIST_CHANNELS", "guild_id": "g_1" }
```

Validation: caller is a guild member (`NOT_GUILD_MEMBER`).

Response (`Scope::DIRECT`):

```json
{
  "type": "CHANNEL_LIST",
  "guild_id": "g_1",
  "channels": [
    { "channel_id": "c_1", "name": "general", "channel_type": "TEXT" }
  ]
}
```

### `CREATE_CHANNEL`

Client → server:

```json
{ "type": "CREATE_CHANNEL", "guild_id": "g_1", "name": "general", "channel_type": "TEXT" }
```

Validation: `name` required/non-empty/≤ 64 chars; `channel_type` must be
exactly `"TEXT"` or `"VOICE"` (`MALFORMED_MESSAGE` otherwise); caller passes
`GuildManager::canCreateChannel` — today that means owner-only
(`NOT_GUILD_OWNER` otherwise).

Response (`Scope::TARGETED`, to **all current guild members** including the
owner — this is how non-owner members find out a channel was added):

```json
{
  "type": "CHANNEL_CREATED",
  "guild_id": "g_1",
  "channel_id": "c_2",
  "name": "general",
  "channel_type": "TEXT"
}
```

### `DELETE_CHANNEL`

Client → server:

```json
{ "type": "DELETE_CHANNEL", "guild_id": "g_1", "channel_id": "c_2" }
```

Validation: channel exists in that guild (`CHANNEL_NOT_FOUND`); caller
passes `GuildManager::canDeleteChannel` (owner-only today, `NOT_GUILD_OWNER`
otherwise).

Response (`Scope::TARGETED`, to all guild members):

```json
{ "type": "CHANNEL_DELETED", "guild_id": "g_1", "channel_id": "c_2" }
```

Side effect: any session with `active_channel_id == c_2` has it cleared.

### `JOIN_CHANNEL`

Client → server:

```json
{ "type": "JOIN_CHANNEL", "channel_id": "c_2" }
```

Validation: channel exists (`CHANNEL_NOT_FOUND`); caller is a member of the
channel's guild (`NOT_GUILD_MEMBER`); channel is `TEXT`, not `VOICE`
(`PROTOCOL_VIOLATION` — voice channels aren't joinable this iteration).

Side effect: implicitly clears any previously active channel (single active
channel per connection), then sets the new one.

Response (`Scope::DIRECT`, to joiner only — no guild-wide presence
broadcast per the "explicit join, no fan-out" delivery decision):

```json
{ "type": "CHANNEL_JOINED", "guild_id": "g_1", "channel_id": "c_2" }
```

### `LEAVE_CHANNEL`

Client → server (no params — operates on the connection's current active
channel):

```json
{ "type": "LEAVE_CHANNEL" }
```

Validation: caller has an active channel (`NOT_IN_CHANNEL` otherwise).

Response (`Scope::DIRECT`):

```json
{ "type": "CHANNEL_LEFT", "channel_id": "c_2" }
```

### `CHANNEL_MESSAGE`

Client → server:

```json
{ "type": "CHANNEL_MESSAGE", "content": "hello" }
```

No `channel_id` in the request — the server targets the sender's current
active channel. Validation: caller has an active channel (`NOT_IN_CHANNEL`
otherwise); `content` required/non-empty/≤ 500 chars (same rule as
`CHAT_MESSAGE`).

Response (`Scope::TARGETED`, fd list from
`getFdsWithActiveChannel(channel_id)` — i.e. every connection that currently
has this channel active, sender included):

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

`message_id` is generated by a `std::atomic<int>` counter local to the
channel-message handler, same pattern as `ChatHandler::message_counter_`
(see the atomic-counter fix already landed there). It's a separate counter
space from `CHAT_MESSAGE`'s — uniqueness only needs to hold within a
message kind, matching existing convention.

## Server-Side File Layout

New files, mirroring existing `session/` and `protocol/handlers/` layout:

```
server/
├── include/guild/
│   ├── Guild.hpp
│   ├── Channel.hpp
│   └── GuildManager.hpp
├── src/guild/
│   └── GuildManager.cpp
├── include/protocol/handlers/
│   ├── GuildHandler.hpp       # CREATE_GUILD, LIST_GUILDS, JOIN_GUILD, LEAVE_GUILD, DELETE_GUILD
│   └── ChannelHandler.hpp     # LIST_CHANNELS, CREATE_CHANNEL, DELETE_CHANNEL, JOIN_CHANNEL, LEAVE_CHANNEL, CHANNEL_MESSAGE
├── src/protocol/handlers/
│   ├── GuildHandler.cpp
│   └── ChannelHandler.cpp
└── tests/
    ├── guild/GuildManager.test.cpp
    └── protocol/
        ├── GuildHandler.test.cpp
        └── ChannelHandler.test.cpp
```

`GuildHandler` and `ChannelHandler` each take `SessionManager*` and
`GuildManager*` via setter injection, matching how `ChatHandler` and
`IdentifyHandler` already take `SessionManager*`. Each public method handles
exactly one message type; `Server`'s constructor registers each type
individually with the dispatcher, same as today — e.g.:

```cpp
dispatcher_.registerHandler("CREATE_GUILD", [this](const protocol::Message& msg, int fd) {
    return guild_handler_.handleCreateGuild(msg, fd);
});
```

No changes needed to `MessageDispatcher` itself — it's already a generic
type-string → handler map.

## Gateway Changes

**None.** The gateway forwards any JSON message type unchanged; it doesn't
inspect `type` beyond framing. This is a good sanity check that the design
respects the "gateway is transport-only" constraint — if implementing this
turns out to require gateway changes, that's a signal the design leaked
protocol logic into the transport layer.

## Web Client Changes

### `lib/gateway.ts`

Add one send-helper per new client→server message type, following the
existing `sendChatMessage` shape:

```ts
export function createGuild(name: string): void;
export function listGuilds(): void;
export function joinGuild(guildId: string): void;
export function leaveGuild(guildId: string): void;
export function deleteGuild(guildId: string): void;
export function listChannels(guildId: string): void;
export function createChannel(guildId: string, name: string, channelType: "TEXT" | "VOICE"): void;
export function deleteChannel(guildId: string, channelId: string): void;
export function joinChannel(channelId: string): void;
export function leaveChannel(): void;
export function sendChannelMessage(content: string): void;
```

No changes to `connect()`'s handshake logic (`HELLO`/`IDENTIFY` flow is
untouched per decision #7).

### `app/page.tsx`

This is a bigger UI change than items 3/4 from the last cleanup batch — the
flat `messages: any[]` list this page currently maintains isn't the right
shape for a guild/channel UI. At minimum, state needs to grow to:

```ts
const [guilds, setGuilds] = useState<Guild[]>([]);
const [activeGuildId, setActiveGuildId] = useState<string | null>(null);
const [channels, setChannels] = useState<Channel[]>([]);
const [activeChannelId, setActiveChannelId] = useState<string | null>(null);
const [channelMessages, setChannelMessages] = useState<ChannelMessage[]>([]);
```

The `onMessage` callback needs to switch on `message.type` and route into
the right state updates (`GUILD_LIST` → `setGuilds`, `CHANNEL_LIST` →
`setChannels`, `CHANNEL_MESSAGE` → append to `channelMessages`,
`CHANNEL_DELETED`/`GUILD_DELETED` → clear `activeChannelId`/`activeGuildId`
if it matches, etc.) instead of blindly appending everything to one array
like today.

This spec intentionally stops at describing the required state shape and
routing, not final component/layout design — that's implementation work,
not planning. It may be worth landing the server-side protocol first and
doing the UI as a separate follow-up pass, given the size of the change
relative to the existing page.

## Future Permission Hook

The permission system for delegating channel-creation rights is explicitly
**not** built this iteration, but the design leaves one seam for it:
`GuildManager::canCreateChannel(guild_id, user_id)` and
`canDeleteChannel(guild_id, user_id)`. Today these are implemented as
`return isOwner(guild_id, user_id);` — but handlers call the named
predicate, never `isOwner` directly for authorization decisions. When the
permission system arrives, only `GuildManager`'s two `can*` methods need to
change (e.g. to check a roles/grants table); `ChannelHandler` and the
protocol itself don't need to change at all.

## Deferred: Guild Privacy

`LIST_GUILDS` returning every guild that exists, with no invite links and no
concept of a private guild, is a **deliberate MVP choice**, not an
oversight. There is currently no mechanism to create a guild that isn't
publicly listable, and no invite-token flow for joining one without it
appearing in `LIST_GUILDS`.

This is acceptable for this iteration because there's also no way to
restrict who can `JOIN_GUILD` — every identified client can join any guild
by id already, so hiding a guild from the list wouldn't meaningfully
protect it. A real privacy model (private guilds, invite links/tokens,
join-request approval) is future work, and should land alongside — or after
— the permission system above, since "who can see/join this guild" and
"who can do what once inside it" are closely related authorization
concerns. No API shape is reserved for this yet; it's an open design
question for whoever picks it up.

## Backward Compatibility

`CHAT_MESSAGE` is untouched — same request shape, same response shape, same
`Scope::BROADCAST` global-delivery behavior. It is not reinterpreted as "the
default guild" or deprecated; it remains a fully independent feature
alongside guilds/channels, per decision #6.

## Suggested Implementation Order

1. `Message`/`Scope::TARGETED` + the one new `Server::start()` case — small,
   mechanical, testable in isolation via a `Server` integration test that
   sends to a subset of connected fds.
2. `Session` fields + `SessionManager` membership/query helpers.
3. `Guild`/`Channel` structs + `GuildManager` (pure catalog logic, no
   network — easiest to unit test first).
4. `GuildHandler` (guild lifecycle) wired into `Server`'s dispatcher.
5. `ChannelHandler` (channel lifecycle + `CHANNEL_MESSAGE`) wired in.
6. Update `shared/protocol/README.md` with the message types above (see
   next section — draft content is ready to paste in).
7. Web client: `lib/gateway.ts` helpers, then `page.tsx` state/routing.

## Draft `shared/protocol/README.md` Additions

Not applied to `shared/protocol/README.md` in this session — this is
planning only. The implementation session should fold the following into
that file, in the same style as the existing `HELLO`/`IDENTIFY`/`CHAT_MESSAGE`
sections:

- A new "Guilds and Channels" section after `CHAT_MESSAGE`, containing the
  message definitions from [Protocol Changes](#protocol-changes) above,
  verbatim.
- The five new error codes added to the existing error code table.
- An updated "Connection Lifecycle" list item after step 7: guild/channel
  actions are available to identified clients independent of `CHAT_MESSAGE`.
- A note under "Behavior Notes" documenting `Scope::TARGETED` alongside the
  existing `DIRECT`/`BROADCAST` explanation.

`docs/architecture.md` and `docs/gateway-api.md` should also get short
mentions of the guild/channel flow once implemented — not drafted here in
full since their existing content is closer to prose summary than spec, and
is easier to update accurately after the real implementation exists.
