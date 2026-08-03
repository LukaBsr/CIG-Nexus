# Settings Menu and Appearance/Theme System — Design Document

**Status: design, not implemented.** This is a planning document only.
Nothing described here has been built. Do not start implementation
against it until it has been reviewed and the arbitration point flagged
below (search for **`ARBITRATION`**) has been resolved.

## Scope

Two related but separable pieces:

1. **Settings shell** — a settings surface with one section today
   (Appearance) but a structure that doesn't need rearchitecting when a
   second and third section (Account, Notifications, ...) show up later.
2. **Theme system** — replace the single hardcoded ink/teal/violet
   palette (`web/app/globals.css`) with a small registry of named themes,
   switchable at runtime, with a local-first / optionally-synced
   persistence model.

**Not addressed here, explicitly out of scope:**

- The actual content of any settings section beyond Appearance (Account,
  Notifications, etc.) — this document only establishes that the shell
  has room for them, not what they contain.
- Per-guild or per-channel theme overrides — theming is account/device
  scoped, global to the whole app, same granularity as `role_theme` is
  per-guild but nothing here proposes a per-guild *appearance* theme.
- Light mode / system-preference (`prefers-color-scheme`) detection —
  `web/app/globals.css` already documents this product as "dark-only by
  design," not "dark theme option." Every theme this document adds is a
  dark palette variant; switching to a light-capable model is a separate,
  larger decision not made here.
- Live cross-device/cross-tab theme push while multiple sessions are
  open simultaneously. Sync updates the account's stored value and other
  devices pick it up **on their next login**, not in real time over the
  open WebSocket. Real-time propagation is a reasonable future addition
  (the protocol already has the machinery — see
  `docs/social-presence-design.md`'s `TARGETED`/`BROADCAST` delivery
  modes) but isn't designed here.

---

## 1. Settings Shell

### 1.1 UI pattern: modal, not a panel

**Recommendation: a centered modal dialog**, opened from a new gear icon
in `Header` (`web/components/Header.tsx`), sitting alongside
`SignalIndicator`.

Reasoning: the app's only existing multi-view mechanism is `TabGroup`
(`Lobby` / `Guilds`), which is *navigational* — it changes what's
occupying the main content area for as long as you're looking at it.
Settings isn't navigation in that sense; it's a transient overlay you
open, change something in, and dismiss, without losing your place in the
lobby or guild you were in. A modal captures that "temporary overlay on
top of unchanged state underneath" model directly; a third `TabGroup`
entry would make Settings look like a peer of Lobby/Guilds and imply it
replaces the main view the way switching tabs does today, which isn't
the right mental model and would need `activeGuildId`/`activeChannelId`
state to survive being unmounted for no real reason.

A right-side slide-in panel was considered and rejected only because it
adds a persistent-vs-overlay layout question (does it push content over
or float above it?) that a centered modal sidesteps entirely — not
because it's a wrong pattern in general. If a future section (e.g. a
live notification feed) turns out to want to stay open *alongside* the
main view rather than blocking it, that's a reason to revisit, not a
reason to build the panel now speculatively.

### 1.2 Structure: a section registry, not a single form

The modal is a fixed shell (backdrop, dismiss button, title) around two
things that don't change shape as sections are added:

- A **section list** (left rail inside the modal, or a top tab strip on
  narrow viewports) driven by a small static registry:

  ```ts
  // web/lib/settings/sections.ts (illustrative, not final naming)
  export interface SettingsSection {
    id: string;                 // "appearance", "account", ...
    label: string;              // display name
    Component: React.ComponentType;
  }

  export const SETTINGS_SECTIONS: SettingsSection[] = [
    { id: "appearance", label: "Appearance", Component: AppearanceSettings }
  ];
  ```

- A **content area** that renders `SETTINGS_SECTIONS.find(s => s.id ===
  activeSectionId).Component`.

Adding "Account" or "Notifications" later means appending one entry to
`SETTINGS_SECTIONS` and writing that one component — the modal shell,
section-switching logic, and open/close state never change. This is the
same reason `docs/social-presence-design.md` §2.2 fixes the
authorization *model* in place (rank thresholds) so adding a tier later
is additive rather than a rework: here the *shell* is fixed in place so
adding a section is additive rather than a rework.

With only one section (Appearance) existing initially, the section rail
technically has nothing to switch between — it should still be built as
a list of one, not as "just show Appearance directly and add the rail
later," since retrofitting the rail after the fact is exactly the
rearchitecting this section structure exists to avoid.

### 1.3 State ownership

Modal open/closed state (`useState<boolean>` or similar) lives wherever
`Header` is rendered (`web/app/page.tsx` today), passed down the same
way `view`/`setView` already is for the Lobby/Guilds tabs. No new global
state management is introduced for this — the theme value itself (§2–3)
is the only piece of state that needs to outlive the modal being closed,
and that's handled separately (§3), not as modal state.

---

## 2. Theme System

### 2.1 Token architecture: CSS custom properties, swapped by a `data-theme` attribute

`web/app/globals.css` already defines its palette as Tailwind v4
`@theme` variables:

```css
@theme {
  --color-ink: #0d0e18;
  --color-surface: #1a1c2e;
  --color-slate: #4a4e72;
  --color-teal: #5eead4;
  --color-violet: #8b5cf6;
  --color-ivory: #edeffb;
}
```

Tailwind v4 compiles utilities like `bg-teal`/`text-violet` (used
throughout `web/components/*` and `web/app/*` today — 15+ call sites)
against `var(--color-teal)` etc., not against the literal hex value.
That means every existing component **already** gets its color from a
CSS custom property indirectly; nothing about `bg-teal`/`text-ivory`
call sites needs to change. What changes is only where those variables'
*values* come from:

```css
/* Default values (today's palette), scoped to :root so they apply
   before any theme is known — see §3.2 for why this matters. */
:root {
  --color-ink: #0d0e18;
  --color-surface: #1a1c2e;
  --color-slate: #4a4e72;
  --color-teal: #5eead4;
  --color-violet: #8b5cf6;
  --color-ivory: #edeffb;
}

/* Named theme overrides, selected by a data-theme attribute on <html>. */
[data-theme="abyss"] {
  /* identical to :root above — abyss is the default theme */
}

[data-theme="ember"] {
  --color-teal: #f87171;   /* red accent replaces teal */
  --color-violet: #fb923c; /* warm secondary accent */
  /* ink/surface/slate/ivory unchanged — only the accent pair differs */
}

@theme inline {
  /* Tailwind v4: point the generated utilities at the cascade-resolved
     custom properties above, rather than hardcoding values in @theme
     directly. This is what makes bg-teal etc. resolve to whichever
     [data-theme="..."] block is active on <html>. */
  --color-ink: var(--color-ink);
  --color-surface: var(--color-surface);
  --color-slate: var(--color-slate);
  --color-teal: var(--color-teal);
  --color-violet: var(--color-violet);
  --color-ivory: var(--color-ivory);
}
```

(The exact Tailwind v4 mechanics for "generate utilities from
already-declared custom properties" should be verified against the
installed Tailwind version at implementation time — the shape above is
the intended *result*: existing utility classes keep working unchanged,
and a `data-theme` attribute is the single switch that changes what they
resolve to. This is a CSS/build-tool detail, not a design decision, and
is why it's not treated as an `ARBITRATION`.)

Switching themes at runtime is then one DOM write —
`document.documentElement.setAttribute("data-theme", themeId)` — with no
React re-render required for the color change itself to take effect,
since the browser recomputes the cascade immediately. React state is
still used to drive the *settings UI* (which theme is selected/shown),
but the color change itself doesn't depend on a re-render completing.

### 2.2 Theme registry: mechanism decoupled from content

Mirroring `docs/social-presence-design.md` §2.2's `role_rank`/`role_theme`
split — the *mechanism* (an integer rank, a `data-theme` attribute) is
fixed in place once; the *content* (how many themes exist, what they're
called) is free to grow without touching the mechanism:

```ts
// web/lib/appearance/themes.ts (illustrative)
export interface ThemeDefinition {
  id: string;          // "abyss", "ember" — matches a [data-theme="..."] CSS block
  label: string;        // "Abyss (default)", "Ember"
  swatch: { accent: string; secondary: string }; // for rendering a preview dot in the picker UI
}

export const THEMES: ThemeDefinition[] = [
  { id: "abyss", label: "Abyss (default)", swatch: { accent: "#5eead4", secondary: "#8b5cf6" } },
  { id: "ember", label: "Ember",            swatch: { accent: "#f87171", secondary: "#fb923c" } }
];

export const DEFAULT_THEME_ID = "abyss";
```

Adding a third theme is: one new `[data-theme="..."]` CSS block, one new
`THEMES` entry, zero changes to the picker component, the persistence
code (§3), or any consuming component's `bg-teal`/`text-violet` classes.
This is deliberately the same shape as `docs/social-presence-design.md`
§2.2's `ROLE_THEMES` static registry (recommended there over a DB table
"since exactly one theme exists in this iteration" and a table would buy
data-driven themes nothing in the request asks for yet) — same
reasoning applies here with two themes instead of one.

**Naming**: `abyss` (today's ink/teal/violet, marked default) and
`ember` (red/orange accent variant) are used as concrete illustrative
names throughout this document so the examples are readable; the actual
launch names aren't a design decision this document needs to make.

### 2.3 Unknown/removed theme ids degrade, not error

If a stored theme id (local or synced) doesn't match any entry in
`THEMES` — a theme was deprecated after a user selected it, or a synced
value predates a registry change — the picker and the `data-theme`
application both fall back to `DEFAULT_THEME_ID` rather than rendering
broken/unstyled UI or throwing. Same "a display gap should degrade, not
break the response" principle `docs/social-presence-design.md` §2.2
applies to an unmapped `role_rank`/`role_theme` combination.

---

## 3. Persistence

### 3.1 Local application is always immediate and unconditional

Selecting a theme in the picker applies it **locally right away**,
regardless of the sync toggle's state — `sync` only ever controls
whether that same value *also* gets written to the account. This keeps
the product usable offline and keeps theme changes feeling instant (no
network round trip gates the visual change), matching how
`docs/social-presence-design.md` §4.5 already chose to keep the hottest
path (message send) independent of Postgres availability where
possible.

### 3.2 Where the local value lives: a cookie, not `localStorage`

**Recommendation: a plain (non-`httpOnly`) cookie**, e.g. `theme`,
read server-side.

The concrete failure mode to avoid is **flash of wrong theme (FOWT)**: a
Next.js App Router page renders server-side first; if the initial theme
were only knowable from `localStorage`, the server would have to render
*some* default (today's `abyss`) and then a client-side script would
swap `data-theme` after hydration — producing a visible flash on every
load for anyone using `ember`. `RootLayout` (`web/app/layout.tsx`) is
already a server component; reading a plain cookie there via
`next/headers`' `cookies()` and setting `data-theme` directly on the
server-rendered `<html>` element means the very first byte of HTML the
browser paints already has the right theme — zero flash, zero blocking
inline script needed (the common `localStorage`-based workaround, used
by libraries like `next-themes`, is exactly a small blocking
`<script>` in `<head>` to avoid this same flash; a cookie sidesteps
needing that script at all because the server already knows the value).

This mirrors `REFRESH_COOKIE`'s existing pattern (`web/lib/auth/session.ts`)
of a cookie read server-side to make an early decision — with the
opposite `httpOnly` setting, since the refresh cookie is deliberately
inaccessible to JS (session security) while the theme cookie must be
directly writable by client JS for an instant local switch that doesn't
wait on a request round trip:

```ts
// web/lib/appearance/cookie.ts (illustrative)
export const THEME_COOKIE = "theme";
export const THEME_COOKIE_OPTIONS = {
  httpOnly: false,        // must be readable/writable by client JS
  secure: process.env.NODE_ENV === "production",
  sameSite: "lax" as const,
  path: "/",
  maxAge: 60 * 60 * 24 * 400 // ~13 months, cookie max per current browser caps
};
```

Client-side, changing the theme is `document.cookie = ...` (or a tiny
wrapper) plus the `data-theme` DOM write from §2.1 — both synchronous,
no fetch involved unless sync is also on (§3.5).

### 3.3 The sync toggle

A boolean, shown in the Appearance section next to the theme picker
("Sync across devices"). Its effect:

- **Off** (default): the `theme` cookie is the only source of truth,
  exactly as today's `web/public/branding` styling is effectively
  device-local already. Nothing is sent to the server when the theme
  changes.
- **On**: every local theme change is also pushed to the account
  (§3.5); on every future login (§3.6), the account's stored theme is
  pulled and overwrites the local cookie on that device, even if that
  device had a different local value.

### 3.4 Data model

```sql
ALTER TABLE users ADD COLUMN theme TEXT;
ALTER TABLE users ADD COLUMN theme_sync_enabled BOOLEAN NOT NULL DEFAULT false;
```

`theme` is nullable with no `DEFAULT` — `NULL` means "sync has never
been turned on for this account, or was turned on and off without ever
successfully pushing a value," and is treated identically to
`DEFAULT_THEME_ID` by any reader, the same degrade-not-break handling as
§2.3. A `NOT NULL DEFAULT 'abyss'` was considered and rejected: it would
make every account look like it has an explicit synced preference from
the moment the migration runs, before any user has ever touched the
sync toggle, which is a false signal (`theme_sync_enabled = false` is
already sufficient to say "this column doesn't mean anything yet" —
having `theme` simultaneously carry a non-NULL default fights that).

`theme` is **free text, not a Postgres `ENUM`** — deliberately mirroring
`guild_memberships`' move *away* from a string-literal role model
(`docs/social-presence-design.md` §2.2) but in the *other* direction
from `guilds.visibility`'s `guild_visibility` `ENUM`
(`docs/social-presence-design.md` §1.7). The difference is what each
value's cardinality/change-rate looks like going forward: guild
visibility is a small, fixed, behaviorally-load-bearing set (each value
branches real server logic) unlikely to grow — a closed `ENUM` fits.
Themes are the opposite: purely cosmetic, expected to grow over time
(same as `role_theme`, §2.2 of that same document, which is *also* free
text for exactly this reason), and adding one should never require a
migration. Free text plus the code-level registry validating/falling
back on unknown values (§2.3) is the same tradeoff `role_theme` already
made, applied here on purpose.

No new index is needed — `theme`/`theme_sync_enabled` are only ever
read/written by primary-key (`users.id`) lookup, already covered by the
table's existing primary key.

### 3.5 API surface: a session-authenticated `/api` route, not an `/internal/*` route

This is a **browser-facing** feature with no C++/WebSocket protocol
involvement at all — the gateway/server never need to know a user's
theme. It does **not** belong under `web/app/internal/*`, which is
reserved for the C++ server's own Postgres-access bridge, authenticated
by `INTERNAL_API_SHARED_SECRET` and never called by a browser
(`docs/auth/discord-design.md` §8.1/§9.1's isolation requirement — every
route under `internal/` is covered by `web/test/internalIsolation.test.ts`
specifically *because* it must never be browser-reachable). A theme
endpoint must be reachable directly from the browser, so it's an
`web/app/api/*` route instead, authenticated the same way
`GET /api/auth/session-token` already is — the `__session` httpOnly
refresh cookie, resolved to a `users` row via `findActiveSessionByRefreshToken`.

```
PATCH /api/user/appearance
```

Body (both fields optional; only the fields present are updated —
changing the theme while sync is already on doesn't require resending
the sync flag, and toggling sync doesn't require resending the theme):

```json
{ "theme": "ember", "sync_enabled": true }
```

Response: the resulting row's `{ "theme": "ember", "sync_enabled": true
}`, so the client can confirm what actually got stored (relevant for the
`ARBITRATION` below, where "turning sync on" and "what theme ends up
synced" aren't always the same value the client sent).

No `GET` endpoint is needed. The pull-on-login described in §3.6 happens
as a side effect of the existing login flow, not as a separately-fetched
value — see below.

**Implementation note, added post-design**: the claim above ("no `GET`
endpoint is needed") covers the pull-on-login flow, but a second gap
surfaced during implementation — the Appearance section still needs to
render the sync toggle's correct checked/unchecked state on open,
including on a fresh page load, and doing that with a `GET` would
reintroduce exactly the round trip this section is arguing against. The
fix mirrors §3.2's own local-cookie approach one level up: a second,
non-httpOnly cookie, `theme_sync` (`"1"` or `"0"`), locally mirrors
`users.theme_sync_enabled`, written by the same client-side helper and
under the identical `secure`/`sameSite: "lax"`/`httpOnly: false` flags
`THEME_COOKIE_OPTIONS` already defines for `theme` (§3.2) — same
non-sensitive, non-authorization-bearing security posture, just a
second single-purpose flag rather than a second copy of account state.
It's updated wherever `theme` is: on every successful
`PATCH /api/user/appearance` sync-flag change, and (once §3.6 lands) as
part of pull-on-login alongside the theme cookie itself.

### 3.6 Pull-on-login happens in the OAuth callback, not on every page load

`GET /api/auth/discord/callback` (`web/app/api/auth/discord/callback/route.ts`)
already does a server-side `NextResponse.redirect(new URL("/",
request.url))` after creating the session, and already attaches the
refresh cookie to that same response via `response.cookies.set(...)`.
This is the natural, already-existing point to also read
`theme`/`theme_sync_enabled` for the just-authenticated user and, if
`theme_sync_enabled` is true, set the local `theme` cookie on that same
redirect response to the account's synced value — overwriting whatever
that device's `theme` cookie previously held, per the requested
behavior ("on login on any device with sync enabled, the account's
theme is pulled and applied, overriding that device's local value").

This is deliberately **not** implemented as a check on every request in
`RootLayout` — that would add a Postgres read to every single page load
for every user (sync on or off), for a value that only actually changes
at login. Doing it once, at the one server-side redirect that already
exists at login time, gets the identical end-user-visible result
(correct theme from first paint, every session) at zero added steady-
state cost. If a login flow is ever added that *doesn't* go through a
server-redirect (a pure client-side auth flow), this decision needs
revisiting — everything else in this document is unaffected either way.

### 3.7 `ARBITRATION`: conflict resolution when sync is turned on

**The scenario**: a device has local theme `ember` (sync currently off).
The account already has a synced value from a *different* device — say
`abyss`, with `theme_sync_enabled = true` account-wide already (sync was
turned on previously, elsewhere). The user now flips the sync toggle on
*this* device. Which value wins?

Two reasonable answers:

- **Push (last-write-wins on the toggle action)**: turning sync on, on
  this device, pushes this device's current local theme (`ember`) to
  the account, overwriting the account's previous synced value
  (`abyss`). The device where you just took an action wins.
- **Pull (existing account state wins)**: turning sync on adopts
  whatever the account already has (`abyss`), discarding this device's
  local selection — treating "the account already has a synced
  preference" as authoritative over "I just flipped a toggle."

**Recommendation: push / last-write-wins**, i.e. turning sync on always
sends `PATCH /api/user/appearance { sync_enabled: true, theme:
<this device's current local value> }`, and the account's `theme`
column is unconditionally overwritten to match. This is the simplest
model, requires no "which value do you want to keep?" prompt UI, and
matches the common consumer-app convention (turning on sync/backup for
a setting typically means "make my current state the source of truth
going forward," not "silently discard what I just chose in favor of
something I did on another device that I may not even remember").

This is flagged as a genuine judgment call, not a foregone conclusion —
the pull interpretation is defensible too (arguably more
"principle-of-least-surprise" for a user who forgot they'd already
enabled sync elsewhere and expected turning it on to just *join* the
existing synced state rather than clobber it). A third option — prompt
the user to choose when the two values differ — was considered and
rejected as disproportionate machinery for a cosmetic, low-stakes,
easily-reversible setting; the cost of guessing wrong here is "the theme
looks different than expected until you change it again," not data
loss.

If review lands on push (this document's recommendation), no extra
schema or endpoint work is implied — the same `PATCH` shape above
already carries both fields. If review lands on pull instead, the only
change needed is client-side: read the response's `theme` (§3.5,
already returns the resulting stored value) and apply that locally
instead of assuming the value sent was the value that stuck.

---

## 4. Security and Privacy

- **No sensitive data.** `theme`/`theme_sync_enabled` carry no PII, no
  secrets, and no data that needs to be encrypted at rest beyond
  Postgres's existing baseline — same sensitivity tier as `role_theme`
  and `guilds.visibility` already in the schema.
- **No cross-account leakage risk.** The `PATCH` endpoint resolves the
  target user exclusively from the caller's own `__session` refresh
  cookie (`findActiveSessionByRefreshToken`), the same authentication
  `GET /api/auth/session-token` already uses — there is no `user_id`
  field in the request body for a caller to substitute another
  account's id into, so there's no IDOR-shaped path from "authenticated
  as user A" to "modify user B's theme." The pull-on-login write (§3.6)
  is scoped to the user the OAuth callback just authenticated in that
  same request, never a value read from client-suppliable input.
- **No new cross-origin surface.** The endpoint is same-origin,
  cookie-authenticated, and mutates only the calling user's own row —
  no new CORS configuration or public read endpoint is introduced.
  There is no `GET` that returns another user's theme; nothing about
  this feature makes any user's appearance preference visible to any
  other user at all (unlike, say, `role_label`, which is broadcast to
  guild members by design).
- **Cookie hygiene.** The `theme` cookie is intentionally non-`httpOnly`
  (§3.2) since client JS must read/write it — this is an accepted,
  narrow exception to "cookies should be `httpOnly` by default," scoped
  to a value that is cosmetic and never used for any authorization
  decision. It carries `secure`/`sameSite: "lax"` matching
  `REFRESH_COOKIE_OPTIONS`'s existing non-`httpOnly`-independent flags,
  and nothing about its value can be leveraged to affect session
  validity, authentication, or authorization even if read or forged by
  a third party — worst case of a tampered/garbage cookie value is
  handled by §2.3's fallback-to-default, not an error state.
- **Rate limiting**: `PATCH /api/user/appearance`, like every other
  authenticated mutation endpoint, should sit behind the existing
  per-user rate-limiting mechanism (`docs/social-presence-design.md` §5
  established this as standing guidance for authenticated internal-API-
  adjacent mutations); a generous limit is appropriate here given the
  low stakes, mainly to blunt accidental client bugs (a runaway retry
  loop) rather than any realistic abuse scenario.

---

## 5. Proposed Implementation Order

1. **Theme tokens + registry** (§2). Convert `web/app/globals.css` to
   the `:root` + `[data-theme="..."]` + `@theme inline` structure, add
   `web/lib/appearance/themes.ts` with `abyss` and one new variant.
   Verify every existing `bg-*`/`text-*`/`border-*` color utility still
   resolves correctly with `data-theme="abyss"` hardcoded (no picker,
   no persistence yet) — this step should be a visual no-op.
2. **Local persistence, no sync** (§3.1–3.2). Add the `theme` cookie,
   read it in `RootLayout`, add a minimal theme-switcher (even before
   the full settings shell exists) to verify instant, flash-free local
   switching end-to-end.
3. **Settings shell + Appearance section** (§1). Build the modal, the
   section registry with just `appearance`, and the real theme-picker
   UI wired to the mechanism from step 2.
4. **Schema + API** (§3.4–3.5). Add the `users` columns, the
   `PATCH /api/user/appearance` route, and the sync toggle UI wired to
   it (per whichever `ARBITRATION` outcome review lands on).
5. **Pull-on-login** (§3.6). Wire the Discord OAuth callback to apply
   an account's synced theme to the redirect response's cookie.
6. **`internalIsolation.test.ts` / route inventory update** — confirm
   the new `/api/user/appearance` route is exercised by whatever test
   coverage already asserts on the shape of `web/app/api/*` vs.
   `web/app/internal/*` (not a new isolation mechanism, just extending
   existing coverage to the new route, per `docs/social-presence-design.md`
   §5's closing checklist item on this same point).

## Related Documentation

- [`social-presence-design.md`](social-presence-design.md) — the
  `role_rank`/`role_theme` decoupling (§2.2) and static-registry-vs-DB-
  table reasoning (§2.2, §2.3) this document's theme registry (§2.2) and
  data-model choice (§3.4) directly mirror.
- [`auth/discord-design.md`](auth/discord-design.md) — the session/
  cookie model (§6) and `/internal/*` isolation requirement (§8.1/§9.1)
  this document's persistence design (§3.2, §3.5) builds on and stays
  consistent with.
- [`guilds/design.md`](guilds/design.md) — origin of the
  `guilds.visibility`-style `ENUM` precedent this document's §3.4
  contrasts against when explaining why `users.theme` is free text
  instead.
