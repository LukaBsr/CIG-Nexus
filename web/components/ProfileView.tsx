"use client";

import { useEffect, useState } from "react";

import { fetchProfile, type WireProfile } from "@/lib/user/api";

import { Avatar } from "./Avatar";

interface ProfileViewProps {
  // The wire user_id to view, or null to render nothing. The caller
  // (app/page.tsx) renders this with `key={userId}` — a fresh mount per
  // userId, not just a prop change — so `profile` below always starts at
  // its true "loading" value (undefined) for a newly opened profile,
  // instead of this component needing to reset it itself, which a
  // synchronous setState at the top of an effect would otherwise require.
  userId: string | null;
  onClose: () => void;
  // docs/social/friends-dms-design.md §4.6's self-view exception — when
  // userId === myUserId, this is the caller's own profile (always
  // viewable, per that exception), and "Edit profile" replaces the plain
  // display with a path back into Settings -> Profile instead.
  myUserId: string | null;
  onEditProfile: () => void;
}

const DEFAULT_ACCENT = "#5eead4";

// docs/social/friends-dms-design.md §4.3/§4.6 — the "view profile" action
// §4.3 always implied but never had a UI for until now. Deliberately does
// not distinguish "doesn't exist," "blocked," and "no relationship" in its
// copy — fetchProfile() already collapses all three (and a rate limit, and
// no session) into the same null, and §4.6's whole point is that the
// caller shouldn't be able to tell them apart. Mirrors SettingsModal's
// overlay/Escape/click-outside-to-close shape exactly, for one consistent
// modal pattern across the app. §4.6's self-view exception means viewing
// your own id always succeeds here too (no separate "can't view yourself"
// case to render) — the only difference is the "Edit profile" button.
export function ProfileView({ userId, onClose, myUserId, onEditProfile }: ProfileViewProps) {
  // undefined = loading; null = loaded, unavailable (see the comment
  // above); WireProfile = loaded, viewable — same three-state shape
  // ProfileSettings.tsx already uses for the caller's own profile.
  const [profile, setProfile] = useState<WireProfile | null | undefined>(undefined);

  useEffect(() => {
    if (!userId) {
      return;
    }
    // Catches a rejected fetchProfile() (a network failure, or fetch()/
    // response.json() throwing) the same as fetchProfile's own existing
    // "non-2xx -> null" contract — without this, a rejection left `profile`
    // stuck at `undefined` forever (an unclosable "Loading…") with an
    // unhandled promise rejection, instead of the "unavailable" state every
    // other failure already renders.
    let cancelled = false;
    void fetchProfile(userId)
      .then((result) => {
        if (!cancelled) {
          setProfile(result);
        }
      })
      .catch(() => {
        if (!cancelled) {
          setProfile(null);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [userId]);

  useEffect(() => {
    if (!userId) {
      return;
    }
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        onClose();
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [userId, onClose]);

  if (!userId) {
    return null;
  }

  const accent = profile?.accent_color ?? DEFAULT_ACCENT;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-page/70 p-4" onClick={onClose}>
      <div
        className="w-full max-w-sm overflow-hidden rounded-lg border border-slate/40 bg-surface shadow-xl"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="h-2" style={{ backgroundColor: accent }} />

        <div className="flex items-start justify-between gap-2 p-5 pb-0">
          <div className="min-w-0" />
          <button
            onClick={onClose}
            aria-label="Close profile"
            className="shrink-0 font-mono text-lg leading-none text-muted transition-colors hover:text-fg"
          >
            &times;
          </button>
        </div>

        {profile === undefined && (
          <p className="px-5 pb-6 font-mono text-sm text-muted">Loading…</p>
        )}

        {profile === null && (
          <p className="px-5 pb-6 font-mono text-sm text-muted">Profile unavailable.</p>
        )}

        {profile && (
          <div className="flex flex-col items-center gap-3 px-5 pb-6 text-center">
            <Avatar url={profile.avatar_url} name={profile.display_name} size={64} />
            <div>
              <h2 className="font-mono text-base font-semibold text-fg">{profile.display_name}</h2>
              {profile.status_message && (
                <p className="mt-0.5 font-mono text-xs text-muted">{profile.status_message}</p>
              )}
            </div>
            {profile.bio && (
              <p className="w-full whitespace-pre-wrap break-words border-t border-slate/20 pt-3 text-left font-sans text-sm text-fg/80">
                {profile.bio}
              </p>
            )}
            {userId === myUserId && (
              <button
                onClick={onEditProfile}
                className="mt-1 rounded-md border border-brand bg-brand/10 px-4 py-1.5 font-mono text-sm text-brand transition-colors hover:bg-brand/20"
              >
                Edit profile
              </button>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
