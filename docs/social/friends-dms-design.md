# Friends, Blocking, Direct Messages, and Profiles — Design Document

**Status: design, not implemented.** This is a planning document only.
Nothing described here has been built. Do not start implementation against
it until it has been reviewed and the arbitration points flagged throughout
(search for **`ARBITRATION`**) have been resolved.

## Scope

Four interrelated features, none of which exist today in any form:

1. **Friend system** — friend requests (send/accept/reject/cancel), a
   shareable per-user friend code, a friends list, and remove-friend.
2. **Blocking** — block/unblock a user, and its enforcement across the
   other three features.
3. **Direct messages** — 1:1 conversations, gated by friendship or shared
   guild membership.
4. **Customizable profiles** — display name, bio, status message, accent
   color, custom avatar — plus a new Profile section in the settings modal
   (`docs/settings/appearance-design.md`).

This document treats them as one unit because §2 depends on §1's data model
(you can only unfriend someone you're friends with) and reaches into §3's
permission model directly, and §3 explicitly reuses §4 of
`docs/guilds/social-presence-design.md` rather than inventing new
persistence machinery. §4 (profiles) is the most independent of the four
but is included here because its settings-modal integration is a small,
natural fourth entry alongside the social features.

**Not addressed here, explicitly out of scope:**

- **Group DMs.** Everything in §3 is strictly 1:1. A group DM (N > 2
  participants, membership changes, etc.) is a reasonable future
  extension, but it doesn't fit `dm_conversations`' canonical-pair model
  (§3.4) without a real redesign — treat this document's DM design as
  the 1:1 foundation, not a stepping stone with the group case already
  half-built in.
- **A fully general, configurable permission system.** Unchanged from
  `docs/guilds/social-presence-design.md`'s own non-goal — nothing here
  proposes anything beyond the existing rank model plus the new
  friend/block/DM-participant checks this document adds.
- **Desktop client** (`desktop/`) — not the active development path per
  `CLAUDE.md`.
- **Symmetric (blocker-side) presence/profile suppression.** The request
  specifies one direction only — the blocked user can't see the blocker.
  Whether the blocker should *also* stop seeing the blocked user is a
  reasonable UX extension most consumer platforms do implement, but it
  isn't asked for here and isn't built — see §2.5's note. It would be a
  purely client-side addition later (filter the blocker's own locally-held
  roster/presence view by their own block list) requiring no protocol
  change, so deferring it costs nothing structural.
- **Banner as a second uploaded image asset.** "Accent color/banner" is
  read here as one setting: a single hex color used both for profile-card
  accenting and as a solid-color banner strip — not a second image upload
  alongside the avatar. See §4.1. Flagged as an interpretation, not one of
  the three arbitration points, so it isn't silently narrowed without
  saying so.

---

## 1. Friend System

### 1.1 What this feature is

A mutual, symmetric relationship between two users, established through an
explicit request/accept handshake — never unilateral. This is the one
place this document deliberately does **not** fully mirror the guild
invite precedent: an open guild's invite grants membership the instant
it's redeemed (§1.3 of `docs/guilds/social-presence-design.md`), because
joining a guild is an act the joiner takes about a group. Friendship is
different in kind — it's a claim about a specific *relationship* between
two named individuals, and one side unilaterally declaring it doesn't make
it true. So both the friend-code redemption path (§1.3) and the direct
`SEND_FRIEND_REQUEST` path (§1.5) always produce a pending request, never
instant friendship — the atomic, race-safe *mechanics* of invite
redemption are reused explicitly (§1.3); the "redemption grants the thing
directly" *outcome* is not.

### 1.2 Data model

```sql
-- Directed, pending only. A row here always means "requester has asked
-- recipient to be friends and is waiting."
CREATE TABLE friend_requests (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    requester_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    recipient_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (requester_id <> recipient_id),
    UNIQUE (requester_id, recipient_id)
);
CREATE INDEX idx_friend_requests_recipient ON friend_requests(recipient_id);

-- Symmetric, canonically ordered so a friendship is exactly one row
-- regardless of who sent the original request. user_id_a is always the
-- lexicographically smaller UUID — enforced by the CHECK, not left to
-- application discipline alone.
CREATE TABLE friendships (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id_a  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    user_id_b  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (user_id_a < user_id_b),
    UNIQUE (user_id_a, user_id_b)
);
CREATE INDEX idx_friendships_b ON friendships(user_id_b);
-- (user_id_a is already the UNIQUE constraint's leading column, so a
-- lookup "everyone user X is friends with" needs both this index and the
-- unique one to cover X appearing on either side of the pair.)

-- One column on users, not a separate table — a friend code is 1:1 with
-- an account, not a resource with its own lifecycle beyond "current value."
ALTER TABLE users ADD COLUMN friend_code TEXT UNIQUE;
```

`users.friend_code` is added nullable, backfilled for every existing row
via the same generator described in §1.3, then tightened to `NOT NULL` —
the standard safe-migration shape (add nullable → backfill → constrain)
since a portable SQL `DEFAULT` can't produce a `base64url`-encoded random
value the way `web/lib/internal/inviteCodes.ts`'s Node-side generator does.
New accounts get a code generated inline at creation (Discord OAuth
callback, alongside whatever other defaults are already set there).

No `friend_request_id` retained on the resulting `friendships` row, and no
audit/history table for past requests or removed friendships — same
reasoning as guild invites' "no per-redemption audit table" (§1.2 of the
guild doc): nothing in this request asks for a friendship history feature,
and it's a separable additive change later if needed.

### 1.3 Friend codes — reusing the invite redemption pattern

**Generation.** Cryptographically random, the exact same shape as
`generateInviteCode()` (`web/lib/internal/inviteCodes.ts`): 10 random
bytes, `base64url`-encoded, ≈80 bits of entropy, not derived from
`user_id` or any guessable sequence. A new `web/lib/internal/
friendCodes.ts` with a `generateFriendCode()` function that's
line-for-line the same as the invite generator (the two are allowed to
stay textually duplicated rather than sharing a helper — a friend code
colliding with the invite-code alphabet/length choice is a coincidence of
both wanting "opaque random token," not a reason to couple their futures
together).

**Redemption — `ADD_FRIEND_BY_CODE`.** This is where the invite pattern's
*mechanics* are reused explicitly, per the request: ordered validation
(code exists → not self → not blocked either direction → not already
friends), and the resulting mutation happens in the same transaction as
the lookup, exactly like `redeemInvite` (§1.5, `web/lib/internal/
invites.ts`). The difference from invite redemption (§1.1 above) is
entirely in *what* the transaction produces: not a membership row, a
`friend_requests` row (or, per §1.4's reverse-pending case, an immediate
`friendships` row) — `ADD_FRIEND_BY_CODE` resolves the code to a
`recipient_id` and then runs **exactly the same transaction body**
`SEND_FRIEND_REQUEST` does (§1.4); the only thing that differs between the
two entry points is how the target user is identified (by code vs. by
direct `user_id`). Implemented as one shared internal function with two
callers, not two parallel implementations that could drift.

**Regeneration.** `REGENERATE_FRIEND_CODE` overwrites the stored code with
a freshly generated one, immediately invalidating the old one (no grace
period, no `revoked_at` — unlike guild invites, a friend code has no
`use_count`/`max_uses` concept to make a distinction between "revoked" and
"replaced" meaningful; there's exactly one live code per user at a time).
A hygiene affordance mirroring `REVOKE_INVITE`'s spirit for a user who
posted their code somewhere they regret.

### 1.4 Lifecycle

Validation order for `SEND_FRIEND_REQUEST` (client → server, target by
`user_id`) and `ADD_FRIEND_BY_CODE` (target resolved from `code` first,
then identical from here):

1. Target exists — `USER_NOT_FOUND` otherwise.
2. Target is not the caller — `PROTOCOL_VIOLATION` (self-request), mirrors
   the guild doc's convention of reusing this code for "the action doesn't
   make sense against yourself."
3. Blocked either direction (caller blocked target, or target blocked
   caller) — **same `USER_NOT_FOUND`** the "target doesn't exist" case
   uses, not a distinct code. See §2.6's `ARBITRATION` — this is the
   friend-request-specific instance of that same recommendation, applied
   consistently.
4. Already friends — `PROTOCOL_VIOLATION`.
5. **A pending request already exists in the *reverse* direction** (target
   already sent the caller a request) — this is not an error. Auto-accept:
   delete the reverse `friend_requests` row and insert the `friendships`
   row (canonically ordered) in the same transaction, exactly as if
   `ACCEPT_FRIEND_REQUEST` had been called. Mirrors a pattern most
   consumer platforms already use (sending a request to someone who
   already requested you just completes the friendship instead of leaving
   two crossed pending requests sitting there) and avoids a genuinely
   confusing UI state (two simultaneous pending requests between the same
   pair, in opposite directions, that a client would have to reconcile).
6. Otherwise: insert a new `friend_requests` row (`onConflictDoNothing` on
   `(requester_id, recipient_id)` for idempotency against a double-send
   race, mirroring `guildJoinRequests`' `onConflictDoNothing` usage).

`ACCEPT_FRIEND_REQUEST` (client → server, identifies the request by the
original `requester_id`): validates a pending row `(requester_id,
recipient=caller)` exists (`FRIEND_REQUEST_NOT_FOUND` otherwise); same
transaction as step 5 above (delete request, insert friendship).

`REJECT_FRIEND_REQUEST` / `CANCEL_FRIEND_REQUEST`: both just delete the
`friend_requests` row (as recipient or as requester respectively —
`FRIEND_REQUEST_NOT_FOUND` if the caller isn't the correct party for the
row they're referencing, same as `REVOKE_INVITE` scoping revocation to the
invite's own guild).

`REMOVE_FRIEND`: deletes the `friendships` row for the canonical pair.
`FRIEND_NOT_FOUND` if no such row exists — mirrors `revokeInvite`'s
boolean-existence check, just with a dedicated error code since removing a
friend (unlike revoking an invite) has no natural "already revoked, no-op
success" reading.

### 1.5 Protocol messages

All require the connection to be identified first.

**`SEND_FRIEND_REQUEST`**: `{ "type": "SEND_FRIEND_REQUEST", "user_id": "u_2" }`.
On success (new pending request), two messages, mirroring the guild doc's
"a single client action can trigger more than one outgoing message"
pattern (`JOIN_REQUEST_RECEIVED` alongside the direct confirmation):

- `Scope::DIRECT` to the caller: `{ "type": "FRIEND_REQUEST_SENT", "user_id": "u_2" }`
- `Scope::TARGETED` to every connection identified as the recipient
  (`getFdsForUser`): `{ "type": "FRIEND_REQUEST_RECEIVED", "user_id": "u_1" }`

On the auto-accept path (§1.4 step 5), both sides instead get the
`FRIEND_ADDED` pair described below.

**`ACCEPT_FRIEND_REQUEST`**: `{ "type": "ACCEPT_FRIEND_REQUEST", "user_id": "u_1" }`
(the original requester's id). Two `Scope::TARGETED` messages, one per
participant, same shape from each side's own perspective — mirrors "the
approval-path copies of `GUILD_JOINED`" the guild doc's Behavior Notes
already describe for `APPROVE_JOIN_REQUEST`:

- to the original requester's connections: `{ "type": "FRIEND_ADDED", "user_id": "u_2" }`
- to the acceptor's connections: `{ "type": "FRIEND_ADDED", "user_id": "u_1" }`

**`REJECT_FRIEND_REQUEST`**: `{ "type": "REJECT_FRIEND_REQUEST", "user_id": "u_1" }`.
`Scope::TARGETED` to the original requester (mirrors `JOIN_REQUEST_REJECTED`
being delivered to the requester, not just confirmed to the rejecter):
`{ "type": "FRIEND_REQUEST_REJECTED", "user_id": "u_2" }`.

**`CANCEL_FRIEND_REQUEST`**: `{ "type": "CANCEL_FRIEND_REQUEST", "user_id": "u_2" }`.
`Scope::TARGETED` to both parties (the recipient's pending-requests view
needs to drop the now-canceled entry too):
`{ "type": "FRIEND_REQUEST_CANCELED", "user_id": "<other party>" }` to each.

**`REMOVE_FRIEND`**: `{ "type": "REMOVE_FRIEND", "user_id": "u_2" }`.
`Scope::TARGETED` to both participants: `{ "type": "FRIEND_REMOVED", "user_id": "<other party>" }`.

**`LIST_FRIENDS`** → `Scope::DIRECT`:
```json
{ "type": "FRIEND_LIST", "friends": [ { "user_id": "u_2", "username": "web_user" } ] }
```
Deliberately no embedded online/offline status — same reasoning
`PRESENCE_UPDATE`'s docs already state: the client intersects this list
against the globally-received `PRESENCE_UPDATE` stream locally, rather
than the server duplicating presence logic into this response.

**`LIST_FRIEND_REQUESTS`** → `Scope::DIRECT`:
```json
{ "type": "FRIEND_REQUEST_LIST", "incoming": [ { "user_id": "u_3", "created_at": "..." } ], "outgoing": [] }
```

**`FETCH_FRIEND_CODE`** / **`REGENERATE_FRIEND_CODE`** → `Scope::DIRECT`:
`{ "type": "FRIEND_CODE", "code": "aB3kD9qP2x" }`.

**`ADD_FRIEND_BY_CODE`**: `{ "type": "ADD_FRIEND_BY_CODE", "code": "aB3kD9qP2x" }`.
Response shape is whichever of `FRIEND_REQUEST_SENT`/`FRIEND_REQUEST_RECEIVED`
or the `FRIEND_ADDED` pair applies, exactly as `SEND_FRIEND_REQUEST` (§1.4).

New error codes: `USER_NOT_FOUND`, `FRIEND_REQUEST_NOT_FOUND`,
`FRIEND_NOT_FOUND`, `FRIEND_CODE_NOT_FOUND`. `USER_NOT_FOUND` is
introduced here as a general-purpose code — §2 (blocking) and §3 (DMs)
both reuse it rather than each minting their own "no such user" code.

### 1.6 Internal API additions

- `POST /internal/friend-requests` — body `{ requesterId, recipientId }`
  (or `{ requesterId, code }` for the code path — same handler, §1.3).
  Runs the full §1.4 transaction (including the auto-accept branch) and
  returns a discriminated result (`{ ok: true, kind: "request" | "friends", ... }`
  or `{ ok: false, error: ... }`), same "always 200, discriminate on body"
  convention `POST /internal/guild-invites/:code/redeem` already
  established.
- `POST /internal/friend-requests/:requesterId/accept` — body `{ recipientId }`.
- `DELETE /internal/friend-requests/:requesterId` — body `{ recipientId, actor: "requester" | "recipient" }`
  (distinguishes cancel from reject for the differing notification target
  in §1.5, even though the row deletion itself is identical).
- `DELETE /internal/friendships/:userId` — body `{ otherUserId }`.
- `GET /internal/friendships?userId=` — for `LIST_FRIENDS`.
- `GET /internal/friend-requests?userId=` — for `LIST_FRIEND_REQUESTS`,
  returning both directions in one call.
- `GET /internal/friend-codes/:code` / `POST /internal/friend-codes/:userId/regenerate`.

Every route above rides on the same `/internal/*` isolation
(`isAuthorizedInternalRequest`, network-unreachable from the browser) as
every existing internal route.

---

## 2. Blocking

### 2.1 What blocking does

A one-directional relationship (`blocker` → `blocked`) that, the moment
it's created, atomically:

1. Cancels any pending `friend_requests` row between the two users, in
   either direction.
2. Removes an existing `friendships` row between the two, if one exists.
3. From then on: the blocked user cannot send the blocker a new friend
   request (§2.3), cannot start or continue a DM with the blocker under
   any permission path including shared guild membership (§2.4), and
   stops receiving the blocker's `PRESENCE_UPDATE`s and profile data
   (§2.5).

Blocking is **not** guild-scoped and has no effect on existing guild
membership — a blocked user can still be in the same guild as the
blocker, see them in `LIST_MEMBERS`, and see their guild messages. This
document doesn't extend blocking into guild-message visibility; doing so
would need a much larger per-message filtering mechanism guild messaging
doesn't have today (`CHAT_MESSAGE`/`CHANNEL_MESSAGE` are plain
`BROADCAST`/`TARGETED`-to-guild, not per-recipient-filtered), and nothing
in the request asks for it.

### 2.2 Data model

```sql
-- Directed, not symmetric — A blocking B says nothing about whether B
-- has blocked A.
CREATE TABLE user_blocks (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    blocker_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    blocked_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (blocker_id <> blocked_id),
    UNIQUE (blocker_id, blocked_id)
);
CREATE INDEX idx_user_blocks_blocked ON user_blocks(blocked_id);
```

### 2.3 `BLOCK_USER` transaction

Same same-transaction discipline as everywhere else in this document:

```sql
BEGIN;
INSERT INTO user_blocks (blocker_id, blocked_id) VALUES (:blocker, :blocked)
  ON CONFLICT (blocker_id, blocked_id) DO NOTHING;
DELETE FROM friend_requests
  WHERE (requester_id = :blocker AND recipient_id = :blocked)
     OR (requester_id = :blocked AND recipient_id = :blocker);
DELETE FROM friendships
  WHERE (user_id_a, user_id_b) = (LEAST(:blocker, :blocked), GREATEST(:blocker, :blocked));
COMMIT;
```

Blocking an already-blocked user is idempotent success (`ON CONFLICT DO
NOTHING`), not an error — blocking is a state-setting action, not a strict
transition, and there's no useful distinction between "already blocked"
and "just blocked" for the caller to react to.

### 2.4 Interaction with friend requests and DMs

Covered precisely by §1.4 step 3 (friend requests) and §3.2 (DMs) — both
check `user_blocks` in both directions before allowing the respective
action, and both fail the same way `USER_NOT_FOUND`/`DM_NOT_PERMITTED`
would fail for an unrelated legitimate reason (§2.6).

### 2.5 Interaction with presence and profile visibility

**Presence.** `PRESENCE_UPDATE` today is pure `Scope::BROADCAST` — every
connected client gets every transition, with no per-recipient filtering
mechanism at all (`shared/protocol/README.md`'s "there is no per-guild-
scoped variant" note). Honoring "a blocked user cannot see the blocker's
presence" requires this specific emission to become **exclusionary
`Scope::TARGETED`**: when user `U`'s connection count crosses `0↔1`, the
resulting `PRESENCE_UPDATE` is delivered to every currently-identified
connection *except* those identified as a user `U` has blocked. This is a
real, new capability `SessionManager` doesn't have today — enumerating
"every identified fd" (today's `BROADCAST` path likely doesn't even need
this, since `Scope::BROADCAST` at the `Server.cpp` level probably just
means "every open connection," identified or not) minus a per-user
exclusion set. Flagged explicitly as required new surface area, not a
free extension of existing plumbing.

Computing the exclusion set requires the C++ server to know, synchronously
and without a live DB round trip on every presence transition, which users
`U` has blocked. See §3.3's `blocked_user_ids` cache — the same
in-memory, IDENTIFY-hydrated-and-live-updated cache that DM permission
re-checking needs is exactly what presence filtering needs too; this
document proposes building it once and using it for both.

**Profile.** Covered in §4.5 — a blocked caller's profile fetch returns
the same "not found" shape a nonexistent `user_id` would, checked live
inside the same internal API call (profile reads are never cached, so
this needs no C++-side state at all, unlike presence).

**Scope note**: this is exactly the one direction the request specifies
— the blocked user loses visibility into the blocker, not the reverse.
See the Scope section's note on why full symmetry isn't built here.

### 2.6 `ARBITRATION`: silent failure vs. explicit error

When a blocked user tries to reach the blocker (friend request, DM, or —
structurally — a presence/profile lookup), does the system tell them
they're blocked, or fail in a way indistinguishable from an unrelated,
ordinary failure?

- **Explicit (`USER_BLOCKED`-style error code)**: immediately informative
  to a legitimate client that hit an unexpected wall — "why can't I friend
  this person" has a clear answer instead of a dead end. Costs nothing
  extra to implement (it's still just an error response).
- **Silent (reuse an existing, unrelated-looking error/response shape)**:
  a blocked user gets `USER_NOT_FOUND` sending a friend request (same as a
  typo'd or deleted `user_id`), `DM_NOT_PERMITTED` sending a DM (same as
  an ordinary stranger with no shared context), and simply never receives
  the blocker's `PRESENCE_UPDATE`/profile data (no signal at all, the same
  as if the blocker were just never online). No `BLOCKED` code exists
  anywhere in the protocol.

**Recommendation: silent**, reusing an existing error shape at every
touchpoint rather than introducing any `BLOCKED`/`USER_BLOCKED` code.
This is the same reasoning `docs/guilds/social-presence-design.md` §1.8
already applied to `private`-guild probing (`GUILD_NOT_FOUND` reused
instead of a distinguishing `GUILD_PRIVATE` code) — the request itself
names this precedent explicitly ("consistent with how most consumer
platforms handle this to avoid harassment feedback loops"), and the
mechanism is identical: if "blocked" and "doesn't apply to you for an
ordinary reason" are indistinguishable on the wire, someone can't probe a
list of `user_id`s to enumerate who's blocked them, and can't use a
`BLOCKED`-vs-not divergence as a signal to escalate a specific person
(exactly the feedback loop the "silent" option exists to prevent).
**This is presented as a recommendation, not a foregone conclusion** —
flagged per instruction as a genuine judgment call for review, matching
how §1.8's `GUILD_NOT_FOUND` choice was itself flagged as a call rather
than assumed.

### 2.7 Protocol messages

**`BLOCK_USER`**: `{ "type": "BLOCK_USER", "user_id": "u_2" }`. Validation:
target exists (`USER_NOT_FOUND`), target is not self (`PROTOCOL_VIOLATION`).
`Scope::DIRECT` only, to the blocker: `{ "type": "USER_BLOCKED", "user_id": "u_2" }`.
**No notification to the target** — consistent with §2.6's silent-failure
reasoning applied to the block action itself, not just its downstream
effects. The blocked user simply starts silently failing to reach the
blocker from this point on.

**`UNBLOCK_USER`**: `{ "type": "UNBLOCK_USER", "user_id": "u_2" }`.
`Scope::DIRECT`: `{ "type": "USER_UNBLOCKED", "user_id": "u_2" }`. Also
silent to the target — unblocking doesn't restore a prior friendship or
pending request; it just removes the block, leaving both users to
re-friend from scratch if they want to.

**`LIST_BLOCKS`** → `Scope::DIRECT`:
```json
{ "type": "BLOCK_LIST", "blocked": [ { "user_id": "u_3", "username": "web_user", "blocked_at": "..." } ] }
```

No new error codes beyond the reused `USER_NOT_FOUND`/`PROTOCOL_VIOLATION`.

### 2.8 Internal API additions

- `POST /internal/blocks` — body `{ blockerId, blockedId }`. Runs §2.3's
  transaction.
- `DELETE /internal/blocks/:blockerId/:blockedId`.
- `GET /internal/blocks?blockerId=` — for `LIST_BLOCKS` and for hydrating
  `blocked_user_ids` at `IDENTIFY` (§3.3).

`BLOCK_USER`/`UNBLOCK_USER`'s handlers are also where the C++-side
`blocked_user_ids` cache (§3.3) gets updated live, in addition to the
internal API write — mirrors how a protocol action that changes guild
membership updates `Session.guild_ids` immediately rather than waiting
for a future reconnect to pick it up.

---

## 3. Direct Messages

### 3.1 Scope

Strictly 1:1. There is no `dm_conversation` with more than two
participants — see the Scope section's note on group DMs as a future,
separately-designed extension.

### 3.2 Permission model

A DM may be **sent** if the two users are friends **or** share at least
one guild membership, **and** neither has blocked the other:

```
canSendDm(a, b) := (areFriends(a, b) || shareAnyGuild(a, b)) && !isBlockedEitherDirection(a, b)
```

### 3.3 `ARBITRATION`: checked at conversation-start only, or re-checked on every send?

- **Start-time only**: `canSendDm` is checked once, when the first message
  in a conversation is sent (or an explicit "start conversation" action,
  if one existed — it doesn't, see §3.4). Every subsequent `DM_SEND` in
  that conversation is unconditional. Simple, and matches the intuition
  that a DM thread, once legitimately opened, is a standing relationship
  the two people chose to have.
- **Re-checked on every send**: `canSendDm` is evaluated fresh on every
  single `DM_SEND`. A conversation that was legitimately opened while the
  two users shared a guild goes silently inert (new sends fail, existing
  history stays readable) the moment neither condition holds anymore —
  e.g., the shared guild is left and the two never became friends.

**Recommendation: re-check on every send.** This is the specific
precedent the request names directly: rank checks (`role_rank`) and
session revocation are both re-evaluated on every action that depends on
them, not cached from the moment a session/membership was established —
authorization in this codebase is consistently "true right now," not
"was true when this started." A DM thread inheriting a one-time-checked,
permanently-standing permission would be the only authorization boundary
in the whole protocol that worked that way. Existing history remains
fully readable either way (§3.4's `FETCH_HISTORY` requires only
conversation participancy, never `canSendDm`) — only new sends are gated,
so re-checking costs a real user nothing except the ability to keep
*adding* messages to a thread whose shared context has fully lapsed.
Flagged as a recommendation, not a foregone conclusion, per instruction.

**Making the re-check actually cheap.** For this recommendation to be
consistent with the precedent it cites — not just nominally re-checking,
but re-checking the way rank/revocation checks do, which are both
in-memory, synchronous, zero-round-trip checks — `canSendDm` needs to be
answerable without a live Postgres query on every `DM_SEND`. Shared-guild
membership already is: `Session.guild_ids` is hydrated at `IDENTIFY` and
kept live (`docs/guilds/social-presence-design.md` §1.10's "Required
fix"). This document proposes the same treatment for the other two
inputs:

- **`friend_ids`**: a per-session set, hydrated at `IDENTIFY` from
  `GET /internal/friendships?userId=` (§1.6), updated live whenever
  `FRIEND_ADDED`/`FRIEND_REMOVED` fires for that session (§1.5).
- **`blocked_user_ids`**: a per-session set, hydrated at `IDENTIFY` from
  `GET /internal/blocks?blockerId=` (§2.8), updated live on
  `BLOCK_USER`/`UNBLOCK_USER` (§2.8).

With both cached, `canSendDm` for a given `DM_SEND` becomes three
in-memory set lookups (`friend_ids.contains`, `guild_ids` intersection
against the peer's own cached guild set, and `blocked_user_ids.contains`
in both directions) plus, for the reverse block direction (does the
*peer* have the caller blocked): if the peer has at least one active
connection, this is answered directly from the peer's own in-memory
`blocked_user_ids` — an `IDENTIFY`-hydrated session on the same server
process, no cross-process lookup involved, since presence tracking and DM
handling both live inside the same C++ process. If the peer has **zero
active connections**, there is no in-memory session state to consult for
them, and this one leg of the check falls back to a single live call,
`GET /internal/blocks?blockerId=<peer>` (§2.8), checked for the caller's
`user_id` in the result before the send is allowed.

**Resolved: live fallback for a disconnected peer, not a process-wide
index.** The alternative — mirroring every session's `blocked_user_ids`
into a second, process-wide `user_id → blocked_by` index kept live
independent of any particular connection — was considered and rejected.
Covering a disconnected peer that way means permanently maintaining
global in-memory state populated from *every* user who has ever blocked
anyone, not just currently-connected users, for the sake of a check that
only matters on the minority of `DM_SEND`s where the recipient happens to
be offline at that exact moment — most DM traffic is between two users
both actively using the app, i.e. both connected, i.e. already covered by
the ordinary two-sided in-memory path with no fallback needed at all.
Paying a permanent, always-hydrated global data structure to shave a
rare-case query down to an in-memory lookup doesn't earn its cost here.
The live-fallback query fires only when it's actually needed (recipient
offline), which keeps `DM_SEND`'s common case — recipient online — fully
in-memory and zero-round-trip, the thing the re-check-on-every-send
recommendation above actually depends on holding up; the rare
offline-recipient case paying one Postgres round trip on that one send is
a fine trade, not a regression against the "cheap like rank/revocation
checks" bar — rank and revocation checks don't have an analogous "the
other party might not be present" wrinkle to begin with, so this fallback
isn't papering over a gap in that comparison, it's simply the one place
DM authorization is structurally different from either precedent.

### 3.4 Data model — extending message persistence, not reinventing it

Per the request, this reuses `docs/guilds/social-presence-design.md` §4's
architecture directly: one `messages` table, server-assigned `seq`,
fire-and-forget persistence with bounded retry, keyset-paginated history.
A DM conversation becomes a **third** message scope alongside lobby
(`channel_id IS NULL`, no DM) and channel (`channel_id` set):

```sql
-- Canonically ordered, exactly like friendships (§1.2) — a DM conversation
-- between two users is one row regardless of who sent the first message.
CREATE TABLE dm_conversations (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id_a  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    user_id_b  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    CHECK (user_id_a < user_id_b),
    UNIQUE (user_id_a, user_id_b)
);

ALTER TABLE messages
  ADD COLUMN dm_conversation_id UUID REFERENCES dm_conversations(id) ON DELETE CASCADE,
  ADD CONSTRAINT messages_at_most_one_scope
    CHECK (NOT (channel_id IS NOT NULL AND dm_conversation_id IS NOT NULL));
-- Three mutually exclusive scopes: both NULL = lobby, channel_id set =
-- channel, dm_conversation_id set = DM. Never both.

CREATE UNIQUE INDEX idx_messages_dm_seq ON messages(seq) WHERE dm_conversation_id IS NOT NULL;
CREATE INDEX idx_messages_dm_created ON messages(dm_conversation_id, seq) WHERE dm_conversation_id IS NOT NULL;
```

**A third, shared `seq` id-space**, mirroring the channel id-space's own
shape exactly: `docs/guilds/social-presence-design.md` §4.2's comment is
explicit that the channel counter is "one shared across every channel
(not per-channel)," not one counter per channel. This document proposes
the same for DMs — one `dm_message_counter_` in the C++ server, shared
across every DM conversation, not one per conversation-pair — rather than
inventing a new "per-entity counter" shape nothing else in the codebase
uses. Seeded at startup the same way, via a new
`GET /internal/messages/last-sequence?scope=dm` call alongside the
existing lobby/channel variants.

**Conversations are never addressed by id on the wire.** `DM_SEND` and
`FETCH_HISTORY`'s DM branch (§3.5) both take the *peer's* `user_id`, never
a `dm_conversation_id` — the client shouldn't need to learn or cache an
internal conversation id just to send a message to someone it already
knows the `user_id` of. The server resolves (or creates, `ON CONFLICT DO
NOTHING`, canonically ordered) the `dm_conversations` row internally on
every `DM_SEND`/history fetch. This keeps `dm_conversation_id` a pure
internal/Postgres implementation detail, the same way `seq` itself is
never client-supplied.

### 3.5 Protocol messages

**`DM_SEND`** (client → server): `{ "type": "DM_SEND", "user_id": "u_2", "content": "hello" }`.

Validation: identified (`NOT_IDENTIFIED`); `content` non-empty, ≤500 chars
(mirrors `CHAT_MESSAGE` exactly); target is not self (`PROTOCOL_VIOLATION`);
`canSendDm(caller, target)` (§3.2) — `DM_NOT_PERMITTED` on failure,
whether the real reason is "no shared context," "blocked," or a
nonexistent target (§2.6).

**Revised at implementation**: no separate "target exists" check
(`USER_NOT_FOUND`) — an earlier draft of this section listed one, but a
nonexistent `user_id` already fails both of `canSendDm`'s checks on its
own (it's in no one's `friend_ids`, and `fetchGuildIdsForUser`/session
lookups for it come back empty), so it already produces `DM_NOT_PERMITTED`
with zero extra work. Adding a dedicated existence check would mean a
mandatory live internal-API call on **every** `DM_SEND`, even the common
case (peer online, everything else already in-memory) — directly
undermining §3.3's "make the recheck actually cheap" resolution, the
entire reason `friend_ids`/`blocked_user_ids` caches and the live-fallback
design exist in the first place. This is also arguably more consistent
with §2.6's silent-failure spirit than the original draft was: a
nonexistent target and a blocked one become fully indistinguishable, not
just a blocked one.

On success: server resolves/creates the conversation (§3.4), assigns
`seq` from the shared DM counter, and delivers, `Scope::TARGETED`, to
every connection identified as **either** participant
(`getFdsForUser(sender) ∪ getFdsForUser(recipient)` — covers each side's
multiple tabs/devices):

```json
{ "type": "DM_MESSAGE", "message_id": 7, "timestamp": 1741104000, "user_id": "u_1", "content": "hello" }
```

Deliberately the same shape `CHAT_MESSAGE`'s broadcast already uses (no
extra `recipient_id`/`conversation_id` field) — a receiving client
determines the peer for routing to the right thread exactly the way it
already determines "is this my own echo or someone else's" for
`CHAT_MESSAGE`: compare `user_id` to its own identified id; whichever it
isn't is the conversation this belongs to. No new client-side concept
needed beyond what `CHAT_MESSAGE` handling already does.

Persisted fire-and-forget with bounded retry — this reuses §4.5's Option
B recommendation directly rather than re-litigating the tradeoff; nothing
about DMs changes the reasoning that already won for lobby/channel
messages.

**`FETCH_HISTORY`, extended**: gains an optional `peer_id` field,
mutually exclusive with `channel_id` (send at most one; both omitted still
means lobby, unchanged):

```json
{ "type": "FETCH_HISTORY", "peer_id": "u_2", "before_seq": null, "limit": 50 }
```

Validation: identified only. **No `canSendDm` check and, per the same
implementation-time revision as `DM_SEND` above, no separate "target
exists" check either** — history reads never depend on the send-permission
being currently true, only on `dm_conversation_id` resolving to a
conversation the caller is actually `user_id_a`/`user_id_b` of (which is
automatic — the resolution in §3.4 is keyed by the caller's own id, so
there's no path for a caller to read a conversation they aren't part of).
A `peer_id` with no conversation history at all — whether because none
has been sent yet, or because the id doesn't correspond to a real account
— just yields an empty page (`has_more: false, messages: []`), the same
"empty is a valid, distinct state" reasoning `listInvites` already uses,
not an error. Skipping the existence check here costs nothing extra in
the "genuinely no history" case either way, and avoids a live call this
read-only path has no other reason to make.

Response, extended with a `peer_id` echo alongside the existing
`channel_id` (exactly one of the two is non-null, matching the request):

```json
{
  "type": "MESSAGE_HISTORY",
  "channel_id": null,
  "peer_id": "u_2",
  "messages": [ { "message_id": 6, "timestamp": 1741104000, "user_id": "u_2", "content": "hi" } ],
  "has_more": false
}
```

**`LIST_DM_CONVERSATIONS`** (client → server, no fields) → `Scope::DIRECT`:

```json
{ "type": "DM_CONVERSATION_LIST", "conversations": [ { "peer_id": "u_2", "last_message_at": "..." } ] }
```

Minimal by design — no unread counts or message previews, neither of
which was asked for and both of which would need real additional state
(a per-participant read-cursor) this document doesn't otherwise need.

New error codes: `DM_NOT_PERMITTED` (reused for both "no shared context"
and "blocked," per §2.6). `USER_NOT_FOUND` is reused from §1.

### 3.6 Internal API additions

- `POST /internal/dm-conversations/resolve` — body `{ userIdA, userIdB }`
  (order-independent input). Get-or-create, canonically ordered, returns
  the `dm_conversation_id`. Called by both `DM_SEND` and `FETCH_HISTORY`'s
  DM branch before touching `/internal/messages`.
- `GET /internal/dm-conversations?userId=` — for `LIST_DM_CONVERSATIONS`.
- `POST /internal/messages` and `GET /internal/messages?...` (both already
  exist, §4.2/§4.4 of the guild doc) gain an optional `dmConversationId`
  parameter alongside the existing `channelId`, rather than a parallel
  `/internal/dm-messages` route pair — same underlying table, same
  read-through/write pattern, only the scoping column differs.
- `GET /internal/messages/last-sequence?scope=dm` — the third seed-value
  variant (§3.4).

---

## 4. Customizable Profiles + Settings

### 4.1 Data model

```sql
ALTER TABLE users
  ADD COLUMN display_name TEXT CHECK (display_name IS NULL OR char_length(display_name) BETWEEN 1 AND 32),
  ADD COLUMN bio TEXT CHECK (bio IS NULL OR char_length(bio) <= 300),
  ADD COLUMN status_message TEXT CHECK (status_message IS NULL OR char_length(status_message) <= 100),
  ADD COLUMN accent_color TEXT CHECK (accent_color IS NULL OR accent_color ~ '^#[0-9a-fA-F]{6}$'),
  ADD COLUMN custom_avatar_path TEXT;
```

All nullable, no `DEFAULT` — every existing row keeps working unchanged,
same "`NULL` means never customized" reasoning `theme`/`theme_sync_enabled`
already established (§3.4 of the appearance doc, cited directly in
`users.ts`'s own comment). Resolution order for what a client actually
renders:

- **Display name**: `display_name ?? discord_global_name ?? discord_username`.
- **Avatar**: `custom_avatar_path` if set, else whatever Discord-avatar URL
  construction the client already performs today from `discord_id`/
  `discord_avatar_hash`.
- **Bio / status message / accent color**: no fallback chain needed —
  `NULL` renders as "not set" (empty bio, no status, default UI accent).

`display_name` is capped at 32 chars (roughly matching a normal username
length) and, unlike `discord_username`, has no uniqueness constraint —
it's cosmetic, not an identifier; `user_id` and `discord_id` remain the
only things anything keys lookups off of.

### 4.2 `ARBITRATION`: avatar storage

A custom avatar needs actual file storage. Nothing in this stack has that
today — `users.discord_avatar_hash` is just a hash Discord's own CDN
resolves, never a file this app stores itself.

- **Local disk, bind-mounted volume.** A new docker-compose volume, e.g.
  `./data/avatars:/app/data/avatars`, mounted **read-write** (unlike the
  existing `./secrets/*.pem:...:ro` mounts — this volume needs to be
  written to at runtime). Served by a small dynamic route
  (`web/app/uploads/avatars/[filename]/route.ts`) that reads the file
  from disk and returns it with the right `Content-Type` on every
  request, **not** by Next.js's built-in `public/` static-file handler —
  an earlier draft of this section assumed the latter (a bind-mounted
  subdirectory of `public/`, on the theory that Next's static handler
  reads `public/` straight from disk per request), and that assumption
  was wrong, confirmed by actually uploading a file against a running
  instance and getting a `404`: this stack's production server
  (`server.mjs`) indexes `public/` once at process startup, so a file
  written after boot — which is every avatar upload, by definition —
  never becomes servable that way, no matter how long the process keeps
  running. A pre-existing build-time `public/` asset (e.g.
  `/branding/icon.svg`) served fine from the same running container in
  the same test, isolating the problem to runtime-written files
  specifically, not a mount or permissions issue. The dynamic-route fix
  keeps every other part of this recommendation intact (local disk, no
  cloud dependency, same bind-mount shape as the secrets mounts) — only
  "which handler serves the bytes" changes.
- **Cloud object storage (S3-compatible: S3, R2, MinIO, ...).** The
  standard production answer — durable independent of the container
  filesystem, no volume-permission questions, scales past a single host
  trivially. Introduces a new external dependency and a new credential
  (`AVATAR_STORAGE_*` secrets) into a stack that is currently fully
  self-hosted via Docker Compose with zero cloud dependencies anywhere
  else in it (Postgres and Redis are both containers in the same compose
  file).

**Recommendation: local disk**, consistent with the deployment model
every other part of this stack already commits to — nothing else in
`docker-compose.yml` reaches outside the compose network, and an avatar
upload feature is not a strong enough reason to be the first thing that
does. This is presented as a real infrastructure decision, not a given —
if this deployment is ever expected to run multi-host (multiple `web`
replicas behind a load balancer, no shared filesystem), local disk stops
working entirely (an avatar uploaded via one replica wouldn't be visible
through another), and cloud storage becomes the only correct answer, not
just the more scalable one. Flagged explicitly so that decision isn't
silently foreclosed by picking local disk now.

### 4.3 Settings section

Extends `SETTINGS_SECTIONS` (`web/lib/settings/sections.ts`,
`docs/settings/appearance-design.md` §1.2) exactly as designed — one new
entry, no change to the modal shell:

```ts
export const SETTINGS_SECTIONS: SettingsSection[] = [
  { id: "appearance", label: "Appearance", Component: AppearanceSettings },
  { id: "profile", label: "Profile", Component: ProfileSettings }
];
```

`ProfileSettings` (`web/components/settings/ProfileSettings.tsx`, new)
holds: display name / bio / status message text inputs, an accent color
picker, and an avatar upload control with a preview — form fields for
every column in §4.1, plus a "reset to Discord avatar" action (clears
`custom_avatar_path`).

### 4.4 REST additions

Profile data needs no new WebSocket message types at all — see §4.5 for
why. It's pure account data, the same category `theme`/`theme_sync_enabled`
already are, so it follows `PATCH /api/user/appearance`'s exact pattern:
`__session`-refresh-cookie authenticated, target resolved only from the
caller's own session (never a client-supplied `user_id`).

- **`PATCH /api/user/profile`** — body `{ display_name?, bio?,
  status_message?, accent_color? }`, each field optional and independently
  validated against §4.1's constraints, mirroring `PATCH /api/user/
  appearance`'s per-field validation loop exactly.
- **`POST /api/user/profile/avatar`** — `multipart/form-data`, one file
  field. The first file-upload route in this codebase — needs its own
  validation the appearance route never had to think about (§5).
- **`DELETE /api/user/profile/avatar`** — clears `custom_avatar_path`
  (reverts to the Discord avatar), deletes the now-orphaned file from
  disk.
- **`GET /api/users/:id/profile`** — viewing *another* user's profile
  (the "view profile" UI action). Also `__session`-cookie authenticated
  (needs to know the caller's own identity, not just the target's), since
  the block check (§2.5/§2.6) is "has the target blocked the caller,"
  checked live in the same query — a nonexistent target and a target who
  has blocked the caller both yield the same `404`, per §2.6.

### 4.5 Where profile data reaches the C++ server

**It doesn't need to, as new push data — the existing internal API
queries that already resolve `username` live are extended to also select
the new columns.** This was checked directly, not assumed: `username` in
every roster/message/join-request response
(`web/lib/internal/catalog.ts:234`, `web/lib/internal/messages.ts:100`,
`web/lib/internal/joinRequests.ts:56`) is already a **live join against
`users`** at internal-API response-build time, not a value cached inside
`GuildManager`'s write-through catalog cache (that cache holds guild/
channel/membership *structure* — ids, `role_rank` — not user identity
fields). So extending those same three queries to additionally select
`display_name`, `custom_avatar_path`, and resolve the fallback chain from
§4.1 introduces **zero new cache-invalidation surface** — profile updates
already "just show up" the next time any of these live-joined queries
runs, the same way a `discord_username` change already would.

Concretely: `LIST_MEMBERS`, `CHAT_MESSAGE`, `CHANNEL_MESSAGE`, `DM_MESSAGE`,
and the friend/join-request list entries all gain optional
`display_name`/`avatar_url` fields alongside their existing `user_id`/
`username` (username stays, as the raw Discord-sourced fallback value and
because removing an existing wire field is a breaking change for no
reason). **No new protocol message types are needed for this** — this is
the one piece of the whole document that's a pure wire-shape extension to
already-shipped types, not new surface area.

`PRESENCE_UPDATE` **stays exactly as-is** (`user_id` + `status` only) —
deliberately not extended with display name/avatar, for the same reason
`LIST_FRIENDS` doesn't embed presence (§1.5): the client already has
`display_name`/`avatar_url` from wherever it built its roster/friend list,
and merges by `user_id` locally. Bloating a broadcast that fires on every
connect/disconnect with profile fields nobody asked for would be adding
scope, not closing a gap.

`bio` is the one field that deliberately does **not** ride along in any
of these — it has no reason to be attached to every chat message or
roster snapshot. It's fetched only via `GET /api/users/:id/profile`
(§4.4), on an explicit "view profile" action.

---

## 5. Security Checklist

- [ ] **Friend code entropy and guessability.** Same 80-bit random-token
  entropy as invite codes (§1.3) — not sequential, not derived from
  `user_id`. Unlike a guild invite, a friend code has no `max_uses`
  ceiling to exhaust, so the only defense against brute-force guessing is
  entropy plus rate limiting (next item) — there's no equivalent of
  `INVITE_MAX_USES_REACHED` eventually shutting a guessing campaign down
  on its own.
- [ ] **Rate limiting on `ADD_FRIEND_BY_CODE` and `SEND_FRIEND_REQUEST`.**
  Both go through the internal API, keyed by `user_id`, same Redis-backed
  sliding-window mechanism the guild doc's §5 already calls for on invite
  redemption — capped per-caller per-minute. Without this, a compromised
  or malicious account can machine-guess friend codes (mitigated by
  entropy alone, but rate limiting is the actual defense-in-depth per the
  guild doc's own framing) or mass-spam `SEND_FRIEND_REQUEST` at
  sequential/enumerated `user_id`s as a harassment vector independent of
  guessing anything.
- [ ] **DM authorization enforcement is server-side and re-checked, not
  trusted from client state.** `DM_SEND` always evaluates `canSendDm`
  fresh server-side (§3.2/§3.3) — a client that cached "I'm allowed to
  message this person" from an earlier successful send cannot use that
  cached belief to bypass a same-request check; there is no client-
  supplied "I'm authorized" field anywhere in `DM_SEND`'s payload.
- [ ] **Blocking bypass vectors.** Enumerate and confirm each explicitly:
  - Can a blocked user still reach the blocker via a *new* friend
    request? No — §1.4 step 3 checks blocks before either the code or
    direct-`user_id` path can create a `friend_requests` row.
  - Can a blocked user still DM the blocker via shared-guild membership,
    even a guild joined *after* the block? No — `canSendDm` ANDs the
    block check against the friends-or-shared-guild check every time
    (§3.2), so shared guild membership alone is never sufficient once a
    block exists in either direction.
  - Can a blocked user re-add the blocker as a friend and thereby restore
    DM access? No — `SEND_FRIEND_REQUEST`/`ADD_FRIEND_BY_CODE` both fail
    with `USER_NOT_FOUND` against a blocked pair (§1.4 step 3), so the
    friendship path back into `canSendDm`'s first clause is closed too.
  - Does unblocking silently restore a prior friendship or open DM
    permission? No — §2.7 explicitly does not restore the deleted
    `friendships`/`friend_requests` rows; both parties start from "no
    relationship" and would need to re-friend (or still share a guild)
    from scratch.
- [ ] **Presence/profile block enforcement doesn't have a stale-cache
  window that outlives the block.** `BLOCK_USER`'s handler updates the
  in-memory `blocked_user_ids` cache (§3.3) synchronously in the same
  handler invocation that persists the block via the internal API — there
  is no gap where the DB has recorded a block but the live presence-
  filtering cache hasn't caught up yet for a connection that's already
  identified.
- [ ] **Avatar upload — file type validation.** Validated by sniffing
  actual file bytes (magic-number/signature check) against an allowlist
  (`image/png`, `image/jpeg`, `image/webp`), not by trusting the
  client-declared `Content-Type` or the uploaded filename's extension —
  either of those can be trivially spoofed to disguise an arbitrary file
  as an image.
- [ ] **Avatar upload — size limit.** A hard cap (e.g. 2 MiB) enforced
  server-side before the file is written to disk, not just a client-side
  UI hint — an unbounded upload is both a disk-exhaustion vector (§4.2's
  local-disk option has no cloud provider absorbing unbounded storage
  growth) and, combined with the local-disk choice, directly consumes the
  same host filesystem the rest of the stack runs on.
- [ ] **Avatar upload — path traversal.** Structurally not possible, not
  just filtered: the stored filename is server-generated (a fresh random
  token, same entropy pattern as invite/friend codes) plus an extension
  derived from the *validated* magic-number check above — never the
  client-supplied filename or any part of it. There is no code path where
  client input becomes part of a filesystem path.
- [ ] **Avatar upload — served content can't be executed as something
  other than an image.** Since the extension is server-chosen from the
  validated file type (not client-supplied), the dynamic serving route
  (§4.2) sets the `Content-Type` from a fixed extension→MIME-type table,
  not from anything client-supplied — a validated-as-image upload can't
  end up served as `text/html` or similar in a way that would enable
  stored-content-type confusion attacks against whoever views it. Also
  worth confirming directly: the route validates the requested filename
  against the exact pattern `saveAvatar` produces before ever touching
  the filesystem, so a request for an arbitrary path under the avatar
  directory can't be smuggled through the dynamic segment either.
- [ ] **`GET /api/users/:id/profile` doesn't leak whether a user exists
  vs. is blocked.** Both cases return the identical `404` (§4.4/§2.6) —
  same reasoning as `GUILD_NOT_FOUND` for private guilds, applied to
  profile lookups.
- [ ] **`display_name` doesn't become a second identity/authentication
  surface.** It's explicitly non-unique (§4.1) and never used to resolve
  a user for any authorization-relevant lookup — every internal join and
  every wire message still keys on `user_id`. A malicious user setting
  their display name to impersonate someone else's username is a UI/UX
  concern (not addressed further here), not a security boundary this
  document's model depends on.
- [ ] **`/internal/*` isolation, still the load-bearing assumption.**
  Every new internal endpoint in this document (`friend-requests`,
  `friendships`, `friend-codes`, `blocks`, `dm-conversations`, the
  `messages`/`last-sequence` extensions) rides on the same isolation
  `web/test/internalIsolation.test.ts` already verifies — every new route
  added here needs to be added to that test's route list, not assumed
  automatically covered.

---

## 6. Proposed Implementation Order

1. **Profiles — schema, REST endpoints, settings section** (§4). The most
   self-contained of the four: no dependency on friends, blocking, or
   DMs, and (per §4.5) touches the protocol layer not at all beyond
   extending existing wire shapes with optional fields. Includes the
   avatar-storage infrastructure decision (§4.2), which is worth
   resolving early since it's the one genuine infra question in this
   document and nothing downstream depends on it being resolved a
   particular way.
2. **Friend system** (§1). Depends on nothing from §2/§3. Establishes
   `friend_ids`-shaped session state conceptually (even though the actual
   C++ cache isn't strictly needed until step 4's DM re-check) and the
   `USER_NOT_FOUND` error code both later sections reuse.
3. **Blocking** (§2). Depends on §1's `friendships`/`friend_requests`
   tables existing (the `BLOCK_USER` transaction touches both). Also the
   step that adds the `blocked_user_ids` session cache and the
   presence-filtering `Scope::TARGETED` conversion (§2.5) — worth landing
   before DMs so DM's `canSendDm` (step 4) has both caches it depends on
   already in place, rather than building them concurrently with the
   feature that needs them.
4. **Direct messages** (§3). Depends on steps 2 and 3 directly
   (`canSendDm` is defined in terms of both) and reuses §4 of
   `docs/guilds/social-presence-design.md`'s already-implemented
   persistence architecture, not anything new to build from scratch.
   Sequenced last both because it's the largest net-new surface area of
   the four and because it's a hard dependency on everything else in this
   document landing first — the one feature that couldn't reasonably be
   built or even usefully reviewed in isolation.

Each step should update `shared/protocol/README.md` with its new message
types/error codes as it lands, per `CLAUDE.md`'s "Reference Documentation"
requirement — not batched to the end.

## Related Documentation

- [`../guilds/social-presence-design.md`](../guilds/social-presence-design.md)
  — the invite atomic-redemption pattern (§1.2–1.3) mirrored in §1.3
  above; the message-persistence architecture (§4) reused, not reinvented,
  in §3.4; the `Session.guild_ids` hydration-at-`IDENTIFY` precedent
  (§1.10) mirrored by `friend_ids`/`blocked_user_ids` in §3.3.
- [`../guilds/design.md`](../guilds/design.md) — `TARGETED` delivery mode
  and `getFdsInGuild`/`getFdsForUser`, both reused directly for DM
  delivery (§3.5) and presence filtering (§2.5).
- [`../settings/appearance-design.md`](../settings/appearance-design.md)
  — the settings modal shell and `SETTINGS_SECTIONS` registry (§1.2)
  extended in §4.3; the `PATCH /api/user/appearance` REST pattern mirrored
  by `PATCH /api/user/profile` in §4.4.
- [`../auth/discord-design.md`](../auth/discord-design.md) — the
  write-through cache / `/internal/*` isolation pattern this document
  assumes throughout, and the opaque-random-token pattern (§6) reused for
  friend codes (§1.3) and avatar filenames (§5).
- [`../../shared/protocol/README.md`](../../shared/protocol/README.md) —
  current wire protocol; every section above is a proposed delta against
  it.
