import { type NextRequest, NextResponse } from "next/server";

import { createMembership, getGuildIdsForUser, getGuildMembers, InvalidReferenceError } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    guild_id?: unknown;
    user_id?: unknown;
    role_rank?: unknown;
  } | null;
  if (!body || typeof body.guild_id !== "string" || typeof body.user_id !== "string") {
    return NextResponse.json({ error: "guild_id and user_id are required" }, { status: 400 });
  }
  if (body.role_rank !== undefined && (!Number.isInteger(body.role_rank) || (body.role_rank as number) < 0)) {
    return NextResponse.json({ error: "role_rank must be a non-negative integer" }, { status: 400 });
  }

  try {
    const membership = await createMembership(
      body.guild_id,
      body.user_id,
      body.role_rank as number | undefined
    );
    return NextResponse.json(membership, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}

// docs/guilds/social-presence-design.md §2.3 (LIST_MEMBERS): live read, never
// cached. ?guild_id=g_... — matches this document's own query-param naming
// (cf. FETCH_HISTORY's channel_id), not the guildId camelCase the path-param
// route below uses, since that one mirrors its URL segment name instead.
//
// docs/social/friends-dms-design.md §3.3 adds the ?user_id= variant — the
// same underlying table, queried the other direction (a user's guild ids,
// not a guild's members), for canSendDm()'s disconnected-peer fallback.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const guildId = request.nextUrl.searchParams.get("guild_id");
  if (guildId) {
    const members = await getGuildMembers(guildId);
    if (!members) {
      return NextResponse.json({ error: "not found" }, { status: 404 });
    }
    return NextResponse.json({ members });
  }

  const userId = request.nextUrl.searchParams.get("user_id");
  if (userId) {
    const guildIds = await getGuildIdsForUser(userId);
    if (guildIds === null) {
      return NextResponse.json({ error: "invalid user_id" }, { status: 400 });
    }
    return NextResponse.json({ guild_ids: guildIds });
  }

  return NextResponse.json({ error: "guild_id or user_id is required" }, { status: 400 });
}
