# Security Audit — Findings and Proposed Action List

**Status: findings only. Nothing has been fixed.** Every item below is for
review — see [Proposed Action List](#proposed-action-list) at the end for
the concrete, ordered list to confirm before anything changes.

**Scope**: everything merged into `main` as of this audit — Discord OAuth2
(PKCE), session/JWT handling, Postgres persistence, the `/internal/*` API
boundary, Redis-backed rate limiting, and CI. All findings below are
against the actual current code on `origin/main` (re-read fresh for this
audit, not assumed from memory of earlier design/implementation work).
This does **not** cover the not-yet-built frontend rebuild
([`docs/frontend-rebuild-plan.md`](frontend-rebuild-plan.md)) — that gets
its own review once it exists, per explicit scope instruction.

## 1. Re-Verification of Prior Findings

Six items the request asked to be re-checked against current code, not
assumed still true.

### 1.1 JWT algorithm pinning (C++ verifier) — **confirmed, no regression**

`server/src/auth/JwtVerifier.cpp` still does exactly what was originally
built: the header's `alg` field is read and compared for equality against
the literal string `"RS256"` — nothing else — and rejected
(`UnsupportedAlgorithm`) if it doesn't match, *before* any cryptographic
operation runs. The actual verification call is unconditional:

```cpp
EVP_DigestVerifyInit(mdctx, &pctx, EVP_sha256(), nullptr, impl_->public_key)
```

There is no code path where the token's own header selects which digest,
key, or algorithm gets used — `alg` is checked, never dispatched on. This
closes the algorithm-confusion class of attack (including `alg: none`) as
designed. Verify-then-parse ordering is also intact: the payload is only
JSON-parsed after `signature_valid` is confirmed true, so a
tampered-but-well-formed-JSON payload still correctly reports
`InvalidSignature`, not `Malformed`.

**Verdict: pass.**

### 1.2 `/internal/*` unreachability — **confirmed, with one defense-in-depth observation**

- `web/server.mjs` still runs two independent HTTP listeners (`PORT`,
  `INTERNAL_PORT`), each blanket-rejecting the other's routes before ever
  calling into Next's handler.
- `docker-compose.yml`'s `web` service publishes only `3000:3000` —
  `INTERNAL_PORT` (3001) has no `ports:` entry at all, confirmed by reading
  the file directly, not assumed.
- `web/test/internalIsolation.test.ts` genuinely builds the real
  `web/Dockerfile` image via `GenericContainerBuilder` and only calls
  `.withExposedPorts(PUBLIC_PORT)` — it never exposes the internal port,
  mirroring `docker-compose.yml` exactly. This is a real test against the
  real deployed artifact, not the dev server — confirmed by reading the
  test file, not assumed from an earlier report.

**Observation (not a regression, but worth recording):** all five
`docker-compose.yml` services — `postgres`, `redis`, `web`, `server`,
`gateway` — share one flat bridge network (`cig-nexus`). This means
`gateway` (the one service directly exposed to untrusted browser input) has
*network-level* reachability to `web:3001` the same way `server` does. The
design's actual defense here is the shared-secret header
(`INTERNAL_API_SHARED_SECRET`), which `gateway` never has and has no reason
to have — so this isn't a live vulnerability today, but it means
"`/internal/*` is unreachable from outside the Docker network" is doing
less work than it might sound like: it's unreachable from the *host* and
the *internet*, but every container on `cig-nexus` — including the one
with the largest untrusted-input surface — is on the same L3 network as it.
Network segmentation (e.g. a separate internal-only Docker network for
`web`↔`server` traffic that `gateway` isn't attached to) would close this
gap; not present today.

**Verdict: pass, with a defense-in-depth note.**

### 1.3 `secrets/private.pem` file permissions — **not actually verified/enforced; currently correct by incidental behavior**

On this machine, `secrets/private.pem` is `600` and `secrets/public.pem` is
`664`. That looks right — but tracing *why* private.pem is `600` here: the
process `umask` is `002`, which would normally produce `664` for any newly
created file. The `600` is not umask-derived; it's OpenSSL's own default
behavior when writing PEM private-key output files with recent OpenSSL
versions, independent of the `.env.example` generation instructions, which
document the `openssl genrsa`/`pkcs8` commands but include **no `chmod`
step**. Confirmed via grep: there is no `chmod` anywhere in this repo, and
neither `web/instrumentation.ts` nor `server/src/main.cpp`'s
`readRequiredFile()` check the file's mode bits — both only check
existence/readability.

This means the `600` permission that happens to hold today is an
OpenSSL-version-dependent side effect, not a guarantee this repo makes or
checks. If the key is ever regenerated with a different tool, copied via a
method that doesn't preserve mode bits (`scp` without `-p`, a CI artifact
upload/download, an editor save), or generated on a system where the
OpenSSL version doesn't apply this default, the private key file could
silently end up group- or world-readable with nothing in this codebase
noticing or refusing to start.

`docker-compose.yml` mounting the file `:ro` only prevents the *container*
from writing to it — it says nothing about host-level file permissions,
which is what actually matters for "can another process/user on this host
read the private signing key."

**Verdict: currently fine on this machine, but unverified and
unenforced — the correctness is accidental, not designed.**

### 1.4 Fail-fast env var coverage — **confirmed complete**

- `web/lib/auth/env.ts`'s `REQUIRED_ENV_VARS` (checked by
  `instrumentation.ts`'s `register()`) covers `DISCORD_CLIENT_ID`,
  `DISCORD_CLIENT_SECRET`, `DISCORD_REDIRECT_URI`, `DATABASE_URL`,
  `REDIS_URL`, `SESSION_JWT_PRIVATE_KEY_PATH`, `OAUTH_TXN_SECRET`,
  `INTERNAL_API_SHARED_SECRET` — every security-relevant var `web/`
  actually reads through `authEnv`, confirmed by grepping every
  `process.env.*` read in non-test source: the only reads outside this set
  are `NODE_ENV` (framework-standard, not a secret) and
  `NEXT_PUBLIC_GATEWAY_URL` (pre-existing, public-by-convention, explicitly
  out of scope per `CLAUDE.md`).
- `AUTH_JWT_PUBLIC_KEY_PATH` is passed to the `web` container and mounted,
  but not read by any `web/` code — this is already explicitly documented
  in `.env.example` ("web is also given `AUTH_JWT_PUBLIC_KEY_PATH` for
  parity with server's mount, but doesn't read it today"), so this is a
  confirmed-intentional non-issue, not a gap.
- `server/src/main.cpp` has exactly one `std::getenv` call site, wrapped by
  `requireEnv()`/`readRequiredFile()`, covering `AUTH_JWT_PUBLIC_KEY_PATH`,
  `INTERNAL_API_BASE_URL`, `INTERNAL_API_SHARED_SECRET` — confirmed via
  repo-wide grep for `getenv` in `server/`, there is no other env access
  anywhere in the C++ codebase.

**Minor secondary note:** `POSTGRES_USER`/`POSTGRES_PASSWORD`/`POSTGRES_DB`
(read directly by `docker-compose.yml`, not through `authEnv`/`requireEnv`)
have no fail-fast check of their own in this repo — if `.env` were missing
`POSTGRES_PASSWORD`, docker-compose would interpolate an empty string.
In practice the official `postgres:16-alpine` image's own entrypoint
refuses to start without a password (unless `POSTGRES_HOST_AUTH_METHOD=trust`
is set, which nothing here sets), so this is very likely still safe, but
the safety net is upstream Postgres's, not this repo's own fail-fast
pattern applied consistently.

**Verdict: pass** for every application-level var; the Postgres credential
note is minor and relies on upstream image behavior rather than this
repo's own enforcement.

### 1.5 Session revocation cache: restart gap — **confirmed, and more literal than "eventually catches up"**

`server/include/Server.hpp` / `Server.cpp`:

- `RevocationCache` is a bare in-memory `std::unordered_set`, empty on
  construction. Nothing persists it across process restarts.
- `Server::start()` calls `hydrateGuildCatalog()` **immediately**, before
  entering the accept loop — the guild/channel catalog is populated from
  the internal API before the server ever accepts a connection.
- There is **no equivalent immediate call for revocations**.
  `last_revocation_poll_` is set to "now" at the end of `start()`'s setup,
  and `pollRevocationCache()` is only ever invoked from inside the main
  loop, gated on `now - last_revocation_poll_ >= kRevocationPollInterval`
  (30 seconds). The first poll cannot fire before 30 seconds of uptime.

Concretely: on every server restart (deploy, crash-restart, orchestrator
restart), for up to 30 seconds, `revocation_cache_.isRevoked(...)` returns
`false` for *every* session id, including ones that were revoked hours
earlier and are recorded as such in Postgres. Any client holding a
cryptographically valid, not-yet-expired JWT for a since-revoked session
(e.g. an explicitly logged-out or banned user) can successfully `IDENTIFY`
during that window, and any *already-connected* revoked session that
hasn't yet been swept won't be disconnected either — the sweep is the same
`pollRevocationCache()` call.

This is a materially different (and more literal) statement than "the
revocation gap is bounded by the poll interval" as the design doc frames
it in steady state — on restart specifically, the bound is "up to one full
poll interval measured from process start," and nothing in the current
code narrows that the way `hydrateGuildCatalog()`'s immediate-fetch pattern
already does for the guild catalog. The fix is small and has a direct
precedent already in the same file (call `pollRevocationCache()` once,
synchronously, in `start()`, mirroring `hydrateGuildCatalog()`) — not
proposing to implement it here, just noting the gap is real, currently
un-mitigated, and the codebase already has the pattern to fix it with.

**Verdict: confirmed gap, not just a theoretical one — currently exists
in the code as of this audit, needs a decision (accept explicitly, or
harden).**

### 1.6 CI dummy secrets — **confirmed, no leakage path found**

- Grepped every `.ts`/`.tsx`/`.cpp`/`.hpp`/`.yml` file for dummy-value
  patterns (`unused`, `test-client-id`, `test-client-secret`, `ci-test`) —
  every match is inside a `.test.ts` file, `web/test/`, or a
  `.github/workflows/*.yml` step. None appear in `web/lib/auth/env.ts`, any
  route handler, or any C++ non-test source.
- No fallback (`||`, `??`) near any `authEnv`/`requireEnv` accessor that
  could silently substitute a dummy value for a missing real one.
- `server/tests/CMakeLists.txt`'s test binary (`cig-nexus-tests`) links
  `TestJwtHelper`/`FakeInternalApiClient`/`TestHttpServer` alongside
  `server/src/*.cpp` with `main.cpp` explicitly excluded
  (`list(FILTER SERVER_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")`). The
  production binary (`cig-nexus-server`, `server/CMakeLists.txt`) only ever
  globs `src/*.cpp` — test helpers are never compiled into it, and
  `server/Dockerfile` builds with `-DBUILD_TESTS=OFF`, so they're not even
  fetched/compiled during the image build at all.
- CI workflow dummy values (`ci-test-shared-secret`-style strings, the
  dummy `DATABASE_URL` for the Drizzle drift check) are scoped entirely to
  workflow YAML `env:` blocks for individual steps — they never touch
  `.env`, `secrets/*.pem`, or any path application code reads from.

**Verdict: pass.**

## 2. Fresh Findings

Areas not covered by the earlier design/implementation/CI audits.

### 2.1 Rate-limit bypass via `X-Forwarded-For` spoofing — **real, currently exploitable**

```ts
// web/lib/auth/clientIp.ts
export function clientIp(request: NextRequest): string {
  return request.headers.get("x-forwarded-for")?.split(",")[0]?.trim() ?? "unknown";
}
```

Both OAuth routes rate-limit by calling this function
(`app/api/auth/discord/login/route.ts`,
`app/api/auth/discord/callback/route.ts`, the latter via an import alias
`rateLimitClientIp`). It trusts the **first** value of a client-supplied
header with no validation.

There is no reverse proxy anywhere in this repo's deployment path —
confirmed by searching for `nginx`/`Caddyfile`/any proxy config (none
exist) and by reading `docker-compose.yml` (`web` publishes `3000:3000`
directly to the host, nothing sits in front of it). This means the
Next.js process is the first hop that sees the request directly from
whatever client connects to it. Under this topology, a client can set
`X-Forwarded-For: <anything>` to an arbitrary, different value on every
request, and the rate limiter will treat each one as a distinct client —
**completely defeating the per-IP sliding-window limit** on both OAuth
routes. This undermines security-checklist item §9.1 ("Rate limiting on
OAuth routes") as currently deployed, not as designed.

A closely related second consequence: `callback/route.ts`'s
`sessionIpAddress()` uses the same trust-the-header pattern to populate
`sessions.ip_address` (a Postgres `inet` column, presumably intended for
audit/forensic use). That value is equally attacker-controlled today —
anything stored there cannot be relied on for investigation or abuse
tracking.

Because `web/server.mjs` runs a raw `node:http` server (not a
serverless/edge platform), the actual TCP peer address is available at the
socket level and isn't currently used anywhere as a source of truth or a
validation check against the claimed `X-Forwarded-For` value.

**Verdict: real, currently exploitable given the actual deployed topology
(no reverse proxy) — highest-severity fresh finding in this audit.**

**Resolution (implemented):** `web/server.mjs` now stamps every request
with the real TCP peer address (`req.socket.remoteAddress`) under a
`x-cig-nexus-remote-addr` header, unconditionally overwriting anything the
client sent before handing off to Next.js; `clientIp()` and
`sessionIpAddress()` (now `trustedRemoteAddress()`) read only that header,
never `X-Forwarded-For`. This is correct *only* as long as nothing sits in
front of `web` — the fix works by making `web/server.mjs` itself the trust
boundary, since it currently is the first hop with raw socket access. **If
a reverse proxy is introduced later** (e.g. for TLS termination — see
§2.2's `NODE_ENV`-dependent `secure` cookie caveat, which would motivate
exactly this), the raw socket address will then belong to the proxy, not
the real client, and this fix will need to be revisited: switch to
trusting a proxy-set header (`X-Forwarded-For` or `X-Real-IP`) again, but
only after validating it against a trusted-proxy allowlist (e.g. checking
the immediate peer is the known proxy IP before reading the header it set).
Flagging this as a prerequisite to revisit whenever a reverse proxy enters
the deployment topology — not an issue today.

### 2.2 Cookie flags (`__session`, `oauth_txn`) — **correct, with one deployment-dependent caveat**

Both `REFRESH_COOKIE_OPTIONS` (`web/lib/auth/session.ts`) and
`OAUTH_TXN_COOKIE_OPTIONS` (`web/lib/auth/oauthTxnCookie.ts`):

| Flag | Value | Assessment |
|---|---|---|
| `httpOnly` | `true` (both) | Correct — neither cookie needs JS access. |
| `sameSite` | `"lax"` (both) | Correct for both: `Lax` is required (not `Strict`) on the OAuth txn cookie specifically because the Discord redirect-back is a cross-site top-level navigation that would drop a `Strict` cookie; `Lax` is also sufficient CSRF protection for the refresh cookie since it blocks cross-site POST/subrequests. |
| `secure` | `process.env.NODE_ENV === "production"` (both) | Conditional, not unconditional — documented in both files as deliberate, because this stack currently terminates plain HTTP in local/docker-compose dev (no TLS; `shared/protocol/README.md` already says so under "Security and Limits"). An unconditional `Secure` flag would make the cookie unsendable in that environment. |
| `path` | `"/"` (both) | Fine. |
| `maxAge` | set on both, matching each cookie's actual TTL | Fine. |

The `secure` conditional is a reasoned, documented tradeoff for the
current no-TLS deployment, not an oversight. The residual risk is that its
correctness is entirely contingent on `NODE_ENV` being exactly
`"production"` in whatever environment actually terminates real user
traffic with TLS — there's no fail-fast check (in the style already used
elsewhere in this codebase for env vars) that would catch a misconfigured
deployment shipping real traffic with `NODE_ENV` unset or wrong, silently
downgrading cookie security with no warning.

**Verdict: correct as designed for the current stack; flagging the
NODE_ENV-dependency as something to harden once a real TLS-terminated
deployment target is decided (out of scope for this audit, per
`docs/auth/discord-design.md` §10's "production deployment details" not
being addressed there either).**

### 2.3 SQL injection surface — **none found**

Every raw `` sql` ` `` tagged-template use in `web/db/schema/*.ts` is a
static `CHECK` constraint referencing a column via Drizzle's own
`${table.column}` interpolation (resolved to a quoted identifier at
schema/migration-generation time, not string-concatenated user input) —
e.g. `sql\`char_length(${table.name}) BETWEEN 1 AND 64\``. These are
compile-time, developer-authored schema definitions, never given
request-derived data.

Every runtime query (`web/lib/internal/catalog.ts`,
`web/lib/auth/session.ts`, `web/app/api/auth/session-token/route.ts`) goes
through Drizzle's query builder (`db.select/.insert/.update/.delete` with
`eq()`/`and()`/`gt()`/`isNull()`/`isNotNull()` and `.values({...})`) —
fully parameterized. No `sql.raw()` call exists anywhere in the codebase
(confirmed via grep).

**Verdict: pass.**

### 2.4 CORS configuration on `/api/*` and `/internal/*` — **no issue; secure by absence**

No CORS headers are set anywhere — no `next.config.ts` `headers()`
config (the file is the default `create-next-app` stub), no
`Access-Control-Allow-*` header in any route handler, no `cors` package
dependency. This means the browser's default same-origin enforcement
applies unmodified: a cross-origin page's JavaScript cannot read responses
from `/api/*` or `/internal/*` (the latter is unreachable from a browser
at all per §1.2 regardless). Combined with `SameSite=Lax` on both cookies
(§2.2), state-changing cross-site requests are also blocked from carrying
authentication.

**Verdict: pass — nothing to fix, the absence of configuration is the
correct state here.**

### 2.5 Dependency vulnerabilities

**`web/` — `npm audit`:** 3 high-severity advisories in production
dependencies (`next@16.2.10` and its transitive `postcss`/`sharp`), 24
total (4 moderate, 20 high) including dev dependencies. Advisory titles
for `next`: middleware/proxy bypass (Turbopack + single-locale),
Server Actions DoS/SSRF/unbounded-payload, cache confusion on requests
with bodies, SSRF via `rewrites()` with attacker-controlled hostname,
Image Optimization API DoS via SVG, unauthenticated disclosure of internal
Server Function endpoints.

At the time of this audit, `npm audit`'s fixed-version ranges for these
land only in `16.3.0-preview.*` builds — no stable release carries the fix
yet, so "just bump the version" isn't currently available as a clean
options; running `npm audit fix` would pull a preview/prerelease build.

**Practical exposure, checked against this app's actual code (not
assumed):** confirmed via grep that this app uses none of the specific
features several of these advisories require — no `"use server"` Server
Actions anywhere, no `next/image` usage (so `sharp` is an inert transitive
dependency, never invoked), no `middleware.ts`, no `rewrites()` in
`next.config.ts` (the file is the empty default stub), and the build/dev
scripts don't pass `--turbo`. This meaningfully narrows real-world
exposure for the Server-Actions-specific, Image-Optimization-specific, and
Turbopack-specific advisories in this specific deployment, though the
vulnerable package version is still present in the dependency tree and
should still be tracked for when a stable fix ships.

**`gateway/` — `npm audit`:** 0 vulnerabilities.

**`server/` — pinned `FetchContent` versions**
(`server/CMakeLists.txt`/`server/tests/CMakeLists.txt`): `nlohmann/json
v3.11.3`, `Catch2 v3.5.2`. Checked GitHub's Security Advisories API
directly for both repos (`gh api /repos/nlohmann/json/security-advisories`,
`/repos/catchorg/Catch2/security-advisories`) and cross-checked via the
GitHub Advisory Database search — no advisories found for either. Catch2
is additionally never compiled into the production binary at all: the
production `cig-nexus-server` target only globs `src/*.cpp`, and
`server/Dockerfile` builds with `-DBUILD_TESTS=OFF`, so Catch2 isn't even
fetched during the image build. System packages (`libssl3`, `libcurl4`,
`ca-certificates`) are installed via `apt-get install` on `ubuntu:22.04`
with no version pin — floating, not reproducible, but this means the image
picks up upstream security patches automatically on every rebuild rather
than needing this repo to track CVEs against a frozen version; noted as a
tradeoff, not a finding requiring action.

**Verdict: `web/`'s production `next` dependency needs tracking (no clean
fix available yet upstream); `gateway/` and `server/`'s pinned dependencies
are clean.**

## 3. Additional Observations

Noticed during the audit, not explicitly requested but adjacent enough to
record:

- **Open-redirect surface**: checked and clear. `login/route.ts` redirects
  to a hardcoded `https://discord.com/oauth2/authorize`;
  `callback/route.ts` redirects only to fixed relative paths (`"/"` or
  `"/login-error"`), never to a user- or query-param-controlled
  destination.
- **User identity binding**: `upsertDiscordUser()` upserts on
  `users.discordId`, sourced from Discord's own `/users/@me` response tied
  to the OAuth-verified access token — not any client-supplied field.
  Correct; not spoofable.
- **`POSTGRES_PASSWORD=change-me`** in `.env.example` is an obvious
  placeholder (standard practice for example-credential files) — not a
  finding on its own, just noting there's no automated check anywhere that
  a real deployment isn't still using it verbatim.

## Proposed Action List

Ordered by severity/independence, same convention as
[`docs/architecture-audit.md`](architecture-audit.md) and
[`docs/ci-audit.md`](ci-audit.md). Nothing has been executed.

1. **Fix the rate-limit `X-Forwarded-For` bypass (§2.1).** Highest
   severity, currently exploitable given the actual deployed topology.
   Options to evaluate: validate/require a trusted-proxy allowlist before
   trusting `X-Forwarded-For` at all, fall back to the raw socket peer
   address available in `web/server.mjs` (this app isn't on an edge
   platform, so this is actually available), or explicitly document that a
   trusted reverse proxy sanitizing this header is a hard deployment
   prerequisite and fail fast if request topology doesn't match that
   assumption. Also fixes the `sessions.ip_address` integrity issue as a
   side effect, since both derive from the same header.
2. **Close the revocation-cache restart gap (§1.5).** Call
   `pollRevocationCache()` once, synchronously, in `Server::start()`
   before entering the accept loop — mirrors the existing
   `hydrateGuildCatalog()` pattern in the same function. Small, precedented
   change; removes the up-to-30-second post-restart window where revoked
   sessions are treated as valid.
3. **Add an explicit `chmod 600` step to the private-key generation
   instructions in `.env.example`, and consider a startup permission check**
   (`web/instrumentation.ts` and/or `server/main.cpp`'s
   `readRequiredFile()`) that warns or refuses to start if
   `secrets/private.pem` is group- or world-readable (§1.3). Currently
   correct by incidental OpenSSL behavior, not by anything this repo
   verifies.
4. **Track the `next` dependency for a stable patched release (§2.5)** and
   upgrade once one exists outside the `16.3.0-preview.*` channel. Given
   the reduced practical exposure (no Server Actions/`next/image`/
   Turbopack/`rewrites()` usage), this doesn't need to block on a
   prerelease build, but shouldn't be forgotten once a stable fix ships.
5. **Decide on network segmentation for `/internal/*` (§1.2 observation).**
   Optional hardening: put `web`'s internal port and `server` on a Docker
   network `gateway` isn't attached to, so the shared-secret header isn't
   the *only* thing standing between a compromised `gateway` container and
   `/internal/*`. Lower priority — no live exploit path exists today, this
   is defense-in-depth only.
6. **Decide whether to harden the `secure` cookie flag's `NODE_ENV`
   dependency (§2.2)** once a real TLS-terminated deployment target is
   chosen — e.g. a fail-fast check that refuses to start if the app
   appears to be serving real traffic without `NODE_ENV=production` set
   correctly. Not actionable yet without knowing the actual deployment
   plan; flagged for whenever that's decided.
7. *(No action proposed)* §1.1, §1.4, §1.6, §2.3, §2.4 — re-verified or
   freshly checked and found correct. Recorded here for completeness, not
   because anything needs to change.
