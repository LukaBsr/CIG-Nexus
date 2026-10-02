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

**Plausible root cause candidate, not yet confirmed**: the second bullet
above describes more than a log pattern to watch for — an uncaught
exception thrown between `IdentifyHandler`'s `createSession()` call and its
`return response` (e.g. from the internal-API hydration call,
`fetchSessionContext`, or from JSON handling inside it — see the 2026-09
update below: this used to be three separate calls, now one) would
propagate out of the handler, through the dispatcher lambda, into
`Server::start()`'s `catch (const std::exception&)` around
`dispatcher_.dispatch()` — which logs "Dispatch error" and sends
`PROTOCOL_VIOLATION`, but never runs the `if (response.type ==
"IDENTIFIED")` check that triggers `incrementPresence()`. The `Session`
this connection's `IdentifyHandler` already created is left behind in
`SessionManager` with a real `user_id` but no matching presence count.
That connection's *eventual* disconnect still goes through
`removeSessionTrackingPresence()`, which calls `decrementPresence(user_id)`
for a user who was never counted online for this connection — for a
`user_id` with another live, correctly-counted connection, this decrements
a real count that wasn't incremented for this socket, which is exactly the
"stuck above the actual number of live connections" shape described at the
top of this entry. This wasn't checkable before the instrumentation above
existed (nothing surfaced whether `handle()` completed for a given fd);
it's now a specific, falsifiable hypothesis rather than one item in a
general list — the thing to look for is a `session created` line with no
matching `identify success` line, immediately followed or preceded by a
`decrementPresence`/`incrementPresence` count that doesn't add up for that
`user_id`.

**Update (IDENTIFY hardening B2): the specific mechanism above is now
structurally closed, though not because anyone confirmed it was the actual
cause.** `IdentifyHandler::handle()` no longer makes any internal-API call
at all — the post-IDENTIFY blocks/friends/profile load
(`fetchSessionContext`) moved to `session::SessionHydrationWorker`, run on
its own thread and applied later by `Server::processHydrationResults()`.
The window this candidate described (an exception between
`createSession()` and `return response`, inside `IdentifyHandler`) is now
just a few no-throw field assignments and an in-memory `GuildManager`
lookup — there's essentially nothing left in that path that plausibly
throws. If the leak recurs after this change, this specific candidate is
likely ruled out; if it doesn't recur, that's circumstantial support for
it having been the cause, not proof.

**A related but distinct risk this change introduces, found by inspection,
not observed**: `SessionHydrationWorker::run()` (`server/src/session/
SessionHydrationWorker.cpp`) calls `InternalApiClient::fetchSessionContext`
with no `try`/`catch` around it. An uncaught exception escaping a
`std::thread`'s entry function calls `std::terminate()` — i.e., a bug that
used to be a silent presence-count leak would instead crash the whole
process. In practice `CurlInternalApiClient`'s methods already catch
`nlohmann::json::exception` internally and return `nullopt` rather than
throwing, so this is a latent gap, not a live one — but it's worth naming
because `persistence::MessagePersistenceWorker::run()` has the exact same
gap around its own `InternalApiClient::createMessage()` call, predating
this change. Not fixed here (scope discipline: matching the existing,
established pattern rather than hardening only the new copy of it, per the
session's own reasoning at the time) — a real candidate for both workers to
get a `try`/`catch` together, later, as its own change.

**Also fixed here, confirmed by a failing-then-passing test, not just
reasoned about**: a second tab for the same user whose hydration never
completes must not, on disconnecting, decrement the *first* tab's real,
already-online presence count —
`Server::removeSessionTrackingPresence()` now gates the decrement on
`Session::session_context_ready`, not merely "did this fd ever get a
`user_id`." Reverting that check back to the old condition during this
work reproduced exactly this symptom (a spurious `offline` broadcast for a
user with another connection still fully online) in
`tests/integration/Server.test.cpp`'s "A second tab's hydration never
completing..." test — this is a different bug from the one this document
otherwise tracks, but the same *family*: a presence decrement not matched
by a real increment, for the same user_id, corrupting a shared counter.

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
`IdentifyHandler` made three synchronous internal-API calls
(`fetchBlocks`, `fetchFriends`, `fetchUserProfile`, 5 s timeout each)
between creating the session and the presence increment; a later pass
combined these into one call (`fetchSessionContext`), and a further pass
after that (IDENTIFY hardening B2, see the update above) moved the call
off `IdentifyHandler` and this main loop entirely, onto
`session::SessionHydrationWorker`'s own thread — closing the specific
exception window the root-cause candidate above described, though not
confirmed as the actual cause. Presence broadcasts also now go through
`broadcastExcluding` (identified sessions only).
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

## IDENTIFY hydration's ~60s retry bound is only that short because the web client can't reconnect on its own

**Context**: IDENTIFY hardening B2 made the post-`IDENTIFY` blocks/friends/
profile load (`session::SessionHydrationWorker`) asynchronous with bounded
retry (`shared/protocol/README.md`'s [Asynchronous IDENTIFY
Hydration](../shared/protocol/README.md#asynchronous-identify-hydration)
section) instead of retrying forever. If the internal API stays down past
that budget, the server sends `SESSION_CONTEXT_UNAVAILABLE` and disconnects
rather than leaving an unenforced block list in place indefinitely.

**Why the bound is ~60s and not tighter**: the web client
(`web/hooks/useGatewayConnection.ts`) has no automatic reconnect logic
today — a dropped connection just stays dropped until the user manually
refreshes the page. Given that, disconnecting is a real, user-visible cost
(not "the client silently retries a moment later"), so the retry budget was
deliberately set generous enough to ride out a brief internal-API blip
rather than the tightest bound that would still be technically correct.

**Prerequisite for tightening this later**: shortening the retry window
(or reintroducing anything closer to "fail fast") only becomes a good
tradeoff once the web client can transparently reconnect and re-`IDENTIFY`
after a `SESSION_CONTEXT_UNAVAILABLE`-triggered disconnect, without the
user noticing or losing their place. Until `useGatewayConnection` gains
that reconnect flow, treat ~60s as close to a floor, not a starting point
to shrink opportunistically.

---

## `web/test/internalIsolation.test.ts` intermittently times out under local resource contention, not a code defect

**Symptom**: `npm test`'s full 59-file suite occasionally reports one
failure — always the same file, `test/internalIsolation.test.ts` — with
`Error: Hook timed out in 300000ms` inside its `beforeAll` (specifically
at `network = await new Network().start();`, the first line of the hook).
Every other file in the run passes; the isolation test's own assertions
never run at all, let alone fail — the timeout is in container/network
provisioning, before the test body executes.

**Investigated (2026-09), reproduced and bounded, not a security concern**:
`npm test` (the full 59-file suite) was run four times in a row on the
same machine, on `feat/profile-view-visibility` before merging PR #55, in
response to that PR's review asking whether this recurs:

| Run | Result | Duration |
|---|---|---|
| 1 (original, during #55) | `internalIsolation.test.ts` failed (`beforeAll` timeout) | 420s |
| 2 | 59/59 passed | 150s |
| 3 | 59/59 passed | 242s |
| 4 | `internalIsolation.test.ts` failed (`beforeAll` timeout) | 413s |

The two failures and the two full passes correlate directly with total
run duration (~420s vs. ~150–240s) — the slow runs are slow throughout,
not just at the isolation test, consistent with system-wide resource
pressure rather than a bug specific to this test or to any of the other
58 files. At the time of a slow run, `free -h` showed the host (7.1 GiB
total RAM) down to under 500 MiB free with active swap use; `docker system
df` showed no leftover containers accumulating between runs (2 total, one
weeks-old and exited) — so this isn't stale-container buildup, it's
memory/IO contention during the run itself. `internalIsolation.test.ts` is
explicitly the heaviest file in the suite (it builds the real `web`
Docker image and starts two containers on a dedicated network — see its
own header comment) and already gets a generous, deliberately-set 300s
`beforeAll` timeout (5x the suite's global 60s default,
`test/internalIsolation.test.ts:88`) specifically because it's slow even
when healthy; under this machine's memory pressure, even that budget
wasn't always enough.

**Not reproduced in CI**: the last 15 `web-ci.yml` runs on GitHub Actions
(as of 2026-09-29) all succeeded. Small sample, and this doesn't rule out
the same mechanism ever affecting a `ubuntu-latest` runner (comparable
~7 GiB RAM spec to the machine above) under its own load, but there is no
observed occurrence there to date — this is currently a local, not a CI,
flake.

**Why this isn't being treated as a bug to fix**: no assertion inside
`internalIsolation.test.ts` has ever failed — only container/network
*provisioning* timed out, before the isolation check itself ran. The
`/internal/*` isolation property this test verifies (`docs/security-audit.md`
§1.2) was never actually exercised-and-failed in any of these runs; the
failure mode is "didn't get to run in time," not "ran and found the
boundary broken." Raising the hook timeout further
would only mask slow-machine symptoms, not fix anything, and this test is
correctly the last line of defense for a real security property, so it
should stay strict about actually running to completion rather than being
loosened.

**If this recurs and needs investigating further**: check `free -h`/
`docker system df` at failure time first — the working hypothesis is
purely "not enough headroom on this machine for a 59-file suite's peak
concurrent Docker usage," not application or test-code behavior. A fix,
if one is ever needed, belongs in CI/local-environment resourcing (e.g.
running this file in its own `vitest` invocation, separate from the other
58 files, so its container build doesn't compete with whatever the rest
of the suite already has in flight) rather than in `internalIsolation.test.ts`
itself.

---

## No React component-rendering tests exist anywhere in `web/` (backlog decision, not an urgent gap)

**What's missing**: `web/`'s test suite (59 files, ~380 tests as of v0.8) covers API routes, internal-API/Postgres query functions, auth/JWT logic, and the gateway-protocol hook's pure-mapping functions — but zero tests render a React component and assert on what it shows or does. `vitest.config.ts` sets `test.environment: "node"` for the whole suite; there is no jsdom or happy-dom environment configured anywhere, and no `@testing-library/react` (or any React rendering/testing library) in `package.json`. This is true despite the app itself being almost entirely interactive components (`web/components/`, `web/app/page.tsx`) — the UI layer has no automated coverage of its own, only of the data/logic it's built on top of.

**Why this is being flagged now**: `ProfileView`'s self-view exception (`docs/social/friends-dms-design.md` §4.6, v0.8) is the first piece of UI logic in this codebase where "does the right thing conditionally render" is itself the thing worth testing — whether the "Edit profile" button shows only when `userId === myUserId`, and whether clicking it correctly closes `ProfileView` and opens `SettingsModal` to its Profile section. Two such tests were asked for when that feature was built; they weren't added, because writing them means standing up React Testing Library and a jsdom/happy-dom environment first — a real infrastructure decision (which library, whether it's a second `environment` in the existing `vitest.config.ts` or a separate project/config, whether it runs alongside or serially with the Postgres/Redis-backed `node`-environment tests) rather than something to make unilaterally inside a feature PR.

**Not treated as urgent**: nothing here is broken, and the codebase has shipped a substantial amount of conditional UI logic already (role-gated buttons, self-exclusion guards throughout `MemberList`/`FriendsView` before this same PR removed several of them, `SettingsModal`'s section switching) without this kind of test and without an incident traceable to that gap. This is a backlog item for a deliberate decision — when component-level UI logic is complex enough that manual browser verification alone stops being sufficient — not a fix to schedule reactively.

**Update — now tied to a real incident, not just a hypothetical.** A manual pass against this same feature found self-view via `MemberList` apparently not opening `ProfileView` despite a confirmed, correct 200 response from `GET /api/users/:id/profile`. Investigating it required exactly the tooling this entry describes not existing: a temporary, uncommitted `@testing-library/react` + jsdom harness (installed with `npm install --no-save`, deleted afterward, nothing added to `package.json`/the lockfile) was the only way to actually exercise the click -> fetch -> render sequence and trace real state transitions instead of guessing from reading the code. That harness rendered the real component tree (`page.tsx`, `MemberList`, `ProfileView`, mocking only `useGatewayConnection`) and reproduced the click→fetch→render path successfully across several timing variations (instant resolve, a realistic 150ms delay, with and without `React.StrictMode`), which did surface two real, separate, now-fixed issues — `ProfileView`'s fetch effect double-firing a real network request under Strict Mode's dev-only double-invoke (confirmed by reading Next's own installed source: `__NEXT_STRICT_MODE_APP` defaults to `true` when `next.config.ts` doesn't set `reactStrictMode`, which this repo doesn't), and a rejected `fetchProfile()` promise having no `.catch()`, leaving `profile` stuck at its loading value indefinitely instead of falling back to the existing "unavailable" state — but did **not** reproduce the originally reported symptom (no DOM node at all, zero console output) under any variation tried, including an attempt to force production-mode React (`NODE_ENV=production`), which failed outright because React Testing Library's `act()` has no implementation in React's production build (`TypeError: React.act is not a function`) — itself concrete evidence of how much is untestable about this app's actual `docker compose up --build` production runtime without either a real browser or a much larger investment in replicating Next's production bundle inside a test harness. If this specific symptom is reproduced again, the two most useful next facts are: whether it happens under `npm run dev` or the Docker-composed production build (materially different React behavior between the two, per above), and whether React DevTools' **Components** panel (not just Elements) shows a `ProfileView` instance with a non-null `userId` prop at all right after the click — that single check would distinguish "never mounted" from "mounted, rendered nothing."

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
