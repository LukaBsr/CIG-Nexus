"use client";

import { useState } from "react";

import { useDismissablePopover } from "@/hooks/useDismissablePopover";
import type { FriendRequest } from "@/lib/types";

import { Avatar } from "./Avatar";

interface FriendRequestsPopoverProps {
  incoming: FriendRequest[];
  outgoing: FriendRequest[];
  onAccept: (userId: string) => void;
  onReject: (userId: string) => void;
  onCancel: (userId: string) => void;
}

function InboxIcon() {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M2 9.5 4 3.5h8l2 6" />
      <path d="M2 9.5v3a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1v-3h-3.2a2 2 0 0 1-3.6 0H2Z" />
    </svg>
  );
}

// docs/social/friends-dms-design.md §1.5 — same shape as JoinRequestInbox
// (icon-trigger popover, badge count, dismiss-on-outside-click), extended
// with an "outgoing" section since, unlike guild join requests, a friend
// request has a cancelable sender-side view too.
export function FriendRequestsPopover({ incoming, outgoing, onAccept, onReject, onCancel }: FriendRequestsPopoverProps) {
  const [open, setOpen] = useState(false);
  const ref = useDismissablePopover<HTMLDivElement>(open, () => setOpen(false));

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen((o) => !o)}
        aria-label="Friend requests"
        title="Friend requests"
        className={`relative rounded-md p-1.5 transition-colors ${
          open ? "bg-surface text-teal" : "text-ivory/50 hover:text-ivory"
        }`}
      >
        <InboxIcon />
        {incoming.length > 0 && (
          <span className="absolute -right-1 -top-1 flex h-3.5 min-w-3.5 items-center justify-center rounded-full bg-red-400 px-1 font-mono text-[9px] font-bold text-ink">
            {incoming.length}
          </span>
        )}
      </button>

      {open && (
        // left-0, not right-0 — see AddFriendPopover's comment; same
        // narrow-sidebar overflow concern applies here.
        <div className="absolute left-0 top-full z-40 mt-2 w-72 rounded-lg border border-slate/40 bg-surface p-3 shadow-xl">
          <h3 className="mb-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">Incoming</h3>
          {incoming.length === 0 ? (
            <p className="font-mono text-xs text-ivory/40">No incoming requests.</p>
          ) : (
            <ul className="flex list-none flex-col gap-1 p-0">
              {incoming.map((r) => (
                <li key={r.userId} className="flex items-center justify-between gap-2 rounded-md bg-ink/40 px-2 py-1.5">
                  <span className="flex min-w-0 flex-1 items-center gap-2">
                    <Avatar url={r.avatarUrl} name={r.displayName ?? r.username} size={20} />
                    <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory">
                      {r.displayName ?? r.username}
                    </span>
                  </span>
                  <div className="flex shrink-0 gap-1">
                    <button
                      onClick={() => onAccept(r.userId)}
                      className="rounded-md bg-teal/15 px-2 py-1 font-mono text-xs font-semibold text-teal transition-colors hover:bg-teal/25"
                    >
                      Accept
                    </button>
                    <button
                      onClick={() => onReject(r.userId)}
                      className="rounded-md bg-red-400/15 px-2 py-1 font-mono text-xs font-semibold text-red-300 transition-colors hover:bg-red-400/25"
                    >
                      Reject
                    </button>
                  </div>
                </li>
              ))}
            </ul>
          )}

          <h3 className="mt-3 mb-2 border-t border-slate/20 pt-3 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Outgoing
          </h3>
          {outgoing.length === 0 ? (
            <p className="font-mono text-xs text-ivory/40">No outgoing requests.</p>
          ) : (
            <ul className="flex list-none flex-col gap-1 p-0">
              {outgoing.map((r) => (
                <li key={r.userId} className="flex items-center justify-between gap-2 rounded-md bg-ink/40 px-2 py-1.5">
                  <span className="flex min-w-0 flex-1 items-center gap-2">
                    <Avatar url={r.avatarUrl} name={r.displayName ?? r.username} size={20} />
                    <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory">
                      {r.displayName ?? r.username}
                    </span>
                  </span>
                  <button
                    onClick={() => onCancel(r.userId)}
                    className="shrink-0 rounded-md border border-slate/40 px-2 py-1 font-mono text-xs text-ivory/60 transition-colors hover:border-red-400 hover:text-red-400"
                  >
                    Cancel
                  </button>
                </li>
              ))}
            </ul>
          )}
        </div>
      )}
    </div>
  );
}
