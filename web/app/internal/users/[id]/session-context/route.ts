import { type NextRequest, NextResponse } from "next/server";

import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { listBlocks } from "@/lib/internal/blocks";
import { getUserProfileSummary } from "@/lib/internal/catalog";
import { listFriends } from "@/lib/internal/friends";

// Combines three of IdentifyHandler's IDENTIFY-time hydration calls
// (fetchUserProfile, fetchBlocks, fetchFriends — GET
// /internal/users/:id/profile, /internal/blocks, /internal/friendships)
// into one round trip. Each of those three routes stays as-is and is
// still called independently elsewhere (LIST_FRIENDS, LIST_BLOCKS,
// DMHandler's live peer-block fallback) — this is additive, purely for
// the one call site that previously made all three sequentially on the
// server's single-threaded loop.
export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { id } = await params;
  const [profile, blocks, friends] = await Promise.all([
    getUserProfileSummary(id),
    listBlocks(id),
    listFriends(id)
  ]);

  if (!profile || !blocks || !friends) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }

  return NextResponse.json({ ...profile, blocks, friends });
}
