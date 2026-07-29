import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

// SESSION_JWT_PRIVATE_KEY_PATH/AUTH_JWT_PUBLIC_KEY_PATH now point at real
// files on disk (secrets/*.pem, bind-mounted in docker-compose.yml) rather
// than holding key content directly — tests need an actual file to point
// the env var at.
export function writeTempPemFile(content: string, filename = "key.pem"): string {
  const dir = mkdtempSync(path.join(tmpdir(), "cig-nexus-test-"));
  const filePath = path.join(dir, filename);
  writeFileSync(filePath, content, "utf8");
  return filePath;
}
