import type { ChannelMessage, ChatMessage, DmMessage } from "@/lib/types";

import { Avatar } from "./Avatar";

interface MessageListProps {
  messages: ChatMessage[] | ChannelMessage[] | DmMessage[];
  emptyText: string;
  // Optional so call sites that don't care about "view profile" (none
  // today, but nothing forces every consumer of this shared component to)
  // aren't required to thread it through.
  onViewProfile?: (userId: string) => void;
}

// Replaces the duplicated chatMessages/channelMessages rendering blocks
// (docs/frontend-rebuild-plan.md, extraction target 1) — both message
// shapes render as an identical header row (username + formatted
// timestamp) plus a content div, so one component covers both.
export function MessageList({ messages, emptyText, onViewProfile }: MessageListProps) {
  if (messages.length === 0) {
    return <p className="px-3 py-6 text-center font-mono text-sm text-ivory/40">{emptyText}</p>;
  }

  return (
    <ul className="flex list-none flex-col gap-0.5 p-0">
      {messages.map((message) => (
        <MessageListItem key={message.messageId} message={message} onViewProfile={onViewProfile} />
      ))}
    </ul>
  );
}

interface MessageListItemProps {
  message: ChatMessage | ChannelMessage | DmMessage;
  onViewProfile?: (userId: string) => void;
}

// The border-l accent (revealed on hover) is the subject-specific detail
// here — a nod to each row being one arrived frame, without permanently
// colorizing every row and turning a long history into visual noise.
function MessageListItem({ message, onViewProfile }: MessageListItemProps) {
  // docs/social/friends-dms-design.md §4.6's self-view exception — clicking
  // your own message's avatar opens your own ProfileView (with its "Edit
  // profile" button) exactly like anyone else's, no exclusion here.
  return (
    <li className="flex gap-3 border-l-2 border-transparent px-3 py-1.5 transition-colors hover:border-teal/40 hover:bg-surface/60">
      {onViewProfile ? (
        <button
          onClick={() => onViewProfile(message.userId)}
          aria-label={`View ${message.displayName ?? message.username}'s profile`}
          className="mt-0.5 shrink-0 rounded-full"
        >
          <Avatar url={message.avatarUrl} name={message.displayName ?? message.username} size={28} />
        </button>
      ) : (
        <Avatar url={message.avatarUrl} name={message.displayName ?? message.username} size={28} className="mt-0.5" />
      )}
      <div className="min-w-0 flex-1">
        <div className="flex items-baseline gap-2 font-mono text-xs text-ivory/50">
          <span className="font-semibold text-violet">{message.displayName ?? message.username}</span>
          <span>{new Date(message.timestamp * 1000).toLocaleTimeString()}</span>
        </div>
        <div className="mt-0.5 break-words font-sans text-sm text-ivory">{message.content}</div>
      </div>
    </li>
  );
}
