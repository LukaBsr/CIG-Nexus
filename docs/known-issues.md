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

**Instrumented (2026-09), not yet left running long enough to catch it**:
`SessionManager::incrementPresence`/`decrementPresence`,
`Server::removeSessionTrackingPresence`, and `IdentifyHandler::handle`'s
entry and post-session-creation points now log to stderr
(`server/include/util/DebugFlags.hpp`,
`server/src/util/DebugFlags.cpp`) when `CIG_NEXUS_DEBUG_PRESENCE=1` is set
in the server's environment — off by default, zero output otherwise
(verified: a real reconnect-cycle run produces zero `[presence-debug]`
lines without the flag, and the full expected sequence with it). Each line
carries `fd`, `user_id`, and the before/after count. What to look for once
a long, mixed, realistic session (real browser + concurrent scripts +
occasional container restarts) is run with the flag on:

- An `incrementPresence` line for some `user_id` with no later
  `decrementPresence` line for that same `user_id`'s connection — the
  count-stuck-high symptom directly.
- An `IdentifyHandler session created fd=X user_id=Y` line with no
  following `identify success fd=X user_id=Y` line — a session that got
  created but never reached the point where presence would increment,
  which would itself explain the *next* `removeSessionTrackingPresence` for
  that fd finding a `user_id` that was never counted as online (see
  `decrementPresence`'s "decrement without a matching increment" line).
- `removeSessionTrackingPresence` firing for a `fd` that was reused
  unexpectedly close in time to a `Client connected` log for the same `fd`
  number, per the fd-reuse-race idea below.

**Not yet tried**:

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

**Re-checked 2026-09 (code read, not reproduced)**: the suspect path is
unchanged in the ways that matter — `incrementPresence` runs once per
`IDENTIFIED` response in `Server::start()`, `decrementPresence` runs from
`removeSessionTrackingPresence()`, and both places that remove sessions
(a failed read, and the revocation sweep) go through that wrapper. No new
mechanism was found by inspection. What did change since this was logged:
`IdentifyHandler` now makes three synchronous internal-API calls
(`fetchBlocks`, `fetchFriends`, `fetchUserProfile`, 5 s timeout each)
between creating the session and the presence increment, and presence
broadcasts now go through `broadcastExcluding` (identified sessions only).
The instrumentation plan below is still the right next step, and it should
also log the IDENTIFY handler's entry/exit per fd so any dispatch that
creates a session but never produces `IDENTIFIED` shows up.

**Update**: this instrumentation now exists — see "Instrumented (2026-09)"
above. It hasn't yet been run against a real long, mixed session.

**If picked up again**: set `CIG_NEXUS_DEBUG_PRESENCE=1` and run a long,
mixed, realistic session rather than re-running clean stress tests — this
issue has now survived four different deliberate reproduction attempts, so
further "try to reproduce it cleanly" effort has a low expected return
relative to "catch it happening organically with logging in place," which
is what the instrumentation above is for.

---

## Resolved

### Client's `myGuildIds` didn't reflect pre-existing membership on a fresh connection

Fixed by adding an `is_member` boolean to every `GUILD_LIST` entry
(`server/src/protocol/handlers/GuildHandler.cpp`, spec in
`shared/protocol/README.md`'s `LIST_GUILDS`). The server already hydrates
`Session.guild_ids` from durable membership at `IDENTIFY`, so the flag is
just that session state exposed on the wire; `useGatewayConnection`
rebuilds `myGuildIds` from it on every `GUILD_LIST` (via
`memberGuildIdsFromList`), so a returning user's guild rail and
Join/Request buttons are correct immediately after a page load. Originally
found while building `GuildRail.tsx`; the old tab UI had the same gap.
