# CI Workflow Audit — Proposal

**Status: proposal only. Nothing in `.github/workflows/` has been
modified.** Every recommendation below is for review — see
[Proposed Action List](#proposed-action-list) at the end for the concrete,
ordered list to confirm before anything changes.

Scope: `.github/workflows/` as it exists today (`ci-server.yml`,
`web-ci.yml` — there are no others), audited against the repo's current
post-auth-merge state (Postgres/Redis-backed persistence, RSA-signed
sessions, the `/internal/*` isolation boundary, and the gateway, which has
never had CI). This follows up on the gap flagged but not acted on in
[`docs/architecture-audit.md`](architecture-audit.md) §5/§6/item 8.

## 1. Current State

### `ci-server.yml`

```yaml
on: [push, pull_request]   # no path filter — runs on every change, repo-wide
steps:
  - checkout
  - apt-get install cmake g++
  - cmake -S server -B server/build
  - cmake --build server/build
  - ctest --test-dir server/build
```

### `web-ci.yml`

```yaml
on:
  push/pull_request:
    paths: ["web/**"]
steps:
  - checkout
  - setup-node@v4 (node 20)
  - npm install
  - npm run build
```

## 2. What's Missing: No Gateway CI

`gateway/` has zero CI coverage — confirmed, there is no
`gateway-*.yml`/`*-gateway.yml` file, and `gateway/package.json` has only
`build` and `start` scripts (no `test`, no `lint`). This means `gateway/`
changes are the only service where a broken `tsc` compile wouldn't be
caught until someone runs `docker compose up --build` locally or in
production.

**Proposal**: add a workflow matching `web-ci.yml`'s existing shape —
same actions (`checkout`, `setup-node@v4`, node 20), same steps
(`npm install`, `npm run build`), path-filtered to `gateway/**`. `tsc -p
tsconfig.json` (the existing `build` script) already fails on type errors,
so this alone closes the "nothing checks gateway/ at all" gap without
needing new scripts. Adding `lint`/`test` scripts to `gateway/package.json`
itself is a separate, larger change (there's no ESLint config or test
files in `gateway/` today) — out of scope for this CI-only audit; noted
under [§5](#5-anything-else-missing).

## 3. What's Now Required That Wasn't Before: Postgres/Redis for `web/` Tests

**Finding: `web-ci.yml` never runs `npm test` at all — only `npm run
build`.** This is the central issue in this audit. It means the entire
Vitest suite covering `db/`, `lib/auth/*`, `lib/internal/*`, the OAuth
routes, `instrumentation.ts`, and the `/internal/*` Docker-isolation
integration test (`web/test/internalIsolation.test.ts`) **does not run in
CI today, at all** — not "failing," simply never invoked. Every one of
those tests currently only runs when someone happens to run `npm test`
locally.

This is not a "missing Postgres/Redis services:" gap in the way the
question implies, though — it's simpler and more upstream than that.
`web/test/global-setup.ts` already provisions its own ephemeral Postgres
and Redis via `@testcontainers/postgresql`/`@testcontainers/redis`
(started once per test run, torn down after), and
`internalIsolation.test.ts` provisions its own Postgres testcontainer plus
builds and runs the real Docker image, all via the Docker daemon that's
already present on GitHub-hosted `ubuntu-latest` runners. **No `services:`
block is needed in the workflow YAML** — testcontainers manages its own
containers directly against the runner's Docker socket. Adding a
`services: {postgres, redis}` block would be redundant with (and
potentially conflict with) what `global-setup.ts` already does; the actual
fix is just adding the missing `npm test` step.

Secrets: confirmed by reading every test file that touches an
`authEnv`-gated variable (`lib/auth/env.ts`) — each one sets its own
short-lived dummy value directly in the test file (e.g.
`process.env.DISCORD_CLIENT_ID = "test-client-id"` in
`app/api/auth/discord/login/route.test.ts`, a temp-file-backed RSA key via
`writeTempPemFile()`/`generateTestRsaKeyPair()` in
`app/api/auth/session-token/route.test.ts`, hardcoded `"unused"` values for
whatever `internalIsolation.test.ts` needs just to satisfy
`instrumentation.ts`'s startup check without exercising real auth). **No
CI-level secret injection is required for the existing test suite to run
as-is** — this is a design choice already made and already correct, not a
gap. (§4 below still proposes CI-level dummies, but for a different reason:
making it possible to write *new* tests, or a future smoke-test job, that
need one without reaching for real values — not because today's suite
needs it.)

The C++ side needs no equivalent: confirmed via grep, `server/tests/`
contains zero `getenv`/`std::getenv` calls. `TestJwtHelper` generates its
own throwaway RSA keypair via OpenSSL directly; the Catch2 suite
constructs `Server`/handler objects in-process and never goes through
`main.cpp`'s `requireEnv()` calls at all. `ci-server.yml`'s `ctest` step
needs no secrets today and none of the changes below add that requirement.

**Separately, likely-broken finding**: `ci-server.yml` installs only
`cmake g++`. `server/CMakeLists.txt` has `find_package(OpenSSL REQUIRED)`
and `find_package(CURL REQUIRED)` (added for the JWT/internal-API work) —
neither `libssl-dev` nor `libcurl4-openssl-dev` is installed by the
workflow. `server/Dockerfile`'s build stage needed both installed
explicitly (found by actually running `docker compose up --build` in an
earlier session) — GitHub's `ubuntu-latest` image is not guaranteed to
ship these `-dev` header packages by default, and this can't be confirmed
without an actual CI run. Whether or not the current workflow happens to
pass today, this should be fixed regardless: matching the Dockerfile's
apt package list removes the ambiguity rather than depending on whatever
happens to be preinstalled.

## 4. Secrets Handling in CI

None of today's tests strictly require CI-injected secrets (§3), but two
things are worth setting up now: a documented, reusable pattern for tests
that do need one, and closing off any path to real credentials leaking
into CI. Proposal:

- **Test-only RSA keypair**: generate fresh at job start, in-workflow —
  `openssl genrsa -out /tmp/ci-private.pem 2048 && openssl rsa -in
  /tmp/ci-private.pem -pubout -out /tmp/ci-public.pem` — one throwaway
  keypair per CI run, discarded when the runner is torn down. This is the
  same approach `internalIsolation.test.ts` and `TestJwtHelper` already
  use (generate locally, never reuse a checked-in value); a workflow-level
  keypair would exist for a future job that needs `SESSION_JWT_PRIVATE_KEY_PATH`/
  `AUTH_JWT_PUBLIC_KEY_PATH` pointed at real files without duplicating
  keygen logic per test file. **Never** derived from or related to any key
  that ever existed in `secrets/` or git history.
- **`INTERNAL_API_SHARED_SECRET`**: a random value generated the same way
  (`openssl rand -hex 32`) or a fixed non-sensitive placeholder
  (`ci-test-shared-secret`) set directly in the workflow YAML as a plain
  `env:` value — it's a shared secret between two services in the same CI
  job, not a real credential, so there's nothing to protect by routing it
  through GitHub Secrets.
- **`DISCORD_CLIENT_SECRET`**: a fixed dummy string
  (`ci-test-discord-client-secret-unused`) set directly in workflow YAML.
  It must never resolve to a real Discord app — nothing in the test suite
  calls Discord's actual token/API endpoints (confirmed: `login`/`callback`
  route tests mock or short-circuit before any real network call, and
  `internalIsolation.test.ts` never exercises the OAuth exchange at all).
- **Explicit confirmation**: no test anywhere in `web/test/` or
  `app/api/auth/discord/*/route.test.ts` performs a real Discord OAuth
  token exchange or hits `discord.com`. That would require a live browser
  session/real user consent and is out of scope for CI, consistent with
  what was already established when these tests were written. This audit
  found nothing that contradicts that — confirmed by reading every OAuth
  route test file, not just the callback one.
- **None of the above ever touch `.env`, `secrets/*.pem`, or any GitHub
  Actions "Secrets" store containing real values.** Everything proposed
  here is either generated fresh in-workflow or a hardcoded, obviously-fake
  placeholder committed in plaintext to the workflow YAML itself — which is
  fine precisely because none of it is real.

## 5. Naming Convention

Current: `ci-server.yml` (`name: ci-server`) vs. `web-ci.yml` (`name: Web
CI`) — inconsistent on both filename ordering and the human-readable
`name:` field's casing.

**Proposal**: `<service>-ci.yml` filenames (`server-ci.yml`, `web-ci.yml`,
`gateway-ci.yml`) — service-first groups related workflows together
alphabetically as more are added per service later (e.g. a hypothetical
`server-release.yml` would sort next to `server-ci.yml`), which
`ci-<service>.yml` doesn't give you. `web-ci.yml` already follows this
pattern; only `ci-server.yml` → `server-ci.yml` is a rename.

For the `name:` field (what actually shows in the GitHub Actions UI, not
the filename), standardize on Title Case service names: `Server CI`, `Web
CI`, `Gateway CI` — matching `web-ci.yml`'s existing `Web CI`, changing
`ci-server`'s `name: ci-server` to `name: Server CI`.

## 6. Anything Else Missing

- **`ci-server.yml` has no path filter** — it runs on every push/PR
  regardless of what changed, unlike `web-ci.yml`'s `paths: ["web/**"]`.
  Propose adding `paths: ["server/**"]` for consistency and faster
  feedback on unrelated changes (e.g. a docs-only PR currently still
  triggers a full C++ build+test cycle).
- **No Drizzle migration-drift check.** `web/db/migrations/` currently has
  exactly one committed migration (`0000_big_lake.sql`). Nothing in CI
  verifies that `db/schema/*.ts` and the committed migrations actually
  agree — a schema change landed without running `drizzle-kit generate`
  first (or without committing the result) would pass `npm run build`
  silently and only surface at deploy time when `drizzle-kit migrate` runs
  against a real database. Propose a step that runs `npx drizzle-kit
  generate` against a throwaway `DATABASE_URL` and fails the job if it
  produces any new/changed file under `db/migrations/` that isn't already
  committed (`git diff --exit-code db/migrations`).
- **No `npm run lint` step anywhere in `web-ci.yml`.** ESLint is
  configured (`eslint-config-next`) but never invoked in CI — only `next
  build`'s own type-checking runs. Propose adding it as its own step
  (fails fast, cheaper than a full build).
- **No C++ formatting check.** `.clang-format` exists at the repo root but
  nothing in `ci-server.yml` runs `clang-format --dry-run --Werror`
  against it. Lower priority than the items above (style drift, not a
  correctness risk) — flagging for completeness, not urging immediate
  action.
- **Cross-service coverage gap**: a change to `shared/protocol/README.md`
  or a C++ handler that changes wire behavior doesn't trigger `web-ci.yml`
  (path-filtered to `web/**` only), so nothing automatically re-checks
  that the web client's assumptions about the wire format still hold.
  There's no integration test spanning gateway+server+web today to run
  even if it were triggered, so this is noted as a known gap rather than
  something with a concrete fix proposed here — a real cross-service
  contract test is a larger effort than this CI-config audit covers.

## Proposed Action List

Ordered by independence/risk, same convention as
[`docs/architecture-audit.md`](architecture-audit.md).

1. **Add the missing `apt-get install libssl-dev libcurl4-openssl-dev` to
   `ci-server.yml`**, matching `server/Dockerfile`'s build-stage package
   list exactly. Lowest-risk, addresses a plausible-broken-CI finding
   first regardless of what else is decided below.
2. **Add `npm test` (`vitest run`) as a step in `web-ci.yml`**, after `npm
   run build`. No `services:` block needed — `web/test/global-setup.ts`
   and `internalIsolation.test.ts` already provision their own
   Postgres/Redis/Docker-image resources via testcontainers against the
   runner's existing Docker daemon. This is the single highest-value
   change in this audit: it's what actually turns on CI coverage for the
   entire persistence/auth/isolation test suite that currently never runs
   anywhere but a developer's machine.
3. **Add `npm run lint` as a step in `web-ci.yml`**, before or alongside
   `npm run build`.
4. **Add a Drizzle migration-drift check step to `web-ci.yml`**: run `npx
   drizzle-kit generate` against a throwaway `DATABASE_URL` (a Postgres
   testcontainer or a `services:` block — either works here since this
   step doesn't share `global-setup.ts`'s state) and fail if it produces
   an uncommitted change under `db/migrations/`.
5. **Create `gateway-ci.yml`**, mirroring `web-ci.yml`'s shape
   (`checkout`, `setup-node@v4` node 20, `npm install`, `npm run build`),
   path-filtered to `gateway/**`.
6. **Rename `ci-server.yml` → `server-ci.yml`**, change its `name:` field
   from `ci-server` to `Server CI`, and align `web-ci.yml`'s and the new
   `gateway-ci.yml`'s `name:` fields to `Web CI`/`Gateway CI` for
   consistency (`web-ci.yml`'s is already correct). Do this as its own
   commit, separate from any content changes above, so the rename's diff
   is easy to review on its own.
7. **Add `paths: ["server/**"]` to `ci-server.yml`** (post-rename,
   `server-ci.yml`), matching `web-ci.yml`'s existing pattern.
8. *(Lower priority, not blocking anything)* Add a `clang-format
   --dry-run --Werror` step to the server workflow using the existing
   `.clang-format` config.
9. *(Explicitly not proposed as part of this list)* A real
   gateway+server+web cross-service contract test — noted as a gap in
   [§6](#6-anything-else-missing), but designing one is out of scope for a
   CI-configuration audit.
