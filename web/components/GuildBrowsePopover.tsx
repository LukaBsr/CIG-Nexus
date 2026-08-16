"use client";

import { useState } from "react";

import { useDismissablePopover } from "@/hooks/useDismissablePopover";
import type { Guild } from "@/lib/types";

import { GuildIcon } from "./GuildIcon";
import { TextInputWithSubmit } from "./TextInputWithSubmit";

interface GuildBrowsePopoverProps {
  guilds: Guild[];
  myGuildIds: Set<string>;
  myPendingJoinRequestGuildIds: Set<string>;
  onJoinGuild: (guildId: string) => void;
  onRequestJoin: (guildId: string) => void;
  onCreateGuild: (name: string) => void;
}

function PlusIcon() {
  return (
    <svg
      width="16"
      height="16"
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinecap="round"
    >
      <path d="M8 2v12M2 8h12" />
    </svg>
  );
}

// docs/frontend-rebuild-plan.md's Deferred section: browsing/joining/
// creating guilds moved here from the old always-visible guild-list
// sidebar once guild *selection* moved to GuildRail's per-guild icons —
// this popover is what's left for discovery, the same content, just
// relocated behind a trigger (mirrors Discord's own "add a server" +
// button in the same rail position).
export function GuildBrowsePopover({
  guilds,
  myGuildIds,
  myPendingJoinRequestGuildIds,
  onJoinGuild,
  onRequestJoin,
  onCreateGuild
}: GuildBrowsePopoverProps) {
  const [open, setOpen] = useState(false);
  const ref = useDismissablePopover<HTMLDivElement>(open, () => setOpen(false));
  const [guildNameInput, setGuildNameInput] = useState("");

  const handleCreate = () => {
    if (guildNameInput.trim()) {
      onCreateGuild(guildNameInput.trim());
      setGuildNameInput("");
    }
  };

  const discoverable = guilds.filter((g) => !myGuildIds.has(g.guildId));

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen((o) => !o)}
        aria-label="Browse or create a guild"
        title="Browse or create a guild"
        className={`flex h-11 w-11 items-center justify-center rounded-full transition-colors ${
          open ? "bg-teal text-ink" : "bg-surface text-teal hover:rounded-xl hover:bg-teal/20"
        }`}
      >
        <PlusIcon />
      </button>

      {open && (
        // left-full (not right/left-0) so it opens into the empty space
        // to the right of the narrow rail, and bottom-0 (not top-0) since
        // this trigger sits near the bottom of the screen — anchoring to
        // the top would push the panel below the viewport, the same
        // "don't overflow off the edge of a narrow, screen-edge-anchored
        // container" issue FriendsView's popovers needed fixing for
        // (web/components/FriendsView.tsx), just the vertical axis here
        // instead of the horizontal one.
        <div className="absolute bottom-0 left-full z-40 ml-2 w-72 rounded-lg border border-slate/40 bg-surface p-3 shadow-xl">
          <h3 className="mb-2 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            All Guilds
          </h3>
          {discoverable.length === 0 ? (
            <p className="font-mono text-xs text-ivory/40">No other guilds to discover yet.</p>
          ) : (
            <ul className="flex list-none flex-col gap-1 p-0">
              {discoverable.map((g) => {
                const isPending = myPendingJoinRequestGuildIds.has(g.guildId);
                return (
                  <li
                    key={g.guildId}
                    className="flex items-center justify-between gap-2 rounded-md bg-ink/40 px-2 py-1.5"
                  >
                    <span className="flex min-w-0 flex-1 items-center gap-2">
                      <GuildIcon guildId={g.guildId} name={g.name} size={20} className="rounded-full" />
                      <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory">{g.name}</span>
                    </span>
                    {isPending ? (
                      <span className="shrink-0 rounded-full border border-slate/40 px-2.5 py-1 font-mono text-xs text-ivory/40">
                        Requested
                      </span>
                    ) : (
                      <button
                        onClick={() =>
                          g.visibility === "application" ? onRequestJoin(g.guildId) : onJoinGuild(g.guildId)
                        }
                        className="shrink-0 rounded-full bg-teal/15 px-2.5 py-1 font-mono text-xs font-semibold text-teal transition-colors hover:bg-teal/25"
                      >
                        {g.visibility === "application" ? "Request" : "Join"}
                      </button>
                    )}
                  </li>
                );
              })}
            </ul>
          )}

          <h3 className="mt-3 mb-2 border-t border-slate/20 pt-3 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Create a Guild
          </h3>
          <TextInputWithSubmit
            value={guildNameInput}
            onChange={setGuildNameInput}
            onSubmit={handleCreate}
            placeholder="New guild name..."
            submitLabel="Create"
          />
        </div>
      )}
    </div>
  );
}
