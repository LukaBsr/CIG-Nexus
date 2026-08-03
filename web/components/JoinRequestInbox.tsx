"use client";

import { useState } from "react";

import { useDismissablePopover } from "@/hooks/useDismissablePopover";
import type { JoinRequest } from "@/lib/types";

interface JoinRequestInboxProps {
  requests: JoinRequest[];
  onApprove: (userId: string) => void;
  onReject: (userId: string) => void;
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

// docs/guilds/social-presence-design.md §1.9: the officer-facing side of the
// application-visibility join flow — LIST_JOIN_REQUESTS/APPROVE_JOIN_REQUEST/
// REJECT_JOIN_REQUEST existed server-side with no client UI until this pass.
export function JoinRequestInbox({ requests, onApprove, onReject }: JoinRequestInboxProps) {
  const [open, setOpen] = useState(false);
  const ref = useDismissablePopover<HTMLDivElement>(open, () => setOpen(false));

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen((o) => !o)}
        aria-label="Join requests"
        title="Join requests"
        className={`relative rounded-md p-1.5 transition-colors ${
          open ? "bg-surface text-teal" : "text-ivory/50 hover:text-ivory"
        }`}
      >
        <InboxIcon />
        {requests.length > 0 && (
          <span className="absolute -right-1 -top-1 flex h-3.5 min-w-3.5 items-center justify-center rounded-full bg-red-400 px-1 font-mono text-[9px] font-bold text-ink">
            {requests.length}
          </span>
        )}
      </button>

      {open && (
        <div className="absolute right-0 top-full z-40 mt-2 w-72 rounded-lg border border-slate/40 bg-surface p-3 shadow-xl">
          <h3 className="mb-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Join Requests
          </h3>
          {requests.length === 0 ? (
            <p className="font-mono text-xs text-ivory/40">No pending requests.</p>
          ) : (
            <ul className="flex list-none flex-col gap-1 p-0">
              {requests.map((r) => (
                <li
                  key={r.userId}
                  className="flex items-center justify-between gap-2 rounded-md bg-ink/40 px-2 py-1.5"
                >
                  <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory">
                    {r.username}
                  </span>
                  <div className="flex shrink-0 gap-1">
                    <button
                      onClick={() => onApprove(r.userId)}
                      className="rounded-md bg-teal/15 px-2 py-1 font-mono text-xs font-semibold text-teal transition-colors hover:bg-teal/25"
                    >
                      Approve
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
        </div>
      )}
    </div>
  );
}
