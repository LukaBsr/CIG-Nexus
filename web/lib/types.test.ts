import { describe, expect, it } from "vitest";

import { memberGuildIdsFromList, type WireGuildListEntry } from "./types";

function entry(guildId: string, isMember: boolean): WireGuildListEntry {
  return { guild_id: guildId, name: guildId, owner_id: "u_1", visibility: "open", is_member: isMember };
}

describe("memberGuildIdsFromList", () => {
  it("returns exactly the guilds flagged is_member", () => {
    const ids = memberGuildIdsFromList([entry("g_1", true), entry("g_2", false), entry("g_3", true)]);

    expect([...ids].sort()).toEqual(["g_1", "g_3"]);
  });

  it("returns an empty set when the caller belongs to none", () => {
    expect(memberGuildIdsFromList([entry("g_1", false)]).size).toBe(0);
    expect(memberGuildIdsFromList([]).size).toBe(0);
  });
});
