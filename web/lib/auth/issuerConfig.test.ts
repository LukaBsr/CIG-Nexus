import { readFileSync } from "node:fs";

import { describe, expect, it } from "vitest";

import { ACCESS_JWT_ISSUER } from "./jwt";

// The C++ server's expected issuer is deployment config
// (AUTH_JWT_EXPECTED_ISSUER), while the web signer's issuer is a code
// constant. These tests catch the two drifting apart in the values this repo
// ships, since nothing at runtime compares them.
const repoRoot = new URL("../../../", import.meta.url);

describe("AUTH_JWT_EXPECTED_ISSUER configuration", () => {
  it(".env.example's value equals the web signer's issuer constant", () => {
    const envExample = readFileSync(new URL(".env.example", repoRoot), "utf8");
    const match = envExample.match(/^AUTH_JWT_EXPECTED_ISSUER=(.*)$/m);

    expect(match).not.toBeNull();
    expect(match![1].trim()).toBe(ACCESS_JWT_ISSUER);
  });

  it("docker-compose.yml passes it to the server as a required (`:?`) variable", () => {
    const compose = readFileSync(new URL("docker-compose.yml", repoRoot), "utf8");
    const serverService = compose.slice(compose.indexOf("\n  server:"), compose.indexOf("\n  gateway:"));

    expect(serverService).toMatch(/^\s+AUTH_JWT_EXPECTED_ISSUER: \$\{AUTH_JWT_EXPECTED_ISSUER:\?[^}]+\}/m);
  });
});
