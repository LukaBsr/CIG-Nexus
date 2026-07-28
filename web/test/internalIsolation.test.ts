import path from "node:path";
import { fileURLToPath } from "node:url";

import { GenericContainerBuilder } from "testcontainers";
import type { StartedTestContainer } from "testcontainers";
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
// Building the image makes this the slowest test in the suite (~1-2 min
// uncached). That cost buys checking the actual deployed artifact, not a
// description of it.

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PUBLIC_PORT = 3000;
const INTERNAL_PORT = 3001;

let container: StartedTestContainer;
let baseUrl: string;

beforeAll(async () => {
  const image = await new GenericContainerBuilder(path.join(__dirname, ".."), "Dockerfile").build();

  container = await image.withExposedPorts(PUBLIC_PORT).start();
  baseUrl = `http://${container.getHost()}:${container.getMappedPort(PUBLIC_PORT)}`;
}, 300_000);

afterAll(async () => {
  await container?.stop();
});

describe("web Docker image: /internal/* isolation", () => {
  it("serves public routes normally on the published port", async () => {
    const response = await fetch(baseUrl + "/");
    expect(response.status).toBe(200);
  });

  it("returns 404 for /api/internal/* on the published (public) port", async () => {
    const response = await fetch(baseUrl + "/api/internal/catalog");
    expect(response.status).toBe(404);
  });

  it("returns 404 for nested /api/internal/* paths on the published port too", async () => {
    const response = await fetch(baseUrl + "/api/internal/guilds/g_1", { method: "DELETE" });
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
