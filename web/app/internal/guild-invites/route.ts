import { type NextRequest, NextResponse } from "next/server";

import { createInvite, listInvites } from "@/lib/internal/invites";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { InvalidReferenceError } from "@/lib/internal/catalog";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

// docs/guilds/social-presence-design.md §1.5: POST /internal/guild-invites — create.
export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as
    | { guild_id?: unknown; created_by?: unknown; max_uses?: unknown; expires_in_seconds?: unknown }
    | null;
  if (!body || typeof body.guild_id !== "string" || typeof body.created_by !== "string") {
    return NextResponse.json({ error: "guild_id and created_by are required" }, { status: 400 });
  }
  if (
    body.max_uses !== undefined &&
    body.max_uses !== null &&
    (!Number.isInteger(body.max_uses) || (body.max_uses as number) <= 0)
  ) {
    return NextResponse.json({ error: "max_uses must be a positive integer or null" }, { status: 400 });
  }
  if (
    body.expires_in_seconds !== undefined &&
    body.expires_in_seconds !== null &&
    (!Number.isInteger(body.expires_in_seconds) || (body.expires_in_seconds as number) <= 0)
  ) {
    return NextResponse.json(
      { error: "expires_in_seconds must be a positive integer or null" },
      { status: 400 }
    );
  }

  try {
    const invite = await createInvite(
      body.guild_id,
      body.created_by,
      (body.max_uses as number | null | undefined) ?? null,
      (body.expires_in_seconds as number | null | undefined) ?? null
    );
    return NextResponse.json(invite, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}

// docs/guilds/social-presence-design.md §1.5: GET /internal/guild-invites?guild_id= — list.
export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const guildId = request.nextUrl.searchParams.get("guild_id");
  if (!guildId) {
    return NextResponse.json({ error: "guild_id is required" }, { status: 400 });
  }

  const invites = await listInvites(guildId);
  if (!invites) {
    return NextResponse.json({ error: "not found" }, { status: 404 });
  }
  return NextResponse.json({ invites });
}
