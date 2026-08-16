# Known Issues

**Status: living document.** Unlike the design docs under `docs/`, this
file is meant to be updated as issues are found, investigated further, and
eventually resolved — entries should be removed (or moved to a "Resolved"
section) once actually fixed, not left to rot.

---

## Presence connection-count leak (unresolved, not reliably reproducible)

**Symptom**: a user's own `PRESENCE_UPDATE` (`online`) broadcast can
silently stop firing on reconnect — the account still functions normally
(WebSocket connects, `IDENTIFY` succeeds, everything else works), but
`SessionManager`'s per-`user_id` connection count appears to already be
above zero going into the new connection, so the `0 → 1` transition that
should trigger the broadcast never happens
(`docs/guilds/social-presence-design.md` §3.1's "returns true iff this
call caused a 0->1 transition" contract). Confirmed once via direct
observation: closing the *only* real connection for the affected user
produced no `offline` broadcast either, meaning the count was stuck above
the actual number of live connections, not just delayed.

**Impact**: cosmetic only, currently — a user's own presence dot (and
other users' view of them) can be wrong (perpetually "offline" or stuck
"online") until the server process restarts, which clears
`SessionManager`'s in-memory `presence_counts_` map. Nothing
authorization- or data-integrity-relevant depends on presence state.

**What's been ruled out** — none of the following reproduce it, despite
deliberately trying to stress each:

1. **`SessionManager::incrementPresence`/`decrementPresence` in
   isolation** — the reference-counting logic itself
   (`server/src/session/SessionManager.cpp`) is a standard, correct
   increment/decrement-with-erase-at-zero pattern; nothing subtle found on
   inspection.
2. **A new C++ integration test**
   (`server/tests/integration/Server.test.cpp`, `"Server broadcasts
   online on every reconnect, not just the first, across repeated
   connect/disconnect cycles"`) drives the same real user through 5
   sequential connect → identify → observe-online → close →
   observe-offline cycles over raw TCP sockets, with an independent
   observer connection watching. **Passes cleanly, every cycle.**
3. **20 rapid reconnect cycles through the real gateway** (WS →
   `ws-to-tcp` bridge → server), scripted against the actual running
   Docker stack. **All 20 clean.**
4. **5 cycles of an ungraceful disconnect** — a child process that
   identifies then gets `SIGKILL`ed (no `ws.close()`, no clean FIN,
   simulating a crashed tab) while an observer watches. **All 5 clean** —
   the gateway's `TcpClient`/`WsServer` (`gateway/src/`) and the server's
   `readFromSocket()`-failure detection both correctly notice and
   propagate this case too.
5. **Real browser, rapid repeated `navigate()` reloads** (10× at ~0.3s
   intervals) from a freshly restarted server — **10/10 clean**, correct
   online/offline pairs throughout.

**What actually happened, the one time it was caught**: during manual
verification earlier in the same session that added presence to the
frontend (`docs/guilds/social-presence-design.md`'s implementation, then
a later density/polish pass), a real user's presence count was found
already stuck (no online, no offline even on full disconnect) after an
extended session of mixed activity — multiple single (non-rapid) browser
reloads, several `docker compose up --build -d web` cycles (which
shouldn't touch the gateway/server containers at all), and various script-
based test connections as *other* users. Restarting the server fixed it
immediately, consistent with in-memory state, not persisted/Postgres
state, being the location of the stuck count.

**Not yet tried**:

- Instrumenting `SessionManager::incrementPresence`/`decrementPresence`
  and `Server::removeSessionTrackingPresence` with temporary logging
  (user_id, resulting count, fd) and leaving a long, mixed, realistic
  session running (real browser + concurrent scripts + occasional
  container restarts) until it recurs, to catch the actual sequence of
  calls around the moment it happens — every attempt so far has been a
  *clean, isolated* reproduction strategy, and the one real occurrence
  happened during genuinely mixed, concurrent activity that none of the
  attempts above fully replicate.
- Checking whether `docker compose up --build -d web` (rebuilding only
  the `web` service) has any observable effect on the `gateway`/`server`
  containers' existing connections — expected to be none (separate
  containers, unaffected network), but not directly verified instrumented
  end-to-end during a rebuild specifically.
- Auditing for TCP-level fd reuse races: `Server::start()`'s loop calls
  `listener_.accept()` then processes all `connections_` in the same
  iteration (`server/src/Server.cpp`) — if a disconnected fd's cleanup
  and a new connection landing on the *same, reused* fd number could ever
  interleave unexpectedly, that's a plausible (if low-probability)
  mechanism; not confirmed or ruled out.

**If picked up again**: start from the instrumentation approach above
rather than re-running clean stress tests — this issue has now survived
four different deliberate reproduction attempts, so further "try to
reproduce it cleanly" effort has a low expected return relative to
"catch it happening organically with logging in place."

---

## Client's `myGuildIds` doesn't reflect pre-existing membership on a fresh connection (unresolved)

**Symptom**: `web/hooks/useGatewayConnection.ts`'s `myGuildIds` (a client-
side `Set<string>` used to decide whether a guild in `GUILD_LIST` renders
as a clickable member entry vs. a Join/Request button) starts empty on
every fresh page load and is only ever added to by `GUILD_CREATED`/
`GUILD_JOINED` events received *during that session*. A user who already
belongs to a guild from a previous session sees it rendered as if they
were not a member — attempting to join it fails with `PROTOCOL_VIOLATION`
("already a member") — until they leave and rejoin, or until some other
in-session action happens to add it back to the set. `GUILD_LIST`'s wire
shape (`shared/protocol/README.md`) has no per-guild membership flag for
the client to bootstrap from, and there is no "my guild ids" fetch
equivalent to `LIST_FRIENDS`/`LIST_BLOCKS`.

**Found while**: building the Discord-style guild rail
(`web/components/GuildRail.tsx`,
`docs/frontend-rebuild-plan.md`'s Deferred section) — the rail's per-guild
icons are driven directly by `myGuildIds`, which made the gap immediately
visible (a real, pre-existing guild membership rendered with no rail icon
at all after a page reload). The old tab-based UI had the exact same
`myGuildIds.has(g.guildId)` check (`web/app/page.tsx`'s guild list, before
this pass) and was equally affected — this isn't a regression introduced
by the rail, just newly visible because the rail makes "which guilds do I
belong to" a more prominent, always-on piece of UI than the old
click-into-the-Guilds-tab-to-notice version was.

**Impact**: cosmetic/UX only for a guild the user already owns (they can
still reach it via any conversation/invite link that names it directly,
and their actual membership row in Postgres is untouched) — but
meaningfully confusing on first login to a returning session, since a
guild you belong to appears to require joining again.

**Not fixed here**: doing so needs either a new field on `GUILD_LIST`'s
per-guild entries (e.g. `is_member`, resolved server-side against the
caller's own id) or a new fetch analogous to `LIST_FRIENDS`/`LIST_BLOCKS`
— real protocol/backend surface area, out of scope for the frontend-only
rail rework that found it.
