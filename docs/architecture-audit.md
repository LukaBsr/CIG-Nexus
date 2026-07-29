# Repository Structure Audit — Proposal

**Status: proposal only. Nothing in this document has been executed.** No
files have been moved, split, renamed, or deleted. Every recommendation
below is for review — see [Proposed Action List](#proposed-action-list) at
the end for the concrete, ordered list to confirm before anything changes.

Scope: this audit covers repo structure, documentation organization, and
frontend code organization ahead of a frontend rebuild. It does not audit
protocol correctness, security, or business logic — those were already
covered by the auth/persistence design work
(`docs/auth-discord-design.md`) and its implementation.

## 1. Current State Inventory

### Root level

| File | Tracked? | Purpose | Notes |
|---|---|---|---|
| `README.md` | yes | Project entry point: overview, quickstart, ports | 212 lines. Current, accurate. |
| `LICENSE` | yes | — | — |
| `.clang-format` | yes | C++ formatting config | — |
| `.gitignore` | yes | — | — |
| `docker-compose.yml` | yes | Full-stack local dev orchestration | 123 lines |
| `.env.example` | yes | Documents every required env var | — |
| `.env` | **no** (gitignored) | Real local secrets | Expected to be untracked. |
| `CLAUDE.md` | **no** (gitignored) | Claude Code project instructions | Must stay at root — Claude Code discovers it there by convention. Its untracked status was flagged twice in earlier sessions and left unresolved; noted here for completeness, not re-litigated. |
| `ARCHITECTURE.md` | **no** (gitignored) | System architecture — **stale** | 98 lines. Predates guilds/channels and auth entirely (still lists "Authentication and authorization" under "Out of Scope"). Fully superseded by `docs/architecture.md`, which is tracked and current. Effectively an orphaned duplicate. |
| `CURRENT_STATE.md` | **no** (untracked, not gitignored — just never added) | Point-in-time "resume after 4 months idle" snapshot dated 2026-07-13 | 146 lines. Documents bugs that are now fixed (e.g. `IDENTIFIED` missing `"type"` in its payload) and a commit history that predates the entire guilds/auth/persistence feature. No ongoing reference value — it was a one-time catch-up artifact, not a living doc. |

**Finding:** two of the four "extra" root files (`ARCHITECTURE.md`,
`CURRENT_STATE.md`) are stale, untracked, and actively misleading if a
contributor opens them expecting current information. Neither is linked
from `README.md`.

### `docs/` (currently flat)

| File | Lines | Topic |
|---|---|---|
| `docs/auth-discord-design.md` | 685 | Discord OAuth2 + Postgres persistence design record |
| `docs/rooms-spec.md` | 651 | Guilds/channels design record |
| `docs/architecture.md` | 184 | Overall system design, current and accurate |
| `docs/gateway-api.md` | 122 | Gateway's transport contract specifically |

### `shared/`

| File | Lines | Notes |
|---|---|---|
| `shared/protocol/README.md` | 532 | The canonical wire protocol spec. **This is the only file anywhere under `shared/`.** |

`shared/` as a directory name conventionally signals shared *code*
(types, constants, utilities used by multiple services). Today it contains
exactly one markdown file. See [§4](#4-docs-reorganization-proposal) for
the tradeoff on whether this should move.

### Per-service docs (not part of this reorg — see [Non-Goals](#6-explicit-non-goals))

| File | Lines |
|---|---|
| `server/README.md` | 191 |
| `gateway/README.md` | 175 |
| `web/README.md` | 132 |
| `desktop/README.md` | 29 |

### `server/` (C++)

63 tracked files (headers, sources, and tests together): `include/`
mirrors `src/` (`protocol/handlers/`,
`session/`, `guild/`, `auth/`, `http/`), plus `tests/` mirroring `src/`
again (`tests/protocol/`, `tests/guild/`, `tests/auth/`, `tests/http/`,
`tests/integration/`). This is a clean, consistent, already-established
layered structure — every handler is one class, one `.hpp`/`.cpp` pair,
one test file. Largest source files (`ChannelHandler.cpp` 354 lines,
`GuildHandler.cpp` 304 lines, `Server.cpp` 304 lines) are long because
they implement several related protocol methods each, not because they
mix unrelated concerns — see [§2](#2-one-document--one-purpose-audit).

### `gateway/` (TypeScript)

5 source files: `index.ts`, `ws/WsServer.ts`, `tcp/TcpClient.ts`,
`protocol/frame.ts`. Intentionally thin per `CLAUDE.md`'s "gateway stays
transport-only" constraint, and it is — no business logic anywhere in it.
Nothing here warrants restructuring.

### `web/` (Next.js)

This is where the volume is, and where the frontend rebuild will start.
Structure today:

```
web/
├── app/
│   ├── page.tsx                # 571 lines — the entire UI in one file
│   ├── layout.tsx               # still create-next-app boilerplate (see §3)
│   ├── api/auth/...             # OAuth routes (8 files)
│   └── internal/...             # internal catalog API (16 files)
├── db/                          # Drizzle schema + client
├── lib/
│   ├── gateway.ts                # WebSocket client + all send-helpers
│   ├── discord.ts
│   ├── auth/                     # 18 files (incl. tests) — env, jwt, session, rate limiting, etc.
│   └── internal/                 # 5 files (incl. tests) — internal API business logic
└── test/                         # shared test infrastructure
```

**There is no `components/` directory and no `hooks/` directory anywhere
in `web/`.** Every piece of UI, all WebSocket message routing, and all
local component state lives in the single 571-line `app/page.tsx`. This
is the main subject of [§3](#3-reusable-component-audit-web).

## 2. "One Document = One Purpose" Audit

Going file-by-file for anything mixing concerns that would benefit from
splitting:

- **`web/app/page.tsx` (571 lines) — mixes three concerns that should be
  three kinds of things**: (1) WebSocket connection lifecycle + protocol
  message routing (a 115-line `switch` on `msg.type` translating wire
  messages into state updates — this is controller/hook logic, not
  rendering), (2) local UI state for two entirely different views (global
  chat vs. guilds/channels) toggled by a single `view` flag instead of
  actual routes, and (3) all JSX/styling for both views. This is the
  single biggest finding in the whole audit — see
  [§3](#3-reusable-component-audit-web) for the concrete breakdown.
- **`server/src/protocol/handlers/{Guild,Channel}Handler.cpp`** (354 and
  304 lines) — **not flagged**. Each implements 5–6 methods for one
  cohesive protocol area (guild lifecycle, channel lifecycle), matching
  the existing one-handler-class-per-protocol-area convention already
  used by `ChatHandler`/`IdentifyHandler`/`HelloHandler`. Length here
  comes from validation thoroughness, not mixed responsibilities.
- **`docs/rooms-spec.md` (651 lines) and `docs/auth-discord-design.md`
  (685 lines)** — long, but each covers exactly one feature end to end
  (data model, protocol, service-by-service changes, deferred work) and
  is explicitly a point-in-time design record, not a living reference.
  Not flagged for splitting — length here is thoroughness, not scope
  creep. Only their *location* is in scope for this audit
  ([§4](#4-docs-reorganization-proposal)).
- **`shared/protocol/README.md` (532 lines)** — a genuine "maybe, low
  priority" candidate: it now covers `HELLO`/`WELCOME`/`IDENTIFY`,
  `CHAT_MESSAGE`, and the entire Guilds and Channels section (8+ message
  types) in one file. Splitting by area (e.g. a core-handshake section vs.
  a guilds/channels section as separate files) would aid navigation as
  more message types are added, but a single canonical wire-protocol spec
  is also a completely defensible convention on its own. **Not
  recommending a split now** — flagging it as something to revisit if/when
  this file crosses roughly the 800–1000 line mark, not before.
- **`web/lib/gateway.ts` (115 lines)** — not flagged. One send-helper per
  outbound message type, cohesive, already small.
- Everything else surveyed (`db/schema/*.ts`, `lib/auth/*.ts`,
  `lib/internal/*.ts`, all C++ handler/manager pairs) is already one
  file, one responsibility — this codebase is generally disciplined about
  file scope outside of `page.tsx`.

## 3. Reusable Component Audit (`web/`)

`web/app/page.tsx` is the only UI code in the entire frontend, so
"duplication" here specifically means duplication *within that one file*.
Concrete patterns found, each appearing 2+ times nearly verbatim:

1. **Message list rendering** — `chatMessages.map(...)` (lines 361–373)
   and `channelMessages.map(...)` (lines 545–558) render the same
   structure: an `<li>` with identical Tailwind classes, a header row
   (username + formatted timestamp), and a content div. → candidate for a
   single `MessageList`/`MessageListItem` component taking messages +
   maybe a key extractor.
2. **Text input + submit button** — the same inline `style={{...}}`
   objects (for the input: `flex:1, padding:0.5rem, borderRadius:4px,
   border:1px solid #ccc, fontFamily:monospace`; for the button:
   `padding:0.5rem 1rem, borderRadius:4px, background:#007bff,
   color:white, ...`) are copy-pasted **four times**: the chat input, the
   new-guild-name input, the new-channel-name input, and the
   channel-message input. → candidate for `TextInput`/`Button` primitives,
   or a single `TextInputWithSubmit` composite given how uniformly the
   pattern repeats.
3. **Tab/toggle buttons** — the "Global Chat" / "Guilds" view switcher
   (lines 287–316) is two near-identical buttons differing only in label
   and active-state check. → candidate for a small `TabButton` or
   `TabGroup` component, especially since more views (DMs? settings?) are
   a likely direction.
4. **Mixed styling paradigms** — the chat view uses raw inline
   `style={{...}}` objects throughout; the guilds view (added later) uses
   Tailwind `className` utility strings throughout, even though Tailwind
   is already a project dependency (`web/package.json`). This isn't just
   a style-consistency nit: it means the two views can't easily share the
   components proposed above without normalizing one or the other first.
   **Recommend standardizing on Tailwind classes** before extracting
   shared components, since inline style objects can't be trivially
   parameterized the way utility classes can.
5. **Protocol message routing lives inside a `useEffect`, not a hook** —
   the entire `switch (msg.type)` block, plus the three `useRef`s that
   exist specifically to work around its stale-closure problem
   (`myUserIdRef`, `activeGuildIdRef`, `activeChannelIdRef`), is 140+
   lines of non-rendering logic embedded in the page component. Extracting
   this into a `useGatewayConnection()` hook (returning `{ status,
   guilds, channels, messages, ... }` and the action dispatchers) would
   let `page.tsx` shrink to close to pure rendering, and would make the
   connection/state logic independently reusable if a second page/route
   ever needs it.
6. **No shared types for wire messages** — `Guild`, `Channel`,
   `ChatMessage`, `ChannelMessage` are defined inline at the top of
   `page.tsx` (lines 16–46), while `lib/gateway.ts`'s `connect()` still
   types its `onMessage` callback as `(msg: any) => void` (the one
   pre-existing lint error noted repeatedly throughout this project's
   history — `@typescript-eslint/no-explicit-any` on `lib/gateway.ts:4`).
   These belong together: shared message/response types should live next
   to `lib/gateway.ts` (or in a new `lib/types.ts`) and be imported by
   both, closing the `any` gap as a side effect rather than a separate
   task.

**Not a component-extraction finding, but directly relevant to planning
the rebuild:** `web/lib/gateway.ts` still sends
`{ type: "IDENTIFY", username: "web_user" }` on `WELCOME` — the
pre-auth protocol shape. It never calls
`GET /api/auth/session-token` or sends `session_token`. Since the
now-merged backend's `IdentifyHandler` requires `session_token` and
returns `AUTH_REQUIRED` without it
(`shared/protocol/README.md`), **the current web client cannot
successfully identify against the current server at all.**
`app/layout.tsx` is also still unmodified `create-next-app` boilerplate
(page title "Create Next App"). Given this, `page.tsx` is less "needs
refactoring" and more "will need to be substantially rewritten anyway" —
worth knowing before deciding how much effort to invest in
component-extracting the *existing* file versus treating this audit's
findings as a checklist for the rebuild instead.

## 4. `docs/` Reorganization Proposal

Proposed structure (topic-based subfolders, matching the pattern
suggested):

```
docs/
├── architecture-audit.md          # this document — stays at docs/ root (meta/process, not a feature doc)
├── architecture/
│   ├── overview.md                # ← docs/architecture.md
│   └── gateway-transport.md       # ← docs/gateway-api.md
├── protocol/
│   └── README.md                  # ← shared/protocol/README.md  (see tradeoff below)
├── auth/
│   └── discord-design.md          # ← docs/auth-discord-design.md
└── guilds/
    └── design.md                  # ← docs/rooms-spec.md (optionally renamed — see below)
```

Rationale per move:

- **`docs/architecture.md` → `docs/architecture/overview.md`**: makes
  room for `gateway-transport.md` alongside it without a naming collision,
  and reads naturally as "the architecture docs" as a group.
- **`docs/gateway-api.md` → `docs/architecture/gateway-transport.md`**:
  it's specifically about one service's transport contract within the
  overall architecture, not a standalone topic — grouping it under
  `architecture/` rather than inventing a `docs/gateway/` folder for one
  file.
- **`docs/auth-discord-design.md` → `docs/auth/discord-design.md`**: the
  folder name carries "auth," so the filename doesn't need to repeat it.
- **`docs/rooms-spec.md` → `docs/guilds/design.md`**: same reasoning, and
  `guilds` matches the code's own naming (`server/include/guild/`,
  `server/src/guild/`) rather than the historical "rooms" name the file's
  own banner already explains was superseded. **Optional**: could also
  just move to `docs/guilds/rooms-spec.md` (move only, no rename) if you'd
  rather not touch the filename a second time — either is fine, this is a
  nice-to-have, not a correctness issue.

**The one genuinely debatable move: `shared/protocol/README.md`.** Two
defensible positions:

- **Move it** (to `docs/protocol/README.md`) for consistency: "all docs
  live under `docs/`, all code lives in service directories" is a simple,
  easy-to-explain rule with no exceptions.
- **Keep it where it is**: `shared/` already reads as "the cross-service
  contract both `server/` and `gateway/` implement," which is a
  recognized pattern in polyglot repos even when the shared thing is a
  spec rather than code. Moving it also has by far the largest blast
  radius of any move in this proposal — it's referenced by path from
  `server/README.md`, `gateway/README.md`, `web/README.md`,
  `docs/architecture.md`, `docs/rooms-spec.md`,
  `docs/auth-discord-design.md`, and `CLAUDE.md`, all of which would need
  their links updated in the same change.

**Recommendation: move it, for consistency, but do it as its own isolated
commit** (not bundled with the other, lower-blast-radius moves), specifically
because of the cross-reference count. If you'd rather avoid that blast
radius entirely, leaving `shared/protocol/README.md` exactly where it is
is a completely reasonable call — flagging the tradeoff rather than
treating this as settled.

Cross-reference fallout applies to every move above, just less severely:
each moved file needs its incoming links (from `README.md`, the
per-service READMEs, and sibling docs' "Related Documentation" sections)
updated to the new path.

## 5. Naming/Convention Consistency Check

- **snake_case wire types leak into browser-facing code without a
  mapping layer.** `lib/gateway.ts`'s outbound functions take idiomatic
  camelCase parameters (`createChannel(guildId, name, channelType)`) and
  only convert to snake_case at the point of JSON serialization — correct
  and consistent. But nothing does the reverse for *inbound* messages:
  `page.tsx`'s `Guild`/`Channel`/`ChatMessage`/`ChannelMessage` types
  (and all the state built from them) use the wire's snake_case field
  names directly (`guild_id`, `owner_id`, `channel_type`) throughout the
  component. `web/lib/internal/*.ts`'s `WireGuild`/`WireChannel`-style
  types also use snake_case, but that's a *different, consistent*
  convention — those are explicitly wire-shaped types for a
  machine-to-machine internal API, named `Wire*` to signal that. The
  inconsistency is specifically that outbound (camelCase params) and
  inbound (snake_case fields used as-is) don't match on the *browser*
  side. Worth deciding one convention and writing it down — either is
  fine, but it isn't currently documented anywhere, so it reads as
  accidental rather than intentional.
- **`web/app/internal/*` doesn't follow the `/api/` prefix** used by
  `web/app/api/auth/*`, even though both are technically API routes. This
  is *intentional* — the path has to match `docs/auth-discord-design.md`
  §8.1's literal `/internal/*` wire contract and the isolation mechanism
  in `web/server.mjs` — but nothing in the repo says so at the point a
  contributor would notice the asymmetry. A one-line comment or a short
  `web/app/internal/README.md` explaining "this is intentionally outside
  `/api/` because of the isolation design" would close the gap cheaply.
- **CI workflow naming is inconsistent**: `.github/workflows/ci-server.yml`
  (prefix) vs. `.github/workflows/web-ci.yml` (suffix) — pick one
  ordering. Related but separate gap: **there is no gateway CI workflow
  at all**, so `gateway/` changes are the only service not covered by any
  automated check.
- **File-naming casing differs by language, which is fine** —
  `server/` is consistently PascalCase-per-class (idiomatic C++),
  `web/lib/` is consistently camelCase-per-module (idiomatic TS for
  function-exporting files, as opposed to class-exporting ones). Not
  flagging this as an inconsistency; different ecosystems, different
  idioms, both internally consistent. Worth noting `gateway/src/`
  actually applies a *finer-grained* version of the same rule
  (`WsServer.ts`/`TcpClient.ts` are PascalCase because they export a
  class; `frame.ts`/`index.ts` are lowercase because they export plain
  functions) — that finer distinction isn't consistently applied in
  `web/`, where everything is camelCase regardless of whether the module
  is class-like. Low priority, purely stylistic.
- **No naming-convention lint rule is configured anywhere** (checked
  `web/eslint.config.mjs` — it's just `eslint-config-next` defaults, no
  `@typescript-eslint/naming-convention` or similar). Any convention
  decided above will stay a documentation-only convention unless a rule
  is added — noting this, not recommending it be added in this pass.

## 6. Explicit Non-Goals

Things this audit deliberately does not touch or propose changing:

- **`desktop/`** — explicitly out of scope per `CLAUDE.md` ("the desktop
  client exists in the repo but is not the active development path").
  Not renamed, not restructured, not otherwise touched.
- **`server/`'s internal structure** — already consistent and
  well-organized (see [§1](#1-current-state-inventory)); no moves,
  splits, or renames proposed inside `server/include`/`server/src`/`server/tests`.
- **`gateway/`'s internal structure** — already minimal and clean; no
  changes proposed.
- **`shared/protocol/README.md`'s content** — flagged as a *possible
  future* split candidate by size ([§2](#2-one-document--one-purpose-audit)),
  but not recommended now, and not attempted in this pass regardless of
  where the file ends up living.
- **The CLAUDE.md git-tracking question** — raised in earlier sessions,
  still unresolved, out of scope for this particular audit; mentioned
  in [§1](#1-current-state-inventory) for completeness only.
- **Actually building any `components/`/`hooks/` structure, or extracting
  any of the patterns identified in [§3](#3-reusable-component-audit-web)**
  — this document identifies *what* should be extracted so the rebuild
  doesn't start by duplicating existing duplication; it doesn't perform
  that extraction. That's frontend-rebuild work, not repo-structure work.
- **Fixing the CI naming inconsistency or adding a gateway CI workflow** —
  flagged in [§5](#5-namingconvention-consistency-check) as a finding,
  not proposed as an action in this pass.
- **Per-service `README.md` files** (`server/README.md`,
  `gateway/README.md`, `web/README.md`, `desktop/README.md`) — these
  already live correctly next to their code and are excluded from the
  `docs/` reorganization; only the root-level standalone docs and the
  currently-flat `docs/` folder are in scope for §4.

## Proposed Action List

Ordered by independence/risk — items later in the list either depend on
earlier ones or carry more cross-reference fallout. Each needs your
explicit go-ahead; none of this has been executed.

1. **Delete `ARCHITECTURE.md`** (root, untracked, superseded by
   `docs/architecture.md`). No cross-references to fix — it's untracked
   and unlinked.
2. **Delete `CURRENT_STATE.md`** (root, untracked, stale point-in-time
   snapshot with no ongoing value). Same — no cross-references.
3. **Create `docs/architecture/`, `docs/auth/`, `docs/guilds/` and move**:
   - `docs/architecture.md` → `docs/architecture/overview.md`
   - `docs/gateway-api.md` → `docs/architecture/gateway-transport.md`
   - `docs/auth-discord-design.md` → `docs/auth/discord-design.md`
   - `docs/rooms-spec.md` → `docs/guilds/design.md` (or
     `docs/guilds/rooms-spec.md` if you'd rather not rename the file)

   Update incoming links in `README.md`, `server/README.md`,
   `gateway/README.md`, `web/README.md`, and each moved doc's own
   "Related Documentation" section.
4. **Decide on `shared/protocol/README.md` → `docs/protocol/README.md`**
   as its own isolated change, given the cross-reference count — see the
   tradeoff in [§4](#4-docs-reorganization-proposal). Do this separately
   from #3 regardless of which way you decide.
5. **Add a short note** (comment or `web/app/internal/README.md`)
   explaining why `web/app/internal/*` intentionally sits outside
   `/api/*` — cheap, closes a real "fresh contributor" confusion gap.
6. **Decide and document the wire-message casing convention** for
   browser-facing code (§5) — e.g. "inbound wire messages get mapped to
   camelCase before touching component state, matching how outbound
   messages already work." This is a decision + a documentation task now;
   applying it is naturally part of the `page.tsx` rebuild in #7.
7. **When the frontend rebuild starts**: extract the five patterns in
   [§3](#3-reusable-component-audit-web) (message list, text-input +
   button, tab buttons, a `useGatewayConnection` hook, shared wire-message
   types) into `web/components/`/`web/hooks/` as the rebuild's foundation,
   rather than rebuilding `page.tsx` in place and re-creating the same
   duplication. This also means wiring up the OAuth login flow and
   `session_token`-based `IDENTIFY`, which the current client doesn't do
   at all (§3) — worth confirming this is understood as a rebuild
   prerequisite, not an optional polish item.
8. *(Lower priority, not blocking anything)* Normalize CI workflow naming
   and add a `gateway` CI workflow (§5).
