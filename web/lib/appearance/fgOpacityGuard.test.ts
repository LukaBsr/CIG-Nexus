import { readdirSync, readFileSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

// docs/design/brand-guidelines.md §2.6 / §7.1: fg's opacity tiers read fine
// on Abyss and Ember, but fail the 3:1 icon minimum on five of the six light
// catalog themes (§5.4, PR L2). PR #67 migrated informational text off these
// tiers to the solid `muted` token and proposed this guard to stop a new one
// appearing — this is that guard, first written here. New dark-theme-only
// icon uses should be `text-muted` (or plain `text-fg`), not a new tier.

const WEB_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const SCAN_DIRS = ["app", "components", "lib", "hooks"];
const SOURCE_EXTENSIONS = [".ts", ".tsx"];

// A flagged tier, bare or arbitrary (text-fg/[.5]), behind any number of
// variant prefixes (hover:, md:, group-hover:, ...). The trailing (?!\w)
// stops /400 from matching /40 as a prefix.
const FG_TIER_CLASS = /(?:[\w-]+:)*text-fg\/(?:40|45|50|55|60|65|70|\[[^\]]*\])(?!\w)/g;

interface Hit {
  file: string;
  line: number;
  token: string;
}

function sourceFiles(dir: string): string[] {
  let entries: string[];
  try {
    entries = readdirSync(dir);
  } catch {
    return []; // a scanned dir need not exist (e.g. no hooks/ yet)
  }
  return entries.flatMap((entry) => {
    if (entry === "node_modules" || entry === ".next") return [];
    const full = path.join(dir, entry);
    if (statSync(full).isDirectory()) return sourceFiles(full);
    const isSource = SOURCE_EXTENSIONS.some((ext) => entry.endsWith(ext));
    return isSource && !entry.includes(".test.") ? [full] : [];
  });
}

function findHits(): Hit[] {
  return SCAN_DIRS.flatMap((dir) =>
    sourceFiles(path.join(WEB_ROOT, dir)).flatMap((file) => {
      const source = readFileSync(file, "utf8");
      const relative = path.relative(WEB_ROOT, file);
      return [...source.matchAll(FG_TIER_CLASS)].map((match) => ({
        file: relative,
        line: source.slice(0, match.index).split("\n").length,
        token: match[0],
      }));
    })
  );
}

// Keyed by file, then by the exact class token, to the number of uses
// allowed. Starts empty: every use this guard could see at the time it was
// added was migrated to text-muted in the same PR. A new entry here is a
// deliberate, reviewed exception, not a default.
const ALLOWLIST: Record<string, Record<string, number>> = {};

describe("no dark-theme-only text-fg opacity tier outside the allowlist", () => {
  it("every text-fg/<tier> use matches its allowlisted count exactly", () => {
    const byFileAndToken = new Map<string, Hit[]>();
    for (const hit of findHits()) {
      const key = `${hit.file}\u0000${hit.token}`;
      byFileAndToken.set(key, [...(byFileAndToken.get(key) ?? []), hit]);
    }

    const failures: string[] = [];
    for (const [key, hits] of byFileAndToken) {
      const [file, token] = key.split("\u0000");
      const allowed = ALLOWLIST[file]?.[token] ?? 0;
      if (hits.length !== allowed) {
        for (const hit of hits) {
          failures.push(
            `${hit.file}:${hit.line}: ${hit.token} — use text-muted (or text-fg); opacity tiers on fg are dark-theme-only`
          );
        }
      }
    }

    expect(failures, failures.join("\n")).toEqual([]);
  });
});
