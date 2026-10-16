// docs/social/friends-dms-design.md §3.7 — when the web client sends
// MARK_DM_READ. Debounced, so a burst of messages or focus changes costs
// one send (the server rate-limits MARK_DM_READ at 10 / 10 seconds as a
// backstop). At most one request in flight; a newer request waits for it.
// A failure (an ERROR whose request_type is MARK_DM_READ) is silent to the
// user: retried once, then given up until the next trigger.
//
// No ack timeout: the server answers every MARK_DM_READ synchronously with
// DM_READ or ERROR, so a missing reply only happens on a dead socket, and
// recovering from that needs a page reload, which builds a new scheduler.

export interface MarkReadScheduler {
  // Ask for peerId's conversation to be marked read up to upTo.
  request(peerId: string, upTo: number): void;
  // A DM_READ arrived (from this tab or another of the user's sessions).
  onDmRead(peerId: string, upTo: number): void;
  // An ERROR with request_type MARK_DM_READ arrived.
  onError(): void;
  dispose(): void;
}

interface Target {
  peerId: string;
  upTo: number;
}

export const MARK_READ_DEBOUNCE_MS = 1500;
export const MARK_READ_RETRY_MS = 2000;

export function createMarkReadScheduler(
  send: (peerId: string, upTo: number) => void
): MarkReadScheduler {
  // Highest marker the server has confirmed per peer — a request at or
  // below it is already done.
  const acked: Record<string, number> = {};
  // One pending slot: the user reads one conversation at a time, so a
  // request for a different peer replaces it.
  let pending: Target | null = null;
  let inFlight: (Target & { retried: boolean }) | null = null;
  let debounceTimer: ReturnType<typeof setTimeout> | null = null;
  let retryTimer: ReturnType<typeof setTimeout> | null = null;

  const isAcked = (target: Target) => target.upTo <= (acked[target.peerId] ?? 0);

  const flush = () => {
    if (inFlight || !pending) {
      return;
    }
    inFlight = { ...pending, retried: false };
    pending = null;
    send(inFlight.peerId, inFlight.upTo);
  };

  // After the in-flight request settles, send whatever is waiting — unless
  // its debounce is still running, in which case that timer will.
  const settle = () => {
    inFlight = null;
    if (pending && isAcked(pending)) {
      pending = null;
    }
    if (!debounceTimer) {
      flush();
    }
  };

  return {
    request(peerId, upTo) {
      const target = {
        peerId,
        upTo: pending?.peerId === peerId ? Math.max(pending.upTo, upTo) : upTo
      };
      if (isAcked(target)) {
        return;
      }
      pending = target;
      if (debounceTimer) {
        clearTimeout(debounceTimer);
      }
      debounceTimer = setTimeout(() => {
        debounceTimer = null;
        flush();
      }, MARK_READ_DEBOUNCE_MS);
    },

    onDmRead(peerId, upTo) {
      acked[peerId] = Math.max(acked[peerId] ?? 0, upTo);
      if (inFlight && inFlight.peerId === peerId && upTo >= inFlight.upTo) {
        if (retryTimer) {
          clearTimeout(retryTimer);
          retryTimer = null;
        }
        settle();
      }
    },

    onError() {
      if (!inFlight || retryTimer) {
        return;
      }
      if (inFlight.retried) {
        settle();
        return;
      }
      inFlight.retried = true;
      retryTimer = setTimeout(() => {
        retryTimer = null;
        if (inFlight) {
          send(inFlight.peerId, inFlight.upTo);
        }
      }, MARK_READ_RETRY_MS);
    },

    dispose() {
      if (debounceTimer) {
        clearTimeout(debounceTimer);
      }
      if (retryTimer) {
        clearTimeout(retryTimer);
      }
      debounceTimer = null;
      retryTimer = null;
      pending = null;
      inFlight = null;
    }
  };
}
