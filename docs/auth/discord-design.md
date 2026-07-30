# Discord OAuth2 Authentication — Design Document

**Status: design, not implemented.** This is a planning document only. Nothing
described here has been built. Do not start implementation against it until
it has been reviewed and the open questions flagged throughout (search for
**`OPEN QUESTION`**) have been resolved.

## 1. Scope and a Key Interpretation

The request this document answers: make Discord OAuth2 (with PKCE) mandatory
for all clients, "so users can retrieve their guilds and write to the
corresponding channels," backed by Postgres + Drizzle, persistence owned
exclusively by the Next.js side.

**Confirmed: Discord is used only as an identity provider.** "Their guilds"
means CIG-Nexus's own `Guild`/`Channel` entities (the ones from
[`../guilds/design.md`](../guilds/design.md)) — currently in-memory on the C++ server —
now persisted in Postgres and scoped to the authenticated user. Discord
OAuth's job stops at "prove this browser belongs to Discord user
`123456789`." Why this reading is correct:

- The requested schema — `users`, `guilds`, `guild_memberships`, `channels` —
  is exactly CIG-Nexus's existing domain model from `docs/guilds/design.md`, not a
  Discord-shaped one (Discord guilds don't have a single `owner_id` column
  that fits this app's ownership model, and Discord channels aren't `TEXT` /
  `VOICE` in the same sense CIG-Nexus's are).
- `docs/guilds/design.md` explicitly chose the name "Guild" *specifically to avoid*
  colliding with Discord's own vocabulary while building an independent,
  Discord-*inspired* model. Importing real Discord guild data would be a much
  larger, different feature and contradicts that prior decision.

**Rejected alternative**: importing or mirroring Discord's actual
guilds/channels — calling `/users/@me/guilds` and mapping real Discord
servers onto CIG-Nexus's guild/channel model. Rejected for the reasons
above; it would also need the `guilds.members.read` scope, per-guild sync
jobs, and a mapping layer that isn't hinted at anywhere in the original
request.

Given the confirmed interpretation, Discord OAuth2 requests only the
`identify` scope (Discord user id, username, avatar). No `email`, no
`guilds` scope, no bot token — this integration never calls Discord's API
for anything beyond "who is this user" during login, which is also why no
Discord access/refresh token is retained afterward (§5).

## 2. Goals / Non-Goals

**Goals:**

- No client can reach `IDENTIFY` (and therefore any protocol action) without
  a valid, server-verified session originating from a completed Discord OAuth
  login.
- Guilds, channels, and guild membership are durable across server restarts.
- The application's own session has a lifetime fully independent of
  Discord's — it isn't derived from or tied to a stored Discord token
  (there isn't one after login completes, §1/§5), so nothing about the app
  session depends on Discord's token expiry/rotation behavior.
- The Gateway remains transport-only, per the project's standing constraint.

**Non-goals (this iteration):** see [§10 Out of Scope](#10-out-of-scope-for-this-iteration).

## 3. High-Level Flow

```text
Browser                Next.js (web/)          Discord            Gateway         C++ Server
   |                        |                     |                  |                |
   |--GET /login----------->|                     |                  |                |
   |                        |--gen verifier/state  |                  |                |
   |<--302 to Discord authorize + code_challenge---|                  |                |
   |------------------------------------------->  |                  |                |
   |<--302 to /callback?code=..&state=..-----------|                  |                |
   |--GET /callback-------->|                     |                  |                |
   |                        |--POST token (code+verifier)------------>|                |
   |                        |<--access_token, refresh_token-----------|                |
   |                        |--GET /users/@me------------------------>|                |
   |                        |<--discord profile------------------------|                |
   |                        |--upsert users, create sessions row      |                |
   |<--Set-Cookie(app session, httpOnly), 302 to app                  |                |
   |                        |                     |                  |                |
   |--GET /api/auth/session-token (cookie auth)-->|                  |                |
   |<--{ jwt, expires_at } (short-lived)-----------|                  |                |
   |                        |                     |                  |                |
   |--WS connect------------------------------------------------->  |                |
   |--HELLO---------------------------------------------------------->|-------------->|
   |<--WELCOME-------------------------------------------------------<|<---------------|
   |--IDENTIFY { session_token: jwt }--------------------------------->|-------------->|
   |                        |                     |                  | verify JWT     |
   |                        |                     |                  | (self, no      |
   |                        |                     |                  |  network call) |
   |<--IDENTIFIED----------------------------------------------------<|<---------------|
```

## 4. OAuth2 + PKCE Flow (Next.js side)

Two Next.js route handlers own the whole flow. Neither the Gateway nor the
C++ server participate in it.

### `GET /api/auth/discord/login`

1. Generate `code_verifier` — a cryptographically random 43–128 char string.
2. Compute `code_challenge = BASE64URL(SHA256(code_verifier))`.
3. Generate `state` — a separate cryptographically random value (anti-CSRF;
   not reused as the PKCE verifier).
4. Store `code_verifier` and `state` in a short-lived (5 min), httpOnly,
   `secure`, `SameSite=Lax` cookie (e.g. `oauth_txn`, value is the two fields
   together, signed/encrypted so a tampered cookie is rejected rather than
   trusted). No server-side transaction table is needed for this step — the
   cookie *is* the transaction, scoped to the one browser doing the login.
5. Redirect to:
   ```text
   https://discord.com/oauth2/authorize
     ?client_id=<DISCORD_CLIENT_ID>
     &redirect_uri=<DISCORD_REDIRECT_URI>
     &response_type=code
     &scope=identify
     &state=<state>
     &code_challenge=<code_challenge>
     &code_challenge_method=S256
   ```

### `GET /api/auth/discord/callback?code=...&state=...`

1. Read `oauth_txn` cookie. If missing/expired/undecryptable → reject, redirect
   to a login-error page. This alone blocks replay after the 5-minute window.
2. Compare `state` query param to the cookie's `state`, constant-time. Mismatch
   → reject (this is the CSRF defense: an attacker can't forge a callback
   without also controlling the victim's `oauth_txn` cookie).
3. Exchange the code:
   ```text
   POST https://discord.com/api/oauth2/token
   grant_type=authorization_code
   code=<code>
   redirect_uri=<DISCORD_REDIRECT_URI>
   code_verifier=<verifier from cookie>
   client_id / client_secret
   ```
   → `access_token`, `refresh_token`, `expires_in`, `scope`.
4. `GET https://discord.com/api/users/@me` with `Authorization: Bearer
   <access_token>` → `{ id, username, global_name, avatar }`.
5. Upsert into `users` keyed on `discord_id` (see [§5](#5-postgres-schema)) —
   only display identity fields (`discord_username`, `discord_global_name`,
   `discord_avatar_hash`) are written. The `access_token`/`refresh_token`
   from step 3 are used transiently for the step-4 profile fetch and then
   discarded — never persisted (§1, §5).
6. Create a `sessions` row (`user_id`, `expires_at`, ...).
7. Issue the application session (see [§6](#6-application-session-model)),
   set it as an httpOnly cookie, clear `oauth_txn`, redirect into the app.
8. Any failure in steps 1–6 (state mismatch, Discord error response, network
   failure) redirects to a login-error page with a generic message — never
   leaks which specific check failed, to avoid giving an attacker probing
   feedback.

## 5. Postgres Schema

Four tables were named in the request (`users`, `guilds`,
`guild_memberships`, `channels`). A fifth, `sessions`, is added here because
the explicitly requested **session revocation** checklist item has no home
without it. Everything below is scoped to exactly what that requirement
needs; no multi-provider auth, no roles beyond the existing owner/member
split, no message persistence (see [§10](#10-out-of-scope-for-this-iteration)).

Since this integration never calls the Discord API again after initial
login (§1: `identify` scope only, no post-login Discord calls), `users`
stores only what's needed to display identity — no Discord access/refresh
token is retained, so there is nothing of that kind to encrypt at rest.

```sql
-- One row per Discord-authenticated person. Holds display identity only —
-- no Discord access/refresh token is stored; this integration never calls
-- Discord's API again after the initial login (§1).
CREATE TABLE users (
    id                    UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    discord_id            TEXT NOT NULL UNIQUE,   -- Discord snowflake, stored as text (exceeds JS safe-int range)
    discord_username      TEXT NOT NULL,
    discord_global_name   TEXT,
    discord_avatar_hash   TEXT,
    created_at             TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at             TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- UNIQUE(discord_id) above doubles as the lookup index for the callback's upsert.

-- Application sessions — decoupled from the Discord token (§6). This is
-- what session_token JWTs reference via their `sid` claim, and what makes
-- revocation possible.
CREATE TABLE sessions (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id             UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    issued_at           TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at          TIMESTAMPTZ NOT NULL,
    refresh_token_hash  TEXT NOT NULL,      -- SHA-256 of the refresh token; raw value never stored
    revoked_at          TIMESTAMPTZ,        -- NULL = active
    user_agent          TEXT,
    ip_address          INET,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX idx_sessions_user_id ON sessions(user_id);
CREATE INDEX idx_sessions_expires_at ON sessions(expires_at); -- for the expiry sweep job

-- CIG-Nexus's own Guild entity (docs/guilds/design.md), now durable.
CREATE TABLE guilds (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
    owner_id    UUID NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX idx_guilds_owner_id ON guilds(owner_id);
-- ON DELETE RESTRICT: deleting a user with guilds they own must be an
-- explicit decision (transfer or cascade-delete the guild first), not an
-- accidental side effect of a user-deletion path.

CREATE TABLE guild_memberships (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_id   UUID NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role       TEXT NOT NULL DEFAULT 'member' CHECK (role IN ('owner', 'member')),
    joined_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (guild_id, user_id)
);
CREATE INDEX idx_guild_memberships_user_id ON guild_memberships(user_id);   -- "list my guilds"
CREATE INDEX idx_guild_memberships_guild_id ON guild_memberships(guild_id); -- "list members of a guild"
-- `role` is intentionally just today's owner/member split stored as data
-- instead of derived from guilds.owner_id, so it's the seam the Future
-- Permission Hook (docs/guilds/design.md) can widen later without a schema change.
-- It is NOT read as a real permission system yet — canCreateChannel/
-- canDeleteChannel still just check for 'owner', per docs/guilds/design.md.

CREATE TYPE channel_type AS ENUM ('TEXT', 'VOICE');

CREATE TABLE channels (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_id      UUID NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
    name          TEXT NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
    channel_type  channel_type NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX idx_channels_guild_id ON channels(guild_id);
```

Wire-format ids stay strings (`"g_<uuid>"`, `"c_<uuid>"`, `"u_<uuid>"`) at the
protocol boundary, matching the existing `g_1`/`c_1`/`u_1` shape from
`shared/protocol/README.md` — only the generation scheme changes internally
(server-assigned incrementing counter → Postgres `UUID`), so the client-side
web code (`lib/gateway.ts`, `page.tsx`) needs no changes to how it *handles*
ids, only that they're no longer small sequential integers.

## 6. Application Session Model

Two credentials, deliberately decoupled:

| | Discord OAuth token | Application session |
|---|---|---|
| Holder | Next.js only, transiently in-memory during the OAuth callback (§4) — never persisted (§5) | Browser (as a JWT) + `sessions` table |
| Used for | The one-time step-4 profile fetch (`GET /users/@me`) during login only; nothing after that (§1) | Authenticating the user to CIG-Nexus itself: web, Gateway (pass-through), C++ server |
| Lifetime | Discarded immediately after login completes | App-controlled, independent of Discord's expiry |
| Ever sent to Gateway/C++ server? | No, never | Yes — this is the credential the protocol carries |

The application session is a **short-lived JWT** (the "access JWT" below)
plus a **long-lived httpOnly refresh cookie** backing it, standard
access/refresh split:

- **Refresh cookie** (`__session`): httpOnly, `secure`, `SameSite=Lax`,
  opaque random value. Its SHA-256 hash matches `sessions.refresh_token_hash`
  for some non-revoked, non-expired row. TTL: e.g. 30 days, sliding
  (refreshed on use).
- **Access JWT**: short TTL (10–15 minutes), signed **RS256** — the private
  key exists only in Next.js; the C++ server holds only the public key. This
  asymmetry is what makes self-validation on the C++ side safe (§9) without
  duplicating a shared secret into a service that also needs to remain
  network-facing. The C++ verifier pins `RS256` as the only accepted
  algorithm **in its own configuration** — it never reads `alg` from the
  token's header to decide how to verify — and rejects any token declaring a
  different algorithm, including `none`. This closes the standard
  algorithm-confusion attack class, where a verifier that trusts a token's
  self-declared `alg` can be tricked into accepting an unsigned or
  differently-signed token.

  Claims:
  ```json
  {
    "sub": "u_<uuid>",
    "discord_id": "<discord snowflake>",
    "username": "<discord username>",
    "sid": "<sessions.id>",
    "iat": 1234567890,
    "exp": 1234568790,
    "iss": "cig-nexus-web",
    "aud": "cig-nexus-server"
  }
  ```

### Bridging the browser-held token to the WebSocket

The web client opens its WebSocket directly to the Gateway
(`ws://localhost:8080`), not proxied through Next.js — that topology is
unchanged (see [Architecture Doc](../architecture/overview.md)). That means the access
JWT has to become readable by client-side JS at some point, since it's
`IDENTIFY`'s payload, sent in-band over that direct WS connection — it can't
stay purely httpOnly the way the refresh cookie does.

Resolution: `GET /api/auth/session-token`, authenticated by the httpOnly
refresh cookie, returns `{ token, expires_at }` in the JSON body. The web
client calls this once per `connect()` (before/while opening the WebSocket)
and holds the JWT in memory only — never `localStorage`/`sessionStorage` — for
exactly the reason the TTL is short: a value that leaks via XSS is only
useful for ≤15 minutes and can't independently refresh itself (refreshing
requires the httpOnly cookie, which JS can't read).

`lib/gateway.ts`'s `connect()` changes from sending a hardcoded
`{ username: "web_user" }` on `WELCOME` to sending
`{ session_token: <fetched jwt> }`.

## 7. Drizzle Structure

```text
web/
├── db/
│   ├── schema/
│   │   ├── users.ts
│   │   ├── sessions.ts
│   │   ├── guilds.ts
│   │   ├── guildMemberships.ts
│   │   ├── channels.ts
│   │   └── index.ts          # re-exports all tables + relations()
│   ├── client.ts              # drizzle(pool) singleton, DATABASE_URL from env
│   └── migrations/            # drizzle-kit generated SQL, checked into git
├── drizzle.config.ts
```

- **Migration workflow**: `drizzle-kit generate` (produces reviewable SQL
  files under `db/migrations/`) + `drizzle-kit migrate` applied at deploy
  time — not `drizzle-kit push`. Push-to-sync is fine for solo prototyping
  but this repo already has a PR-review workflow (`CLAUDE.md`, "Git
  Workflow"); generated migrations give something to actually review.
- **Connection**: a single pooled `pg.Pool` (or `@vercel/postgres` /
  `postgres.js`, whichever this stack lands on), instantiated once and reused
  across API routes — Next.js dev-mode module reloading needs the usual
  "attach the pool to `globalThis` in development" guard to avoid exhausting
  connections on hot reload.
- **`DATABASE_URL`** joins the existing env-var pattern
  (`NEXT_PUBLIC_GATEWAY_URL`) in `web/`'s environment / `docker-compose.yml`.
  A `postgres` service would need adding to `docker-compose.yml` — not done
  in this document (planning only).

## 8. Protocol Extension

### `HELLO` / `WELCOME`

No change to the shape. `HELLO.version` gates protocol compatibility
already; this change is a breaking one for `IDENTIFY` specifically (below),
so it's a natural point to bump the negotiated version, but that's an
implementation-time decision, not a design one — noted here so it isn't
forgotten.

### `IDENTIFY` — redefined

Today: client supplies a free-text `username`, server assigns `user_id`.
Under mandatory auth, a client can no longer assert its own identity —
identity comes from the verified JWT.

Client → server:
```json
{ "type": "IDENTIFY", "session_token": "<JWT>" }
```

Validation:
- `session_token` must exist and be a string.
- JWT signature must verify against the server's cached RS256 public key.
- `exp` must not have passed.
- `aud` must equal `"cig-nexus-server"`.
- The session must not be locally known-revoked (§9 revocation cache).

On success, the server creates a session **exactly as it does today**
(`SessionManager::createSession`), but `username`/`user_id` are populated
from the JWT's `sub`/`username` claims instead of client-supplied fields —
`Session` gains a `discord_id` field alongside the existing `user_id`.
Response shape (`IDENTIFIED`) is unchanged.

New error codes:

| Code | Meaning |
|---|---|
| `AUTH_REQUIRED` | `IDENTIFY` sent without `session_token` |
| `INVALID_SESSION` | signature invalid, malformed, or `aud`/`iss` mismatch |
| `SESSION_EXPIRED` | `exp` has passed |
| `SESSION_REVOKED` | session found in the local revocation cache |

`username` free-text `IDENTIFY` goes away entirely — this is a breaking
protocol change, consistent with "mandatory" auth (there is no
"anonymous but identified" tier to fall back to).

### `CREATE_GUILD` / `JOIN_GUILD` / `LIST_GUILDS` / etc. — wire format unchanged

Client-facing request/response shapes for every guild/channel message in
`shared/protocol/README.md` stay exactly as documented. What changes is
underneath: the C++ handler's authorization/catalog checks now consult a
Postgres-backed source of truth instead of pure in-memory state (§8.1).

### `CHANNEL_MESSAGE` and `CHAT_MESSAGE` — per-channel scope

`CHANNEL_MESSAGE` already uses `Scope::TARGETED`, delivered only to
connections with that channel active (`docs/guilds/design.md`). Under persisted,
authenticated membership, that becomes the actual authorization boundary
this system needed all along — `JOIN_CHANNEL` already requires guild
membership (`NOT_GUILD_MEMBER`), and membership now comes from the durable
`guild_memberships` table instead of ephemeral `Session.guild_ids`.

**Resolved**: `CHAT_MESSAGE` stays exactly as-is — a global lobby, still
`Scope::BROADCAST`, open to any authenticated user. Auth becomes a floor
(you must be identified to send it at all, same `NOT_IDENTIFIED` check as
today) rather than a per-message channel-membership check. It is not
retired or folded into a "default" channel; it remains the fully separate,
orthogonal capability `docs/guilds/design.md` decision #6 already established,
running alongside per-channel messaging (`CHANNEL_MESSAGE`) rather than
being replaced by it.

## 8.1 Server-Side Catalog Sync

This is the least obvious part of the design and deserves its own section.

The request states Postgres/Drizzle persistence is **managed exclusively on
the Next.js side** — meaning the C++ server never opens a database
connection, ever. But `CREATE_GUILD`, `JOIN_GUILD`, `CREATE_CHANNEL`, etc.
are still C++ protocol messages arriving over the existing TCP path (their
wire format is unchanged, per §8 above). Something has to reconcile "C++
handles the mutation" with "only Next.js touches Postgres."

**Chosen approach**: Next.js exposes an **internal-only HTTP API**
(`/internal/guilds`, `/internal/guild-memberships`, `/internal/channels`;
not part of the public site, reachable only on the Docker-internal network,
authenticated by a shared secret header — not by user sessions) purely for
the C++ server to call. `GuildHandler`/`ChannelHandler` call out to this API
synchronously as part of handling `CREATE_GUILD` etc., then respond to the
client only after Next.js confirms the Postgres write.

`GuildManager` becomes a **write-through cache**: still the fast in-memory
structure handlers read from for every request (matching today's
`unordered_map`-based lookups, no per-read network call), but it's no
longer the source of truth —

- **On C++ server startup**: fetch the full current catalog
  (`GET /internal/catalog`) and populate `GuildManager` before accepting
  connections.
- **Endpoints**: `POST /internal/guilds` (`CREATE_GUILD`, also creates the
  owner's membership), `DELETE /internal/guilds/:id` (`DELETE_GUILD`,
  cascades to channels/memberships via the schema's `ON DELETE CASCADE`, §5),
  `POST /internal/guild-memberships` (`JOIN_GUILD`),
  `DELETE /internal/guild-memberships/:guildId/:userId` (`LEAVE_GUILD`),
  `POST /internal/channels` (`CREATE_CHANNEL`),
  `DELETE /internal/channels/:id` (`DELETE_CHANNEL`). `LIST_GUILDS`/
  `LIST_CHANNELS` need no endpoint of their own — they're served from the
  cache, populated by `GET /internal/catalog` and kept current by the
  mutation calls above.
- **On every mutation** (`CREATE_GUILD`, `DELETE_CHANNEL`, ...): call the
  corresponding internal endpoint first; only update the in-memory cache and
  respond to the client if that call succeeds. A failed internal call
  becomes an `ERROR` / `INTERNAL_ERROR` response to the client — the
  in-memory state is never allowed to diverge from Postgres by writing
  locally first.

This keeps the Gateway completely untouched (still a dumb byte pipe — the
sanity check `docs/guilds/design.md` already used: "if implementing this needs
Gateway changes, the design leaked"), and keeps literal Postgres access
inside Next.js only, at the cost of the C++ server gaining a new outbound
HTTP client responsibility it doesn't have today (§8.2).

**Enforcing `/internal/*` isolation.** The shared-secret header (§9.1) is
authentication, not network isolation, and this design does not treat it as
the only defense — `/internal/*` must also be genuinely unreachable from
outside the Docker-internal network, not just "unreachable in practice."
Two concrete requirements, not just design intent:

- **Reverse proxy / ingress configuration must explicitly exclude
  `/internal/*`** from any public-facing route (§8.2) — whatever sits in
  front of the `web` service (nginx, a cloud load balancer, etc.) needs an
  explicit deny/exclude rule for that path prefix, not an assumption that
  it's simply never linked to.
- **An integration test** that attempts to reach `/internal/*` from outside
  the internal Docker network — the same way an external attacker would,
  through whatever the actual public entry point is — and asserts the
  request fails. This is what turns "should be unreachable" into something
  that breaks CI if it regresses, rather than a fact that quietly stops
  being true the next time the proxy config changes.

**`OPEN QUESTION`**: single-instance assumption. This cache design assumes
one C++ server process. If the server is ever horizontally scaled, per-instance
in-memory caches need either a fan-out invalidation mechanism or a shared
cache — out of scope here (see §10), flagged because the write-through
design would need revisiting first.

## 8.2 Required Changes Per Service

**Next.js (`web/`)**
- New routes: `GET /api/auth/discord/login`, `GET /api/auth/discord/callback`,
  `POST /api/auth/logout`, `GET /api/auth/session-token`.
- New internal routes (§8.1): `GET /internal/catalog`,
  `POST /internal/guilds`, `DELETE /internal/guilds/:id`,
  `POST /internal/guild-memberships`,
  `DELETE /internal/guild-memberships/:guildId/:userId`,
  `POST /internal/channels`, `DELETE /internal/channels/:id`, guarded by a
  shared-secret header, not by
  user auth — and excluded from any public-facing route at the reverse
  proxy / ingress level, backed by an integration test asserting that
  exclusion (§8.1).
- Drizzle schema + migrations (§7).
- RS256 keypair generation/storage; a `/.well-known/jwks.json`-style route
  (or a static env-provided public key) so the C++ server can fetch/rotate
  the verification key without a redeploy.
- New runtime dependency: **Redis**, used for OAuth-route rate limiting
  (§9.1) — no other use of Redis is proposed by this design.
- New env vars: `DISCORD_CLIENT_ID`, `DISCORD_CLIENT_SECRET`,
  `DISCORD_REDIRECT_URI`, `DATABASE_URL`, `REDIS_URL`,
  `SESSION_JWT_PRIVATE_KEY_PATH`, `INTERNAL_API_SHARED_SECRET`.
  `SESSION_JWT_PRIVATE_KEY_PATH` holds a file path, not the key content
  itself — the keypair lives in `secrets/*.pem` (gitignored) and is
  bind-mounted into the container, not passed as an env var.

**Gateway (`gateway/`)**
- **No changes.** It forwards `IDENTIFY { session_token }` exactly like it
  forwards `IDENTIFY { username }` today — bytes in, bytes out. This is a
  direct consequence of the "Gateway stays transport-only" constraint from
  `CLAUDE.md`, and this design deliberately keeps it true rather than adding
  a WS-upgrade-time auth gate at the Gateway.

**C++ Server (`server/`)**
- `IdentifyHandler` rewritten to validate `session_token` instead of
  accepting free-text `username` (§8).
- New dependency: an outbound HTTP client for the internal API calls (§8.1)
  — the server currently only does raw listening sockets
  (`TcpListener`/`Connection`), it has never made an outbound HTTP request.
  Recommend `libcurl` (widely available, C-callable) over hand-rolling
  HTTP/1.1 client code.
- New dependency: RS256 JWT verification — recommend OpenSSL directly
  (likely already pulled in transitively via libcurl's TLS backend) with a
  small hand-written base64url + JSON-claims decode, consistent with this
  codebase's existing preference for explicit, minimal parsing
  (`MessageParser` does the same rather than depending on a schema library) —
  rather than pulling in a full JWT framework for one verify call.
- `GuildManager` becomes the write-through cache described in §8.1.
- `Session` (`server/include/session/Session.hpp`) gains a `discord_id`
  field.
- New periodic task: poll `/internal/revoked-sessions` (or similar) on an
  interval (§9) to keep the local revocation cache warm, and a sweep that
  disconnects any live connection whose session has since been revoked or
  expired (closes the gap between "revoked in Postgres" and "socket actually
  closed" — see §9's revalidation checklist item).

## 9. Arbitration: Session Validation on the C++ Server

Two options for how `IdentifyHandler` (and the periodic revalidation sweep,
§8.2) checks whether a `session_token` is currently valid.

### Option 1 — Self-validated JWT, no network call

The C++ server holds the RS256 public key (fetched at startup, refreshed
periodically) and verifies signature + `exp`/`aud`/`iss` locally, in-process,
for every `IDENTIFY`. The verifier pins `RS256` as the only accepted
algorithm (§6) rather than trusting the token's own `alg` header — required
for self-validation to be safe at all, since self-validation is exactly the
scenario algorithm-confusion attacks target.

- **Pro**: zero added latency and zero new failure mode on the hot path —
  `IDENTIFY` doesn't get slower or gain a new way to fail (Next.js being
  down) just because auth exists.
- **Pro**: no coupling between "C++ server can accept connections" and
  "Next.js is currently reachable" — matches the existing design principle
  that the C++ server is the authoritative, standalone backend.
- **Con**: revocation is not immediate. A JWT that's cryptographically valid
  is accepted even if the corresponding `sessions` row was revoked one
  second ago, until the server's local revocation cache catches up.

### Option 2 — Delegate to an internal Next.js endpoint on every connection

`IdentifyHandler` calls `POST /internal/validate-session` with the token on
every `IDENTIFY`, Next.js checks Postgres (`sessions.revoked_at`,
`expires_at`) and answers valid/invalid.

- **Pro**: revocation is immediate and correct by construction — there's
  only one source of truth, checked live, every time.
- **Con**: every `IDENTIFY` now has a hard runtime dependency on Next.js
  being up and Postgres being reachable — a Next.js/DB outage means the C++
  server, otherwise healthy, can't accept new identified connections at all.
- **Con**: added latency on every `IDENTIFY` (an extra network hop + DB
  query), on a path that's currently a pure in-memory operation.

### Recommendation: Option 1, with a bounded revocation gap

Self-validate the JWT (Option 1) as the primary check — it preserves the
server's ability to operate independently and keeps `IDENTIFY` cheap. Bound
the revocation-latency downside two ways instead of paying for it on every
request:

1. **Short access-JWT TTL** (§6: 10–15 min) already limits how long a
   revoked-but-not-yet-known token can be used to *newly* `IDENTIFY` — worst
   case is bounded by the TTL, not unbounded.
2. **Local revocation cache, pull-based**: the C++ server polls
   `GET /internal/revoked-sessions?since=<ts>` on a short interval (e.g. 30s)
   and keeps a small in-memory set of revoked `sid`s, checked on `IDENTIFY`
   *and* on the periodic sweep of already-connected sessions (§8.2) — so an
   explicit logout/ban is reflected within one poll interval even for a
   connection that already completed `IDENTIFY` and is sitting on an
   otherwise-still-valid JWT.

This is Option 1's low-latency, no-hard-dependency behavior for the common
case, with Option 2's correctness applied only where it's actually needed
(explicit revocation), on a cheap polling schedule rather than the hot path.
If a Next.js/DB outage happens, new logins/identifies keep working
(self-validated), and only *newly-revoked* sessions fail to be revoked
promptly until Next.js recovers — a reasonable degradation, not a full
outage.

## 9.1 Security Checklist

- [ ] **Cookies**: `oauth_txn`, refresh cookie (`__session`) both
  `httpOnly`, `secure`, `SameSite=Lax`. `SameSite=Strict` was considered and
  rejected for the refresh cookie specifically — it would break the OAuth
  redirect-back-from-Discord flow, which is a cross-site top-level
  navigation.
- [ ] **Rate limiting on OAuth routes** — `/api/auth/discord/login` and
  `/callback`, per-IP sliding window, backed by Redis (§8.2, §10) — chosen
  over a Postgres-backed counter table to keep the hot rate-limit check off
  the primary database and out of contention with Drizzle's connection
  pool.
- [ ] **Systematic server-side revalidation before channel access** —
  covered structurally: every guild/channel action already re-checks live
  membership against the write-through cache (§8.1) on every request, not
  just at `IDENTIFY` time; combined with the revocation-cache sweep (§9) for
  already-connected sessions.
- [ ] **Session revocation** — `sessions.revoked_at`, checked via the
  poll-based cache (§9). Logout (`POST /api/auth/logout`) sets
  `revoked_at = now()` immediately in Postgres; propagation to the C++
  server is bounded by the poll interval, not instantaneous.
- [ ] **JWT signing key isolation** — RS256 private key never leaves
  Next.js; C++ server holds only the public key.
- [ ] **JWT algorithm pinned server-side** — the C++ verifier accepts only
  `RS256`, configured explicitly, and never derives the algorithm from the
  token's own header (§6, §9) — closes algorithm-confusion attacks,
  including `alg: none`.
- [ ] **Generic OAuth failure responses** — callback errors don't leak which
  specific validation step failed (§4, step 8).
- [ ] **Internal API isolation** — `/internal/*` routes authenticated by a
  shared secret, **and** verified unreachable from outside the
  Docker-internal network by both explicit reverse-proxy/ingress exclusion
  and a dedicated integration test (§8.1) — the shared secret alone is not
  treated as sufficient defense.

## 10. Out of Scope for This Iteration

- **Message history persistence.** `chat_messages`/`channel_messages` tables
  are not part of this schema (§5) — the four/five tables here cover
  identity, sessions, and the guild/channel catalog only. Chat content
  remains ephemeral/in-memory, exactly as today.
- **Importing or mirroring real Discord guilds/channels** (§1, interpretation B).
- **Multi-provider auth** (email/password, other OAuth providers). The
  schema could extend to it later, but nothing here reserves space for it
  speculatively.
- **Permission system beyond owner/member.** `guild_memberships.role`
  reserves the column; the actual permission logic is still the Future
  Permission Hook from `docs/guilds/design.md`, untouched.
- **Voice channel authentication/transport** — voice channels remain
  metadata-only, unaffected by this design.
- **Desktop client** (`desktop/`) — not the active development path per
  `CLAUDE.md`; this design covers the web client only.
- **Redis operational details** (eviction policy, persistence/AOF config,
  managed vs. self-hosted) — that Redis backs OAuth rate limiting is decided
  (§9.1, §8.2); how it's run in production is not.
- **Horizontal scaling of the C++ server** — the write-through cache (§8.1)
  assumes one instance; multi-instance cache coherency is unaddressed.
- **GDPR-style data export/deletion workflows.**
- **Admin tooling** for manually revoking sessions or banning users — the
  data model supports it (`sessions.revoked_at`); no UI/API for triggering
  it is designed here.
- **`docker-compose.yml` changes** (adding `postgres` and `redis` services,
  new env vars) — implementation-time work, not drafted in this planning
  document.

## Related Documentation

- [`../guilds/design.md`](../guilds/design.md) — the guild/channel domain model this
  design persists and authenticates against.
- [`../architecture/overview.md`](../architecture/overview.md) — service responsibilities; this
  design's "Gateway stays transport-only" constraint comes directly from
  here.
- [`../../shared/protocol/README.md`](../../shared/protocol/README.md) — current
  wire protocol; §8 above is the delta this design proposes against it.
