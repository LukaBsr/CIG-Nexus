"use client";

import { useEffect, useRef, useState } from "react";

import type { SettingsSectionProps } from "@/lib/settings/sections";
import {
  ACCENT_COLOR_PATTERN,
  BIO_MAX_LENGTH,
  DISPLAY_NAME_MAX_LENGTH,
  STATUS_MESSAGE_MAX_LENGTH
} from "@/lib/user/profile";
import { deleteAvatarRequest, fetchProfile, patchProfile, uploadAvatar, type WireProfile } from "@/lib/user/api";

const DEFAULT_ACCENT = "#5eead4";

// docs/social/friends-dms-design.md §4.3 — the second SETTINGS_SECTIONS
// entry. Text fields (display name/bio/status/accent color) save together
// via one "Save" button rather than per-keystroke PATCHes; avatar
// upload/remove act immediately on selection, since a file picker isn't
// the same kind of "still typing" input a text field is.
export function ProfileSettings({ userId }: SettingsSectionProps) {
  // undefined = not yet loaded (or no userId yet); null = loaded, no
  // profile data. Loading state is derived from this rather than tracked
  // as a separate boolean, so the effect never needs to setState in the
  // no-userId branch — it just doesn't run at all.
  const [profile, setProfile] = useState<WireProfile | null | undefined>(undefined);
  const [displayName, setDisplayName] = useState("");
  const [bio, setBio] = useState("");
  const [statusMessage, setStatusMessage] = useState("");
  const [accentColor, setAccentColor] = useState(DEFAULT_ACCENT);
  // The <input type="color"> element always needs *some* valid hex value
  // to render, so accentColor's initial state is never actually "unset"
  // the way displayName/bio/statusMessage's empty strings are. Without
  // this flag, every Save would send DEFAULT_ACCENT even for a user who
  // never touched the picker, permanently overwriting §4.1's "NULL means
  // never customized" with a value nobody chose.
  const [accentTouched, setAccentTouched] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [avatarError, setAvatarError] = useState<string | null>(null);
  const [avatarPending, setAvatarPending] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!userId) {
      return;
    }
    let cancelled = false;
    void fetchProfile(userId).then((result) => {
      if (cancelled) {
        return;
      }
      setProfile(result);
      setDisplayName(result?.display_name ?? "");
      setBio(result?.bio ?? "");
      setStatusMessage(result?.status_message ?? "");
      setAccentColor(result?.accent_color ?? DEFAULT_ACCENT);
      setAccentTouched(false);
    });
    return () => {
      cancelled = true;
    };
  }, [userId]);

  const handleSave = async () => {
    setSaving(true);
    setSaveError(null);

    const result = await patchProfile({
      display_name: displayName.trim().length > 0 ? displayName.trim() : null,
      bio: bio.length > 0 ? bio : null,
      status_message: statusMessage.length > 0 ? statusMessage : null,
      // Omitted entirely (not sent as null) unless the user actually
      // opened the picker — see accentTouched's comment above.
      ...(accentTouched ? { accent_color: ACCENT_COLOR_PATTERN.test(accentColor) ? accentColor : null } : {})
    });

    setSaving(false);

    if (!result) {
      setSaveError("Couldn't save profile — try again.");
      return;
    }

    setProfile(result);
    setAccentTouched(false);
  };

  const handleAvatarSelect = async (file: File | undefined) => {
    if (!file) {
      return;
    }
    setAvatarPending(true);
    setAvatarError(null);

    const result = await uploadAvatar(file);

    setAvatarPending(false);
    if (fileInputRef.current) {
      fileInputRef.current.value = "";
    }

    if (!result.ok) {
      setAvatarError(result.error);
      return;
    }
    setProfile((prev) => (prev ? { ...prev, avatar_url: result.avatar_url } : prev));
  };

  const handleRemoveAvatar = async () => {
    setAvatarPending(true);
    setAvatarError(null);

    const result = await deleteAvatarRequest();

    setAvatarPending(false);

    if (!result) {
      setAvatarError("Couldn't remove avatar — try again.");
      return;
    }
    setProfile((prev) => (prev ? { ...prev, avatar_url: result.avatar_url } : prev));
  };

  if (!userId) {
    return <p className="font-mono text-sm text-ivory/40">Connecting…</p>;
  }

  if (profile === undefined) {
    return <p className="font-mono text-sm text-ivory/40">Loading…</p>;
  }

  return (
    <div className="flex flex-col gap-5">
      <div className="flex items-center gap-4">
        <div className="flex h-16 w-16 shrink-0 items-center justify-center overflow-hidden rounded-full border border-slate/40 bg-surface">
          {profile?.avatar_url ? (
            // eslint-disable-next-line @next/next/no-img-element -- user-uploaded/Discord-CDN URLs, not build-time-known assets
            <img src={profile.avatar_url} alt="" className="h-full w-full object-cover" />
          ) : (
            <span className="font-mono text-lg text-ivory/40">{displayName.charAt(0).toUpperCase() || "?"}</span>
          )}
        </div>
        <div className="flex flex-col gap-1.5">
          <div className="flex gap-2">
            <button
              onClick={() => fileInputRef.current?.click()}
              disabled={avatarPending}
              className="rounded-md border border-slate/40 px-3 py-1 font-mono text-xs text-ivory transition-colors hover:border-teal"
            >
              Upload avatar
            </button>
            {profile?.avatar_url && (
              <button
                onClick={() => void handleRemoveAvatar()}
                disabled={avatarPending}
                className="rounded-md border border-slate/40 px-3 py-1 font-mono text-xs text-ivory/60 transition-colors hover:border-red-400 hover:text-red-400"
              >
                Remove
              </button>
            )}
          </div>
          <input
            ref={fileInputRef}
            type="file"
            accept="image/png,image/jpeg,image/webp"
            className="hidden"
            onChange={(e) => void handleAvatarSelect(e.target.files?.[0])}
          />
          {avatarError && <p className="font-mono text-xs text-red-400">{avatarError}</p>}
        </div>
      </div>

      <label className="flex flex-col gap-1">
        <span className="font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
          Display name
        </span>
        <input
          type="text"
          value={displayName}
          maxLength={DISPLAY_NAME_MAX_LENGTH}
          onChange={(e) => setDisplayName(e.target.value)}
          placeholder="Falls back to your Discord name"
          className="rounded-md border border-slate/40 bg-ink px-3 py-1.5 font-mono text-sm text-ivory outline-none focus:border-teal"
        />
      </label>

      <label className="flex flex-col gap-1">
        <span className="font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
          Status message
        </span>
        <input
          type="text"
          value={statusMessage}
          maxLength={STATUS_MESSAGE_MAX_LENGTH}
          onChange={(e) => setStatusMessage(e.target.value)}
          className="rounded-md border border-slate/40 bg-ink px-3 py-1.5 font-mono text-sm text-ivory outline-none focus:border-teal"
        />
      </label>

      <label className="flex flex-col gap-1">
        <span className="font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">Bio</span>
        <textarea
          value={bio}
          maxLength={BIO_MAX_LENGTH}
          onChange={(e) => setBio(e.target.value)}
          rows={3}
          className="resize-none rounded-md border border-slate/40 bg-ink px-3 py-1.5 font-mono text-sm text-ivory outline-none focus:border-teal"
        />
      </label>

      <label className="flex items-center gap-3">
        <span className="font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
          Accent color
        </span>
        <input
          type="color"
          value={ACCENT_COLOR_PATTERN.test(accentColor) ? accentColor : DEFAULT_ACCENT}
          onChange={(e) => {
            setAccentColor(e.target.value);
            setAccentTouched(true);
          }}
          className="h-7 w-10 cursor-pointer rounded border border-slate/40 bg-ink"
        />
      </label>

      <div className="flex items-center gap-3 border-t border-slate/20 pt-4">
        <button
          onClick={() => void handleSave()}
          disabled={saving}
          className="rounded-md border border-teal bg-teal/10 px-4 py-1.5 font-mono text-sm text-teal transition-colors hover:bg-teal/20"
        >
          {saving ? "Saving…" : "Save"}
        </button>
        {saveError && <p className="font-mono text-xs text-red-400">{saveError}</p>}
      </div>
    </div>
  );
}
