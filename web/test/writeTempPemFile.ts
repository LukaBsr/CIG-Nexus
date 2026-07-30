import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

// SESSION_JWT_PRIVATE_KEY_PATH/AUTH_JWT_PUBLIC_KEY_PATH now point at real
// files on disk (secrets/*.pem, bind-mounted in docker-compose.yml) rather
// than holding key content directly — tests need an actual file to point
// the env var at.
//
// mode defaults to 0o600 (owner read/write only) rather than leaving it to
// writeFileSync's umask-dependent default (0o664 under this repo's umask)
// — env.ts's readRequiredFile() now refuses to read a *_PRIVATE_KEY_PATH
// file that's group- or world-readable (docs/security-audit.md §1.3), so a
// fixture simulating a correctly-permissioned real key needs to actually be
// one. Tests exercising the rejection path pass a looser mode explicitly.
export function writeTempPemFile(content: string, filename = "key.pem", mode = 0o600): string {
  const dir = mkdtempSync(path.join(tmpdir(), "cig-nexus-test-"));
  const filePath = path.join(dir, filename);
  writeFileSync(filePath, content, { encoding: "utf8", mode });
  return filePath;
}
