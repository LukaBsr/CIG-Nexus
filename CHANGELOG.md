# Changelog

## Unreleased

**Fixes**
- `BROADCAST` (lobby chat and `PRESENCE_UPDATE`) now reaches identified connections only; previously a socket that never completed `IDENTIFY` still received them (#30)
- `GUILD_LIST` entries carry `is_member`, so a returning user's guild rail and Join buttons are correct after a page load (#31)
- `ProfileView` falls back to its existing "Profile unavailable" state when `fetchProfile()` rejects (a network failure, or a malformed response body); previously an unhandled rejection left the modal stuck on "Loading…" indefinitely
- Guild icon initials are a fixed light color (`#edeffb`) instead of the `text-ivory` theme token, and the background lightness is 30% instead of 32%, so the initials keep at least 4.5:1 contrast at every hue. Previously the worst hue fell to 4.2:1 (`docs/design/brand-guidelines.md` §2.5–2.6)

**Features**
- View-profile UI: clicking an avatar in the lobby/channel/DM message lists, the guild member roster, or the friends/DM-conversation lists opens a profile card (display name, status message, bio, accent-colored header) — the "view profile" action `docs/social/friends-dms-design.md` §4.3/§4.4 always implied but never had a UI for (`web/components/ProfileView.tsx`). Clicking your own avatar now opens the same card with an "Edit profile" button that jumps straight to Settings → Profile (previously excluded everywhere)
- `GET /api/users/:id/profile` always allows viewing your own profile, independent of the friends-or-shared-guild check below — a self-view exception (§4.6), not a relaxation: it only ever returns a caller's own already-editable data. This also fixes an inconsistency found while adding it: self-view was previously already possible by accident whenever the caller belonged to any guild (an unguarded side effect of the shared-guild check matching a user against themselves), and 404'd otherwise

**Security**
- `GET /api/users/:id/profile` now requires the caller to be friends with, or share a guild with, the target (mirroring `canSendDm`'s permission shape), in addition to the existing block check — previously any authenticated user could view any other account's `bio`/`status_message`/`accent_color` with no relationship required. Nonexistent target, blocked (either direction, not just target-blocked-caller as before), and no-relationship all return an identical 404. Also newly rate limited (60/minute per caller, fail-open)
- The C++ JWT verifier now checks the `iss` claim against a new required server setting, `AUTH_JWT_EXPECTED_ISSUER` (`cig-nexus-web` in `.env.example`); a missing or different issuer is rejected with `INVALID_SESSION`. **Upgrade note:** add the new line from `.env.example` to your `.env` — the server refuses to start without it, and `docker-compose.yml` now makes `docker compose up`/`build`/`config` fail with a clear message if it is unset. Tokens already issued by the web signer carry `iss: "cig-nexus-web"` and keep working
- `JwtVerifier` now rejects a token whose `exp` is not a non-negative integer as malformed (`INVALID_SESSION`); a negative `exp` previously wrapped to a huge unsigned value and never expired. The web signer never emits one, so no real token is affected
- In-memory, per-user rate limiting in the C++ protocol layer (`util::RateLimiter`): `CREATE_INVITE` (20/hour), `JOIN_VIA_INVITE` (10/minute), `REQUEST_JOIN` (5/minute), `SEND_FRIEND_REQUEST`/`ADD_FRIEND_BY_CODE` (10/minute, shared bucket), `REGENERATE_FRIEND_CODE` (3/hour), `CREATE_GUILD` (10/hour), `CHAT_MESSAGE`/`CHANNEL_MESSAGE`/`DM_SEND` (20/10 seconds each) — new `RATE_LIMITED` error code, documented in `shared/protocol/README.md`'s new Rate Limits section
- Rate limiting on `PATCH /api/user/profile`, `PATCH /api/user/appearance`, and `POST`/`DELETE /api/user/profile/avatar` (shared bucket), per-user, fail-open on a Redis error (logged, not silent) unlike the OAuth routes' existing fail-closed limiter

**Reliability**
- `IDENTIFY` now hydrates blocks/friends/profile from one combined internal-API call (`fetchSessionContext`, new `GET /internal/users/:id/session-context` route) instead of three sequential ones — halves the worst-case stall other connections could see behind a slow web service during IDENTIFY (three 5s-timeout calls -> one)
- `IDENTIFY`'s blocks/friends/profile load is now fully asynchronous (`session::SessionHydrationWorker`, bounded retry summing to ~60s) instead of blocking the single-threaded server's handling of every other connection while it runs; `IDENTIFIED` is sent immediately, guild membership hydrates in-memory as before, and `DM_SEND`/the connection's own `PRESENCE_UPDATE` wait for the load to finish (new `SESSION_HYDRATING` retryable error code, new `SESSION_CONTEXT_UNAVAILABLE` + disconnect if the retry budget is exhausted — see `shared/protocol/README.md`'s new Asynchronous IDENTIFY Hydration section). Also fixes a related presence-corruption bug found while building this: a second tab whose hydration never completes could, on disconnect, wrongly decrement a different, already-online tab's real presence count

**Changed**
- Color tokens renamed with no visual change: `teal` -> `brand`, `violet` -> `brand-2`, `ink` -> `page`, `ivory` -> `fg`, in `web/app/globals.css` and every utility class that used them (`text-`, `bg-`, `border-`, `accent-`). Custom properties follow the same mapping (`--color-brand`, etc.). Theme ids and the Abyss/Ember palettes are unchanged
- Design tokens v0.2 (`web/app/globals.css`): new `raised`, `muted`, `warning`, `danger` and `online` tokens. Status colors that were literal `red-400`/`green-400` utilities (presence dot, connection indicator, error text) now use `danger` and `online`, with the same computed colors.
- Visible changes, intentional: Abyss `brand-2` (usernames, emphasis) is lighter, `#8b5cf6` -> `#a079f8`, for 4.5:1 contrast on panels; Ember has its own `danger` color (`#ff4d79`), distinct from its coral brand; the page declares `color-scheme: dark`, so native scrollbars, form controls and autofill follow the dark base.
- A token test (`web/lib/appearance/tokens.test.ts`) reads `globals.css` and checks that the eleven tokens are declared and that text and accent colors meet contrast minimums in Abyss and Ember.**Docs & CI**

**Docs & CI**
- Documented the OpenSSL/libcurl build prerequisites for non-Docker server builds (#28)
- `docs/security-audit.md` §2.5 re-traced against real `npm audit` output; new weekly Dependency Advisory Watch workflow that files an issue only when a high/critical advisory ID appears or disappears (#29)
- Consistency sweep: stale READMEs, status banners, and audit notes updated to match the code; `CLAUDE.md` is now tracked in git
- Fixed a `.gitignore` regression from the `.vscode/` ignore change that had also silently un-ignored `data/` (uploaded avatars)
- Added opt-in stderr logging (`CIG_NEXUS_DEBUG_PRESENCE=1`) around presence increments/decrements and `IDENTIFY` for diagnosing the open presence-count-leak known issue; off by default
- Gateway: Node 18 (end-of-life) -> Node 24 in the Dockerfile and CI; added its first test suite (frame codec unit tests, WebSocket<->TCP bridge integration tests, `npm ci --omit=dev` in the runtime image)
- `server-ci.yml`'s formatting check now pins `clang-format==18.1.8` via a pip-installed venv instead of apt's version, so it no longer drifts with the runner image; the same pinned-venv recipe is documented in `CLAUDE.md` and `server/README.md` for local use (#40)
- Documented first-run secrets setup: generating the RSA keypair and its required file modes (`private.pem` 600, `public.pem` 644) before the first `docker compose up`, and how to recover if Docker already created root-owned directories in their place; fixed `CLAUDE.md`'s server test binary path (#50)
- Brand and design system charter v0.2 (`docs/design/brand-guidelines.md`): logo family, color tokens, typography, and a 13-theme catalog (`docs/design/theme-catalog.css` and `.json`); light and dark logo variants (`mark-dark`, `mark-light`, `icon-light`, `lockup-dark`, `lockup-light`) added to `web/public/branding/`. Documentation only — no theme is applied by this change
- Brand charter updated to v0.2.1 to describe `main` after #61–#63: final token names are `page`, `fg`, `brand` and `brand-2` (replacing `ink`, `ivory`, `teal` and `violet`); the guild icon fix (#61) is recorded as shipped; the theme catalog CSS and JSON use the final token names. New findings: no theme declares `color-scheme` yet, and a rule is added against conflicting same-property utilities in one `className` (the cascade-order issue found while renaming). A drift-guard test that keeps the catalog and `globals.css` in step is a follow-up. (`docs/design/brand-guidelines.md`, `docs/design/theme-catalog.css`, `docs/design/theme-catalog.json`)
- Brand charter updated to v0.3 to describe `main` after #65. It corrects the status-color values, which Tailwind v4 defines in OKLCH (`danger` and `online`, not the v3 hex in earlier drafts), and adds four themes to the catalog (Tar, Chocolate, Espresso, Amethyst), 17 in total. (`docs/design/brand-guidelines.md`, `docs/design/theme-catalog.css`, `docs/design/theme-catalog.json`)


## v0.7.0 — 2026-08-16

**Guilds**
- Guild invites: creation (optional `max_uses`/`expires_in_seconds`), redemption (race-safe against concurrent `max_uses`-limited use), listing, and revocation
- Guild visibility (`open`/`application`/`private`) and the join-request flow `application` guilds require (`REQUEST_JOIN`, `LIST_JOIN_REQUESTS`, `APPROVE_JOIN_REQUEST`/`REJECT_JOIN_REQUEST`)
- Rank-based guild roles (`role_rank`, replacing the old binary owner/member column) with three tiers — Crew, Officer, Captain — and per-guild theme-resolved display labels; member roster exposed via `LIST_MEMBERS`
- Dispatcher widened to return `std::vector<Message>`, so a single handler action (e.g. approving a join request) can notify two different recipients with two different payloads
- Density/polish pass on the guild view UI, wiring up presence, roster, and join-requests end to end
- Navigation reworked from horizontal tabs into a persistent Discord-style guild rail (Lobby, one icon per joined guild, Friends), with guild browse/join/create behind a `+` popover

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
- Customizable profiles: `display_name` and `avatar_url` (custom avatar upload, magic-byte validated, 2 MiB cap) are wired into every roster/message/list response that already resolved `username`; `bio`, `status_message` and `accent_color` are stored and editable but not yet displayed anywhere
- Friends, blocking, and direct-message UI in the web client

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

## v0.6.0 — 2026-07-31

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
