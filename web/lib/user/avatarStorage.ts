import { randomBytes } from "node:crypto";
import { mkdir, unlink, writeFile } from "node:fs/promises";
import path from "node:path";

// docs/social/friends-dms-design.md §4.2, revised after live verification:
// NOT under public/ (see the correction note in §4.2 — Next.js's
// production server indexes public/ once at process startup, so a file
// written there after boot 404s forever regardless of being physically on
// disk; confirmed directly against the running container, not assumed). A
// plain data directory instead, still local disk, still bind-mounted the
// same way docker-compose.yml's secrets/*.pem mounts are, just served by
// the dynamic route below (web/app/uploads/avatars/[filename]/route.ts)
// rather than relying on static-file serving.
const AVATAR_DIR = path.join(process.cwd(), "data", "avatars");
export const AVATAR_URL_PREFIX = "/uploads/avatars";

export function avatarDirectory(): string {
  return AVATAR_DIR;
}

// §5 checklist: size cap enforced server-side, not just a client hint.
export const MAX_AVATAR_BYTES = 2 * 1024 * 1024;

type ImageType = "png" | "jpeg" | "webp";

const EXTENSION_BY_TYPE: Record<ImageType, string> = {
  png: "png",
  jpeg: "jpg",
  webp: "webp"
};

// §5 checklist: validated by sniffing actual file bytes, not by trusting
// the client-declared Content-Type or the uploaded filename's extension —
// both are trivially spoofable. Signatures per each format's own spec,
// not a third-party library, since three fixed byte-signature checks
// don't need one.
function sniffImageType(bytes: Buffer): ImageType | null {
  if (
    bytes.length >= 8 &&
    bytes[0] === 0x89 &&
    bytes[1] === 0x50 &&
    bytes[2] === 0x4e &&
    bytes[3] === 0x47 &&
    bytes[4] === 0x0d &&
    bytes[5] === 0x0a &&
    bytes[6] === 0x1a &&
    bytes[7] === 0x0a
  ) {
    return "png";
  }
  if (bytes.length >= 3 && bytes[0] === 0xff && bytes[1] === 0xd8 && bytes[2] === 0xff) {
    return "jpeg";
  }
  if (
    bytes.length >= 12 &&
    bytes.toString("ascii", 0, 4) === "RIFF" &&
    bytes.toString("ascii", 8, 12) === "WEBP"
  ) {
    return "webp";
  }
  return null;
}

export class InvalidAvatarError extends Error {}

// §5 checklist: the stored filename is a fresh random token plus an
// extension derived from the *validated* sniffed type — never the
// client-supplied filename or any part of it, so there is no code path
// where client input becomes part of a filesystem path (path traversal is
// structurally impossible here, not just filtered).
export async function saveAvatar(bytes: Buffer): Promise<string> {
  if (bytes.length === 0) {
    throw new InvalidAvatarError("empty file");
  }
  if (bytes.length > MAX_AVATAR_BYTES) {
    throw new InvalidAvatarError(`file exceeds ${MAX_AVATAR_BYTES} bytes`);
  }

  const type = sniffImageType(bytes);
  if (!type) {
    throw new InvalidAvatarError("file is not a recognized PNG, JPEG, or WEBP image");
  }

  await mkdir(AVATAR_DIR, { recursive: true });

  const filename = `${randomBytes(16).toString("hex")}.${EXTENSION_BY_TYPE[type]}`;
  await writeFile(path.join(AVATAR_DIR, filename), bytes);

  return `${AVATAR_URL_PREFIX}/${filename}`;
}

// Best-effort — a failed delete (already gone, permission hiccup) doesn't
// block the caller from proceeding; it just means an orphaned file sits
// on disk rather than the request failing to clear/replace the avatar
// value in Postgres, which is the state that actually matters.
export async function deleteAvatar(avatarPath: string): Promise<void> {
  if (!avatarPath.startsWith(`${AVATAR_URL_PREFIX}/`)) {
    return;
  }
  const filename = path.basename(avatarPath);
  try {
    await unlink(path.join(AVATAR_DIR, filename));
  } catch {
    // ENOENT or similar — nothing left to clean up.
  }
}
