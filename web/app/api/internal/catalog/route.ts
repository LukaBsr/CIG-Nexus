import { type NextRequest, NextResponse } from "next/server";

import { getCatalog } from "@/lib/internal/catalog";
import { isAuthorizedInternalRequest } from "@/lib/internal/auth";

export async function GET(request: NextRequest): Promise<NextResponse> {
  if (!isAuthorizedInternalRequest(request)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const catalog = await getCatalog();
  return NextResponse.json(catalog);
}
