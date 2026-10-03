# CIG Nexus — Brand and Design System

**Status: draft v0.2 (2026-10-03) — first brand charter, revised after research.** It documents the visual
system the web client already ships, then proposes a v0.2 revision informed by
Discord's current design and 2026 color and type trends: a light theme, five
new tokens, a revised type scale, and a catalog of 13 themes
(7 dark, 6 light) with full palettes.

How to read it:

- **[As-built]** — documented from the code on `main`. Values were checked
  against `web/app/globals.css`, `web/lib/appearance/themes.ts`,
  `web/app/layout.tsx` and measured component usage (counts are over
  `web/app` + `web/components`, 2026-10-03).
- **[Proposed]** — new in this document. Needs a decision before anyone
  implements it. Section 9 lists every open decision.

**Source of truth.** Values live in code, this document describes them. If
the two disagree, the code wins and this document has a bug — fix it in the
same PR as the code change (see `CLAUDE.md`, Documentation and Changelog
Discipline).

**Companion files** (produced from the same dataset as section 7, so all three agree at v0.2; keep them in sync when editing):

- `docs/design/theme-catalog.css` — one `[data-theme]` block per theme, ready to paste into `web/app/globals.css`.
- `docs/design/theme-catalog.json` — the same tokens plus swatches, contrast figures and the reference-to-final tuning of every color.

---

## 1. Brand

### 1.1 Essence [As-built, with interpretation]

CIG Nexus is a lightweight real-time chat platform that keeps transport,
protocol and UI cleanly separated (README, Vision). The identity follows from
that: an instrument panel, not a social app. Dark by default, monospace
voice, one bright accent on a quiet ink background.

**The mark** is a crystal shard drawn as line art. The three nodes on its
vertical axis read as the platform's three hops: the muted top node is the
browser, the diamond at the core is the gateway, the bright bottom node is the
server. (`icon.svg` itself only says "three-hop topology"; the
browser/gateway/server mapping is an interpretation of the architecture.)

### 1.2 Logo family

All marks share the same 512x512 canvas and geometry, so any `mark-*` or
`icon-*` file is a drop-in replacement for another in `<Image>`.

| File | Background | Status | Use |
|---|---|---|---|
| `icon.svg` | baked dark tile | As-built | favicon, app icon, social avatar, dark-theme tile |
| `icon-light.svg` | baked light tile | **New** | light-theme tile (rail, home button) |
| `mark-dark.svg` | transparent | **New** | inline on dark surfaces |
| `mark-light.svg` | transparent | **New** | inline on light surfaces |
| `lockup-dark.svg` | transparent | **New** | landing page, README header, social card on dark |
| `lockup-light.svg` | transparent | **New** | same, on light surfaces |
| `lockup.svg`, `lockup.png` | baked dark | As-built, **to retire** | see below |

**Why the legacy lockup should be retired:**

1. Its wordmark is live `<text>` with a system-font stack, so it renders in
   whatever the exporting machine had. The shipped `lockup.png` shows a bold
   italic sans, not the monospace the SVG declares and not Geist Mono.
2. The mark is placed at a negative offset: the top node is cropped by the
   canvas edge.
3. The canvas is 1000x300 but the artwork stops near x=600.
4. The dark background is baked in, so it cannot sit on a light surface.

The new lockups fix all four: wordmark converted to outlines from Geist Mono
Bold, full mark visible, tight 516x299 canvas, transparent background.

**Light variants are not simple recolors.** On a light background the same
gradient washes out, so `mark-light` / `lockup-light` use deeper stops
(blue `#3f5fd0`, teal `#0d7d72`, violet `#6d28d9`), a dark central diamond,
no glow, and strokes about 20% heavier (dark-on-light reads thinner than
light-on-dark, and there is no glow to carry the thin lines).

With a catalog of 13 themes, two logo colorways will not match every
palette. That is the main reason to prefer the inline themed `<Mark>` of 5.3
(item 5): one component that reads the active theme's tokens.

### 1.3 Wordmark

Two treatments exist today. Keep both, with fixed jobs:

| | Stacked lockup wordmark | In-app inline wordmark |
|---|---|---|
| Where | lockup files, README, social cards | `Logo.tsx` in the header and landing page |
| Face | Geist Mono Bold, skewed -6 degrees | Geist Mono Bold (`font-mono font-bold`) |
| Layout | "CIG" over "NEXUS", size ratio 0.72 | one line: "CIG NEXUS" |
| Tracking | about 0.11em (CIG) / 0.06em (NEXUS) | `tracking-[0.2em]` |
| Color | CIG solid ivory/ink, NEXUS in the brand gradient | CIG `text-ivory`, NEXUS `text-teal` (follows the theme) |
| Sizes | n/a | `text-sm` beside a 28px mark, `text-3xl` beside a 64px mark |

Rule: product UI uses the inline wordmark (it is live text and follows the
theme); static artwork uses the stacked lockup. Do not retype the wordmark in
another typeface.

### 1.4 Clear space and minimum size [Proposed]

- Clear space: at least the cap height of "CIG" on every side of a lockup
  (about 10% of its width). The lockup files already include a smaller
  built-in margin; add more next to other elements.
- Minimum sizes: mark 24px tall (the product uses 28px, 44px and 64px);
  icon tile 16px, favicon only (the shard silhouette and bottom node must stay
  recognizable). Always use the dark tile `icon.svg` for favicons: at 16px the
  light tile loses its edge on light surfaces. Lockup 120px wide.

### 1.5 Usage rules [Proposed]

Do:

- Use `mark-dark` / `lockup-dark` on dark surfaces and the `-light` files on
  light surfaces. Pick by the background actually behind the logo.
- Keep the proportions. Scale uniformly.

Do not:

- Put `mark-dark` on a light surface or `mark-light` on a dark one (both
  nearly vanish).
- Stretch, rotate, outline, recolor outside the palette, or add effects.
- Add the glow to the light variants.
- Place the gradient mark on mid-tone or busy photographic backgrounds; use
  the baked tile instead.

---

## 2. Color

### 2.1 Token architecture [As-built]

Six tokens in a Tailwind v4 `@theme` block in `web/app/globals.css` (`ink`,
`surface`, `slate`, `teal`, `violet`, `ivory`). Tailwind compiles every color
utility against `var(--color-...)`, so a `[data-theme="..."]` block that
redeclares tokens re-skins the whole app with no component changes. There is
deliberately no `[data-theme="abyss"]` block: the `@theme` defaults are Abyss.
The active theme is a `data-theme` attribute on `<html>`, set server-side
from a cookie (no flash), with ids registered in `web/lib/appearance/themes.ts`.
Section 2.7 proposes five more tokens.

### 2.2 Palettes

Roles are positional, not literal: in a light theme `ink` is the light page
color and `ivory` is the dark text color. Daylight is an exact inversion of
Abyss (its `ink` is Abyss's `ivory` and vice versa), which keeps the primary
text contrast identical (16.8:1).

| Token | Role | Abyss [As-built] | Ember [As-built] | Daylight [Proposed] |
|---|---|---|---|---|
| `ink` | page background | `#0d0e18` | `#0d0e18` | `#edeffb` |
| `surface` | panels, rail, cards, inputs | `#1a1c2e` | `#1a1c2e` | `#ffffff` |
| `slate` | borders, dividers, muted chrome (used at 20-50% opacity) | `#4a4e72` | `#4a4e72` | `#4a4e72` |
| `teal` | primary accent: connected, links, active, primary buttons | `#5eead4` | `#f87171` | `#0d6b63` |
| `violet` | secondary accent: ownership, emphasis, usernames | `#8b5cf6` | `#fb923c` | `#6d28d9` |
| `ivory` | primary text (used at 25-100% opacity) | `#edeffb` | `#edeffb` | `#0d0e18` |
| `danger` | errors, blocked, destructive (fixed literal today) | `#f87171` | `#f87171` | `#b91c1c` |
| `online` | online / connected dot (fixed literal today) | `#4ade80` | `#4ade80` | `#15803d` |

Notes:

- Ember overrides only `teal` and `violet`; it keeps the dark base.
- `danger` and `online` are **not tokens today**: the code uses Tailwind
  literals `red-400` / `red-300` / `green-400`. The values above are those
  literals (Abyss, Ember) and the proposed light values (Daylight). See 5.3.
- Brand blue `#7c9cf6` exists only inside the logo gradient. It is not a UI
  token.
- In Ember the accent (`#f87171`) and the danger literal (`red-400`,
  `#f87171`) are the same color, so an error and the wordmark's "NEXUS" are
  indistinguishable by color. v0.2 gives Ember its own danger (2.7).

Contrast (WCAG 2.x), accent text on the page background:

| Pair | Ratio | |
|---|---|---|
| Abyss `teal` on `ink` | 13.0 | |
| Abyss `violet` on `ink` / on `surface` | 4.5 / 4.0 | below 4.5 on surface |
| Ember `teal` / `violet` on `ink` | 6.9 / 8.5 | |
| Abyss `teal` on Daylight `ink` | 1.3 | unusable, hence a new teal |
| Abyss `violet` on Daylight `ink` | 3.7 | too low for small text |
| Daylight `teal` on `ink` / `surface` | 5.6 / 6.4 | |
| Daylight `violet` on `ink` | 6.2 | |
| Daylight primary button: `ink` text on `teal` / on `teal/90` | 5.6 / 4.6 | |

### 2.3 Text opacity tiers [As-built]

Text hierarchy is made with opacity on `ivory`, not with extra tokens.
Contrast is measured on the page background (`ink`).

| Utility | Uses | Typical role | Abyss | Daylight |
|---|---|---|---|---|
| `text-ivory` | 39x | primary text | 16.8 | 16.8 |
| `text-ivory/90` | 3x | list-row names | 13.6 | 13.4 |
| `text-ivory/80` | rare | bio / long-form | 10.8 | 9.7 |
| `text-ivory/70` | 4x | secondary icons and text | 8.4 | 6.8 |
| `text-ivory/60` | 6x | descriptions | 6.4 | 4.8 |
| `text-ivory/50` | 10x | timestamps, icon buttons, tertiary text | 4.8 | 3.5 |
| `text-ivory/40` | 40x | uppercase section labels (18), empty states, helper text | 3.5 | 2.6 |
| `text-ivory/30` | 4x | placeholders | 2.5 | 2.0 |

**Proposed rule (replaces the earlier "/60 or stronger" idea):** informational
text is `ivory` or the new solid `muted` token, both guaranteed AA in every
theme. The opacity tiers (`/90` ... `/30`) are for decoration: placeholders,
disabled states, hover tints, dividers. About 60 current uses of `/40` to
`/70` (40 + 10 + 6 + 4) would be reviewed for migration to `text-muted`. See
2.6 for why a solid token beats a stricter opacity rule.

### 2.4 Borders, tints and overlays [As-built]

- Borders and dividers: `border-slate/20` (21 uses, section dividers) and
  `border-slate/40` (21 uses, controls); text inputs use `border-slate/50`.
- Panels: `bg-surface`; translucent rails `bg-surface/40` to `/60`.
- Accent tint for active/selected states: `bg-teal/10` to `/25`.
- Overlays and backdrops: `bg-ink/40`, `bg-ink/70`; elevated cards use
  `shadow-xl`.

### 2.5 Colors that do not follow the theme [As-built]

Three things are intentionally independent of the active theme:

1. **Status colors** (`PresenceDot`, `SignalIndicator`, error text): online is
   green and error is red in every theme, so switching to Ember never makes
   "connected" read as an error. This is a good rule. The weakness is that the
   values are hardcoded literals tuned for dark backgrounds (see 2.6, 5.3).
   v0.2 keeps the rule (status keeps its meaning across themes) but makes the
   values tokens, so each theme can tune them for its own backgrounds.
2. **Guild icons**: background `hsl(hue, 45%, 32%)` with the hue hashed from
   the guild id, so it is theme-independent by design. Its initials must
   therefore also be theme-independent (a fixed light color, not `text-ivory`).
3. **The brand gradient**, which exists only in logo artwork.

### 2.6 Accessibility findings

Targets: 4.5:1 for text, 3:1 for icons, dots and logo strokes.

Computed with sRGB alpha blending for the opacity tiers. Tailwind v4 blends
opacity modifiers in OKLab, so those values differ by a few tenths: confirm in
browser DevTools before locking any threshold. Solid-color figures (the
catalog in section 7) do not have this caveat.

1. **The muted tier is below AA even in Abyss.** `text-ivory/40` is the most
   used tier (40 uses) and the style of 18 of the 19 uppercase labels:
   3.5:1 in Abyss, 2.6:1 in Daylight. `/50` is
   4.8:1 / 3.5:1 and `/60` is 6.4:1 / 4.8:1.
2. **A stricter opacity rule does not scale across themes.** The first draft
   of this charter proposed "informational text at `/60` or stronger". Tested
   against the catalog, it forces soft light-theme text colors to near-black:
   Latte's `#4c4f69` would have to become `#0f1015`, Dawn's `#464261` would
   become `#15141d` and Parchment's `#3c3836` would become `#060606`, which
   erases what makes those themes themselves. A solid `muted` color per theme
   (the approach Discord, Catppuccin and Rose Pine all take) keeps each
   theme's identity and gives AA by construction.
3. **Abyss violet on surface is 4.0:1** (usernames):
   fine on ink (4.5:1), marginal on panels. v0.2
   lightens it to `#a079f8` (4.5:1 on the weakest surface).
4. **Status literals fail on light backgrounds**: `red-400` is
   2.4:1 and `green-400` is 1.5:1 on Daylight `ink`.
5. **Guild initials**: `#edeffb` on `hsl(hue, 45%, 32%)` is 4.2:1 at the
   worst hue. At 30% lightness the worst case is 4.6:1, so lowering the
   lightness by two points is enough.

### 2.7 Token set v0.2 [Proposed]

Eleven tokens instead of six. Names stay positional; see the rename note below.

| Token | Role | Status | Why |
|---|---|---|---|
| `ink` | page background | as-built | unchanged |
| `surface` | panels, rail, cards | as-built | unchanged |
| `raised` | hover, selected rows, input fill | **new** | Discord stacks three surface levels; CIG has two, so hover and inputs borrow translucent tints today |
| `slate` | borders and chrome (used at /20 to /50) | as-built | unchanged |
| `ivory` | primary text | as-built | unchanged (AAA on ink and surface in every catalog theme) |
| `muted` | secondary text, a solid color | **new** | replaces informational use of `ivory/40` to `/70`; AA by construction |
| `teal` | primary accent | as-built | holds a non-teal hue in most catalog themes |
| `violet` | secondary accent | as-built | Abyss value lightened |
| `danger` | errors, blocked | **new token** (literal today) | per-theme tuning |
| `warning` | idle, caution | **new** | idle presence and cautions have no color yet |
| `online` | online dot | **new token** (literal today) | per-theme tuning |

What changes for Abyss and Ember (the two shipping themes):

| Token | As-built | v0.2 | Note |
|---|---|---|---|
| `raised` | none | `#252840` | one step above `surface` |
| `muted` | `ivory/40` to `/70` | `#9aa0c8` | about today's `ivory/60` on `ink`; 5.7:1 on the weakest surface |
| `violet` (Abyss) | `#8b5cf6` | `#a079f8` | lightness only; 4.5:1 on the weakest surface |
| `danger` (Ember) | `#f87171`, same as the accent | `#ff4d79` | distinct from the coral accent |
| `warning` | none | `#fbbf24` | amber, readable on every dark surface |
| `danger`, `online` (Abyss) | `red-400`, `green-400` literals | `#f87171`, `#4ade80` | same values, now tokens |

**Rename note [optional].** `teal`, `violet`, `ink` and `ivory` are
positional, and in the catalog the `teal` token is a periwinkle, a mauve or a
brick red depending on the theme. A mechanical rename (`teal` to `accent`,
`violet` to `accent-2`, `ink` to `page`, `ivory` to `text`) would make the
code say what it means, at the cost of touching every color utility. Not
needed for v0.2; worth deciding before the catalog ships (section 9).

---

## 3. Typography [As-built]

Two families, loaded with `next/font/google` in `web/app/layout.tsx` and
exposed as `--font-geist-sans` / `--font-geist-mono`.

| Family | Tailwind | Job | Uses |
|---|---|---|---|
| Geist Mono | `font-mono` | the interface voice: headings, nav, buttons, labels, usernames, logo | 98 |
| Geist Sans | `font-sans` | reading text: message bodies, inputs, bios, descriptions, landing copy | 7 |

Scale (Tailwind defaults, nothing custom):

| Utility | Size / line | Uses | Where |
|---|---|---|---|
| `text-xs` | 12 / 16 | 50 | metadata, timestamps, uppercase labels, badges |
| `text-sm` | 14 / 20 | 41 | default UI and body text |
| `text-base` | 16 / 24 | 2 | landing paragraph |
| `text-lg` | 18 / 28 | 3 | occasional larger text |
| `text-2xl` | 24 / 32 | 1 | error page title |
| `text-3xl` | 30 / 36 | 1 | large inline wordmark |
| `text-4xl` to `sm:text-5xl` | 36 to 48 | 1 | landing headline |
| `text-[10px]`, `text-[9px]` | 10, 9 | 1, 2 | role badge and micro labels: the only off-scale sizes |

Weights: `font-semibold` (600) is the default emphasis (41 uses);
`font-bold` (700) for page titles and the wordmark (5); `font-medium` once.
Everything else is regular.

**Section label style** (8+ uses, the one fully consistent micro-pattern):
`font-mono text-xs font-semibold tracking-wider uppercase text-ivory/40`.
Headlines use `tracking-tight`; the inline wordmark uses `tracking-[0.2em]`.

Rule: Geist Mono for anything the product *says* (chrome, names, labels),
Geist Sans for anything the user *reads* in sentences.

### 3.1 Revisions informed by Discord and 2026 trends [Proposed]

**What the research says.**

- Discord's text is one proprietary family, gg sans (since December 2022),
  with chat text at 16px and a 1.375 line height, usernames at weight 500,
  timestamps at 12px, and section labels at 12px, weight 600, uppercase.
  gg sans is not available to us; the sizes are what transfers. (The sizes
  come from third-party analyses, not from Discord.)
- 2026 type roundups agree on four things: variable fonts are the default
  delivery format; Inter and Geist are the neutral workhorses for product UI;
  monospace as a brand voice is a recognized trend; and 16px is the floor for
  body text, with line height of at least 1.4 for long reading and sizes in
  `rem` so user text scaling works.
- CIG is already on-trend on type: Geist and Geist Mono are variable, neutral
  and explicitly named in 2026 roundups, and a monospace-led interface is the
  identity. **No font change is proposed.**

**What does change.** The mismatch is size, not family: chat messages are
`text-sm` (14px), the density Discord and the 16px floor both argue against for
the main reading surface.

| Role | As-built | v0.2 | Why |
|---|---|---|---|
| Chat message body | Geist Sans 14 / 20, `ivory` | Geist Sans 16 / 22 (`text-base`, `leading-[1.375]`), `ivory` | 16px floor; Discord's 16 / 1.375 |
| Text inputs | Geist Sans 14 | Geist Sans 16 | iOS Safari zooms into inputs below 16px |
| Username | Geist Mono semibold 14, `violet` | unchanged | the mono voice |
| Timestamp, metadata, helper text | Geist Mono 12, `ivory/40` to `/50` | Geist Mono 12, `muted` | AA (2.3) |
| Section label | mono 12 semibold, `tracking-wider`, uppercase, `ivory/40` | same style, `muted` | AA; Discord uses +0.02em, CIG's wider tracking suits mono |
| Bio and long-form | Geist Sans 14, `ivory/80` | Geist Sans 14 / 22 (`leading-relaxed`), `ivory` | line height at least 1.4 |
| Role badge and micro labels | 10px and 9px | 12px (`text-xs`) | below the 12px floor today |
| Everything else | as in the scale above | unchanged | |

Also: keep sizes in `rem` (Tailwind already does), keep the variable font
files, and treat APCA (the contrast model proposed for WCAG 3) as a later
check, not a v0.2 requirement.

---

## 4. Shape, spacing and layout [As-built]

- **Radius:** `rounded-md` (6px) is the default for controls, inputs and rows
  (35 uses); `rounded-full` for avatars, dots and pills (19); `rounded-lg`
  (8px, 8 uses) and `rounded-xl` (12px, 7) for cards, modals and popovers.
- **Spacing:** a 4px grid. Most used: `gap-2` (8px, 29), `px-3` (12px, 18),
  `gap-1` (4px, 15), `py-1.5` (6px, 14), `py-2` (8px, 12), `p-3` (12px, 12).
- **Borders:** 1px, always `slate` at partial opacity (see 2.4).
- **Layout constants:**

| Element | Size |
|---|---|
| Guild rail | 72px wide; buttons 44x44; dividers 32px wide, 1px |
| Active-guild pill | 4px x 24px, `rounded-r-full`, `bg-ivory`, 10px left of the button |
| Member list | 208px wide (`w-52`), `p-3` |
| Header | `px-6 py-4`, bottom border `slate/20` |
| Popovers | 288px wide (`w-72`) |
| Profile modal | max 384px (`max-w-sm`); 8px accent stripe in the user's accent color (default `#5eead4`) |
| Settings modal | max 672px (`max-w-2xl`) |
| Avatars | 20 (member rows, popovers), 24 (lists, settings), 28 (messages), 44 (rail), 64 (profile) |

### 4.1 Discord-inspired details [Proposed, optional]

- **Three surface levels.** `ink` (page) < `surface` (panels) < `raised`
  (hover, selected, inputs) mirrors Discord's three grays. Hover and selected
  rows would use `bg-raised` instead of translucent tints, which also keeps
  them legible in every theme.
- **Guild icon shape.** Discord's server icons are rounded squares that
  become circles on hover. CIG's rail buttons are 44px; a radius transition
  from `rounded-xl` to `rounded-full` on hover and on the active guild is a
  small, cheap affordance.
- **UI density.** Discord added a density option in March 2025. A
  `data-density` attribute scaling message-group spacing (for example 4 / 8 /
  16px) is a candidate for a later release, not v0.2.

---

## 5. Theming

### 5.1 Adding a theme [As-built rule]

One CSS block plus one registry entry, never a change to a consuming
component (`docs/settings/appearance-design.md` 2.2). An unknown id falls
back to the default theme.

### 5.2 Daylight [Proposed]

The first theme that flips the base palette instead of only swapping accents.
It supersedes the "dark-only by design" scope-out in
`appearance-design.md`. Values are the v0.2 set (11 tokens); the six original
tokens are unchanged from the first draft.

```css
[data-theme="daylight"] {
  color-scheme: light;
  --color-ink: #edeffb;           /* page background */
  --color-surface: #ffffff;       /* panels, rail, cards */
  --color-raised: #e2e5f6;        /* hover, selected, input fill */
  --color-slate: #4a4e72;         /* borders at /20-/50 */
  --color-ivory: #0d0e18;         /* primary text */
  --color-muted: #4d5278;         /* secondary text */
  --color-teal: #0d6b63;          /* primary accent */
  --color-violet: #6d28d9;        /* secondary accent */
  --color-danger: #b91c1c;        /* errors, blocked */
  --color-warning: #b45309;       /* idle, caution */
  --color-online: #15803d;        /* online dot */
}
```

```ts
// web/lib/appearance/themes.ts
{ id: "daylight", label: "Daylight", swatch: { accent: "#0d6b63", secondary: "#6d28d9" } },
```

Why these values: `ivory`/`ink` are an exact inversion of Abyss, so the
primary text contrast is the same (16.8:1). `teal` is the
lightest teal of its ramp for which both `text-teal` and the primary button's
hover state (`text-ink` on `bg-teal/90`, 4.6:1) stay at or above
4.5:1. `slate` is unchanged because it is only ever used at partial opacity.

### 5.3 Required follow-ups for Daylight and the catalog [Proposed]

1. **Guild icon initials:** stop using `text-ivory`; use a fixed light color
   and set the background lightness to 30% (see 2.6, item 5). In progress as a
   separate change.
2. **New tokens:** add `raised`, `muted`, `warning` to `@theme`, and turn the
   `red-400` / `red-300` / `green-400` literals into `danger` / `online`
   (about 20 uses: `grep -rn "red-\(300\|400\)\|green-400" web/app
   web/components`). Without this, error text is 2.4:1 on Daylight.
3. **Migrate informational text** from `ivory/40` to `/70` (about 60 uses) to
   `text-muted`, and keep the opacity tiers for decoration (2.3).
4. **`color-scheme`:** each catalog block sets it (`light` or `dark`), so
   native scrollbars, form controls and autofill follow the theme. Check that
   the default Abyss declares `dark`.
5. **Logo in the UI** — decide between:
   - A. Two static assets swapped by CSS (`icon.svg` / `icon-light.svg`, or
     `mark-dark` / `mark-light`). Simple, no JS. The mark stays blue-teal
     under every accent, which will clash with some of the 13 themes.
   - B. One inline `<Mark>` component whose gradient reads CSS variables.
     One source, follows every theme including Ember, no flash. Needs the
     brand blue promoted to a token.
   Recommendation: B for the header and rail; static files for the favicon,
   README and social cards.
6. **Manual pass** on every view (lobby, guilds, DMs, friends, settings, the
   profile modal, popovers) in each theme that ships. There is no
   visual-regression suite, so cost scales with the number of themes (7.16).
7. **Docs:** update `appearance-design.md` (scope and status) in the PR that
   ships the first new theme, and add the new files to `CLAUDE.md`.

---

## 6. Research basis

### 6.1 Discord

Sources: Discord's own support pages and blog for the structure (four themes,
sync options, density), community color extractions for the hex values. **Discord
does not publish its design tokens**, so every hex below is a community
extraction, consistent across several sources but not an official spec.

| Observation | Value | CIG decision |
|---|---|---|
| Four base themes: Light, Ash, Dark, Onyx (Onyx is true black, for OLED) | official (support pages, Mar 2025 desktop update, 2026 mobile update) | Adapt: Chalk, Ash, Onyx join the catalog |
| "Sync with computer" and a choice of which light and which dark theme to use | official | Adapt: system mode proposal in 7.17 |
| Three dark surface levels: server rail, sidebar, chat | `#1e1f22`, `#2b2d31`, `#313338` (community) | Adapt: `ink` / `surface` / `raised` ramp |
| Light surfaces | `#ffffff`, `#f2f3f5`, `#e3e5e8` (community) | Used as the Chalk reference |
| One saturated accent | blurple `#5865f2` (official brand) | Keep the one-accent discipline; secondary accent only for emphasis |
| Solid text tiers rather than opacity | `#dbdee1` normal, `#b5bac1` muted (community) | Adopt: solid `muted` token |
| Status colors | red `#f23f43`, green `#23a55a` (community) | Adopt: `danger`, `online`, plus `warning` |
| Typeface gg sans, 16px chat text, 1.375 line height | font official; sizes from third-party analyses | Adapt: 16px chat body, keep Geist (3.1) |
| Density options | official (Mar 2025) | Idea only (4.1) |
| Server icons: rounded squares that become circles on hover | third-party analysis | Optional (4.1) |

### 6.2 2026 trends

Trend roundups and blog posts, used as direction rather than proof.

| Trend | Evidence | CIG decision |
|---|---|---|
| Warm soft white as the quiet choice | Pantone 2026 Color of the Year is Cloud Dancer 11-4201, `#F0EEE9`, announced 4 Dec 2025, the first white in the program's history | Adopt: Cloud theme |
| Teal and "calm earth": khaki, jade, walnut, teal | WGSN and Coloro pick Transformative Teal; paint brands move to jade, walnut, khaki | Validates the teal brand accent; adopt: Evergreen and Cloud's walnut secondary |
| Fatigue with dark-only products | opinion pieces on a UI "cleanse" away from dark-mode overload | At least two light themes should ship |
| Variable fonts are the default delivery format | multiple 2026 type roundups | Keep Geist; use the variable files |
| Inter and Geist named as neutral UI sans | same roundups | No font change |
| Monospace as brand voice | 2026 font-trend pieces | Already CIG's identity |
| 16px body floor, line height at least 1.4, `rem` sizing, APCA discussion | typography guides | 16px chat body; APCA as a later check |

### 6.3 Reference palettes

Catppuccin (Mocha, Latte), Rose Pine (and Dawn), Tokyo Night and Gruvbox
(light) were read from their official repositories, so the reference values in
section 7 are exact for those themes. They are published under open licenses
and are used here as starting points: every CIG theme is a re-mapping onto our
tokens, tuned for contrast, not a copy.

### 6.4 Sources

- Discord support: how to change themes (`support.discord.com/hc/en-us/articles/207260127`); mobile visual refresh (`.../42383370736023`).
- Discord blog: improving mobile with squircles, styles and spacing (`discord.com/blog/improving-mobile-with-squircles-styles-and-spacing`).
- Engadget and PC Gamer, 25 Mar 2025: new desktop themes and UI density.
- Community extractions of Discord colors and type: `themeandcolor.com/blog/discord-dark-mode-colors`, `oh-my-design.kr/design-systems/discord`, `colorpalettegenerator.ai/brands/discord`, `discordtextlab.com`, and a third-party Discord design-system write-up for type sizes.
- Pantone Cloud Dancer and 2026 trend roundups: `colorcombinations.org/trends/color-trends-2026` (hex `#F0EEE9` confirmed), Adobe Express color trends.
- Palettes: `catppuccin/palette` (`palette.json`), `rose-pine/rose-pine-palette`, `folke/tokyonight.nvim` (`lua/tokyonight/colors`), `morhetz/gruvbox`.
- Typography roundups: Shakuro, Made Good Designs, Gleam Studio.

---

## 7. Theme catalog

### 7.1 How the themes are built

13 themes: 7 dark and 6 light. Abyss and Ember ship today;
Daylight is the proposal above; the other ten are new.

Each theme maps a reference palette onto the 11 tokens of 2.7, then colors are
nudged **in lightness only** (hue and saturation kept) until they meet these
targets on `ink`, `surface` and `raised`. Every adjustment is listed under its
theme.

| Token | Target |
|---|---|
| `ivory` | 7:1 on `ink` and `surface` (AAA), 4.5:1 on `raised` |
| `muted`, `teal`, `violet`, `danger` | 4.5:1 on all three surfaces |
| `teal` as a button | `ink` text on `teal` 4.5:1, and on `teal/90` (hover) 4.5:1 |
| `warning`, `online` | 3:1 on all three surfaces (they are dots, not text) |

All 13 themes meet every target. That is by construction, since the
colors were tuned to them: it proves the palettes are internally consistent,
not that they look good, so each shipped theme still needs the manual pass
(5.3, item 6).

### 7.2 At a glance

| Theme | Mode | Family | `ink` | `surface` | `ivory` | `teal` | `violet` | Status |
|---|---|---|---|---|---|---|---|---|
| **Abyss** | dark | CIG brand | `#0d0e18` | `#1a1c2e` | `#edeffb` | `#5eead4` | `#a079f8` | As-built |
| **Ember** | dark | CIG brand | `#0d0e18` | `#1a1c2e` | `#edeffb` | `#f87171` | `#fb923c` | As-built |
| **Daylight** | light | CIG brand | `#edeffb` | `#ffffff` | `#0d0e18` | `#0d6b63` | `#6d28d9` | Proposed |
| **Onyx** | dark | Discord | `#000000` | `#111214` | `#f2f3f5` | `#6f7af4` | `#eb459e` | Proposed |
| **Ash** | dark | Discord | `#2f3136` | `#36393f` | `#dcddde` | `#9fafe6` | `#32bbff` | Proposed |
| **Midnight** | dark | Tokyo Night | `#16161e` | `#1a1b26` | `#c0caf5` | `#7aa2f7` | `#bb9af7` | Proposed |
| **Mocha** | dark | Catppuccin | `#181825` | `#1e1e2e` | `#cdd6f4` | `#cba6f7` | `#f5c2e7` | Proposed |
| **Evergreen** | dark | 2026 calm earth | `#0e1714` | `#141f1b` | `#e8f0ea` | `#5cc9a7` | `#d6936f` | Proposed |
| **Chalk** | light | Discord | `#f2f3f5` | `#ffffff` | `#313338` | `#3e4df0` | `#c31572` | Proposed |
| **Cloud** | light | Pantone 2026 | `#f0eee9` | `#faf9f6` | `#26241f` | `#116871` | `#9c512d` | Proposed |
| **Latte** | light | Catppuccin | `#e6e9ef` | `#eff1f5` | `#484b63` | `#7c24ed` | `#0a55ea` | Proposed |
| **Dawn** | light | Rose Pine | `#faf4ed` | `#fffaf3` | `#464261` | `#6e5888` | `#ba3f3a` | Proposed |
| **Parchment** | light | Gruvbox | `#ebdbb2` | `#fbf1c7` | `#3c3836` | `#065b6b` | `#813966` | Proposed |

`teal` and `violet` are the primary and secondary accents. In most themes the
`teal` token is not teal (rename note, 2.7).

### 7.3 Abyss - dark, CIG brand [As-built]

*Ink-navy instrument panel, teal signal. The default.*

**Reference.** All values from globals.css and themes.ts. New in v0.2: raised, muted, warning, and a lightened violet (4.0:1 on surface as-built).

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#0d0e18` | page background | - |
| `surface` | `#1a1c2e` | panels, rail, cards | - |
| `raised` | `#252840` | hover, selected, input fill | - |
| `slate` | `#4a4e72` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#edeffb` | primary text | 12.6:1 min |
| `muted` | `#9aa0c8` | secondary text (solid) | 5.7:1 min |
| `teal` | `#5eead4` | primary accent | 9.7:1 min; button 13.0:1, hover 10.6:1 |
| `violet` | `#a079f8` | secondary accent | 4.5:1 min |
| `danger` | `#f87171` | errors, blocked | 5.2:1 min |
| `warning` | `#fbbf24` | idle, caution | 8.6:1 min (dot) |
| `online` | `#4ade80` | online dot | 8.3:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `violet` #8b5cf6 -> #a079f8.

```css
[data-theme="abyss"] {
  color-scheme: dark;
  --color-ink: #0d0e18;           /* page background */
  --color-surface: #1a1c2e;       /* panels, rail, cards */
  --color-raised: #252840;        /* hover, selected, input fill */
  --color-slate: #4a4e72;         /* borders at /20-/50 */
  --color-ivory: #edeffb;         /* primary text */
  --color-muted: #9aa0c8;         /* secondary text */
  --color-teal: #5eead4;          /* primary accent */
  --color-violet: #a079f8;        /* secondary accent */
  --color-danger: #f87171;        /* errors, blocked */
  --color-warning: #fbbf24;       /* idle, caution */
  --color-online: #4ade80;        /* online dot */
}
```

```ts
{ id: "abyss", label: "Abyss", swatch: { accent: "#5eead4", secondary: "#a079f8" } },
```

### 7.4 Ember - dark, CIG brand [As-built]

*Same ink base with a warm coral and orange accent.*

**Reference.** Accent values from themes.ts. New in v0.2: raised, muted, warning, and a distinct danger (as-built danger shares the accent hue).

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#0d0e18` | page background | - |
| `surface` | `#1a1c2e` | panels, rail, cards | - |
| `raised` | `#252840` | hover, selected, input fill | - |
| `slate` | `#4a4e72` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#edeffb` | primary text | 12.6:1 min |
| `muted` | `#9aa0c8` | secondary text (solid) | 5.7:1 min |
| `teal` | `#f87171` | primary accent | 5.2:1 min; button 6.9:1, hover 5.8:1 |
| `violet` | `#fb923c` | secondary accent | 6.4:1 min |
| `danger` | `#ff4d79` | errors, blocked | 4.5:1 min |
| `warning` | `#fbbf24` | idle, caution | 8.6:1 min (dot) |
| `online` | `#4ade80` | online dot | 8.3:1 min (dot) |

**Tuned for contrast:** nothing, the reference values already meet the targets.

```css
[data-theme="ember"] {
  color-scheme: dark;
  --color-ink: #0d0e18;           /* page background */
  --color-surface: #1a1c2e;       /* panels, rail, cards */
  --color-raised: #252840;        /* hover, selected, input fill */
  --color-slate: #4a4e72;         /* borders at /20-/50 */
  --color-ivory: #edeffb;         /* primary text */
  --color-muted: #9aa0c8;         /* secondary text */
  --color-teal: #f87171;          /* primary accent */
  --color-violet: #fb923c;        /* secondary accent */
  --color-danger: #ff4d79;        /* errors, blocked */
  --color-warning: #fbbf24;       /* idle, caution */
  --color-online: #4ade80;        /* online dot */
}
```

```ts
{ id: "ember", label: "Ember", swatch: { accent: "#f87171", secondary: "#fb923c" } },
```

### 7.5 Daylight - light, CIG brand [Proposed]

*Cool lavender-white page, deep teal and violet. The neutral light theme.*

**Reference.** Our own design: ink and ivory swapped from Abyss; teal and violet deepened for contrast.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#edeffb` | page background | - |
| `surface` | `#ffffff` | panels, rail, cards | - |
| `raised` | `#e2e5f6` | hover, selected, input fill | - |
| `slate` | `#4a4e72` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#0d0e18` | primary text | 15.3:1 min |
| `muted` | `#4d5278` | secondary text (solid) | 6.0:1 min |
| `teal` | `#0d6b63` | primary accent | 5.1:1 min; button 5.6:1, hover 4.6:1 |
| `violet` | `#6d28d9` | secondary accent | 5.7:1 min |
| `danger` | `#b91c1c` | errors, blocked | 5.2:1 min |
| `warning` | `#b45309` | idle, caution | 4.0:1 min (dot) |
| `online` | `#15803d` | online dot | 4.0:1 min (dot) |

**Tuned for contrast:** nothing, the reference values already meet the targets.

```css
[data-theme="daylight"] {
  color-scheme: light;
  --color-ink: #edeffb;           /* page background */
  --color-surface: #ffffff;       /* panels, rail, cards */
  --color-raised: #e2e5f6;        /* hover, selected, input fill */
  --color-slate: #4a4e72;         /* borders at /20-/50 */
  --color-ivory: #0d0e18;         /* primary text */
  --color-muted: #4d5278;         /* secondary text */
  --color-teal: #0d6b63;          /* primary accent */
  --color-violet: #6d28d9;        /* secondary accent */
  --color-danger: #b91c1c;        /* errors, blocked */
  --color-warning: #b45309;       /* idle, caution */
  --color-online: #15803d;        /* online dot */
}
```

```ts
{ id: "daylight", label: "Daylight", swatch: { accent: "#0d6b63", secondary: "#6d28d9" } },
```

### 7.6 Onyx - dark, Discord [Proposed]

*Pure black page, near-black panels, blurple and fuchsia. Best on OLED.*

**Reference.** True-black page is Discord Onyx (official). #1e1f22 is Discord's darkest dark surface and #f23f43 / #23a55a its red / green (community-extracted). Blurple #5865f2 is Discord's brand color. The idle yellow #f0b232 is commonly cited but was not confirmed in the sources read. Panel steps and the fuchsia pairing are ours.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#000000` | page background | - |
| `surface` | `#111214` | panels, rail, cards | - |
| `raised` | `#1e1f22` | hover, selected, input fill | - |
| `slate` | `#4e5058` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#f2f3f5` | primary text | 14.8:1 min |
| `muted` | `#b5bac1` | secondary text (solid) | 8.4:1 min |
| `teal` | `#6f7af4` | primary accent | 4.5:1 min; button 5.8:1, hover 4.8:1 |
| `violet` | `#eb459e` | secondary accent | 4.6:1 min |
| `danger` | `#f3474a` | errors, blocked | 4.6:1 min |
| `warning` | `#f0b232` | idle, caution | 8.7:1 min (dot) |
| `online` | `#23a55a` | online dot | 5.2:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `teal` #5865f2 -> #6f7af4; `danger` #f23f43 -> #f3474a.

```css
[data-theme="onyx"] {
  color-scheme: dark;
  --color-ink: #000000;           /* page background */
  --color-surface: #111214;       /* panels, rail, cards */
  --color-raised: #1e1f22;        /* hover, selected, input fill */
  --color-slate: #4e5058;         /* borders at /20-/50 */
  --color-ivory: #f2f3f5;         /* primary text */
  --color-muted: #b5bac1;         /* secondary text */
  --color-teal: #6f7af4;          /* primary accent */
  --color-violet: #eb459e;        /* secondary accent */
  --color-danger: #f3474a;        /* errors, blocked */
  --color-warning: #f0b232;       /* idle, caution */
  --color-online: #23a55a;        /* online dot */
}
```

```ts
{ id: "onyx", label: "Onyx", swatch: { accent: "#6f7af4", secondary: "#eb459e" } },
```

### 7.7 Ash - dark, Discord [Proposed]

*Soft mid-gray, easy on the eyes for long sessions. Old-blurple and sky blue.*

**Reference.** Discord's pre-2022 dark palette (the original dark that Discord now calls Ash), recalled from the legacy UI and partly confirmed by community palette sites. The exact hexes of today's Ash were not published in the sources read, so treat this as inspired by, not a copy. Text and accents are tuned.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#2f3136` | page background | - |
| `surface` | `#36393f` | panels, rail, cards | - |
| `raised` | `#40444b` | hover, selected, input fill | - |
| `slate` | `#72767d` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#dcddde` | primary text | 7.2:1 min |
| `muted` | `#b9bbbe` | secondary text (solid) | 5.1:1 min |
| `teal` | `#9fafe6` | primary accent | 4.5:1 min; button 6.0:1, hover 5.2:1 |
| `violet` | `#32bbff` | secondary accent | 4.5:1 min |
| `danger` | `#f69696` | errors, blocked | 4.5:1 min |
| `warning` | `#faa61a` | idle, caution | 4.9:1 min (dot) |
| `online` | `#43b581` | online dot | 3.8:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `teal` #7289da -> #9fafe6; `violet` #00a8fc -> #32bbff; `danger` #f04747 -> #f69696.

```css
[data-theme="ash"] {
  color-scheme: dark;
  --color-ink: #2f3136;           /* page background */
  --color-surface: #36393f;       /* panels, rail, cards */
  --color-raised: #40444b;        /* hover, selected, input fill */
  --color-slate: #72767d;         /* borders at /20-/50 */
  --color-ivory: #dcddde;         /* primary text */
  --color-muted: #b9bbbe;         /* secondary text */
  --color-teal: #9fafe6;          /* primary accent */
  --color-violet: #32bbff;        /* secondary accent */
  --color-danger: #f69696;        /* errors, blocked */
  --color-warning: #faa61a;       /* idle, caution */
  --color-online: #43b581;        /* online dot */
}
```

```ts
{ id: "ash", label: "Ash", swatch: { accent: "#9fafe6", secondary: "#32bbff" } },
```

### 7.8 Midnight - dark, Tokyo Night [Proposed]

*Deep indigo-blue with periwinkle and lilac. Calm, focused, a little cinematic.*

**Reference.** Tokyo Night (night variant), read from the official repository: bg #1a1b26, bg_dark #16161e, bg_highlight #292e42, fg #c0caf5, fg_dark #a9b1d6, comment #565f89, blue #7aa2f7, magenta #bb9af7, red #f7768e, yellow #e0af68, green #9ece6a.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#16161e` | page background | - |
| `surface` | `#1a1b26` | panels, rail, cards | - |
| `raised` | `#292e42` | hover, selected, input fill | - |
| `slate` | `#565f89` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#c0caf5` | primary text | 8.3:1 min |
| `muted` | `#a9b1d6` | secondary text (solid) | 6.4:1 min |
| `teal` | `#7aa2f7` | primary accent | 5.3:1 min; button 7.1:1, hover 6.0:1 |
| `violet` | `#bb9af7` | secondary accent | 5.8:1 min |
| `danger` | `#f7768e` | errors, blocked | 5.1:1 min |
| `warning` | `#e0af68` | idle, caution | 6.7:1 min (dot) |
| `online` | `#9ece6a` | online dot | 7.3:1 min (dot) |

**Tuned for contrast:** nothing, the reference values already meet the targets.

```css
[data-theme="midnight"] {
  color-scheme: dark;
  --color-ink: #16161e;           /* page background */
  --color-surface: #1a1b26;       /* panels, rail, cards */
  --color-raised: #292e42;        /* hover, selected, input fill */
  --color-slate: #565f89;         /* borders at /20-/50 */
  --color-ivory: #c0caf5;         /* primary text */
  --color-muted: #a9b1d6;         /* secondary text */
  --color-teal: #7aa2f7;          /* primary accent */
  --color-violet: #bb9af7;        /* secondary accent */
  --color-danger: #f7768e;        /* errors, blocked */
  --color-warning: #e0af68;       /* idle, caution */
  --color-online: #9ece6a;        /* online dot */
}
```

```ts
{ id: "midnight", label: "Midnight", swatch: { accent: "#7aa2f7", secondary: "#bb9af7" } },
```

### 7.9 Mocha - dark, Catppuccin [Proposed]

*Warm, soft purple-brown darks with mauve and pink pastels.*

**Reference.** Catppuccin Mocha, read from the official palette.json: mantle #181825, base #1e1e2e, surface0 #313244, text #cdd6f4, subtext0 #a6adc8, mauve #cba6f7, pink #f5c2e7, red #f38ba8, yellow #f9e2af, green #a6e3a1.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#181825` | page background | - |
| `surface` | `#1e1e2e` | panels, rail, cards | - |
| `raised` | `#313244` | hover, selected, input fill | - |
| `slate` | `#585b70` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#cdd6f4` | primary text | 8.7:1 min |
| `muted` | `#a6adc8` | secondary text (solid) | 5.6:1 min |
| `teal` | `#cba6f7` | primary accent | 6.2:1 min; button 8.6:1, hover 7.2:1 |
| `violet` | `#f5c2e7` | secondary accent | 8.2:1 min |
| `danger` | `#f38ba8` | errors, blocked | 5.4:1 min |
| `warning` | `#f9e2af` | idle, caution | 9.9:1 min (dot) |
| `online` | `#a6e3a1` | online dot | 8.5:1 min (dot) |

**Tuned for contrast:** nothing, the reference values already meet the targets.

```css
[data-theme="mocha"] {
  color-scheme: dark;
  --color-ink: #181825;           /* page background */
  --color-surface: #1e1e2e;       /* panels, rail, cards */
  --color-raised: #313244;        /* hover, selected, input fill */
  --color-slate: #585b70;         /* borders at /20-/50 */
  --color-ivory: #cdd6f4;         /* primary text */
  --color-muted: #a6adc8;         /* secondary text */
  --color-teal: #cba6f7;          /* primary accent */
  --color-violet: #f5c2e7;        /* secondary accent */
  --color-danger: #f38ba8;        /* errors, blocked */
  --color-warning: #f9e2af;       /* idle, caution */
  --color-online: #a6e3a1;        /* online dot */
}
```

```ts
{ id: "mocha", label: "Mocha", swatch: { accent: "#cba6f7", secondary: "#f5c2e7" } },
```

### 7.10 Evergreen - dark, 2026 calm earth [Proposed]

*Forest-black with jade and a walnut-clay secondary. Organic and quiet.*

**Reference.** Original palette from the 2026 calm-earth direction (jade, khaki, walnut, teal). No third-party source.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#0e1714` | page background | - |
| `surface` | `#141f1b` | panels, rail, cards | - |
| `raised` | `#1d2c26` | hover, selected, input fill | - |
| `slate` | `#46605a` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#e8f0ea` | primary text | 12.6:1 min |
| `muted` | `#9db3a8` | secondary text (solid) | 6.6:1 min |
| `teal` | `#5cc9a7` | primary accent | 7.2:1 min; button 9.0:1, hover 7.5:1 |
| `violet` | `#d6936f` | secondary accent | 5.7:1 min |
| `danger` | `#f0716f` | errors, blocked | 5.1:1 min |
| `warning` | `#f2c14e` | idle, caution | 8.7:1 min (dot) |
| `online` | `#8bd450` | online dot | 8.1:1 min (dot) |

**Tuned for contrast:** nothing, the reference values already meet the targets.

```css
[data-theme="evergreen"] {
  color-scheme: dark;
  --color-ink: #0e1714;           /* page background */
  --color-surface: #141f1b;       /* panels, rail, cards */
  --color-raised: #1d2c26;        /* hover, selected, input fill */
  --color-slate: #46605a;         /* borders at /20-/50 */
  --color-ivory: #e8f0ea;         /* primary text */
  --color-muted: #9db3a8;         /* secondary text */
  --color-teal: #5cc9a7;          /* primary accent */
  --color-violet: #d6936f;        /* secondary accent */
  --color-danger: #f0716f;        /* errors, blocked */
  --color-warning: #f2c14e;       /* idle, caution */
  --color-online: #8bd450;        /* online dot */
}
```

```ts
{ id: "evergreen", label: "Evergreen", swatch: { accent: "#5cc9a7", secondary: "#d6936f" } },
```

### 7.11 Chalk - light, Discord [Proposed]

*Neutral cool white and gray, blurple accent. Familiar and crisp.*

**Reference.** Discord Light surfaces (#ffffff, #f2f3f5, #e3e5e8; community-extracted) and the blurple brand accent. Text color and the fuchsia pairing are ours.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#f2f3f5` | page background | - |
| `surface` | `#ffffff` | panels, rail, cards | - |
| `raised` | `#e3e5e8` | hover, selected, input fill | - |
| `slate` | `#80848e` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#313338` | primary text | 10.0:1 min |
| `muted` | `#5c5e66` | secondary text (solid) | 5.1:1 min |
| `teal` | `#3e4df0` | primary accent | 4.7:1 min; button 5.4:1, hover 4.5:1 |
| `violet` | `#c31572` | secondary accent | 4.5:1 min |
| `danger` | `#c42429` | errors, blocked | 4.6:1 min |
| `warning` | `#ad790d` | idle, caution | 3.0:1 min (dot) |
| `online` | `#209652` | online dot | 3.0:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `teal` #5865f2 -> #3e4df0; `violet` #eb459e -> #c31572; `danger` #da373c -> #c42429; `warning` #f0b232 -> #ad790d; `online` #23a55a -> #209652.

```css
[data-theme="chalk"] {
  color-scheme: light;
  --color-ink: #f2f3f5;           /* page background */
  --color-surface: #ffffff;       /* panels, rail, cards */
  --color-raised: #e3e5e8;        /* hover, selected, input fill */
  --color-slate: #80848e;         /* borders at /20-/50 */
  --color-ivory: #313338;         /* primary text */
  --color-muted: #5c5e66;         /* secondary text */
  --color-teal: #3e4df0;          /* primary accent */
  --color-violet: #c31572;        /* secondary accent */
  --color-danger: #c42429;        /* errors, blocked */
  --color-warning: #ad790d;       /* idle, caution */
  --color-online: #209652;        /* online dot */
}
```

```ts
{ id: "chalk", label: "Chalk", swatch: { accent: "#3e4df0", secondary: "#c31572" } },
```

### 7.12 Cloud - light, Pantone 2026 [Proposed]

*Warm soft white, charcoal text, deep teal. The 2026 quiet-luxury look.*

**Reference.** Pantone Cloud Dancer #F0EEE9 as the page color (confirmed). Charcoal text, the Transformative-Teal-inspired accent and the walnut secondary are ours.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#f0eee9` | page background | - |
| `surface` | `#faf9f6` | panels, rail, cards | - |
| `raised` | `#e6e3dc` | hover, selected, input fill | - |
| `slate` | `#8a857b` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#26241f` | primary text | 12.1:1 min |
| `muted` | `#5f5b52` | secondary text (solid) | 5.3:1 min |
| `teal` | `#116871` | primary accent | 5.1:1 min; button 5.6:1, hover 4.6:1 |
| `violet` | `#9c512d` | secondary accent | 4.5:1 min |
| `danger` | `#b83729` | errors, blocked | 4.5:1 min |
| `warning` | `#b0741e` | idle, caution | 3.1:1 min (dot) |
| `online` | `#2f8f5b` | online dot | 3.2:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `teal` #13747d -> #116871; `violet` #a4552f -> #9c512d; `danger` #c0392b -> #b83729; `warning` #b7791f -> #b0741e.

```css
[data-theme="cloud"] {
  color-scheme: light;
  --color-ink: #f0eee9;           /* page background */
  --color-surface: #faf9f6;       /* panels, rail, cards */
  --color-raised: #e6e3dc;        /* hover, selected, input fill */
  --color-slate: #8a857b;         /* borders at /20-/50 */
  --color-ivory: #26241f;         /* primary text */
  --color-muted: #5f5b52;         /* secondary text */
  --color-teal: #116871;          /* primary accent */
  --color-violet: #9c512d;        /* secondary accent */
  --color-danger: #b83729;        /* errors, blocked */
  --color-warning: #b0741e;       /* idle, caution */
  --color-online: #2f8f5b;        /* online dot */
}
```

```ts
{ id: "cloud", label: "Cloud", swatch: { accent: "#116871", secondary: "#9c512d" } },
```

### 7.13 Latte - light, Catppuccin [Proposed]

*Cool lavender-gray light with mauve and blue. Soft and tidy.*

**Reference.** Catppuccin Latte, read from the official palette.json: mantle #e6e9ef, base #eff1f5, crust #dce0e8, text #4c4f69, subtext0 #6c6f85, mauve #8839ef, blue #1e66f5, red #d20f39, yellow #df8e1d, green #40a02b.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#e6e9ef` | page background | - |
| `surface` | `#eff1f5` | panels, rail, cards | - |
| `raised` | `#dce0e8` | hover, selected, input fill | - |
| `slate` | `#9ca0b0` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#484b63` | primary text | 6.4:1 min |
| `muted` | `#5f6275` | secondary text (solid) | 4.5:1 min |
| `teal` | `#7c24ed` | primary accent | 4.8:1 min; button 5.2:1, hover 4.5:1 |
| `violet` | `#0a55ea` | secondary accent | 4.5:1 min |
| `danger` | `#c50e35` | errors, blocked | 4.5:1 min |
| `warning` | `#b27117` | idle, caution | 3.0:1 min (dot) |
| `online` | `#3a9027` | online dot | 3.1:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `ivory` #4c4f69 -> #484b63; `muted` #6c6f85 -> #5f6275; `teal` #8839ef -> #7c24ed; `violet` #1e66f5 -> #0a55ea; `danger` #d20f39 -> #c50e35; `warning` #df8e1d -> #b27117; `online` #40a02b -> #3a9027.

```css
[data-theme="latte"] {
  color-scheme: light;
  --color-ink: #e6e9ef;           /* page background */
  --color-surface: #eff1f5;       /* panels, rail, cards */
  --color-raised: #dce0e8;        /* hover, selected, input fill */
  --color-slate: #9ca0b0;         /* borders at /20-/50 */
  --color-ivory: #484b63;         /* primary text */
  --color-muted: #5f6275;         /* secondary text */
  --color-teal: #7c24ed;          /* primary accent */
  --color-violet: #0a55ea;        /* secondary accent */
  --color-danger: #c50e35;        /* errors, blocked */
  --color-warning: #b27117;       /* idle, caution */
  --color-online: #3a9027;        /* online dot */
}
```

```ts
{ id: "latte", label: "Latte", swatch: { accent: "#7c24ed", secondary: "#0a55ea" } },
```

### 7.14 Dawn - light, Rose Pine [Proposed]

*Warm cream with plum text, iris and rose. Gentle, a bit romantic.*

**Reference.** Rose Pine Dawn, read from the official palette: base #faf4ed, surface #fffaf3, overlay #f2e9e1, subtle #797593, text #464261, iris #907aa9, rose #d7827e, love #b4637a, gold #ea9d34. Rose Pine has no green, so the online color is ours.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#faf4ed` | page background | - |
| `surface` | `#fffaf3` | panels, rail, cards | - |
| `raised` | `#f2e9e1` | hover, selected, input fill | - |
| `slate` | `#9893a5` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#464261` | primary text | 7.9:1 min |
| `muted` | `#6a6683` | secondary text (solid) | 4.6:1 min |
| `teal` | `#6e5888` | primary accent | 5.1:1 min; button 5.6:1, hover 4.5:1 |
| `violet` | `#ba3f3a` | secondary accent | 4.5:1 min |
| `danger` | `#a44f67` | errors, blocked | 4.5:1 min |
| `warning` | `#bf7614` | idle, caution | 3.0:1 min (dot) |
| `online` | `#2f8f68` | online dot | 3.3:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `muted` #797593 -> #6a6683; `teal` #907aa9 -> #6e5888; `violet` #d7827e -> #ba3f3a; `danger` #b4637a -> #a44f67; `warning` #ea9d34 -> #bf7614.

```css
[data-theme="dawn"] {
  color-scheme: light;
  --color-ink: #faf4ed;           /* page background */
  --color-surface: #fffaf3;       /* panels, rail, cards */
  --color-raised: #f2e9e1;        /* hover, selected, input fill */
  --color-slate: #9893a5;         /* borders at /20-/50 */
  --color-ivory: #464261;         /* primary text */
  --color-muted: #6a6683;         /* secondary text */
  --color-teal: #6e5888;          /* primary accent */
  --color-violet: #ba3f3a;        /* secondary accent */
  --color-danger: #a44f67;        /* errors, blocked */
  --color-warning: #bf7614;       /* idle, caution */
  --color-online: #2f8f68;        /* online dot */
}
```

```ts
{ id: "dawn", label: "Dawn", swatch: { accent: "#6e5888", secondary: "#ba3f3a" } },
```

### 7.15 Parchment - light, Gruvbox [Proposed]

*Aged-paper tan with brown text, petrol blue and plum. Retro and warm.*

**Reference.** Gruvbox Light, read from the official repository: light0 #fbf1c7, light1 #ebdbb2, light2 #d5c4a1, dark1 #3c3836, faded_blue #076678, faded_purple #8f3f71, faded_red #9d0006, faded_yellow #b57614, faded_green #79740e.

| Token | Hex | Role | Contrast (min over ink, surface, raised) |
|---|---|---|---|
| `ink` | `#ebdbb2` | page background | - |
| `surface` | `#fbf1c7` | panels, rail, cards | - |
| `raised` | `#d5c4a1` | hover, selected, input fill | - |
| `slate` | `#928374` | borders and chrome, used at /20-/50 | - |
| `ivory` | `#3c3836` | primary text | 6.8:1 min |
| `muted` | `#5a514a` | secondary text (solid) | 4.5:1 min |
| `teal` | `#065b6b` | primary accent | 4.5:1 min; button 5.6:1, hover 4.7:1 |
| `violet` | `#813966` | secondary accent | 4.5:1 min |
| `danger` | `#9d0006` | errors, blocked | 5.0:1 min |
| `warning` | `#966211` | idle, caution | 3.0:1 min (dot) |
| `online` | `#746f0d` | online dot | 3.0:1 min (dot) |

**Tuned for contrast** (lightness only, hue and saturation kept): `muted` #665c54 -> #5a514a; `teal` #076678 -> #065b6b; `violet` #8f3f71 -> #813966; `warning` #b57614 -> #966211; `online` #79740e -> #746f0d.

```css
[data-theme="parchment"] {
  color-scheme: light;
  --color-ink: #ebdbb2;           /* page background */
  --color-surface: #fbf1c7;       /* panels, rail, cards */
  --color-raised: #d5c4a1;        /* hover, selected, input fill */
  --color-slate: #928374;         /* borders at /20-/50 */
  --color-ivory: #3c3836;         /* primary text */
  --color-muted: #5a514a;         /* secondary text */
  --color-teal: #065b6b;          /* primary accent */
  --color-violet: #813966;        /* secondary accent */
  --color-danger: #9d0006;        /* errors, blocked */
  --color-warning: #966211;       /* idle, caution */
  --color-online: #746f0d;        /* online dot */
}
```

```ts
{ id: "parchment", label: "Parchment", swatch: { accent: "#065b6b", secondary: "#813966" } },
```

### 7.16 Which themes to ship first [Proposed]

Every shipped theme costs one manual pass over every view (there is no
visual-regression suite) and a place in the picker. Recommended first batch of
six, chosen for range at modest QA cost:

| Mode | Themes | Why |
|---|---|---|
| Dark | Abyss, Ember, Onyx, Mocha | the brand default and its warm variant; true black for OLED; a soft pastel dark |
| Light | Daylight, Cloud | the neutral inversion of the brand; the 2026 warm white |

Later: Midnight, Evergreen, Ash, Chalk, Latte, Dawn, Parchment, in any order.
Evergreen and Parchment exercise the most unusual hue choices, so they are good
candidates to ship only after the token rename and the themed `<Mark>` land.

### 7.17 Picker and system mode [Proposed]

- **Picker:** group by mode (Dark, Light), show each theme's `swatch`
  (accent and secondary), and mark the current one. The registry already
  carries `swatch`.
- **System mode:** Discord's "Sync with computer" with a separate choice of
  light theme and dark theme. Data model: `theme_mode` (`manual` or `system`),
  `theme_light`, `theme_dark`, next to today's `theme` and `theme_sync_enabled`.
- **Open problem:** `appearance-design.md` 3.2 avoids a blocking inline script
  and renders the right theme from a server-read cookie, which cannot know
  `prefers-color-scheme` on the very first request. Options: a tiny inline
  script, a `Sec-CH-Prefers-Color-Scheme` client hint (Chromium only) plus a
  cookie from the second visit, or accepting one flash on first load. This
  needs an explicit decision and supersedes that document's scope-out of
  system preference.

---

## 8. Governance

- `web/app/globals.css` and `web/lib/appearance/themes.ts` are the source of
  truth for colors. This document follows them.
- The catalog files (`theme-catalog.css`, `theme-catalog.json`) and section 7
  agree at v0.2. If a theme value changes, update all three in the same PR.
- Any new logo file is added to the table in 1.2 in the same PR.
- A token or typography change updates this document and `CHANGELOG.md` in
  the same PR.
- The PDF export (`brand-guidelines.pdf`) is a presentation copy, never the
  source. Regenerate it from this file.

---

## 9. Open decisions

| # | Decision | Recommendation |
|---|---|---|
| 1 | Name and id for the neutral light theme | `daylight` (alternatives: `paper`, `light`) |
| 2 | Adopt the v0.2 tokens `raised`, `muted`, `warning`, and `danger` / `online` as tokens (2.7) | Yes. `muted` is the one that fixes the contrast finding. |
| 3 | Informational text uses `ivory` or `muted`; opacity tiers decorative only | Adopt, and migrate the roughly 60 `/40` to `/70` uses in a small PR with a visual check (it changes hierarchy in Abyss and Ember too). |
| 4 | Chat message body 14px to 16px, inputs to 16px (3.1) | Yes for both. Check message density in the lobby after. |
| 5 | Rename `teal`, `violet`, `ink`, `ivory` to `accent`, `accent-2`, `page`, `text` | Yes, before the catalog ships. It is mechanical but wide. |
| 6 | First batch of themes (7.16) | Abyss, Ember, Onyx, Mocha, Daylight, Cloud. |
| 7 | Logo strategy: static swap (A) or inline themed mark (B) | B for the header and rail. |
| 8 | Retire `lockup.svg` / `lockup.png` | Yes: replace with the new lockups. |
| 9 | System mode and its first-paint behavior (7.17) | Decide after the first batch ships; do not block themes on it. |
| 10 | Clear space and minimum sizes (1.4) | Accept as written; revisit after seeing the mark at 24px in the UI. |
| 11 | Where this lives | Version it in `docs/design/`: it shows the system, like CLAUDE.md. Or keep it local with `.git/info/exclude`. |
