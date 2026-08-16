import type { DmConversation, DmMessage, Friend, FriendRequest } from "@/lib/types";

import { AddFriendPopover } from "./AddFriendPopover";
import { Avatar } from "./Avatar";
import { FriendRequestsPopover } from "./FriendRequestsPopover";
import { MessageList } from "./MessageList";
import { PresenceDot } from "./PresenceDot";
import { TextInputWithSubmit } from "./TextInputWithSubmit";

interface FriendsViewProps {
  friends: Friend[];
  incomingFriendRequests: FriendRequest[];
  outgoingFriendRequests: FriendRequest[];
  friendCode: string | null;
  onlineUserIds: Set<string>;
  onAddByCode: (code: string) => void;
  onRegenerateCode: () => void;
  onAcceptRequest: (userId: string) => void;
  onRejectRequest: (userId: string) => void;
  onCancelRequest: (userId: string) => void;
  onBlock: (userId: string) => void;
  dmConversations: DmConversation[];
  activeDmPeerId: string | null;
  dmMessages: DmMessage[];
  onOpenDm: (peerId: string) => void;
  onSendDm: (content: string) => void;
  dmInput: string;
  onDmInputChange: (value: string) => void;
}

// docs/social/friends-dms-design.md §1/§3 — the friends UI panel (list +
// requests + add-by-code, task #99), blocking (#100), and DMs (#101):
// conversation list + thread view. Mirrors the Guilds tab's left-sidebar +
// right-pane layout throughout.
export function FriendsView({
  friends,
  incomingFriendRequests,
  outgoingFriendRequests,
  friendCode,
  onlineUserIds,
  onAddByCode,
  onRegenerateCode,
  onAcceptRequest,
  onRejectRequest,
  onCancelRequest,
  onBlock,
  dmConversations,
  activeDmPeerId,
  dmMessages,
  onOpenDm,
  onSendDm,
  dmInput,
  onDmInputChange
}: FriendsViewProps) {
  const sortedFriends = [...friends].sort((a, b) => {
    const aOnline = onlineUserIds.has(a.userId);
    const bOnline = onlineUserIds.has(b.userId);
    if (aOnline !== bOnline) return aOnline ? -1 : 1;
    return a.username.localeCompare(b.username);
  });

  const sortedConversations = [...dmConversations].sort((a, b) => {
    if (!a.lastMessageAt) return 1;
    if (!b.lastMessageAt) return -1;
    return b.lastMessageAt.localeCompare(a.lastMessageAt);
  });

  const activePeer =
    dmConversations.find((c) => c.peerId === activeDmPeerId) ??
    friends.find((f) => f.userId === activeDmPeerId) ??
    null;

  const handleSend = () => {
    if (dmInput.trim()) {
      onSendDm(dmInput);
      onDmInputChange("");
    }
  };

  return (
    <div className="flex flex-1 overflow-hidden">
      <aside className="flex w-64 shrink-0 flex-col border-r border-slate/20">
        {/* Deliberately NOT inside the scrollable region below: a container
            with overflow-y-auto and no explicit overflow-x also clips
            overflow-x (CSS's "one non-visible axis forces the other to
            auto" rule), which was cutting the two popovers' panels off —
            they need to render outside any overflow-y-auto ancestor. */}
        <div className="flex shrink-0 items-center justify-between p-3 pb-2">
          <h2 className="font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Friends — {friends.length}
          </h2>
          <div className="flex shrink-0 items-center gap-1">
            <FriendRequestsPopover
              incoming={incomingFriendRequests}
              outgoing={outgoingFriendRequests}
              onAccept={onAcceptRequest}
              onReject={onRejectRequest}
              onCancel={onCancelRequest}
            />
            <AddFriendPopover friendCode={friendCode} onAddByCode={onAddByCode} onRegenerateCode={onRegenerateCode} />
          </div>
        </div>

        <div className="flex-1 overflow-y-auto p-3 pt-0">
          {sortedConversations.length > 0 && (
            <>
              <h3 className="mb-1 px-1 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
                Direct Messages
              </h3>
              <ul className="mb-3 flex list-none flex-col gap-0.5 p-0">
                {sortedConversations.map((c) => (
                  <li key={c.peerId}>
                    <button
                      onClick={() => onOpenDm(c.peerId)}
                      className={`flex w-full items-center gap-2 rounded-md px-1.5 py-1.5 text-left transition-colors ${
                        c.peerId === activeDmPeerId ? "bg-surface" : "hover:bg-surface/60"
                      }`}
                    >
                      <PresenceDot online={onlineUserIds.has(c.peerId)} />
                      <Avatar url={c.avatarUrl} name={c.displayName ?? c.username} size={24} />
                      <span
                        className={`min-w-0 flex-1 truncate font-mono text-sm ${
                          c.peerId === activeDmPeerId ? "text-teal" : "text-ivory/90"
                        }`}
                      >
                        {c.displayName ?? c.username}
                      </span>
                    </button>
                  </li>
                ))}
              </ul>
            </>
          )}

          <h3 className="mb-1 px-1 font-mono text-xs font-semibold tracking-wider text-ivory/40 uppercase">
            Friends
          </h3>
          {sortedFriends.length === 0 ? (
            <p className="px-1 font-mono text-xs text-ivory/40">
              No friends yet — add one by code above.
            </p>
          ) : (
            <ul className="flex list-none flex-col gap-0.5 p-0">
              {sortedFriends.map((f) => (
                <li
                  key={f.userId}
                  className="flex items-center gap-2 rounded-md px-1.5 py-1.5 transition-colors hover:bg-surface/60"
                >
                  <button onClick={() => onOpenDm(f.userId)} className="flex min-w-0 flex-1 items-center gap-2 text-left">
                    <PresenceDot online={onlineUserIds.has(f.userId)} />
                    <Avatar url={f.avatarUrl} name={f.displayName ?? f.username} size={24} />
                    <span className="min-w-0 flex-1 truncate font-mono text-sm text-ivory/90">
                      {f.displayName ?? f.username}
                    </span>
                  </button>
                  <button
                    onClick={() => onBlock(f.userId)}
                    className="shrink-0 font-mono text-xs text-ivory/25 transition-colors hover:text-red-400"
                  >
                    Block
                  </button>
                </li>
              ))}
            </ul>
          )}
        </div>
      </aside>

      {!activeDmPeerId ? (
        <section className="flex flex-1 items-center justify-center">
          <p className="font-mono text-sm text-ivory/40">Select a friend to message.</p>
        </section>
      ) : (
        <section className="flex flex-1 flex-col overflow-hidden">
          <div className="flex shrink-0 items-center gap-2 border-b border-slate/20 px-4 py-2">
            <Avatar url={activePeer?.avatarUrl} name={activePeer?.displayName ?? activePeer?.username ?? "?"} size={20} />
            <h2 className="min-w-0 truncate font-mono text-sm font-semibold text-ivory">
              {activePeer?.displayName ?? activePeer?.username ?? activeDmPeerId}
            </h2>
          </div>

          <div className="flex-1 overflow-y-auto px-3 py-2">
            <div className="mx-auto max-w-3xl">
              <MessageList messages={dmMessages} emptyText="No messages yet — say hello." />
            </div>
          </div>

          <div className="shrink-0 border-t border-slate/20 p-3">
            <div className="mx-auto max-w-3xl">
              <TextInputWithSubmit
                value={dmInput}
                onChange={onDmInputChange}
                onSubmit={handleSend}
                placeholder="Message..."
                submitLabel="Send"
              />
            </div>
          </div>
        </section>
      )}
    </div>
  );
}
