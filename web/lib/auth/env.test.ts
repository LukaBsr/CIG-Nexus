import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { writeTempPemFile } from "../../test/writeTempPemFile";
import { authEnv } from "./env";

let originalPath: string | undefined;

beforeEach(() => {
  originalPath = process.env.SESSION_JWT_PRIVATE_KEY_PATH;
});

afterEach(() => {
  if (originalPath === undefined) {
    delete process.env.SESSION_JWT_PRIVATE_KEY_PATH;
  } else {
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = originalPath;
  }
});

describe("authEnv.sessionJwtPrivateKey", () => {
  it("reads the file at SESSION_JWT_PRIVATE_KEY_PATH", () => {
    const content = "-----BEGIN PRIVATE KEY-----\nfake-key-content\n-----END PRIVATE KEY-----\n";
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = writeTempPemFile(content);

    expect(authEnv.sessionJwtPrivateKey).toBe(content);
  });

  it("throws naming the env var when SESSION_JWT_PRIVATE_KEY_PATH is unset", () => {
    delete process.env.SESSION_JWT_PRIVATE_KEY_PATH;

    expect(() => authEnv.sessionJwtPrivateKey).toThrow(
      "Missing required environment variable: SESSION_JWT_PRIVATE_KEY_PATH"
    );
  });

  it("throws naming both the env var and the path when the file doesn't exist", () => {
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = "/nonexistent/private.pem";

    expect(() => authEnv.sessionJwtPrivateKey).toThrow(/SESSION_JWT_PRIVATE_KEY_PATH=\/nonexistent\/private\.pem/);
  });

  // docs/security-audit.md §1.3 / action item 3.
  it("throws when the key file is group-readable", () => {
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = writeTempPemFile("fake-key", "key.pem", 0o640);

    expect(() => authEnv.sessionJwtPrivateKey).toThrow(/group- or world-readable/);
  });

  it("throws when the key file is world-readable", () => {
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = writeTempPemFile("fake-key", "key.pem", 0o644);

    expect(() => authEnv.sessionJwtPrivateKey).toThrow(/group- or world-readable/);
  });

  it("does not throw for a matching var name where the file is owner-only-readable", () => {
    process.env.SESSION_JWT_PRIVATE_KEY_PATH = writeTempPemFile("fake-key", "key.pem", 0o600);

    expect(() => authEnv.sessionJwtPrivateKey).not.toThrow();
  });
});
