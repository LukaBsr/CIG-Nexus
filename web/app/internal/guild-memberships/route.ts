import { type NextRequest, NextResponse } from "next/server";

import { createMembership, InvalidReferenceError } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";
import { isConstraintViolation } from "@/lib/internal/pgErrors";

export async function POST(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const body = (await request.json().catch(() => null)) as {
    guild_id?: unknown;
    user_id?: unknown;
    role?: unknown;
  } | null;
  if (!body || typeof body.guild_id !== "string" || typeof body.user_id !== "string") {
    return NextResponse.json({ error: "guild_id and user_id are required" }, { status: 400 });
  }
  if (body.role !== undefined && body.role !== "owner" && body.role !== "member") {
    return NextResponse.json({ error: "role must be owner or member" }, { status: 400 });
  }

  try {
    const membership = await createMembership(body.guild_id, body.user_id, body.role);
    return NextResponse.json(membership, { status: 201 });
  } catch (err) {
    if (err instanceof InvalidReferenceError || isConstraintViolation(err)) {
      return NextResponse.json({ error: "invalid request" }, { status: 400 });
    }
    throw err;
  }
}
