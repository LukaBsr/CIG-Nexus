import { describe, expect, it } from "vitest";

import {
  applyDmRead,
  applyUnreadCounts,
  classifyDmMessage,
  formatBadge,
  incrementUnread,
  noteSeen,
  totalUnread,
  unreadLabel
} from "./unreadCounts";

const ME = "u_me";
const PEER = "u_peer";

describe("classifyDmMessage", () => {
  it("counts a new message from a peer the user isn't reading", () => {
    expect(classifyDmMessage({ senderId: PEER, messageId: 5 }, ME, {}, false)).toBe("increment");
  });

  it("never counts the user's own message", () => {
    expect(classifyDmMessage({ senderId: ME, messageId: 5 }, ME, {}, false)).toBe("ignore");
  });

  it("ignores a message_id not above the highest already seen from that peer", () => {
    expect(classifyDmMessage({ senderId: PEER, messageId: 5 }, ME, { [PEER]: 5 }, false)).toBe("ignore");
    expect(classifyDmMessage({ senderId: PEER, messageId: 4 }, ME, { [PEER]: 5 }, false)).toBe("ignore");
    expect(classifyDmMessage({ senderId: PEER, messageId: 6 }, ME, { [PEER]: 5 }, false)).toBe("increment");
  });

  it("marks read instead of counting when the user is reading that conversation", () => {
    expect(classifyDmMessage({ senderId: PEER, messageId: 5 }, ME, {}, true)).toBe("mark-read");
  });
});

describe("live message flow", () => {
  it("increments once per message, and a redelivered message_id doesn't count twice", () => {
    let counts = {};
    let seen = {};
    for (const messageId of [10, 11, 11]) {
      const action = classifyDmMessage({ senderId: PEER, messageId }, ME, seen, false);
      if (action !== "ignore") seen = noteSeen(seen, PEER, messageId);
      if (action === "increment") counts = incrementUnread(counts, PEER);
    }
    expect(counts).toEqual({ [PEER]: 2 });
  });
});

describe("noteSeen", () => {
  it("only moves forward", () => {
    expect(noteSeen({ [PEER]: 7 }, PEER, 3)).toEqual({ [PEER]: 7 });
    expect(noteSeen({ [PEER]: 7 }, PEER, 9)).toEqual({ [PEER]: 9 });
  });
});

describe("applyDmRead", () => {
  it("sets the peer's count to the server's unread value", () => {
    expect(applyDmRead({ [PEER]: 8, u_other: 2 }, PEER, 3)).toEqual({ [PEER]: 3, u_other: 2 });
  });

  it("clears the peer's badge when unread is 0", () => {
    expect(applyDmRead({ [PEER]: 8, u_other: 2 }, PEER, 0)).toEqual({ u_other: 2 });
  });
});

describe("applyUnreadCounts", () => {
  it("replaces the whole map rather than merging into it", () => {
    const next = applyUnreadCounts({ [PEER]: 5, u_new: 1 }, { u_new: 4 });
    expect(next).toEqual({ u_new: 4 });
    expect(next).not.toHaveProperty(PEER);
  });
});

describe("formatBadge", () => {
  it.each([
    [0, ""],
    [1, "1"],
    [99, "99"],
    [100, "99+"],
    [250, "99+"]
  ])("formats %i as %j", (count, expected) => {
    expect(formatBadge(count)).toBe(expected);
  });
});

describe("unreadLabel", () => {
  it("reads naturally for screen readers", () => {
    expect(unreadLabel(1)).toBe("1 unread message");
    expect(unreadLabel(3)).toBe("3 unread messages");
    expect(unreadLabel(100)).toBe("More than 99 unread messages");
  });
});

describe("totalUnread", () => {
  it("sums every peer's count", () => {
    expect(totalUnread({})).toBe(0);
    expect(totalUnread({ a: 3, b: 100, c: 1 })).toBe(104);
  });

  it("goes through formatBadge for the aggregate", () => {
    expect(formatBadge(totalUnread({ a: 60, b: 50 }))).toBe("99+");
  });
});
