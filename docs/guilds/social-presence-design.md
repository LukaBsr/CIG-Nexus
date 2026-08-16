# Guild Invites, Roster, Presence, and Message Persistence — Design Document

**Status: implemented.** All four features have shipped: guild invites,
visibility, and join requests (§1 — `bb88c82`), rank-based roster/roles (§2 —
`f125db0`, with the `Session.guild_ids` hydration fix from §1.10 landing
separately as `d76d992`), presence (§3 — `b163379`, built on the dispatcher's
`std::vector<Message>` widening from §1.9's arbitration, `2f99def`), and
message persistence (§4 — `1453be6`). This document is kept as a design
record — the reasoning and arbitration below reflect decisions made during
planning, not a changelog of the shipped code. The one genuine exception is
§3.3: only the cheap first-pass mitigation (`SO_KEEPALIVE`) shipped
(`server/src/TcpListener.cpp`); the full application-level heartbeat
discussed there was never built, per that section's own "ship without
solving this fully" recommendation — still accurate as written.

## Scope

Four interrelated features for the next release cycle, covering the gap
`docs/guilds/design.md` explicitly left open ("Deferred: Guild Privacy") and
the one `docs/auth/discord-design.md` explicitly deferred ("Message history
persistence", §10):

1. **Guild invites** — a join mechanism for guilds (today only channels have
   one, `JOIN_CHANNEL`; guilds only have `JOIN_GUILD` by id, with no
   discovery-adjacent affordance beyond `LIST_GUILDS` listing everything).
   Now also covers **guild visibility** (open/application/private) and the
   join-request flow it requires — see the revised §1.1 and new §§1.7–1.10.
2. **Guild member roster + roles** — expose `guild_memberships` to clients,
   now with **three tiers** (captain/officer/crew) instead of the originally
   proposed binary owner/member — see the revised §2.
3. **Presence** — online/offline state, lobby-wide and per-guild.
4. **Message persistence** — move `CHAT_MESSAGE`/`CHANNEL_MESSAGE` off pure
   broadcast-and-forget onto Postgres, with history retrieval.

All four build directly on the write-through cache pattern
`docs/auth/discord-design.md` §8.1 established (`GuildManager` reads from an
in-memory cache; Postgres is the source of truth; the C++ server never opens
a database connection itself — it always goes through Next.js's internal
API). This document assumes that pattern as given and extends it; it does
not re-litigate it.

> **Revision note**: this document was revised after initial review to
> incorporate three approved-at-a-concept-level requirements: a custom
> invite-creation UI (no protocol change, §6 only), three-tier roles
> (§2), and guild visibility/privacy (§§1.1, 1.7–1.10). See
> [What Changed From the Previous Version](#what-changed-from-the-previous-version)
> at the end for an itemized delta. Presence (§3) and message persistence
> (§4) are unchanged from the previously-reviewed version.

**Not addressed here, explicitly out of scope:**

- Voice channel presence/transport — voice channels remain metadata-only
  (`docs/guilds/design.md`, decision #3), unaffected by this document.
- A fully general, configurable permission system (custom roles, granular
  per-action grants beyond the three fixed tiers below). §2 widens
  `guild_memberships.role` from two values to three, reusing the Future
  Permission Hook seam `docs/guilds/design.md` left open — but three fixed,
  hardcoded tiers is still not the same as a real permission system, and
  building one remains deferred to whoever picks that up next.
- Desktop client (`desktop/`) — not the active development path per
  `CLAUDE.md`.

Guild privacy is **no longer** out of scope — see the revised §1.1.

---

## 1. Guild Invites

### 1.1 What this feature is (and isn't)

**Revised.** The previous version of this document scoped invites narrowly
— explicitly *not* introducing guild privacy, reasoning that a
half-built privacy model (invite links, but `JOIN_GUILD`-by-id still works
for everyone) would be worse than no privacy model at all. That
reasoning still holds **as an argument against half-measures** — it's why
this revision builds the other half (visibility modes, §§1.7–1.10) in the
same pass rather than leaving `JOIN_GUILD`-by-id as a silent bypass.

Today, `LIST_GUILDS` returns every guild that exists, and any identified
client can `JOIN_GUILD` any of them by id — there is no privacy model at
all (`docs/guilds/design.md`, "Deferred: Guild Privacy"). For a guild left
at the default **`open`** visibility (§1.8), that stays true, and an
invite link for an open guild is still **not** a security boundary — it's
a **discovery/convenience** mechanism, a shareable code so a user doesn't
need to already know a `guild_id` or browse `LIST_GUILDS` to join a guild a
friend pointed them at.

**What's new in this revision**: guild visibility (§§1.7–1.10) closes
`docs/guilds/design.md`'s "Deferred: Guild Privacy" gap for guilds that
opt into `application` or `private` mode. For those, invites (and, for
`application`, the new join-request flow) *do* become the actual
access-control boundary, not just convenience — which is exactly the
half-measure the previous version of this section was refusing to build in
isolation. `docs/guilds/design.md` also predicted this would land "alongside
— or after — the permission system," since privacy and permissions are
closely related; §2's three-tier roles landing in the same revision as
visibility is a direct consequence of that prediction, not a coincidence.

### 1.2 Data model

```sql
CREATE TABLE guild_invites (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_id    UUID NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
    code        TEXT NOT NULL UNIQUE,   -- URL-safe random token, e.g. 10 random bytes, base62/base64url encoded
    created_by  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    max_uses    INTEGER,                -- NULL = unlimited
    use_count   INTEGER NOT NULL DEFAULT 0 CHECK (use_count >= 0),
    expires_at  TIMESTAMPTZ,            -- NULL = never expires
    revoked_at  TIMESTAMPTZ,            -- NULL = active
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX idx_guild_invites_guild_id ON guild_invites(guild_id);
-- code's inline UNIQUE already indexes it; called out separately here since
-- it's the lookup path for every JOIN_VIA_INVITE.
```

No per-redemption audit table (`guild_invite_redemptions` or similar) this
iteration — `use_count` is an aggregate counter, not a log of who redeemed
when. That's a reasonable future addition (useful for abuse investigation)
but nothing in the request asks for it, and it's a separable, additive
change later if needed.

**Code generation.** Cryptographically random, not derived from the guild
id or any guessable sequence — reuse the same "opaque random value" pattern
`docs/auth/discord-design.md` §6 already established for the refresh
cookie, at a length that makes brute-force enumeration impractical (10
random bytes ≈ 80 bits of entropy is comfortably enough; the auth doc's own
security checklist principle of "rate limiting on the endpoint that
validates it" — see §5 below — is the real defense-in-depth here, not
length alone).

### 1.3 Lifecycle

- **Creation**: `max_uses` and `expires_at` are both optional at creation
  time (see ARBITRATION below for the default when omitted).
- **Redemption**: validated in this order — invite exists (`INVITE_NOT_FOUND`
  otherwise), not revoked (`INVITE_REVOKED`), not expired (`INVITE_EXPIRED`),
  under `max_uses` if set (`INVITE_MAX_USES_REACHED`), caller not already a
  member (reuse the existing `PROTOCOL_VIOLATION` — matches how `JOIN_GUILD`
  already treats "already a member," per `docs/guilds/design.md`'s
  convention of reusing existing error shapes where the failure matches).
  On success: `use_count` increments and the membership is created in the
  **same Postgres transaction**, guarded by `use_count < max_uses OR
  max_uses IS NULL` in the `UPDATE`'s `WHERE` clause — this is what makes
  concurrent redemptions against a `max_uses`-limited invite race-safe
  (two simultaneous redemptions of the last remaining use can't both
  succeed; the second one's conditional `UPDATE` affects zero rows and the
  internal API returns the max-uses error instead).
- **Expiration**: passive — checked at redemption time (`expires_at <
  now()`), not swept/deleted proactively. An expired invite is just inert;
  cleanup, if ever needed, is a housekeeping job, not part of this design.
- **Revocation**: sets `revoked_at`. Same effect as expiration for
  redemption purposes, kept as a separate field (not "just set `expires_at`
  to now") so `LIST_INVITES` can distinguish "the creator turned this off"
  from "it ran out on its own" if that distinction is ever surfaced in a UI.

### 1.4 Protocol messages

All require the connection to be identified first, same convention as
every existing guild/channel action.

**`CREATE_INVITE`** (client → server):

```json
{ "type": "CREATE_INVITE", "guild_id": "g_1", "max_uses": null, "expires_in_seconds": null }
```

Validation: guild exists (`GUILD_NOT_FOUND`); caller passes
`GuildManager::canCreateInvite(guild_id, user_id)` — see §5 on why this is
a named predicate, not an inline `isOwner` check. `max_uses` if present
must be a positive integer; `expires_in_seconds` if present must be a
positive integer (converted to an absolute `expires_at` server-side, same
reasoning as JWTs already use absolute `exp`, not client-supplied
durations, elsewhere in this codebase).

Response (`Scope::DIRECT`, to creator):

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

No `invite_url` field — building a shareable link (`https://.../invite/<code>`)
is a web-client concern, not a protocol one; the server has no opinion on
the app's public URL.

**`JOIN_VIA_INVITE`** (client → server):

```json
{ "type": "JOIN_VIA_INVITE", "code": "aB3kD9qP2x" }
```

On success, response is `GUILD_JOINED` — the **exact same shape**
`JOIN_GUILD` already returns (`docs/guilds/design.md`), reused rather than
inventing a parallel response type, since "you're now a member of this
guild, here's its channels" is identical information regardless of how you
got there.

**`LIST_INVITES`** (client → server):

```json
{ "type": "LIST_INVITES", "guild_id": "g_1" }
```

Validation: caller passes `canCreateInvite` (same predicate as creation —
if you can create invites, you can see the ones that exist; a member who
can't create invites has no need to enumerate them). Response
(`Scope::DIRECT`):

```json
{
  "type": "INVITE_LIST",
  "guild_id": "g_1",
  "invites": [
    { "code": "aB3kD9qP2x", "max_uses": null, "use_count": 3, "expires_at": null, "revoked_at": null, "created_at": "..." }
  ]
}
```

**`REVOKE_INVITE`** (client → server):

```json
{ "type": "REVOKE_INVITE", "guild_id": "g_1", "code": "aB3kD9qP2x" }
```

Validation: invite exists and belongs to `guild_id` (`INVITE_NOT_FOUND`
otherwise); caller passes `canCreateInvite` (same authorization as
creation/listing). Response (`Scope::DIRECT`): `{ "type": "INVITE_REVOKED",
"guild_id": "g_1", "code": "aB3kD9qP2x" }`.

New error codes: `INVITE_NOT_FOUND`, `INVITE_EXPIRED`, `INVITE_REVOKED`,
`INVITE_MAX_USES_REACHED`.

### 1.5 Internal API additions

- `POST /internal/guild-invites` — create. Body: `{ guildId, createdBy,
  maxUses, expiresAt }`.
- `GET /internal/guild-invites?guildId=` — list.
- `POST /internal/guild-invites/:code/redeem` — **one call**, not two. Body:
  `{ userId }`. Does the validate-and-increment-and-create-membership
  transaction described in §1.3 atomically and returns the guild (for
  `GUILD_JOINED`'s payload) or a typed error the C++ handler maps to the
  error codes above. A single round trip here, rather than "check invite"
  then "create membership" as two separate calls, closes the race window a
  two-call version would have between validation and the membership write.
- `DELETE /internal/guild-invites/:code` — revoke.

`GuildManager` does **not** cache invites — see §4.4 for why history/roster-
adjacent, infrequently-read data doesn't need to go through the write-through
cache the way the guild/channel catalog does; the same reasoning applies
here. `CREATE_INVITE`/`REVOKE_INVITE`/`LIST_INVITES` each make a live
internal API call.

### 1.6 `ARBITRATION`: single-use vs. reusable by default

Two reasonable defaults when `max_uses`/`expires_in_seconds` are omitted at
creation:

- **Reusable, no expiry** (matches Discord's own default invite behavior) —
  simplest for the common "drop a permanent link in a bio/README" use case.
- **Single-use, short-lived** (e.g., 24h) — safer default that discourages
  invite-link sprawl, requires explicit opt-in for a "permanent" link.

**Recommendation: reusable, no expiry, by default.** Given §1.1's framing —
invites aren't a privacy/security boundary, since the guild is joinable by
id regardless — a restrictive default here would add friction without a
corresponding security benefit. Both `max_uses` and `expires_in_seconds`
remain available for a creator who explicitly wants a tighter invite (e.g.,
a one-time invite for a specific person). This recommendation is
conditional on §1.1's scope holding — if guild privacy is ever built on top
of this and invites become an actual access-control boundary, this default
should be revisited alongside that work.

**Revision note**: §1.1's condition is no longer hypothetical as of this
revision — see §§1.7–1.10. The recommendation above stands for `open`
guilds; for `application`/`private` guilds it's revisited directly in
§1.8.

### 1.7 Guild visibility — data model and wire additions

```sql
CREATE TYPE guild_visibility AS ENUM ('open', 'application', 'private');
ALTER TABLE guilds ADD COLUMN visibility guild_visibility NOT NULL DEFAULT 'open';
```

`DEFAULT 'open'` means every existing guild keeps today's exact behavior
with no backfill decision needed — the column addition alone is the whole
migration.

`CREATE_GUILD` (`docs/guilds/design.md`) gains an optional `visibility`
field, defaulting to `'open'` if omitted:

```json
{ "type": "CREATE_GUILD", "name": "My Guild", "visibility": "open" }
```

Every guild-shaped wire object gains a `visibility` field alongside the
existing `guild_id`/`name`/`owner_id`: `GUILD_CREATED`, each entry in
`GUILD_LIST`, and `GUILD_JOINED`.

### 1.8 Visibility modes

| Mode | Listed in `LIST_GUILDS`? | `JOIN_GUILD`-by-id | Invite link (`JOIN_VIA_INVITE`) |
|---|---|---|---|
| `open` (default) | Yes, to everyone | Works directly (today's behavior) | Creates membership directly |
| `application` | Yes, to everyone | Rejected — `GUILD_REQUIRES_APPROVAL`, telling the client to use `REQUEST_JOIN` (§1.9) instead | Creates a **join request**, not direct membership — see the arbitration below |
| `private` | **No** — filtered out of `LIST_GUILDS` for non-members (members still see their own guild; see the required fix in §1.9) | Rejected — see `ARBITRATION` below | Creates membership directly (this is the mode's only door) |

**`ARBITRATION`: what does `JOIN_GUILD`-by-id return for a `private` guild
the caller isn't a member of?** Two options:

- **`GUILD_NOT_FOUND`** — indistinguishable from a genuinely nonexistent
  `guild_id`. Leaks nothing: someone probing random/guessed ids learns
  nothing about which ones exist-but-are-private versus don't exist at all.
- **A distinct code** (e.g. `GUILD_PRIVATE`) — more informative for a
  legitimate client that hit a stale/shared id by accident, at the cost of
  confirming to anyone probing that a given id exists and is private.

**Recommendation: `GUILD_NOT_FOUND`.** Consistent with this codebase's
existing precedent of not giving probing feedback (`docs/auth/discord-
design.md` §4 step 8's generic OAuth failure responses, cited in this
document's own §1.3 for invite redemption's "already a member" reuse).
The same `GUILD_NOT_FOUND` choice also applies to `REQUEST_JOIN` attempted
against a `private` guild (§1.9) and to `LIST_MEMBERS`/`LIST_CHANNELS` if a
non-member somehow supplies a private guild's id directly — private means
private, not "private except to whichever error code you trigger."

**`ARBITRATION`: does an invite bypass `application`'s approval
requirement, or fall into the same request queue?** Two readings:

- **Invite bypasses approval** — the officer who shared the code already
  vetted the recipient by choosing to share it with them; requiring a
  *second* approval step is redundant friction.
- **Invite still requires approval** — `application` mode's guarantee ("an
  officer-or-above explicitly approved every member") should hold
  regardless of entry path; an invite link, once shared, can spread beyond
  the officer's control (forwarded, posted publicly), so "possession of the
  code" isn't the same as "the officer approved this specific person."

**Recommendation: invite still requires approval** (the table above
reflects this — `JOIN_VIA_INVITE` against an `application` guild creates a
join request, consuming the invite's `use_count`, rather than instant
membership). Chosen because it keeps `application` mode's guarantee
unconditional rather than silently weaker whenever an invite happens to be
involved — a mode whose approval requirement has an exception is a mode
whose guarantee an officer can't actually rely on. Flagged as a genuine
judgment call, not a foregone conclusion — the "bypass" reading is
reasonable too, and cheaper to explain to end users ("here's an invite" vs.
"here's an invite, but you'll still need to wait for approval").

### 1.9 Join requests

New table:

```sql
CREATE TABLE guild_join_requests (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_id     UUID NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
    user_id      UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    requested_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (guild_id, user_id)
);
CREATE INDEX idx_guild_join_requests_guild_id ON guild_join_requests(guild_id);
```

**`REQUEST_JOIN`** (client → server):

```json
{ "type": "REQUEST_JOIN", "guild_id": "g_1" }
```

Validation: guild exists and is `private` → `GUILD_NOT_FOUND` (§1.8); guild
is `open` → `PROTOCOL_VIOLATION` ("guild is open — use `JOIN_GUILD`
instead," reusing the existing wrong-action-for-this-state convention, e.g.
`LEAVE_GUILD`-as-owner); caller already a member → `PROTOCOL_VIOLATION`
(existing "already a member" reuse, §1.3); caller already has a pending
request for this guild → `JOIN_REQUEST_ALREADY_PENDING`.

Response (`Scope::DIRECT`, to requester): `{ "type": "JOIN_REQUESTED",
"guild_id": "g_1" }`. Also broadcasts `{ "type": "JOIN_REQUEST_RECEIVED",
"guild_id": "g_1", "user_id": "u_2", "username": "..." }` to every
officer-or-above member currently connected (`Scope::TARGETED`, fd list =
`getFdsInGuild(guild_id)` filtered to officer-or-above), so officers don't
have to poll `LIST_JOIN_REQUESTS` to notice a new one — same
"guild-wide notifications keep state in sync without polling" principle
`docs/guilds/design.md` already used for `CHANNEL_CREATED` et al.

**`LIST_JOIN_REQUESTS`** (client → server):

```json
{ "type": "LIST_JOIN_REQUESTS", "guild_id": "g_1" }
```

Validation: caller passes `canApproveJoinRequest` (officer-or-above, §2.2).
Response (`Scope::DIRECT`):

```json
{
  "type": "JOIN_REQUEST_LIST",
  "guild_id": "g_1",
  "requests": [
    { "user_id": "u_2", "username": "web_user", "requested_at": "2026-08-01T12:00:00Z" }
  ]
}
```

**`APPROVE_JOIN_REQUEST`** / **`REJECT_JOIN_REQUEST`** (client → server):

```json
{ "type": "APPROVE_JOIN_REQUEST", "guild_id": "g_1", "user_id": "u_2" }
{ "type": "REJECT_JOIN_REQUEST", "guild_id": "g_1", "user_id": "u_2" }
```

Validation: caller passes `canApproveJoinRequest`; the request exists
(`JOIN_REQUEST_NOT_FOUND` otherwise). Approval creates the membership and
deletes the request row atomically, in the same single-internal-call
pattern §1.3 already established for invite redemption (new endpoint:
`POST /internal/guild-join-requests/:id/approve`); rejection just deletes
the row (`DELETE /internal/guild-join-requests/:id`).

**`ARBITRATION`: notifying two different audiences with two different
payloads.** Approving a request needs to tell the *approver* "done"
(`JOIN_REQUEST_APPROVED`) **and** tell the *approved user* "you're in now"
(`GUILD_JOINED` — reusing the same shape `JOIN_GUILD`/`JOIN_VIA_INVITE`
already return, per §1.4's precedent) — two different recipients, two
different payloads, from one client action. **Every existing handler in
this codebase returns exactly one `Message` to the dispatcher**
(`docs/guilds/design.md` §"`Message`/`Scope` changes" — one `Message`,
one `Scope`, one recipient set). Rejection has the same shape (confirm to
the rejecter, notify the rejected user). Nothing existing needs this today
— even `DELETE_GUILD`'s broadcast-to-all-members is still *one* payload to
*one* (larger) recipient set, not two different payloads to two different
sets.

Two ways to close the gap:

- **Widen the dispatcher contract to `std::vector<Message>`.** `Server::
  start()`'s scope-switch loop (`docs/guilds/design.md`) runs once per
  returned `Message` instead of once total. General-purpose — every future
  case needing this shape works the same way, not just this one.
- **Give handlers a narrow "extra send" capability** — inject a small
  callback (e.g. `std::function<void(int fd, const Message&)>`) into
  handlers that need out-of-band sends, alongside their existing
  `SessionManager*`/`GuildManager*` setters, used only for this case.

**Recommendation: widen to `std::vector<Message>`.** A general dispatcher
capability, added once, is a smaller total footprint than a special-cased
callback that only `GuildHandler`'s two new methods would use — and it's
the kind of foundational, mechanical, easy-to-test-in-isolation change
`docs/guilds/design.md`'s own implementation order already modeled for
`Scope::TARGETED` itself (done first, alone, before anything used it).
Sequenced accordingly in §6.

`getFdsForUser(user_id)` (new `SessionManager` helper, alongside
`getFdsInGuild`/`getFdsWithActiveChannel`) is also new here — approving a
request needs to reach the approved user's connection(s) even though
they're not yet reflected as a guild member in anyone's `Session` state.
Returns every fd currently identified as that `user_id` (handles multiple
tabs/devices the same way presence's connection-count tracking already had
to, §3.1 — unmodified here, just the same reasoning applied to a second
case). May be empty if the user isn't currently connected, which is fine —
`Scope::TARGETED` with an empty `target_fds` is already a no-op elsewhere
in this codebase.

New error codes: `GUILD_REQUIRES_APPROVAL`, `JOIN_REQUEST_ALREADY_PENDING`,
`JOIN_REQUEST_NOT_FOUND`.

### 1.10 Changing visibility

**`SET_GUILD_VISIBILITY`** (client → server):

```json
{ "type": "SET_GUILD_VISIBILITY", "guild_id": "g_1", "visibility": "private" }
```

Validation: caller passes `canSetGuildVisibility` (captain-only — see the
Security Checklist, §5, for why this isn't widened to officer-or-above).
Response (`Scope::TARGETED`, to all current guild members, same
guild-wide-notification pattern as `CHANNEL_CREATED`): `{ "type":
"GUILD_VISIBILITY_CHANGED", "guild_id": "g_1", "visibility": "private" }`.
New internal endpoint: `POST /internal/guilds/:id/visibility`.

**`ARBITRATION`: what happens to pending join requests when a guild leaves
`application` mode?** (Only relevant leaving-`application` — entering it,
or moving between `open`/`private`, starts with no pending requests to
reconcile.)

- **Delete them.** The approval workflow no longer applies; a requester who
  still wants in re-approaches through whatever the new mode offers
  (`JOIN_GUILD` if now `open`; nothing if now `private`, beyond obtaining
  an invite).
- **Auto-approve them.** Treats "the guild opened up" as implicit blanket
  approval for everyone who was already waiting.

**Recommendation: delete.** An officer's silence on a still-pending request
shouldn't retroactively become approval just because the guild's mode
changed for an unrelated reason later — auto-approval risks admitting
someone no officer ever actually looked at. Implemented as part of the same
transaction as the `visibility` `UPDATE`, in `POST /internal/guilds/:id/
visibility`'s handler: if the guild's *old* visibility was `application`
and the *new* one isn't, delete every `guild_join_requests` row for that
guild.

**Required fix, not optional, for `private` to work at all**:
`Session.guild_ids` (`server/include/session/Session.hpp`) starts empty on
every connection and is only populated by explicit `JOIN_GUILD`/
`CREATE_GUILD` calls made *during that session* — flagged as a soft,
pre-existing UX gap in §3.4's open question (unmodified here). For `open`/
`application` guilds this was survivable (the guild stays listed either
way). For `private`, it's a hard blocker: `LIST_GUILDS`'s new
per-caller filter (§1.8 — show a `private` guild only to its own members)
needs to know the caller's *real* memberships, and today's `Session.
guild_ids` can't answer that reliably for a freshly-reconnected member.

The fix is cheap because the data already flows through the system and is
simply discarded: `GuildManager`'s catalog hydration
(`hydrateGuildCatalog()`) already fetches `Catalog.memberships`
(`WireMembership { guild_id, user_id }`) at startup but doesn't consult it
("delivery eligibility is per-connection `SessionManager` state... not
hydrated from the catalog"). Extending `GuildManager` to also index
memberships by `user_id` (populated at hydration, kept current by the same
`createMembership`/`deleteMembership` calls that already update the cache)
lets `IdentifyHandler` populate `Session.guild_ids` immediately on success
via a pure in-memory lookup — no new network call, no new internal API
endpoint. Scoped here as a prerequisite specifically for §1.8's private-
guild filtering; sequenced in §6.

### 2.1 Protocol message

**`LIST_MEMBERS`** (client → server):

```json
{ "type": "LIST_MEMBERS", "guild_id": "g_1" }
```

Validation: caller is a guild member (`NOT_GUILD_MEMBER` otherwise) —
same precedent as `LIST_CHANNELS` (`docs/guilds/design.md`: "you see a
guild's channel list only after joining it"). Non-members can already see
that a guild exists and its name/owner via `LIST_GUILDS`, but not who's in
it — roster is a step further than bare existence, and gating it behind
membership costs nothing since the requester is presumably about to be (or
already is) a member for any legitimate reason to ask.

Response (`Scope::DIRECT`):

```json
{
  "type": "MEMBER_LIST",
  "guild_id": "g_1",
  "members": [
    { "user_id": "u_1", "username": "web_user", "role_rank": 2, "role_label": "Captain", "joined_at": "2026-07-14T18:00:00Z" }
  ]
}
```

`role_rank` (integer) and `role_label` (string, pre-resolved) are both
present — see §2.2. `role_rank` is what a client uses for permission-gated
UI (e.g. "show the promote button only if `myRank >= role_rank`" without
duplicating any tier logic); `role_label` is what a client *displays*,
already resolved server-side against the guild's chosen theme so no client
needs its own copy of the rank→label mapping. Sending both, rather than
making the client resolve `role_rank` itself, keeps the theme mapping a
single server-side concern (§2.2) — a client that only had `role_rank`
would need to know about themes at all to render anything meaningful.

### 2.2 Roles: rank-based, decoupled from display labels

**Revised again.** The previous revision replaced the original binary
`role` column with a three-value string `CHECK (role IN ('captain',
'officer', 'crew'))`, with predicates comparing against those string
literals directly. That was never implemented, and this revision replaces
it before it ever needs to be: a string-literal model means every future
tier addition (5+ tiers, planned) touches every predicate that compares
against a literal, and ties the *permission* model to whatever labels
happen to be chosen for display — exactly the "second schema/predicate
rework later" this revision exists to avoid. The rank model **fixes the
authorization model in place now**; adding tiers later only ever means
adding rank constants and (optionally) theme entries, never touching a
predicate's comparison logic.

**Schema.** `guild_memberships.role` (string) is replaced outright by
`role_rank` (integer). The three-value string design from the previous
revision was never implemented, so there's no intermediate state to
migrate through — this migrates directly from today's real, shipped
schema (`role TEXT CHECK (role IN ('owner', 'member'))`):

```sql
ALTER TABLE guild_memberships
  ADD COLUMN role_rank INTEGER NOT NULL DEFAULT 0 CHECK (role_rank >= 0);
UPDATE guild_memberships SET role_rank = 2 WHERE role = 'owner';
-- DEFAULT 0 already lands every 'member' row on rank 0 — no second UPDATE needed.
ALTER TABLE guild_memberships DROP CONSTRAINT guild_memberships_role_check;
ALTER TABLE guild_memberships DROP COLUMN role;
```

**No upper bound or enumerated `CHECK` on `role_rank`, deliberately.**
Constraining it to a fixed set (e.g. `CHECK (role_rank IN (0, 1, 2))`)
would silently reintroduce the same per-tier rework problem this model
exists to avoid — every new tier would need a migration touching the
constraint itself, not just an additive constants change. `>= 0` is the
whole constraint, permanently.

**Named rank constants** (e.g. `server/include/guild/RoleRank.hpp`):

```cpp
namespace guild {
constexpr int kMemberRank = 0;
constexpr int kOfficerRank = 1;
constexpr int kOwnerRank = 2;
}
```

Still exactly three tiers functionally, in this iteration — the request is
explicit that the *predicate logic* be rank-generic now specifically so
adding a fourth or fifth tier later is additive (new named constants, and
new theme labels if wanted) rather than a second rework of every predicate
and every string comparison, the way going from binary → three tiers would
otherwise have required doing this same exercise twice.

**Naming scope, unchanged from the previous revision**: `guilds.owner_id`,
`GuildManager::isOwner()`, and the `NOT_GUILD_OWNER` error code keep their
existing names, per that revision's own reasoning — they're the shipped
ownership/authorization mechanism, not a display label, and renaming them
is unrelated churn this document still doesn't propose. `isOwner()` stays
the authority on who the owner is (comparing against `guilds.owner_id`);
`role_rank` is expected to agree with it (the owner's own
`guild_memberships` row always carries `kOwnerRank`) but that agreement is
**maintained by construction**, not DB-enforced — `CREATE_GUILD` creates
the owner's membership row at `kOwnerRank` directly, and `SET_MEMBER_ROLE`
(§2.4) structurally can't target the owner's row. A stricter DB-level
invariant (e.g. a trigger guaranteeing exactly one `kOwnerRank` row per
guild) is possible future hardening, not designed in full here — consistent
with how this document already treats several things as flagged-but-not-
fully-designed rather than silently assumed solid.

**Predicate table.** Same predicates as before; the *mechanism* changes
(string literal → rank threshold), and the *recommended tier per
predicate* does not — this revision isn't re-litigating who can do what,
only how the check is expressed:

| Predicate | Action | Comparison | Notes |
|---|---|---|---|
| `isOwner` | (internal helper, not itself an authorization gate) | `user_id == guilds.owner_id` | Unchanged — does not read `role_rank` at all (see above). |
| `canCreateChannel` | `CREATE_CHANNEL` | `role_rank >= kOfficerRank` | Same tier as the previous revision, now expressed as a rank comparison. |
| `canDeleteChannel` | `DELETE_CHANNEL` | `role_rank >= kOwnerRank` | Same tier and same reasoning as the previous revision (message-history cascade-delete stakes, §4.2) — still deliberately not widened; the `ARBITRATION` flagged there still stands, mechanism aside. |
| `canDeleteGuild` | `DELETE_GUILD` | `role_rank >= kOwnerRank` | Unchanged. |
| `canCreateInvite` | `CREATE_INVITE` / `LIST_INVITES` / `REVOKE_INVITE` (§1.4) | `role_rank >= kOfficerRank` | Same tier as the previous revision. |
| `canApproveJoinRequest` | `APPROVE_JOIN_REQUEST` / `REJECT_JOIN_REQUEST` / `LIST_JOIN_REQUESTS` (§1.9) | `role_rank >= kOfficerRank` | Same tier as the previous revision. |
| `canSetMemberRole` | `SET_MEMBER_ROLE` (§2.4) | `role_rank >= kOwnerRank` | Same tier as the previous revision. |
| `canSetGuildVisibility` | `SET_GUILD_VISIBILITY` (§1.10) | `role_rank >= kOwnerRank` | Same tier as the previous revision. |

Every "officer-or-above" row above calls through one shared helper,
`isOfficerOrAbove(guild_id, user_id)` (`role_rank >= kOfficerRank`), the
same way every owner-only row already calls `isOwner`. The point of both
helpers existing is exactly what the request asked for: if a future tier
insertion changes what "officer-or-above" means, there is exactly **one**
place that changes — no predicate call site does, since none of them
compare against a literal.

**Display labels — a separate, purely cosmetic concern.**
`role_label` in §2.1's response is resolved from `role_rank` **per guild**,
via a theme:

```sql
ALTER TABLE guilds ADD COLUMN role_theme TEXT NOT NULL DEFAULT 'pirate';
```

**`ARBITRATION`: where do theme → label mappings live?**

- **Static in-code registry** (e.g. a small object in the Next.js internal
  API layer, since label resolution happens there — see below):
  ```ts
  const ROLE_THEMES: Record<string, Record<number, string>> = {
    pirate: { 0: "Crew", 1: "Officer", 2: "Captain" }
  };
  ```
- **A `role_themes` Postgres table** (`theme TEXT, role_rank INTEGER, label
  TEXT, PRIMARY KEY (theme, role_rank)`), seeded with the same one built-in
  theme's rows.

**Recommendation: static registry, for now.** Exactly one theme exists in
this iteration ("start with one built-in theme"); a DB table buys
admin-editable/data-driven themes without a redeploy, a capability nothing
in the request asks for yet. The static version is a strict subset of the
table version's shape (same `theme → rank → label` structure), so moving
to a table later is a data migration, not a redesign — pick the DB-backed
version now instead only if selectable/editable themes are expected sooner
than "later."

**No permission logic ever reads `role_theme` or the theme mapping** — the
request is explicit about this, and it falls directly out of §2.1's design:
predicates only ever see `role_rank` (an integer, theme-independent);
`role_label` is a value attached to a response for display, never
consulted by any `can*` check.

**Resolution happens once, server-side, in Next.js** — the same internal
API call that already joins `guild_memberships` against `users` for
`username` (§2.3) also joins against `guilds.role_theme` and the theme
mapping to produce `role_label`, and returns both `role_rank` and
`role_label` together. C++ never resolves a label itself; `GuildHandler`
just relays whatever the internal API already resolved, consistent with
"Next.js owns Postgres access, C++ never touches it directly."
**Fallback**: if a `role_rank` has no entry for the guild's theme (a data
gap — e.g. a theme not yet updated to cover a newly-added tier), resolve to
a generic `"Rank {role_rank}"` string rather than erroring — a display gap
should degrade, not break the response.

**Explicit non-goal, this iteration**: no protocol message to *change* a
guild's `role_theme` is proposed. With exactly one built-in theme, there's
nothing to select between yet; a `SET_GUILD_ROLE_THEME`-shaped message is
the natural next step once a second theme exists, not designed here.

### 2.3 Where the data comes from — `ARBITRATION`-adjacent note

Unlike the guild/channel catalog, roster is not proposed to live in
`GuildManager`'s write-through cache. Two options were considered:

- **Cache it**: extend `GuildManager` to also track per-guild membership
  (user id, username, `role_rank`), populated at catalog hydration and kept
  current via the same `createMembership`/`deleteMembership` calls
  `JOIN_GUILD`/`LEAVE_GUILD` already make. Consistent with the existing
  pattern; requires extending `Catalog`/`WireMembership`
  (`server/include/http/InternalApiClient.hpp`) to carry `username` and
  `role_rank`, which they don't today (`WireMembership` is currently just
  `{ guild_id, user_id }`) — and would need `role_label` resolution to
  happen in C++ too, pulling the theme registry into a service that
  otherwise never needs to know about it (§2.2).
- **Fetch live**: `LIST_MEMBERS` makes a synchronous internal API call
  (`GET /internal/guild-memberships?guildId=`, joined against `users` for
  display name and against `guilds.role_theme` + the theme mapping for
  `role_label`, §2.2) each time it's invoked, same shape as every other
  request/response internal call, but not cached anywhere in C++.

**Recommendation: fetch live.** The write-through cache earns its keep on
hot, frequent, latency-sensitive paths (every `CREATE_GUILD`,
`CHANNEL_MESSAGE`, etc.). `LIST_MEMBERS` is a cold, low-frequency,
UI-driven read (a user opens a member panel occasionally) — caching it
would mean widening `GuildManager`'s responsibility and the wire shape of
`Catalog`/`WireMembership` for a path that doesn't need the latency win,
would add another thing that can silently drift from Postgres if a
mutation path is ever missed, **and** — new to this revision — would move
label resolution into C++ for no benefit, when keeping it fetch-live keeps
theme resolution entirely inside the one service that already owns
Postgres access. Keeping it as a live read keeps the blast radius of this
feature small. If roster reads ever become hot enough to matter, revisit
then with real numbers, not speculatively now.

### 2.4 Assigning roles

Not requested verbatim, but required for the officer tier to be reachable
at all — a rank column with no protocol path to change it is a schema
change with no way to exercise it. Filling this gap explicitly, matching
`docs/guilds/design.md`'s own "Decisions Made While Filling In Gaps"
pattern for exactly this situation (a natural completion needed to make an
approved decision actually work end-to-end, not a new decision on its own).

**`SET_MEMBER_ROLE`** (client → server):

```json
{ "type": "SET_MEMBER_ROLE", "guild_id": "g_1", "user_id": "u_2", "role_rank": 1 }
```

`role_rank`, not a label — the wire boundary for a *permission-relevant*
action should never take a theme-dependent display string, for the same
reason predicates never compare against one (§2.2). A client's role-
management UI shows labels (from `role_label`, §2.1) but sends the rank
underneath.

Validation: caller passes `canSetMemberRole` (`role_rank >= kOwnerRank`,
§2.2); target is a guild member (`NOT_GUILD_MEMBER`); `role_rank` must be
`>= 0` and `< kOwnerRank` — **not** the owner rank or above. Phrased as a
threshold rather than "must be `kOfficerRank` or `kMemberRank`" so this
validation rule doesn't need rewording when a fourth/fifth tier is added
below owner — it already covers "any rank that isn't the owner's," however
many such ranks eventually exist. Ownership isn't reassignable through this
message regardless: `docs/guilds/design.md` already put ownership transfer
out of scope ("Owner leaving their own guild: disallowed... Ownership
transfer is out of scope"), and `SET_MEMBER_ROLE` isn't the place to
quietly reopen that.

Response (`Scope::TARGETED`, to all current guild members, same
guild-wide-notification pattern as `CHANNEL_CREATED`): `{ "type":
"MEMBER_ROLE_UPDATED", "guild_id": "g_1", "user_id": "u_2", "role_rank": 1,
"role_label": "Officer" }` — carrying the resolved label too, same
reasoning as §2.1: a client reacting to this notification (e.g. updating a
member-list row in place) shouldn't need a follow-up `LIST_MEMBERS` call
just to find out what "rank 1" is called this guild's theme.

**`ARBITRATION`: who can promote/demote?** Recommendation above is
owner-only, simplest and matches `DELETE_GUILD`/`SET_GUILD_VISIBILITY`'s
precedent for guild-structural decisions. A more granular alternative
exists — e.g. officers can promote crew to officer but can't touch other
officers' roles or demote anyone — but that's meaningfully more governance
machinery (who can act on whom, in which direction) than this iteration
asked for, and owner-only is trivially widened later (a rank-range check,
not a rewrite) if it proves too restrictive in practice.

---

## 3. Presence

### 3.1 In-memory tracking

Presence is **not** "does a `Session` exist for this fd" — a user can have
multiple simultaneous connections (multiple tabs/devices; nothing in
`IdentifyHandler` today prevents two fds identifying as the same
`user_id`), and naively broadcasting on every connect/disconnect would
flicker online/offline every time a second tab opens or closes.

Proposed: a small addition — either a new `PresenceTracker` or a method
added to `SessionManager` (leaning `SessionManager`, since it already owns
per-connection lifecycle and this is a derived view of the same data, not
a new domain) — maintaining a `user_id → connection_count` map:

```cpp
// Conceptual addition to SessionManager
void incrementPresence(const std::string& user_id); // called from IdentifyHandler on success
void decrementPresence(const std::string& user_id); // called on disconnect (both paths, see 3.3)
bool isOnline(const std::string& user_id) const;
```

A user transitions **online** when their count goes 0→1 (first connection),
and **offline** when it goes 1→0 (last connection closes). A second/third
tab connecting or closing doesn't emit anything — no visible state change
occurred.

### 3.2 Broadcast content and trigger

```json
{ "type": "PRESENCE_UPDATE", "user_id": "u_1", "status": "online" }
```

(`status` is `"online"` or `"offline"`.) Emitted from `IdentifyHandler` on
the 0→1 transition, and from the connection-cleanup path on the 1→0
transition (§3.3).

### 3.3 Ungraceful disconnects

`Server::start()`'s accept loop already treats a failed `readFromSocket()`
as a disconnect and calls `session_manager_.removeSession(fd)` — this is
the **existing** detection point for both a clean close and a TCP
RST/FIN-then-nothing-more; presence's offline transition should piggyback
on this exact call site, not add a new detection mechanism. No protocol
change is needed for the common case.

**`OPEN QUESTION`**: the gap that isn't solved by the above is a
**half-open connection** — a peer that stops responding without the OS
ever surfacing a read failure (network partition, laptop sleep without a
clean close). TCP alone won't notice this promptly; the server would keep
treating that connection as online (and its user as present) until either
something tries to *write* to that fd and gets `EPIPE`/`ECONNRESET`, or an
OS-level timeout fires. Two mitigations, not mutually exclusive, neither
designed in full here:

- `SO_KEEPALIVE` on accepted sockets — cheap, OS-assisted, but default
  keepalive intervals are often very long (e.g. hours on Linux) unless
  explicitly tuned, which itself needs a decision on acceptable
  false-positive-disconnect risk vs. detection latency.
- An application-level heartbeat (a lightweight ping/pong protocol message,
  with the server treating N missed pongs as a disconnect) — faster,
  precise, more moving parts, and arguably more machinery than presence
  alone justifies this iteration.

Recommendation: ship without solving this fully — `SO_KEEPALIVE` with a
reasonable OS-level timeout as a cheap first pass, explicitly documented as
imperfect (a user could show "online" for up to that timeout after actually
going dark), and revisit a real heartbeat if that proves to matter in
practice rather than building it preemptively.

**Implemented as recommended**: `SO_KEEPALIVE` is enabled on accepted
sockets (`server/src/TcpListener.cpp`). The application-level heartbeat
alternative above was not built — still genuinely open, not just
theoretically so.

### 3.4 `ARBITRATION`: lobby-wide vs. per-guild-scoped broadcast

The request asks presence to be visible both lobby-wide ("who's currently
connected") and per-guild ("which members of this guild are online"). Two
ways to deliver that:

**Option A — lobby-wide broadcast only** (`Scope::BROADCAST`, same delivery
model `CHAT_MESSAGE` already uses). Every connected client receives every
`PRESENCE_UPDATE`, regardless of shared guild membership. The per-guild
view isn't a separate broadcast — the client computes it by intersecting
the globally-received online set against the roster it already has for
that guild (§2). One broadcast path, zero new fan-out logic, no new
targeting computation.

**Option B — per-guild-scoped broadcast** (`Scope::TARGETED`, mirroring
`CHANNEL_MESSAGE`'s delivery model). A presence change delivers only to
connections that share at least one guild with the user whose status
changed — computed as the union of `getFdsInGuild(g)` across every guild
`g` in that user's membership, deduplicated. This needs the *global*
lobby-wide view to still exist somehow (the request explicitly wants a
"who's in the lobby" view too), so Option B isn't actually a full
replacement for Option A — it would mean **two** delivery paths (a
lobby-wide one and a guild-scoped one) doing overlapping work, for a
narrower privacy win described below.

**Recommendation: Option A.** It satisfies both stated visibility
requirements (lobby view + per-guild view) with one broadcast mechanism,
the client-side filter is cheap (a set intersection against data it already
has from §2), and — the reason to actually pick it over B rather than just
"it's simpler" — **there is no existing guild-privacy boundary for Option
B to protect.** `LIST_GUILDS` already tells every identified client that a
given guild exists; nothing about guild membership is currently
confidential. Option B's real cost (a second delivery path, a
`Session.guild_ids`-accuracy dependency — see the open question below) buys
privacy that doesn't exist anywhere else in the system yet. If guild
privacy (§1.1's deferred item) ever gets built, presence scoping should be
revisited *together with it*, the same way `docs/guilds/design.md` already
noted privacy and permissions are "closely related" concerns best handled
together — not decided in isolation now.

**`OPEN QUESTION`** (relevant to Option B if it's ever chosen instead, and
worth flagging regardless): `Session.guild_ids` (`server/include/session/
Session.hpp`) starts **empty** on every new connection and is only
populated by explicit `JOIN_GUILD`/`CREATE_GUILD` calls made *during that
session* — it is not hydrated from durable Postgres membership on
`IDENTIFY`. This is a pre-existing gap (observed in the web client today: a
reconnecting member sees "Join" on a guild they already belong to until
they act on it again), not something introduced by this document, but it
would directly undermine any per-guild-scoped feature — including a future
Option B — until fixed. Flagging it here since it's adjacent to this work,
not proposing the fix as part of this document.

---

## 4. Message Persistence

### 4.1 Today's actual behavior (for accuracy — this is slightly stronger
than "in memory")

`ChatHandler`/`ChannelHandler` don't just skip persistence — they don't
retain messages **anywhere**, not even a bounded in-memory buffer. A
message is built, broadcast once to whoever is currently connected, and
then gone. A client that connects mid-conversation sees nothing before the
moment it connected. This document's schema and retrieval design close that
gap entirely, not just the "survives a restart" part of it.

### 4.2 Schema

One `messages` table, not two, since `CHAT_MESSAGE` and `CHANNEL_MESSAGE`
share an identical shape and differ only in whether they're scoped to a
channel:

```sql
CREATE TABLE messages (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    -- NULL = global lobby (CHAT_MESSAGE); set = a specific channel (CHANNEL_MESSAGE)
    channel_id  UUID REFERENCES channels(id) ON DELETE CASCADE,
    user_id     UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    content     TEXT NOT NULL CHECK (char_length(content) BETWEEN 1 AND 500),
    -- Server-assigned, NOT a Postgres-generated sequence — see §4.3 for why.
    seq         BIGINT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Per-channel id-space uniqueness.
CREATE UNIQUE INDEX idx_messages_channel_seq ON messages(channel_id, seq) WHERE channel_id IS NOT NULL;
-- Lobby id-space uniqueness — a plain UNIQUE(channel_id, seq) doesn't work here
-- since Postgres treats every NULL channel_id as distinct.
CREATE UNIQUE INDEX idx_messages_lobby_seq ON messages(seq) WHERE channel_id IS NULL;

-- Pagination access patterns (§4.4): "most recent N before some point, per channel/lobby".
CREATE INDEX idx_messages_channel_created ON messages(channel_id, seq DESC);
```

`content`'s length check mirrors the existing `500`-character limit already
enforced in `ChatHandler`/`ChannelHandler`. Deleting a channel (or its
guild) cascades to delete its message history via the existing FK-cascade
convention — no soft-delete/archive proposed here.

### 4.3 `message_id` becomes durable, and stays C++-assigned

Today, `message_id` is a per-process `std::atomic<int>` (`ChatHandler::
message_counter_`, `ChannelHandler::message_counter_`), starting at `0` on
every server restart. That's fine when nothing durable references it. It
is **not** fine once messages are persisted: after a restart, the counter
would restart at `1` and collide with already-persisted ids.

The fix is **not** to let Postgres assign the id (e.g., a `bigserial`).
The request's own framing — "the C++ server stays authoritative for
ordering/broadcast, Next.js/Postgres becomes the durable store" — is
exactly right, and a Postgres-assigned id would invert that: the handler
would have to wait on the persistence call to learn the id before it could
even broadcast, directly undermining the fire-and-forget option in §4.5.

Instead: **the C++ server keeps assigning `message_id` from its own atomic
counter**, exactly as today, but that counter is seeded at startup from the
durable high-water mark instead of always starting at `0` — a new internal
API call, e.g. `GET /internal/messages/last-sequence` (lobby) and
`GET /internal/messages/last-sequence?channelId=` (per channel), folded
into server startup alongside the existing catalog hydration
(`hydrateGuildCatalog()`). The persistence write then passes this
server-computed value through as `seq`, rather than letting Postgres invent
its own. This keeps the id meaning stable between what a client saw live
and what `FETCH_HISTORY` later returns, and keeps message send fully
independent of Postgres being reachable (§4.5).

### 4.4 History retrieval

**`FETCH_HISTORY`** (client → server):

```json
{ "type": "FETCH_HISTORY", "channel_id": "c_1", "before_seq": null, "limit": 50 }
```

(`channel_id` omitted/`null` = lobby history.) `before_seq: null` on the
first call fetches the most recent page; subsequent calls pass the oldest
`seq` seen so far to page further back — keyset pagination, not
`OFFSET`-based, to avoid the well-known performance cliff of offset
pagination on a growing table.

Response (`Scope::DIRECT`):

```json
{
  "type": "MESSAGE_HISTORY",
  "channel_id": "c_1",
  "messages": [
    { "message_id": 42, "timestamp": 1741104000, "user_id": "u_1", "username": "web_user", "content": "hello" }
  ],
  "has_more": true
}
```

Validation: for a channel, caller must be a member of its guild
(`NOT_GUILD_MEMBER`) — same boundary `CHANNEL_MESSAGE` already enforces to
send; reading history shouldn't be looser than writing to it. For the
lobby, only `NOT_IDENTIFIED` applies, matching `CHAT_MESSAGE`'s existing
fully-open-to-any-identified-client model.

**This is deliberately not membership-join-time-scoped.** A member sees the
full paginated history regardless of when they joined — five minutes ago or
five months ago, invited or joined directly — because nothing in this
design filters `messages` by the requester's `joined_at`. This directly
answers the interaction with feature 1: **an invited member sees the same
channel history as any other current member**, not just messages sent after
they joined.

**Initial backfill on `JOIN_CHANNEL`**: `CHANNEL_JOINED`'s response shape
stays unchanged — no embedded message page. The client calls
`FETCH_HISTORY` as a separate, explicit follow-up right after receiving
`CHANNEL_JOINED`. Considered embedding the first page directly in
`CHANNEL_JOINED` instead; rejected because it makes that response's size
unpredictable (depends on channel activity) and couples two independently-
useful, independently-retriable actions ("join this channel" and "load its
history") into one — if history fetch fails or needs retrying, you
shouldn't have to re-join to try again.

**A new kind of internal API call.** Every internal API call so far
(`docs/auth/discord-design.md` §8.1) is a *mutation* that also updates
`GuildManager`'s write-through cache. `FETCH_HISTORY`'s internal call
(`GET /internal/messages?channelId=&beforeSeq=&limit=`) is the **first
read-through, not write-through** call — nothing in C++ caches message
history (an unbounded, ever-growing dataset doesn't fit the same
bounded-catalog caching story `GuildManager` uses for guilds/channels), so
every `FETCH_HISTORY` is a live round trip to Next.js. Flagged explicitly
since it's a deliberate exception to the established pattern, not an
oversight.

### 4.5 `ARBITRATION`: fire-and-forget vs. blocking persistence before broadcast

This is the highest-stakes decision in this document — it changes the
latency and failure characteristics of the single most frequently-sent
message type in the whole protocol.

**Option A — blocking (persist, then broadcast).** The handler calls
`POST /internal/messages` synchronously and only builds/broadcasts the
`Message` after it succeeds — the same pattern every other mutation
handler already uses (`CREATE_GUILD` et al. never update local state or
respond until the internal call confirms).

- **Pro**: correctness by construction. Every message a client sees live is
  guaranteed to already be durable; `FETCH_HISTORY` can never be missing
  something a client watched arrive in real time.
- **Pro**: no new pattern to reason about — identical to how every other
  mutation in this codebase already works.
- **Con**: adds a network round trip + Postgres write to the hottest path
  in the system. Every single chat message, not just guild/channel
  management actions (which are comparatively rare), now pays this cost.
- **Con**: a new hard runtime dependency on Next.js/Postgres for the
  most basic feature of the app (sending a chat message) — mirrors exactly
  the tradeoff `docs/auth/discord-design.md` §9 already weighed for
  `IDENTIFY` validation (their "Option 2") and rejected for the same
  reason: coupling a hot, frequent path to another service's availability.

**Option B — fire-and-forget (broadcast, then persist asynchronously).**
The handler builds and broadcasts immediately — zero behavior change to
today's latency — then hands the persistence write to a background
mechanism (a small worker/thread pool, or a queue drained on the existing
main-loop tick, similar in spirit to the revocation-cache poll already in
`Server::start()`) that doesn't block the response path.

- **Pro**: zero added latency on the hot path — matches the reasoning
  `docs/auth/discord-design.md` §9 used to justify self-validated JWTs
  (Option 1 there): the server keeps operating at today's speed and
  independence even if Next.js/Postgres is briefly degraded.
- **Con**: a real, user-visible correctness gap. If the async persistence
  call fails (Next.js down, a transient Postgres error, or the process
  crashing between broadcast and the write completing) after the message
  was already broadcast and seen live, it is **permanently missing** from
  history — a client that reconnects or pages back sees a gap where a
  message they (or someone else) watched arrive used to be.
- **Required detail, not optional**: the persisted `seq` must be the
  server-assigned value computed at broadcast time (§4.3), not whatever
  order the async writes happen to land in Postgres — otherwise concurrent
  or retried writes could persist out of the order clients actually
  observed.

**Recommendation: Option B, with bounded retry.** Consistent with the
precedent this same codebase already set for the structurally identical
tradeoff in `docs/auth/discord-design.md` §9 — prioritize hot-path latency
and service independence, accept a bounded consistency gap, and mitigate
it rather than pay for synchronous correctness on every request. Mitigation:
a small number of retries with backoff (e.g., 3 attempts over a few
seconds) on persistence failure, plus structured logging on final failure
so a persistently-broken write path is observable rather than silently
losing messages forever. This narrows the loss window to "the persistence
path was down for longer than the retry budget," not "any transient
hiccup," but does not eliminate it — full elimination requires Option A's
latency cost. This is presented as a recommendation, not a foregone
conclusion — flagged per instruction as a genuine judgment call for review,
not something to silently ship either way.

---

## 5. Security Checklist

- [ ] **Who can create invites?** **Revised: officer-or-above**, not
  owner-only as the previous version of this document had it — via
  `GuildManager::canCreateInvite(guild_id, user_id)` (§2.2's predicate
  table), now that three tiers exist. Reasoning unchanged in kind, just in
  the answer: this is still a named predicate, not an inline check, so the
  boundary is one place to change if it needs revisiting again.
- [ ] **Who can approve/reject join requests?** Officer-or-above
  (`canApproveJoinRequest`, §1.9/§2.2) — given directly by the request.
- [ ] **Who can change a guild's visibility?** **Captain-only**
  (`canSetGuildVisibility`, §1.10/§2.2), deliberately not widened to
  officer-or-above. Visibility is a guild-wide structural decision with
  real consequences for every current and future member (it changes who
  can discover and join the guild at all), closer in kind to `DELETE_GUILD`
  than to day-to-day guild management — the same reasoning `canDeleteGuild`
  already uses for staying captain-only. Flagged as its own checklist item
  since the request specifically asked this be confirmed, not left implicit.
- [ ] **Who can promote/demote members?** Captain-only (`canSetMemberRole`,
  §2.4) — see §2.4's `ARBITRATION` for the more granular alternative
  considered and not chosen.
- [ ] **What happens to pending join requests when a guild's visibility
  changes?** Deleted, if the guild is leaving `application` mode (§1.10)
  — an officer's silence on a pending request shouldn't become implicit
  approval just because the mode changed for an unrelated reason. Done
  atomically with the visibility change itself, not as a separate cleanup
  step that could be skipped or race with a new request arriving mid-change.
- [ ] **Does probing a `private` guild's id leak its existence?** No —
  `JOIN_GUILD`/`REQUEST_JOIN` against a `private` guild the caller isn't a
  member of both return the same `GUILD_NOT_FOUND` a genuinely nonexistent
  id would (§1.8's `ARBITRATION`), and `LIST_GUILDS` excludes `private`
  guilds from non-members' results entirely (§1.8) rather than listing them
  with a "you can't see this" marker that would itself confirm existence.
- [ ] **Rate limiting on invite creation and redemption**, and, as of this
  revision, **join-request creation**. All three go through the internal
  API (§1.5/§1.9), not a public HTTP route, so they can't reuse
  `web/lib/auth/rateLimit.ts`'s existing per-IP limiter as-is (these are
  authenticated, C++-originated calls acting on behalf of a specific
  `user_id`, not anonymous inbound HTTP requests) — but the same
  Redis-backed sliding-window mechanism should be applied inside the
  internal API routes themselves, keyed by `user_id`. Illustrative limits,
  not final: invite creation capped per guild per hour (blunts invite-link
  spam from a compromised owner/officer account); redemption attempts
  capped per user per minute (blunts brute-force guessing of invite codes,
  the real defense-in-depth alongside code entropy from §1.2);
  `REQUEST_JOIN` capped per user per minute across all guilds (blunts
  spamming join requests at officers to be annoying/DoS-adjacent, distinct
  from `INVITE_MAX_USES_REACHED`-style abuse since a rejected request could
  otherwise be immediately resubmitted).
- [ ] **Invite code entropy.** Codes must be generated from a
  cryptographically secure random source at sufficient length (§1.2) — not
  sequential, not derived from `guild_id` or `created_at`, so knowing one
  valid code (or a guild's id) gives no information toward guessing
  another.
- [ ] **Roster exposure.** `LIST_MEMBERS` requires guild membership
  (§2.1) — a non-member can see a guild exists (`LIST_GUILDS`) but not who's
  in it, a real if narrow privacy boundary worth keeping even though guild
  existence itself is already public.
- [ ] **Does presence leak information across guild boundaries?** Yes,
  under the recommended lobby-wide broadcast (§3.4) — every connected
  client learns every other identified user's online/offline status,
  regardless of shared guild membership. This is **consistent with, not a
  regression from,** the current baseline: `LIST_GUILDS` and `JOIN_GUILD`
  already expose guild existence and membership-by-id to every identified
  client with no privacy model at all. No existing boundary is newly
  crossed. If guild privacy is ever built (§1.1), presence scoping needs
  revisiting alongside it, per §3.4.
- [ ] **`FETCH_HISTORY` authorization mirrors send authorization.** Channel
  history requires guild membership, same as sending to that channel (§4.4)
  — reading shouldn't be looser than writing.
- [ ] **Message content length.** `messages.content`'s `CHECK` constraint
  (§4.2) mirrors the existing 500-character wire-level limit — persistence
  doesn't get to be a backdoor around a validation rule already enforced at
  the protocol layer.
- [ ] **`/internal/*` isolation, still the load-bearing assumption.** Every
  new internal endpoint in this document (`guild-invites`, `guild-
  memberships` roster read, `messages`, and — as of this revision —
  `guild-join-requests` and `guilds/:id/visibility`) rides on the same
  isolation `docs/auth/discord-design.md` §8.1/§9.1 already requires and
  `web/test/internalIsolation.test.ts` already verifies against the real
  built image — no new isolation mechanism is proposed or needed, but every
  new route added here inherits that requirement and should be covered by
  the existing test's route list, not assumed automatically covered.

---

## 6. Proposed Implementation Order

**Revised.** Two steps inserted (3 and 5 below) as prerequisites the
revision's new content surfaced; step 2's scope grew (three tiers, not
binary); step 4 (was step 4, "Guild invites") is now the merged
invites+visibility+join-requests work, expanded accordingly. Presence's
own content (§3) is unchanged — it moves from step 3 to step 4 purely
because a new, unrelated step was inserted ahead of it, not because
anything about it changed.

1. **Message persistence — schema + internal API + protocol** (§4).
   Unchanged from the previous version. Placed first because it's the most
   self-contained of the four features (no dependency on invites, roster,
   or presence) and introduces the first read-through internal API call
   (§4.4), worth stabilizing before the other features add more surface
   area on top of the same internal-API machinery.
2. **Guild member roster + roles** (§2). **Scope grew across two
   revisions**: first to three tiers, then (this revision) to a rank-based
   model (`guild_memberships.role_rank`, named rank constants, and a
   separate cosmetic label-theme resolution layer — §2.2) so a future
   tier 4/5 addition stays additive rather than repeating this rework a
   third time, plus `SET_MEMBER_ROLE` (§2.4) to make ranks above the
   default reachable at all. Still small and isolated relative to the
   other three features. Sequenced before presence for the same reason as
   before — presence's client-side per-guild filtering (§3.4) needs a
   roster to filter against — and before step 6, since `application`-mode
   join-request approval (§1.9) needs `isOfficerOrAbove` to already be
   assignable before it's meaningfully usable.
3. **New: hydrate `Session.guild_ids` from durable membership on
   `IDENTIFY`** (§1.10's "Required fix" note). Small, mechanical, no
   protocol change — a `GuildManager` addition (index memberships by
   `user_id`, populated from data the write-through cache already fetches
   at startup) plus one new call in `IdentifyHandler`'s success path.
   Placed here, before both presence and invites/visibility, because it's
   cheap, foundational, and a hard blocker for §1.8's private-guild
   `LIST_GUILDS` filtering specifically — better landed early than
   discovered as a blocker partway through step 6.
4. **Presence** (§3). Content unchanged from the previous version — see
   the note at the top of this section for why its step number moved.
   Depends on step 2 for the per-guild view's client-side filtering to
   have something to filter against. Ship the `SO_KEEPALIVE` mitigation
   (§3.3) alongside this, not as a later follow-up.
5. **New: widen handler dispatch to return `std::vector<Message>`**
   (§1.9's `ARBITRATION`). Small, mechanical, testable in isolation the
   same way `Scope::TARGETED` itself was (`docs/guilds/design.md`'s
   implementation order: land the mechanism alone before anything depends
   on it). A hard prerequisite for `APPROVE_JOIN_REQUEST`/
   `REJECT_JOIN_REQUEST` in step 6, which is the first handler needing to
   notify two different recipients with two different payloads.
6. **Guild invites, guild visibility, and join requests** (§1, all
   subsections). **Expanded in this revision** — previously just invite
   creation/redemption/listing/revocation (§§1.2–1.6); now also visibility
   modes (§1.8), the join-request flow (§1.9), and changing visibility
   (§1.10), since `private` guilds are defined entirely in terms of
   invites ("joinable only via invite") and `application` guilds' approval
   flow depends on the officer tier from step 2. Still sequenced last: it's
   the largest net-new surface area of all four features, and benefits
   from the internal-API-call conventions used in steps 1–4, the
   role-tier work from step 2, and the two mechanical prerequisites in
   steps 3 and 5 all being in place first, rather than being the first
   thing to prove any of those patterns out. **Also includes**: a custom
   invite-creation UI (form fields for `max_uses`/`expires_in_seconds`,
   §1.4 — no protocol or schema change, since both fields already existed
   in `CREATE_INVITE` before this revision; this is purely the client-side
   work of exposing them instead of a client hardcoding `null`/`null`).

Each step should update `shared/protocol/README.md` with its new message
types/error codes as it lands, same discipline `docs/guilds/design.md`
called for and CLAUDE.md's "Reference Documentation" section requires —
not batched to the end.

## Related Documentation

- [`design.md`](design.md) — the guild/channel domain model this document
  extends; source of the "Deferred: Guild Privacy" and "Future Permission
  Hook" notes referenced throughout.
- [`../auth/discord-design.md`](../auth/discord-design.md) — the
  write-through cache pattern (§8.1), the `/internal/*` isolation
  requirement (§8.1/§9.1), and the self-validation-vs-delegate-live
  arbitration (§9) this document's §4.5 mirrors.
- [`../../shared/protocol/README.md`](../../shared/protocol/README.md) —
  current wire protocol; every section above is a proposed delta against it.

## What Changed From the Previous Version

Three new requirements, approved at a concept level with specifics to work
out here. Nothing about presence (§3) or message persistence (§4) changed
— neither section was touched. Everything below is either new content or
an edit to §1/§2/§5/§6.

**Scope section**
- Feature list (bullets 1–2) reworded to flag that invites now include
  visibility, and roster/roles now means three tiers, not binary.
- Added a revision note pointing at this section.
- "Not addressed here" list: removed the "Guild privacy... out of scope"
  bullet (no longer true) and reworded the Future Permission Hook bullet
  to reflect that three *fixed* tiers now exist, while a fully general
  permission system remains deferred.

**§1.1 (What this feature is (and isn't))**
- Rewritten. Previously declared guild privacy explicitly out of scope and
  explained why bolting it on would be worse than not attempting it. Now
  explains that this revision builds the *other half* the previous version
  was refusing to build alone, and forward-references §§1.7–1.10.

**§1.6 (single-use vs. reusable default)** — unchanged content, one
sentence appended noting §1.1's "if privacy is ever built" condition is no
longer hypothetical.

**New: §1.7 Guild visibility — data model and wire additions** —
`guilds.visibility` enum column; `CREATE_GUILD` gains an optional
`visibility` field; every guild-shaped wire object gains `visibility`.

**New: §1.8 Visibility modes** — behavior table for `open`/`application`/
`private`; two `ARBITRATION`s: what `JOIN_GUILD`-by-id returns for a
`private` guild (recommended: `GUILD_NOT_FOUND`, information-hiding), and
whether an invite bypasses `application`'s approval requirement
(recommended: no, it still queues a request).

**New: §1.9 Join requests** — `guild_join_requests` table; `REQUEST_JOIN`,
`LIST_JOIN_REQUESTS`, `APPROVE_JOIN_REQUEST`, `REJECT_JOIN_REQUEST` (the
exact four message names requested); a `JOIN_REQUEST_RECEIVED` officer
notification not explicitly requested but needed for officers to discover
new requests without polling; the two-different-payloads-to-two-different-
audiences architecture finding and its `ARBITRATION` (recommended: widen
handler dispatch to `std::vector<Message>`); the new `getFdsForUser`
`SessionManager` helper this requires; four new error codes.

**New: §1.10 Changing visibility** — `SET_GUILD_VISIBILITY`; an
`ARBITRATION` on pending-join-request handling when a guild leaves
`application` mode (recommended: delete them); the `Session.guild_ids`
hydration gap elevated from §3.4's soft, optional open question to a hard,
required prerequisite specifically for `private` guilds, with a concrete
(cheap) proposed fix — §3.4's own text is unmodified, this only
cross-references it.

**§2.1 (`LIST_MEMBERS`)** — example `role` value updated from `"owner"` to
`"captain"`; one sentence added confirming the response shape itself is
unchanged, only the value range widened.

**§2.2 (Roles)** — rewritten. Previously recommended staying binary; now
specifies the three-tier `CHECK` constraint (`captain`/`officer`/`crew`,
per your naming choice), a migration relabeling existing rows, an explicit
note that `owner_id`/`isOwner()`/`NOT_GUILD_OWNER` keep their existing
names (only the role *label* changes), and a predicate table covering
every `can*` check named in the request (`canCreateChannel`,
`canDeleteChannel`, `canCreateInvite`, plus the newly-needed
`canApproveJoinRequest`, `canSetMemberRole`, `canSetGuildVisibility`) with
a per-row tier recommendation and reasoning — two of which change from
what the previous version implied (`canCreateChannel` and `canCreateInvite`
widen to officer-or-above; `canDeleteChannel` deliberately does not widen,
flagged as its own `ARBITRATION` given message-history cascade-delete
stakes from §4.2).

**New: §2.3 note** — unchanged (still "fetch live", not cached).

**New: §2.4 Assigning roles** — `SET_MEMBER_ROLE`, not explicitly
requested but required for the `officer` tier to be reachable at all;
flagged as a gap-filling completion in the same style
`docs/guilds/design.md` already used for this kind of thing. An
`ARBITRATION` on who can promote/demote (recommended: captain-only).

**§5 (Security Checklist)**
- "Who can create invites?" bullet revised: officer-or-above, not
  owner-only.
- Added: who can approve/reject join requests (officer-or-above, given
  directly by the request).
- Added: who can change visibility (captain-only) — the specific question
  the request asked to confirm.
- Added: who can promote/demote members (captain-only).
- Added: what happens to pending join requests on a visibility change
  (deleted).
- Added: whether probing a private guild's id leaks its existence (no).
- Rate-limiting bullet extended to also cover `REQUEST_JOIN`.
- `/internal/*` isolation bullet extended to list the two new endpoint
  groups (`guild-join-requests`, `guilds/:id/visibility`).

**§6 (Implementation Order)** — two new steps inserted (the
`Session.guild_ids` hydration fix; the dispatcher `vector<Message>`
widening), step 2's description expanded for three tiers, and the former
step 4 ("Guild invites") expanded into "Guild invites, guild visibility,
and join requests," now including the custom invite-creation UI note.
Presence's step-4 content is byte-for-byte the same as the previous
version's step 3 — only its position in the numbered list changed, as a
direct, disclosed consequence of the two insertions ahead of it.

## What Changed — Rank-Based Role Model (this revision)

**Scope of this revision: §2 only.** §§1, 3, 4, and the previous delta
entry above are unmodified — nothing in them was rewritten, only a small
number of prose cross-references into §2 (e.g. §6 step 2's description)
were updated where they described §2's *previous* mechanism. §5's checklist
entries didn't need any change: they already only ever said "captain-only"/
"officer-or-above" as English shorthand, never as wire values, so they
remain accurate unchanged.

**§2.1 (`LIST_MEMBERS`)** — response shape changed: the single `role`
string field is replaced by two fields, `role_rank` (integer, for
permission-gated client UI) and `role_label` (string, pre-resolved for
display) — both requested explicitly, so the client never needs its own
copy of the rank→label mapping.

**§2.2 (Roles) — rewritten again.** Previously: a three-value string
`CHECK (role IN ('captain', 'officer', 'crew'))`, predicates comparing
string literals. Now:
- `guild_memberships.role` (string) replaced by `role_rank` (integer,
  `CHECK (role_rank >= 0)`, no upper bound or enumeration, deliberately —
  an enumerated `CHECK` would reintroduce the per-tier rework problem this
  model exists to avoid). Migration goes directly from today's real
  `role IN ('owner', 'member')` schema to `role_rank`, since the
  three-value string design was never implemented — no intermediate state
  to pass through.
- Named rank constants (`kMemberRank = 0`, `kOfficerRank = 1`,
  `kOwnerRank = 2`) replace the string literals; every predicate now
  compares against a constant via `isOfficerOrAbove`/`isOwner`-style
  helpers, never a literal — the specific mechanism the request asked for
  so a future tier addition only ever means adding constants, not touching
  predicate call sites.
- Predicate *tiers* (which action needs officer-or-above vs. owner-only)
  are **unchanged** from the previous revision — this was a mechanism
  change, not a re-litigation of who can do what. The predicate table's
  columns changed (string comparison → rank threshold) but its
  recommendations didn't.
- New: display labels as a decoupled, purely cosmetic layer —
  `guilds.role_theme` (defaults to the one built-in `'pirate'` theme), an
  `ARBITRATION` on where theme→label mappings live (recommended: a small
  static in-code registry for now, since exactly one theme exists;
  explicitly noted as a strict subset of a future `role_themes` table if
  data-driven themes are ever needed sooner), resolution happening once,
  server-side, in the same Next.js internal-API call that already resolves
  `username` — never in C++, and never consulted by any `can*` predicate.
  A fallback (`"Rank {n}"`) is specified for a `role_rank` with no theme
  entry, and an explicit non-goal note that no theme-*changing* message is
  proposed yet (nothing to select between with one theme).
- `guilds.owner_id`/`GuildManager::isOwner()`/`NOT_GUILD_OWNER` still keep
  their existing names, per the previous revision's own reasoning,
  restated here since it's still the governing decision. New note: their
  agreement with `role_rank` (the owner's row always being `kOwnerRank`) is
  maintained by construction, not DB-enforced — flagged, not silently
  assumed.

**§2.3** — updated to reflect that the live internal-API call now also
resolves `role_label` (not just `username`) alongside `role_rank`, and that
caching roster in `GuildManager` would additionally pull theme-resolution
logic into a service that otherwise never needs it — reinforces, doesn't
change, the existing "fetch live" recommendation.

**§2.4 (`SET_MEMBER_ROLE`)** — request field changed from `role` (string)
to `role_rank` (integer), for the same reason predicates never compare
against a label. Validation reworded from "must be `officer` or `crew`,
not `captain`" to "must be `>= 0` and `< kOwnerRank`" — a threshold that
doesn't need rewording when a tier is added below owner, unlike the
enumerated version it replaces. Response now carries `role_rank` and
`role_label` together, same reasoning as §2.1.

**Incidental fix, unrelated to the rank model**: §6 step 2's description
referenced "step 4" for where the roster/roles work needed to be sequenced
before; the actual current numbering (after the previous revision's
insertions) is step 6. Corrected while editing that paragraph for other
reasons — noted here rather than silently, since it's a change to §6, not
§2, and this revision was scoped to §2 only.
