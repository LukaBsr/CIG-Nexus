// docs/settings/appearance-design.md §3.5. Session-cookie authenticated
// (same __session refresh cookie every other browser-facing auth flow
// uses) — this is a web/app/api/* route, not web/app/internal/*, since a
// browser calls it directly and the C++/WebSocket protocol has no
// involvement in appearance settings at all.
export interface AppearancePatchResult {
  theme: string | null;
  sync_enabled: boolean;
}

export async function patchAppearance(body: {
  theme?: string;
  sync_enabled?: boolean;
}): Promise<AppearancePatchResult | null> {
  const response = await fetch("/api/user/appearance", {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    credentials: "include",
    body: JSON.stringify(body)
  });

  if (!response.ok) {
    return null;
  }

  return (await response.json()) as AppearancePatchResult;
}
