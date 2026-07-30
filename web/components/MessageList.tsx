import type { ChannelMessage, ChatMessage } from "@/lib/types";

interface MessageListProps {
  messages: ChatMessage[] | ChannelMessage[];
  emptyText: string;
}

// Replaces the duplicated chatMessages/channelMessages rendering blocks
// (docs/frontend-rebuild-plan.md, extraction target 1) — both message
// shapes render as an identical header row (username + formatted
// timestamp) plus a content div, so one component covers both.
export function MessageList({ messages, emptyText }: MessageListProps) {
  if (messages.length === 0) {
    return <p className="px-3 py-6 text-center font-mono text-sm text-ivory/40">{emptyText}</p>;
  }

  return (
    <ul className="flex list-none flex-col gap-0.5 p-0">
      {messages.map((message) => (
        <MessageListItem key={message.messageId} message={message} />
      ))}
    </ul>
  );
}

interface MessageListItemProps {
  message: ChatMessage | ChannelMessage;
}

// The border-l accent (revealed on hover) is the subject-specific detail
// here — a nod to each row being one arrived frame, without permanently
// colorizing every row and turning a long history into visual noise.
function MessageListItem({ message }: MessageListItemProps) {
  return (
    <li className="flex gap-3 border-l-2 border-transparent px-3 py-1.5 transition-colors hover:border-teal/40 hover:bg-surface/60">
      <div className="min-w-0 flex-1">
        <div className="flex items-baseline gap-2 font-mono text-xs text-ivory/50">
          <span className="font-semibold text-violet">{message.username}</span>
          <span>{new Date(message.timestamp * 1000).toLocaleTimeString()}</span>
        </div>
        <div className="mt-0.5 break-words font-sans text-sm text-ivory">{message.content}</div>
      </div>
    </li>
  );
}
