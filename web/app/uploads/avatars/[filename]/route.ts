import { readFile } from "node:fs/promises";
import path from "node:path";
import { type NextRequest, NextResponse } from "next/server";

import { avatarDirectory } from "@/lib/user/avatarStorage";

// docs/social/friends-dms-design.md §4.2's revised serving mechanism: a
// dynamic route reading straight from disk on every request, not Next.js's
// built-in public/ static handler — that handler indexes public/ once at
// process startup and never discovers a file written afterward, confirmed
// directly against the running container (a runtime-written file 404'd
// even though it existed on disk and even a pre-existing unrelated
// public/ asset served fine). A dynamic route has no such index; it's
// correct by construction for content written after boot, which is every
// avatar upload.
const FILENAME_PATTERN = /^[0-9a-f]{32}\.(png|jpg|webp)$/;

const CONTENT_TYPE_BY_EXTENSION: Record<string, string> = {
  png: "image/png",
  jpg: "image/jpeg",
  webp: "image/webp"
};

export async function GET(_request: NextRequest, { params }: { params: Promise<{ filename: string }> }): Promise<NextResponse> {
  const { filename } = await params;

  // Belt-and-suspenders beyond saveAvatar's own guarantee (§5 checklist):
  // this route takes a URL path segment directly, so it validates the
  // exact shape saveAvatar produces before ever touching the filesystem,
  // rather than trusting that every caller of avatarDirectory() upholds
  // that guarantee forever.
  if (!FILENAME_PATTERN.test(filename)) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }

  const extension = filename.split(".").pop() as string;

  let bytes: Buffer;
  try {
    bytes = await readFile(path.join(avatarDirectory(), filename));
  } catch {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }

  return new NextResponse(new Uint8Array(bytes), {
    headers: {
      "Content-Type": CONTENT_TYPE_BY_EXTENSION[extension],
      // Filenames are random and never reused (a replaced avatar gets a
      // fresh filename, §4.4's POST handler), so this content, once
      // served under this exact path, never changes — safe to cache
      // aggressively.
      "Cache-Control": "public, max-age=31536000, immutable"
    }
  });
}
