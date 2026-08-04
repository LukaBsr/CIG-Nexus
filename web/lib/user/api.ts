// docs/social/friends-dms-design.md §4.4 — mirrors web/lib/appearance/api.ts's
// shape exactly: browser-facing, __session-cookie authenticated fetch
// wrappers around web/app/api/user/profile/*, returning null on any
// non-2xx response rather than throwing.
export interface WireProfile {
  user_id?: string;
  display_name: string;
  bio: string | null;
  status_message: string | null;
  accent_color: string | null;
  avatar_url: string | null;
}

export async function fetchProfile(userId: string): Promise<WireProfile | null> {
  const response = await fetch(`/api/users/${userId}/profile`, { credentials: "include" });
  if (!response.ok) {
    return null;
  }
  return (await response.json()) as WireProfile;
}

export async function patchProfile(body: {
  display_name?: string | null;
  bio?: string | null;
  status_message?: string | null;
  accent_color?: string | null;
}): Promise<WireProfile | null> {
  const response = await fetch("/api/user/profile", {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    credentials: "include",
    body: JSON.stringify(body)
  });
  if (!response.ok) {
    return null;
  }
  return (await response.json()) as WireProfile;
}

export type AvatarUploadResult = { ok: true; avatar_url: string | null } | { ok: false; error: string };

export async function uploadAvatar(file: File): Promise<AvatarUploadResult> {
  const formData = new FormData();
  formData.set("avatar", file);
  const response = await fetch("/api/user/profile/avatar", {
    method: "POST",
    credentials: "include",
    body: formData
  });
  const body = (await response.json().catch(() => null)) as { avatar_url?: string | null; error?: string } | null;
  if (!response.ok) {
    return { ok: false, error: body?.error ?? "Upload failed — try again." };
  }
  return { ok: true, avatar_url: body?.avatar_url ?? null };
}

export async function deleteAvatarRequest(): Promise<{ avatar_url: string | null } | null> {
  const response = await fetch("/api/user/profile/avatar", { method: "DELETE", credentials: "include" });
  if (!response.ok) {
    return null;
  }
  return (await response.json()) as { avatar_url: string | null };
}
