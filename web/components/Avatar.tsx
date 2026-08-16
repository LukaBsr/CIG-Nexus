interface AvatarProps {
  url: string | null | undefined;
  name: string;
  size?: number;
  className?: string;
}

// docs/social/friends-dms-design.md §4.5 — the initials-fallback pattern
// already used inline in ProfileSettings.tsx, extracted so every other
// roster/message/list surface (MemberList, MessageList, JoinRequestInbox,
// friends/DM UI) renders avatars identically.
export function Avatar({ url, name, size = 24, className = "" }: AvatarProps) {
  return (
    <div
      style={{ width: size, height: size }}
      className={`flex shrink-0 items-center justify-center overflow-hidden rounded-full border border-slate/40 bg-surface ${className}`}
    >
      {url ? (
        // eslint-disable-next-line @next/next/no-img-element -- user-uploaded/Discord-CDN URLs, not build-time-known assets
        <img src={url} alt="" className="h-full w-full object-cover" />
      ) : (
        <span className="font-mono text-ivory/40" style={{ fontSize: size * 0.45 }}>
          {name.charAt(0).toUpperCase() || "?"}
        </span>
      )}
    </div>
  );
}
