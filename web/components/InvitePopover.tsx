"use client";

import { useState } from "react";

import { useDismissablePopover } from "@/hooks/useDismissablePopover";
import type { Invite } from "@/lib/types";

import { CreateInviteForm } from "./CreateInviteForm";

interface InvitePopoverProps {
  onSubmit: (maxUses: number | null, expiresInSeconds: number | null) => void;
  lastCreatedInvite: Invite | null;
  onClearLastCreatedInvite: () => void;
}

function LinkIcon() {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
    >
      <path d="M6.5 9.5 9.5 6.5" />
      <path d="M7 4.5 8.2 3.3a2.3 2.3 0 0 1 3.3 3.3L10.3 7.8" />
      <path d="M9 11.5 7.8 12.7a2.3 2.3 0 0 1-3.3-3.3L5.7 8.2" />
    </svg>
  );
}

// docs/guilds/social-presence-design.md §1.4/§6 step 6: the invite-creation
// form used to sit permanently expanded in the guild toolbar — this pass
// turns it into an icon-triggered popover for density, same dismiss
// behavior (click outside / Escape) as the other new toolbar popovers.
export function InvitePopover({ onSubmit, lastCreatedInvite, onClearLastCreatedInvite }: InvitePopoverProps) {
  const [open, setOpen] = useState(false);
  const ref = useDismissablePopover<HTMLDivElement>(open, () => setOpen(false));

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen((o) => !o)}
        aria-label="Create invite"
        title="Create invite"
        className={`rounded-md p-1.5 transition-colors ${
          open ? "bg-surface text-teal" : "text-ivory/50 hover:text-ivory"
        }`}
      >
        <LinkIcon />
      </button>

      {open && (
        <div className="absolute right-0 top-full z-40 mt-2 w-72 rounded-lg border border-slate/40 bg-surface p-3 shadow-xl">
          <h3 className="mb-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Create Invite
          </h3>
          <CreateInviteForm onSubmit={onSubmit} />
          {lastCreatedInvite && (
            <div className="mt-2 flex items-center justify-between gap-2 rounded-md border border-teal/30 bg-teal/10 px-3 py-2 font-mono text-xs text-teal">
              <span>
                Code: <span className="font-semibold">{lastCreatedInvite.code}</span>
              </span>
              <button
                onClick={onClearLastCreatedInvite}
                className="font-semibold text-teal/70 hover:text-teal"
              >
                &times;
              </button>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
