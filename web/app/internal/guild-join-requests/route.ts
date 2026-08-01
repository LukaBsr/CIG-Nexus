import { type NextRequest, NextResponse } from "next/server";

import { createJoinRequest, listJoinRequests } from "@/lib/internal/joinRequests";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { InvalidReferenceError } from "@/lib/internal/catalog";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

// docs/social-presence-design.md §1.9 (REQUEST_JOIN).
export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as { guild_id?: unknown; user_id?: unknown } | null;
  if (!body || typeof body.guild_id !== "string" || typeof body.user_id !== "string") {
    return NextResponse.json({ error: "guild_id and user_id are required" }, { status: 400 });
  }

  try {
    const result = await createJoinRequest(body.guild_id, body.user_id);
    return NextResponse.json({ result }, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}

// docs/social-presence-design.md §1.9 (LIST_JOIN_REQUESTS).
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const guildId = request.nextUrl.searchParams.get("guild_id");
  if (!guildId) {
    return NextResponse.json({ error: "guild_id is required" }, { status: 400 });
  }

  const requests = await listJoinRequests(guildId);
  if (!requests) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ requests });
}
