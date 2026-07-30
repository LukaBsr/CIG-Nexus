import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { REQUIRED_ENV_VARS } from "./lib/auth/env";
import { register } from "./instrumentation";
import { writeTempPemFile } from "./test/writeTempPemFile";

// register()'s file-readability check (§ instrumentation.ts) only cares
// that SESSION_JWT_PRIVATE_KEY_PATH resolves to a readable file — content
// doesn't need to be a real key for that check alone.
const validPrivateKeyPath = writeTempPemFile("-----BEGIN PRIVATE KEY-----\nfake\n-----END PRIVATE KEY-----\n");

const ALL_PRESENT: Record<string, string> = Object.fromEntries(
  REQUIRED_ENV_VARS.map((name) => [
    name,
    name === "SESSION_JWT_PRIVATE_KEY_PATH" ? validPrivateKeyPath : "value"
  ])
);

let originalEnv: NodeJS.ProcessEnv;

beforeEach(() => {
  originalEnv = { ...process.env };
  for (const name of REQUIRED_ENV_VARS) {
    delete process.env[name];
  }
  process.env.NEXT_RUNTIME = "nodejs";
});

afterEach(() => {
  process.env = originalEnv;
  vi.restoreAllMocks();
});

// register() calls process.exit() directly (a *thrown* error was verified
// not to reliably stop Next.js from starting anyway — see the comment in
// instrumentation.ts) — mocked here so the test runner's own process
// survives the assertion.
function mockProcessExit() {
  return vi.spyOn(process, "exit").mockImplementation(() => undefined as never);
}

describe("register", () => {
  it("does not exit when every required env var is present and the key file is readable", async () => {
    Object.assign(process.env, ALL_PRESENT);
    const exitSpy = mockProcessExit();

    await register();

    expect(exitSpy).not.toHaveBeenCalled();
  });

  it("exits with a non-zero code and names every missing var when some are absent", async () => {
    Object.assign(process.env, ALL_PRESENT);
    delete process.env.DATABASE_URL;
    delete process.env.OAUTH_TXN_SECRET;
    const exitSpy = mockProcessExit();
    const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});

    await register();

    expect(exitSpy).toHaveBeenCalledWith(1);
    const message = errorSpy.mock.calls[0]?.[0] as string;
    expect(message).toContain("DATABASE_URL");
    expect(message).toContain("OAUTH_TXN_SECRET");
    expect(message).not.toContain("DISCORD_CLIENT_ID"); // present — must not be listed
  });

  it("treats an empty string the same as a missing var", async () => {
    Object.assign(process.env, ALL_PRESENT);
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = "";
    const exitSpy = mockProcessExit();
    vi.spyOn(console, "error").mockImplementation(() => {});

    await register();

    expect(exitSpy).toHaveBeenCalledWith(1);
  });

  it("exits when SESSION_JWT_PRIVATE_KEY_PATH is set but the file doesn't exist", async () => {
    Object.assign(process.env, ALL_PRESENT);
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = "/nonexistent/private.pem";
    const exitSpy = mockProcessExit();
    const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});

    await register();

    expect(exitSpy).toHaveBeenCalledWith(1);
    const message = errorSpy.mock.calls[0]?.[0] as string;
    expect(message).toContain("SESSION_JWT_PRIVATE_KEY_PATH");
    expect(message).toContain("/nonexistent/private.pem");
  });

  // docs/security-audit.md §1.3 / action item 3.
  it("exits when the key file exists but is group- or world-readable", async () => {
    Object.assign(process.env, ALL_PRESENT);
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = writeTempPemFile(
      "-----BEGIN PRIVATE KEY-----\nfake\n-----END PRIVATE KEY-----\n",
      "loose.pem",
      0o644
    );
    const exitSpy = mockProcessExit();
    const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});

    await register();

    expect(exitSpy).toHaveBeenCalledWith(1);
    const message = errorSpy.mock.calls[0]?.[0] as string;
    expect(message).toContain("group- or world-readable");
  });

  it("does nothing outside the Node.js runtime, even with vars missing", async () => {
    process.env.NEXT_RUNTIME = "edge";
    const exitSpy = mockProcessExit();

    await register();

    expect(exitSpy).not.toHaveBeenCalled();
  });
});
