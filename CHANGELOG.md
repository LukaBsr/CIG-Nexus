# Changelog

## v0.7.0

**Guilds**
- Guild invites: creation (optional `max_uses`/`expires_in_seconds`), redemption (race-safe against concurrent `max_uses`-limited use), listing, and revocation
- Guild visibility (`open`/`application`/`private`) and the join-request flow `application` guilds require (`REQUEST_JOIN`, `LIST_JOIN_REQUESTS`, `APPROVE_JOIN_REQUEST`/`REJECT_JOIN_REQUEST`)
- Rank-based guild roles (`role_rank`, replacing the old binary owner/member column) with three tiers — Crew, Officer, Captain — and per-guild theme-resolved display labels; member roster exposed via `LIST_MEMBERS`
- Dispatcher widened to return `std::vector<Message>`, so a single handler action (e.g. approving a join request) can notify two different recipients with two different payloads
- Density/polish pass on the guild view UI, wiring up presence, roster, and join-requests end to end

**Presence**
- Online/offline presence tracking, derived from per-`user_id` connection counts (0→1/1→0 transitions), broadcast lobby-wide as `PRESENCE_UPDATE`
- `SO_KEEPALIVE` enabled on accepted sockets as a first-pass mitigation for half-open connections
- A repeated-reconnect regression test; a connection-count leak was also found and is tracked, not yet reliably reproducible (`docs/known-issues.md`)

**Message persistence**
- Chat and channel messages are now durable in Postgres instead of broadcast-and-forget; `FETCH_HISTORY` retrieves paginated history (keyset, not offset-based)
- `message_id` stays C++-assigned (not a Postgres sequence) but is now seeded at server startup from the durable high-water mark, so ids stay stable across restarts

**Friends, blocking, DMs, profiles**
- Friend requests (direct or by shareable code), accept/reject/cancel/remove, and a friends list
- Blocking, which silently drops any existing friendship or pending request between the pair
- 1:1 direct messages with history retrieval
- Customizable profiles (`display_name`, `avatar_url`), wired into every roster/message/list response that already resolved `username`

**Settings & appearance**
- Settings modal with a Profile section (display name, avatar) and a Blocked Users section
- Appearance section with two themes (`abyss` default, `ember`), a `data-theme` token system, and account-synced preference (`PATCH /api/user/appearance`, pulled on OAuth login)

**Fixes**
- `Session.guild_ids` now hydrates from durable membership at `IDENTIFY` instead of starting empty — required for `private` guild filtering to work, and incidentally improves reconnect UX for members of `open`/`application` guilds too
- Channel creation and invite creation gated on officer rank, not raw ownership
- Two missing Drizzle migration journal entries restored
- Connected-status indicator dot now stays green across both themes

**Docs**
- `docs/` design records reorganized into topic subfolders (`guilds/`, `social/`, `settings/`)
- New design docs: guild invites/roster/presence/message-persistence, friends/blocking/DMs/profiles, settings/appearance

## v0.6.0

**Auth**
- Discord OAuth2 login (PKCE), with RS256 session JWTs decoupled from the httpOnly refresh cookie
- Postgres + Drizzle persistence for the guild/channel catalog (`users`, `sessions`, `guilds`, `guild_memberships`, `channels`), replacing the C++ server's in-memory-only state

**Protocol**
- `IDENTIFY` now authenticates via a signed `session_token` instead of a client-chosen username
- Per-channel `CHANNEL_MESSAGE` scope added alongside the existing global `CHAT_MESSAGE` lobby broadcast

**Security**
- `/internal/*` isolated onto a second, unpublished port via a custom split-port server — verified against the real built Docker image, not just config
- JWT algorithm pinning (RS256 hardcoded; the token's own `alg` header is never trusted for dispatch)
- Redis-backed rate limiting on the OAuth routes (atomic sliding-window Lua script)
- Fixed an `X-Forwarded-For` rate-limit bypass — the real TCP socket peer address is now the sole source of truth
- Closed the revocation-cache restart gap (a revoked session could briefly become valid again after a server restart)
- Startup now refuses to read a `*_PRIVATE_KEY_PATH` file that's group- or world-readable

**Infra/CI**
- Added `gateway-ci.yml` (the gateway previously had no CI at all)
- `web-ci.yml` now actually runs its test suite (testcontainers-backed, Postgres/Redis included, no `services:` block needed)
- `clang-format --dry-run --Werror` enforced across `server/` (24 pre-existing drifted files reformatted first, verified behavior-unchanged via before/after `ctest`)

**Frontend**
- Full rebuild: `lib/gateway.ts` authenticates via `session_token` instead of the pre-auth `{ username }` shape
- Reusable component extraction (`MessageList`, `Button`/`TextInput`, `TabGroup`, a `useGatewayConnection` hook) and Tailwind-only styling
- New visual identity: crystal-mark logo, ink/teal/violet palette, monospace/sans type pairing
- Public landing page with a Discord login CTA, and a real `/login-error` page

**Docs**
- `docs/` reorganized into `architecture/`, `auth/`, `guilds/` subfolders
- Architecture, CI, and security audits documented (`docs/architecture-audit.md`, `docs/ci-audit.md`, `docs/security-audit.md`)

## v0.5.0 — 2026-07-14

- Guilds and channels: server-side data model and protocol handlers (#11), web client UI (#12)
- Protocol gaps closed, server error handling hardened (#9)
- Bug fixes: atomic message-id counter, dead code removal, chat message rendering (#10)
- Server test suite made optional via `BUILD_TESTS`, so a plain build no longer compiles Catch2 (#15)
- Version bumped across `HelloHandler.cpp` and the README badge (#13) — `web/package.json` and `gateway/package.json` jumped straight from `0.1.0` to `0.5.0` in this same commit, having never individually tracked `0.2.0`/`0.3.0`/`0.4.0`

## v0.4.0 — 2026-03-13

- Session identity system and the `IDENTIFY` handshake added (#8)
- The git tag lands on this commit, but `HelloHandler.cpp`'s `server_version` string was not actually bumped to `"0.4"` until much later, alongside the v0.5.0 work (see above) — for four months, the running server's own `WELCOME.server_version` still read `"0.3"`

## v0.3.0 — 2026-03-05 (no git tag; boundary inferred)

No tag exists for this version. The boundary below is inferred from
`HelloHandler.cpp`'s `server_version` string changing from `"0.1"` to
`"0.3"` in #6, whose PR title explicitly reads "(v0.3)".

- Chat broadcast system, the change that bumped the version string (#6)
- Initial Next.js web client with gateway connectivity (#5) — merged just before #6
- Docker Compose stack, initial Dockerfiles, and architecture docs (#7) — merged just after #6; the README's version badge is introduced here for the first time, already reading `v0.3`

## v0.2.0 — 2026-02-25

- Gateway: WebSocket ↔ TCP bridge (#3)
- Minimal message pipeline, the `HELLO` → `WELCOME` handshake (#4)

## v0.1.0 — 2026-02-09

- TCP framed protocol v0.1 defined (#1)
- Initial C++ server implementation (#2)
