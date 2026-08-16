"use client";

import { useState } from "react";

import { useDismissablePopover } from "@/hooks/useDismissablePopover";

import { Button } from "./Button";
import { TextInputWithSubmit } from "./TextInputWithSubmit";

interface AddFriendPopoverProps {
  friendCode: string | null;
  onAddByCode: (code: string) => void;
  onRegenerateCode: () => void;
}

function PersonPlusIcon() {
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
      <circle cx="6" cy="5.5" r="2.5" />
      <path d="M1.5 13.5c0-2.5 2-4 4.5-4s4.5 1.5 4.5 4" />
      <path d="M12.5 5v4M10.5 7h4" />
    </svg>
  );
}

// docs/social/friends-dms-design.md §1.3 — add-by-code redemption plus
// this account's own shareable code, in one popover (same icon-trigger
// shape as InvitePopover). Regenerating immediately invalidates the old
// code (§1.3), so the confirm-free "Regenerate" action is deliberate, not
// an oversight — mirroring REGENERATE_FRIEND_CODE's own no-grace-period
// design.
export function AddFriendPopover({ friendCode, onAddByCode, onRegenerateCode }: AddFriendPopoverProps) {
  const [open, setOpen] = useState(false);
  const ref = useDismissablePopover<HTMLDivElement>(open, () => setOpen(false));
  const [codeInput, setCodeInput] = useState("");
  const [copied, setCopied] = useState(false);

  const handleSubmit = () => {
    if (codeInput.trim()) {
      onAddByCode(codeInput.trim());
      setCodeInput("");
    }
  };

  const handleCopy = () => {
    if (!friendCode) return;
    void navigator.clipboard.writeText(friendCode).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    });
  };

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen((o) => !o)}
        aria-label="Add friend"
        title="Add friend"
        className={`rounded-md p-1.5 transition-colors ${
          open ? "bg-surface text-teal" : "text-ivory/50 hover:text-ivory"
        }`}
      >
        <PersonPlusIcon />
      </button>

      {open && (
        // left-0, not right-0 (unlike InvitePopover/JoinRequestInbox) —
        // this trigger sits inside a narrow left sidebar near the screen
        // edge, so a right-aligned w-72 panel would overflow off-screen.
        <div className="absolute left-0 top-full z-40 mt-2 w-72 rounded-lg border border-slate/40 bg-surface p-3 shadow-xl">
          <h3 className="mb-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Add Friend by Code
          </h3>
          <TextInputWithSubmit
            value={codeInput}
            onChange={setCodeInput}
            onSubmit={handleSubmit}
            placeholder="Enter a friend code..."
            submitLabel="Add"
          />

          <div className="mt-3 border-t border-slate/20 pt-3">
            <h3 className="mb-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
              Your Code
            </h3>
            {friendCode ? (
              <div className="flex items-center justify-between gap-2 rounded-md border border-teal/30 bg-teal/10 px-3 py-2 font-mono text-xs text-teal">
                <span className="font-semibold">{friendCode}</span>
                <div className="flex shrink-0 gap-2">
                  <button onClick={handleCopy} className="font-semibold text-teal/70 hover:text-teal">
                    {copied ? "Copied" : "Copy"}
                  </button>
                </div>
              </div>
            ) : (
              <p className="font-mono text-xs text-ivory/40">Loading…</p>
            )}
            <Button variant="secondary" onClick={onRegenerateCode} className="mt-2 w-full">
              Regenerate
            </Button>
          </div>
        </div>
      )}
    </div>
  );
}
