import path from "node:path";
import { fileURLToPath } from "node:url";

import { PostgreSqlContainer, type StartedPostgreSqlContainer } from "@testcontainers/postgresql";
import { GenericContainerBuilder, Network } from "testcontainers";
import type { StartedNetwork, StartedTestContainer } from "testcontainers";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

// design doc §8.1: /internal/* must be genuinely unreachable from outside
// the Docker-internal network — not just protected by the shared-secret
// header. This builds and runs the *real* web Docker image (the one
// docker-compose.yml deploys) the same way an external attacker would
// reach it: only the public port published, exactly like
// docker-compose.yml's `ports:` mapping (§10) never lists the internal
// port. No mocking of the isolation mechanism itself — either the real
// image is actually unreachable on /internal/* via the public port, or it
// isn't.
//
// The image's own CMD runs migrations before serving (web/Dockerfile), so
// it needs a real reachable Postgres to boot at all — a second
// testcontainer on a shared Docker network, standing in for
// docker-compose.yml's postgres service.
//
// Building the image makes this the slowest test in the suite (~1-2 min
// uncached). That cost buys checking the actual deployed artifact, not a
// description of it.

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PUBLIC_PORT = 3000;
const INTERNAL_PORT = 3001;

let network: StartedNetwork;
let postgres: StartedPostgreSqlContainer;
let container: StartedTestContainer;
let baseUrl: string;

beforeAll(async () => {
  network = await new Network().start();

  postgres = await new PostgreSqlContainer("postgres:16-alpine")
    .withNetwork(network)
    .withNetworkAliases("postgres")
    .start();

  const image = await new GenericContainerBuilder(path.join(__dirname, ".."), "Dockerfile").build();

  const privateKeyPath = "/tmp/test-private.pem";

  container = await image
    .withNetwork(network)
    // instrumentation.ts's startup check doesn't just check
    // SESSION_JWT_PRIVATE_KEY_PATH is set — it actually reads the file at
    // that path, so (unlike the other "unused" placeholders below) this
    // one needs a real file to exist in the container, not just a string.
    .withCopyContentToContainer([
      {
        content: "-----BEGIN PRIVATE KEY-----\nunused\n-----END PRIVATE KEY-----\n",
        target: privateKeyPath,
        // withCopyContentToContainer defaults to a world-readable mode —
        // env.ts's readRequiredFile() now refuses to read a
        // *_PRIVATE_KEY_PATH file that's group- or world-readable
        // (docs/security-audit.md §1.3), so the real Docker image built
        // here would otherwise fail this test's own startup, not the
        // isolation behavior it's meant to check.
        mode: 0o600
      }
    ])
    .withEnvironment({
      DATABASE_URL: `postgres://${postgres.getUsername()}:${postgres.getPassword()}@postgres:5432/${postgres.getDatabase()}`,
      // This test only exercises HTTP routing/isolation, never real
      // auth — but instrumentation.ts now refuses to start the process at
      // all unless every required var is present (non-empty), so each of
      // these needs *some* value even though none of them need to be
      // functionally valid for what this test checks.
      REDIS_URL: "redis://unused:6379",
      DISCORD_CLIENT_ID: "unused",
      DISCORD_CLIENT_SECRET: "unused",
      DISCORD_REDIRECT_URI: "http://localhost:3000/api/auth/discord/callback",
      SESSION_JWT_PRIVATE_KEY_PATH: privateKeyPath,
      AUTH_JWT_PUBLIC_KEY_PATH: "/tmp/unused-public.pem",
      OAUTH_TXN_SECRET: "unused",
      INTERNAL_API_SHARED_SECRET: "unused"
    })
    .withExposedPorts(PUBLIC_PORT)
    .start();

  baseUrl = `http://${container.getHost()}:${container.getMappedPort(PUBLIC_PORT)}`;
}, 300_000);

afterAll(async () => {
  await container?.stop();
  await postgres?.stop();
  await network?.stop();
});

describe("web Docker image: /internal/* isolation", () => {
  it("serves public routes normally on the published port", async () => {
    const response = await fetch(baseUrl + "/");
    expect(response.status).toBe(200);
  });

  it("routes /api/user/appearance on the published port (session-authenticated, not /internal/*)", async () => {
    // docs/settings/appearance-design.md §3.5: a browser-facing
    // web/app/api/* route, deliberately not under web/app/internal/* —
    // confirms it was never accidentally swept into the /internal/*
    // isolation boundary below. A 401 (no session cookie) rather than a
    // 404 proves the route exists and is reachable on the public port;
    // it's rejected by the endpoint's own session check, not by routing.
    const response = await fetch(baseUrl + "/api/user/appearance", {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ theme: "ember" })
    });
    expect(response.status).toBe(401);
  });

  it("returns 404 for /internal/* on the published (public) port", async () => {
    const response = await fetch(baseUrl + "/internal/catalog");
    expect(response.status).toBe(404);
  });

  it("returns 404 for nested /internal/* paths on the published port too", async () => {
    const response = await fetch(baseUrl + "/internal/guilds/g_1", { method: "DELETE" });
    expect(response.status).toBe(404);
  });

  it("has no host-reachable mapping for the internal port at all", () => {
    // docker-compose.yml never lists INTERNAL_PORT in this service's
    // `ports:` (§6/§10) — mirrored here by simply never calling
    // withExposedPorts(INTERNAL_PORT). getMappedPort() throws for a port
    // that was never published, which is exactly the property being
    // asserted: there is no way to address it from outside the container.
    expect(() => container.getMappedPort(INTERNAL_PORT)).toThrow();
  });
});
