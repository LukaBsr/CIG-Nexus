#!/usr/bin/env node
//
// Diffs high/critical npm-audit advisories against the previous run and
// surfaces only the delta (new + resolved) via a single persistent GitHub
// issue. Deliberately not a build-failure gate and not a raw-count
// comparison: npm audit resolves against the live GitHub Advisory
// Database, so totals drift run-to-run even with an unchanged lockfile
// (see docs/security-audit.md §2.5) — the only thing worth surfacing is
// which specific advisory IDs appeared or disappeared.
//
// Baseline storage: a single GitHub issue's body, not a committed file or
// Actions cache. A committed baseline would mean this job auto-commits to
// the repo on every run; Actions cache entries get evicted after ~7 days
// of inactivity, which is exactly this job's weekly cadence, so cache
// isn't reliable here either. The issue body is both the persistent
// baseline and where a human would look for the notification anyway.

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";

const ISSUE_TITLE = "Dependency advisory watch (auto-tracked)";
const MARKER_START = "<!-- advisory-ids:start -->";
const MARKER_END = "<!-- advisory-ids:end -->";

const WORKSPACES = [
  { name: "web", auditFile: "web-audit.json" },
  { name: "gateway", auditFile: "gateway-audit.json" },
];

const TRACKED_SEVERITIES = new Set(["high", "critical"]);

function extractAdvisories(workspaceName, auditFile) {
  const data = JSON.parse(readFileSync(auditFile, "utf8"));
  const found = new Map(); // id -> { id, severity, title, url, packages: Set }

  for (const [pkgName, vuln] of Object.entries(data.vulnerabilities ?? {})) {
    for (const via of vuln.via) {
      // String entries just point at another dependency name ("Depends on
      // vulnerable versions of X") — the actual advisory is an object.
      if (typeof via !== "object") continue;
      if (!TRACKED_SEVERITIES.has(via.severity)) continue;

      const id = via.url.split("/").pop();
      if (!found.has(id)) {
        found.set(id, {
          id,
          severity: via.severity,
          title: via.title,
          url: via.url,
          packages: new Set(),
        });
      }
      found.get(id).packages.add(`${pkgName} (${workspaceName})`);
    }
  }
  return found;
}

function mergeAdvisoryMaps(maps) {
  const merged = new Map();
  for (const map of maps) {
    for (const [id, entry] of map) {
      if (!merged.has(id)) {
        merged.set(id, { ...entry, packages: new Set(entry.packages) });
      } else {
        for (const p of entry.packages) merged.get(id).packages.add(p);
      }
    }
  }
  return merged;
}

function gh(args, input) {
  return execFileSync("gh", args, {
    input,
    encoding: "utf8",
    env: process.env,
  });
}

function findTrackingIssue() {
  const out = gh([
    "issue", "list",
    "--state", "open",
    "--search", `"${ISSUE_TITLE}" in:title`,
    "--json", "number,title,body",
    "--limit", "5",
  ]);
  const issues = JSON.parse(out);
  return issues.find((i) => i.title === ISSUE_TITLE) ?? null;
}

// Previous state is stored as an array of {id, severity, title, url}
// objects (not bare IDs) so a "resolved" advisory can still be reported
// with its title/severity even though it no longer appears in the
// current audit output.
function parsePreviousAdvisories(body) {
  const previous = new Map();
  if (!body) return previous;

  const start = body.indexOf(MARKER_START);
  const end = body.indexOf(MARKER_END);
  if (start === -1 || end === -1) return previous;

  const block = body.slice(start + MARKER_START.length, end);
  const jsonMatch = block.match(/```json\s*([\s\S]*?)\s*```/);
  if (!jsonMatch) return previous;

  try {
    for (const entry of JSON.parse(jsonMatch[1])) {
      previous.set(entry.id, entry);
    }
  } catch {
    // Malformed marker block (e.g. someone hand-edited the issue) —
    // treat as no known baseline rather than crashing the job.
  }
  return previous;
}

function renderBody(currentMap) {
  const ids = [...currentMap.keys()].sort();
  const rows = ids
    .map((id) => {
      const e = currentMap.get(id);
      return `| ${[...e.packages].sort().join(", ")} | ${e.severity} | [${id}](${e.url}) | ${e.title} |`;
    })
    .join("\n");

  const snapshot = ids.map((id) => {
    const e = currentMap.get(id);
    return { id: e.id, severity: e.severity, title: e.title, url: e.url };
  });

  return [
    `## Currently tracked high/critical advisories`,
    ``,
    `Last checked: ${new Date().toISOString().slice(0, 10)}`,
    ``,
    "Tracks `npm audit` high/critical advisories across `web/` and",
    "`gateway/`. Moderate/low findings and raw count fluctuation are",
    "intentionally not tracked here — see `docs/security-audit.md` §2.5",
    "for why `npm audit`'s totals drift on their own.",
    ``,
    MARKER_START,
    "```json",
    JSON.stringify(snapshot, null, 2),
    "```",
    MARKER_END,
    ``,
    `| Package (workspace) | Severity | Advisory | Title |`,
    `|---|---|---|---|`,
    rows || `| _none_ | | | |`,
  ].join("\n");
}

function renderCommentBody(newIds, resolvedIds, current, previous) {
  const lines = [`Advisory check for ${new Date().toISOString().slice(0, 10)}:`, ``];

  if (newIds.length > 0) {
    lines.push(`**New:**`);
    for (const id of newIds.sort()) {
      const e = current.get(id);
      lines.push(
        `- [${id}](${e.url}) (${e.severity}) — ${e.title} — ${[...e.packages].sort().join(", ")}`
      );
    }
    lines.push(``);
  }

  if (resolvedIds.length > 0) {
    lines.push(`**No longer present (resolved/patched):**`);
    for (const id of resolvedIds.sort()) {
      const e = previous.get(id);
      lines.push(`- ${id} (${e.severity}) — ${e.title}`);
    }
  }

  return lines.join("\n");
}

function main() {
  const current = mergeAdvisoryMaps(
    WORKSPACES.map((w) => extractAdvisories(w.name, w.auditFile))
  );

  const issue = findTrackingIssue();
  const previous = issue ? parsePreviousAdvisories(issue.body) : new Map();

  const newIds = [...current.keys()].filter((id) => !previous.has(id));
  const resolvedIds = [...previous.keys()].filter((id) => !current.has(id));

  if (newIds.length === 0 && resolvedIds.length === 0) {
    console.log("No change in tracked high/critical advisories. Nothing to do.");
    return;
  }

  const body = renderBody(current);

  let issueNumber;
  if (issue) {
    gh(["issue", "edit", String(issue.number), "--body-file", "-"], body);
    issueNumber = issue.number;
  } else {
    const out = gh(
      ["issue", "create", "--title", ISSUE_TITLE, "--body-file", "-"],
      body
    );
    issueNumber = out.trim().split("/").pop();
  }

  gh(
    ["issue", "comment", String(issueNumber), "--body-file", "-"],
    renderCommentBody(newIds, resolvedIds, current, previous)
  );

  console.log(`New: ${newIds.length}, resolved: ${resolvedIds.length}. Updated issue #${issueNumber}.`);
}

main();
