# Frontend Rebuild Plan

**Status: executed.** The rebuild this document planned — session-token
`IDENTIFY`, `components/`/`hooks/` extraction, Tailwind, the CIG Nexus
visual identity — has been built. This document is kept as-written (a
point-in-time plan, not a changelog) for the
[wire-message casing convention](#wire-message-casing-convention) it
decided, which is still live: every new inbound `Wire*` type follows it.
The [Deferred](#deferred) section below is the one part still updated
going forward, since deferred items get resolved by later work.

## Why this rebuild is needed, not optional polish

`web/lib/gateway.ts`'s `connect()` still sends
`{ type: "IDENTIFY", username: "web_user" }` on `WELCOME` — the pre-auth
protocol shape. The now-merged backend's `IdentifyHandler` requires a
`session_token` and returns `AUTH_REQUIRED` without one (see
[`shared/protocol/README.md`](../shared/protocol/README.md) and
[`docs/auth/discord-design.md`](auth/discord-design.md) §8). **The current
web client cannot successfully identify against the current server at
all.** Rewriting `lib/gateway.ts` to call `GET /api/auth/session-token` and
send `session_token` in `IDENTIFY` is therefore the load-bearing
prerequisite for this rebuild, not a nice-to-have alongside it — nothing
else here matters if the client can't connect.

`web/app/layout.tsx` is also still unmodified `create-next-app`
boilerplate (page title "Create Next App") — worth fixing as part of the
same pass, though it isn't blocking.

## Prerequisite, in order

1. **`lib/gateway.ts` session-token rewrite.** `connect()`'s `IDENTIFY`
   payload changes from `{ username: "web_user" }` to
   `{ session_token: <jwt fetched from GET /api/auth/session-token> }`.
   `onMessage`'s `any` parameter (`lib/gateway.ts:4`) gets replaced by a
   real union type as part of the shared-types work below — don't leave it
   as `any` in the rewrite.
2. **Standardize on Tailwind classes only, no inline `style={{...}}`
   objects.** This must happen *before* component extraction, not after —
   it's what makes it possible to share components between the two
   existing views at all. Per the audit (§3, finding 4): the chat view
   currently uses raw inline `style={{...}}` objects throughout, while the
   guilds view uses Tailwind `className` utilities throughout, even though
   Tailwind is already a `web/package.json` dependency. Normalize both
   views onto Tailwind before extracting anything — inline style objects
   can't be trivially parameterized the way utility classes can, so
   extracting a shared component first would just bake the inconsistency
   into the new component's props.

Everything below assumes both of these are already done.

## Wire-message casing convention

**Decision** (resolves audit item 6): snake_case stays on the wire exactly
as the protocol defines it — no change to `shared/protocol/README.md` or
the server. Conversion to camelCase happens at exactly one boundary: inside
the `useGatewayConnection` hook described below. Components never see
snake_case fields; every piece of state the hook exposes (guilds, channels,
messages, etc.) is already camelCase by the time a component reads it.

This mirrors how outbound messages already work correctly today —
`lib/gateway.ts`'s outbound functions take camelCase parameters
(`createChannel(guildId, name, channelType)`) and only convert to
snake_case at the point of JSON serialization. The inbound side just never
had an equivalent conversion point before; the hook boundary is that point,
not each component that happens to render wire data.

Concretely: `useGatewayConnection`'s `onMessage` switch (see below) is
where `{ guild_id, owner_id, channel_type, ... }` from the wire becomes
`{ guildId, ownerId, channelType, ... }` before being written into any
`useState`. `lib/types.ts` (below) should declare both shapes if useful —
a `Wire*` snake_case type matching the wire exactly (matching the naming
convention `web/lib/internal/*.ts` already uses for its own wire-shaped
types), and the camelCase type components actually consume — with a mapper
function between them living next to the wire types.

## Extraction targets (audit §3)

All five, in `web/components/`, `web/hooks/`, and `web/lib/types.ts`
respectively. Line numbers below are from `web/app/page.tsx` as of this
writing (571 lines) — expect drift once the Tailwind-normalization and
gateway.ts prerequisites land first.

1. **`MessageList` / `MessageListItem`** (`web/components/`) — replaces the
   duplicated `chatMessages.map(...)` (page.tsx:361-373) and
   `channelMessages.map(...)` (page.tsx:545-558) blocks, which render an
   identical `<li>` structure: a header row (username + formatted
   timestamp) and a content div. Props: a message array plus whatever key
   extractor/formatter is needed to handle `ChatMessage` and
   `ChannelMessage` as distinct-but-compatible shapes.
2. **`TextInput` / `Button` primitives** (`web/components/`) — replaces
   the four copy-pasted inline-style input+button pairs (chat input,
   new-guild-name input, new-channel-name input, channel-message input).
   Typed props, no `any`. Consider a composite `TextInputWithSubmit` given
   how uniformly the pattern repeats across all four call sites, but the
   two primitives should exist independently either way.
3. **`TabButton` / `TabGroup`** (`web/components/`) — replaces the
   "Global Chat" / "Guilds" view switcher (page.tsx:287-316), currently
   two near-identical buttons differing only in label and active-state
   check.
4. **`useGatewayConnection` hook** (`web/hooks/`) — the real center of this
   rebuild. Replaces the 140+-line `useEffect` currently holding the
   `switch (msg.type)` protocol routing, plus the three `useRef`s that
   exist only to work around its stale-closure problem (`myUserIdRef`,
   `activeGuildIdRef`, `activeChannelIdRef`). Owns the wire-to-camelCase
   conversion described above. Returns connection `status`, all derived
   state (`guilds`, `channels`, `messages`, etc., already camelCase), and
   the action dispatchers (`createGuild`, `joinChannel`, `sendChatMessage`,
   ...) — thin wrappers around `lib/gateway.ts`'s existing send-helpers.
   `page.tsx` should shrink to close to pure rendering once this exists.
5. **Shared wire-message types** (`web/lib/types.ts`) — `Guild`, `Channel`,
   `ChatMessage`, `ChannelMessage` currently defined inline at the top of
   `page.tsx` (lines 16-46) move here, imported by both `lib/gateway.ts`
   and the new hook/components. This is also where the `Wire*` /
   camelCase type pairs from the casing-convention section above should
   live, closing the pre-existing `@typescript-eslint/no-explicit-any` on
   `lib/gateway.ts:4` as a side effect rather than a separate task.

## General clean-code expectations for the rebuild

Matching the discipline already established in `server/` and
`web/lib/auth/` / `web/lib/internal/`:

- **One component = one file = one purpose.** No component doing UI
  rendering *and* protocol routing *and* raw fetch calls in one file — that
  mixing is exactly what's being extracted out of `page.tsx`.
- **No duplicated inline styling.** Tailwind utility classes only (see
  prerequisite above); if the same class combination repeats 3+ times,
  that's a signal that a component boundary is missing, not a place for a
  copy-pasted `className` string.
- **Typed props, no `any`.** Every component and hook gets real prop/return
  types. `lib/gateway.ts`'s `onMessage: (msg: any) => void` is the one
  standing exception today and should not survive this rebuild.
- **Server-authoritative state stays server-authoritative.** The hook
  reflects what the server sends; it doesn't invent optimistic local state
  the server hasn't confirmed, consistent with how the C++ server is
  already the authority for every protocol decision.

## Explicit non-goals for this plan document

- No code in this document has been written or extracted — this is
  planning only, per the closing instruction that produced it.
- Login/OAuth UI (the actual "click to log in with Discord" flow) is
  in scope for the rebuild but not detailed further here beyond the
  `lib/gateway.ts` session-token prerequisite above — the OAuth routes
  themselves (`/api/auth/discord/login`, `/callback`) already exist
  server-side per [`docs/auth/discord-design.md`](auth/discord-design.md);
  only the browser-side "start login" affordance is new UI work.
- CI/lint enforcement of any convention decided here (e.g. a
  naming-convention ESLint rule) is out of scope, consistent with
  `docs/architecture-audit.md` §5's note that no such rule exists today.

## Deferred

- **Guilds/channels layout rework — partially unblocked, not yet done.**
  The current structure (a fixed guild rail, a channel-pill row, and a
  single message pane) is unchanged. Of the features this item was
  waiting on, roles/permissions beyond owner-only and a member roster now
  exist end-to-end (`docs/guilds/social-presence-design.md` — `role_rank`,
  `LIST_MEMBERS`/`SET_MEMBER_ROLE`), but only as data: `members` is
  fetched and used for permission gating (`web/app/page.tsx`'s
  `isOfficerOrAbove`), with no visible member-list UI anywhere yet.
  Guild/member avatars and functional `VOICE` channels are still entirely
  unbuilt. Revisit the layout once a real member-list panel is worth
  building, not before — this item stays deferred, just with less
  remaining to wait on than when it was written.
- **Selectable themes — done.** `docs/settings/appearance-design.md`
  built this: a settings modal with an Appearance section, a
  `data-theme`-attribute token system, and two themes (`abyss` default,
  `ember`). The reasoning below turned out to be half right — a *light*
  theme genuinely would be a second design pass (still out of scope, still
  no concrete reason for it), but an *accent-only dark variant* was in
  fact a small, additive change once the palette was expressed as CSS
  custom properties rather than hardcoded per-component colors. Original
  note, left for context: "the visual identity pass settled on one dark
  theme, deliberately — the brand mark's own background is baked-in dark,
  so a second (e.g. light) theme isn't a small variant of the first, it's
  a second design pass. Out of scope until there's a concrete reason a
  user would need it."
