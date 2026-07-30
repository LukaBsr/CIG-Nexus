# `web/app/internal/*`

These routes live outside the `/api/*` prefix used by `web/app/api/auth/*`
on purpose — this is not an inconsistency to fix.

They exist to implement the internal-only HTTP API described in
[`docs/auth/discord-design.md`](../../../docs/auth/discord-design.md) §8.1:
the C++ server's only way to reconcile protocol mutations (`CREATE_GUILD`,
`JOIN_GUILD`, etc.) with the Postgres data Next.js exclusively owns. Their
path has to match the literal `/internal/*` wire contract that design
document specifies and that `CurlInternalApiClient` (`server/`) calls
against — moving them under `/api/internal/*` would silently break that
contract.

`web/server.mjs` is what actually enforces the isolation: it binds `PORT`
and `INTERNAL_PORT` as two separate listeners, and each one blanket-rejects
the other's routes before ever reaching Next's request handler. Only
`INTERNAL_PORT` is published on the Docker-internal network — it has no
host-reachable mapping in `docker-compose.yml`. Every request here is also
authenticated by a shared-secret header (`INTERNAL_API_SHARED_SECRET`), not
by user sessions, since the caller is the C++ server, not a browser.

See §8.1's "Enforcing `/internal/*` isolation" for why the shared secret
alone isn't treated as sufficient, and `web/test/internalIsolation.test.ts`
for the integration test that verifies these routes are actually
unreachable from outside the internal network in the built Docker image.
