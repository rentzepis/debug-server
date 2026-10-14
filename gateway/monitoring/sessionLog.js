const fs = require("fs");
const path = require("path");

// Written by code-server's session.ts, one line per event:
//   2026-07-27 16:18:38  FOCUS         [--------]  Window focused — …  (172.18.0.3)
// Timestamps carry no zone; student containers and the gateway both run in UTC.
const LINE_PATTERN =
  /^(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2}) {2}(.+?) {2,}\[[^\]]*\] {2}(.*?)(?: {2}\([^)]*\))?$/u;

const TYPES = {
  FOCUS: "focus",
  BLUR: "blur",
  "TAB HIDDEN": "hidden",
  "TAB VISIBLE": "visible",
  DISCONNECT: "disconnect",
  LOGIN: "login",
  LOGOUT: "logout",
};

const USERNAME_PATTERN = /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/u;

/** @returns {{at: number, type: string, label: string, detail: string}[]} in file order */
function parseSessionLog(text) {
  const events = [];
  for (const line of text.split("\n")) {
    const match = LINE_PATTERN.exec(line.trimEnd());
    if (!match) {
      continue;
    }
    const at = Date.parse(`${match[1]}T${match[2]}Z`);
    if (Number.isNaN(at)) {
      continue;
    }
    const label = match[3].trim();
    events.push({ at, type: TYPES[label] || "other", label, detail: match[4] });
  }
  return events;
}

/**
 * Activity between fromMs and toMs. The editor counts as "in use" while the
 * window is focused and the tab visible. awayCount counts each time a student
 * left the editor; awayMs is the time spent connected but not in it.
 */
function summarizeSession(events, fromMs, toMs) {
  const summary = {
    awayCount: 0,
    awayMs: 0,
    longestAwayMs: 0,
    disconnects: 0,
    lastEventAt: null,
  };
  // null = unknown (not yet loaded, or after a disconnect).
  let focused = null;
  let visible = null;
  let awaySince = null;

  const endAway = (at) => {
    if (awaySince === null) {
      return;
    }
    const span = Math.max(0, at - awaySince);
    summary.awayMs += span;
    summary.longestAwayMs = Math.max(summary.longestAwayMs, span);
    awaySince = null;
  };

  for (const event of events) {
    if (event.at < fromMs || event.at > toMs) {
      continue;
    }
    summary.lastEventAt = event.at;
    const wasActive = focused === true && visible === true;
    switch (event.type) {
      case "focus":
        focused = true;
        break;
      case "blur":
        focused = false;
        break;
      case "visible":
        visible = true;
        break;
      case "hidden":
        visible = false;
        break;
      case "disconnect":
        summary.disconnects += 1;
        endAway(event.at);
        focused = null;
        visible = null;
        continue;
      default:
        continue;
    }
    const active = focused === true && visible === true;
    const known = focused !== null || visible !== null;
    if (active) {
      endAway(event.at);
    } else if (known && awaySince === null) {
      awaySince = event.at;
      if (wasActive) {
        summary.awayCount += 1;
      }
    }
  }
  endAway(toMs);
  return summary;
}

/** Reads <dir>/<username>/session-monitoring.jsonl, re-parsing only when the file changes. */
function createSessionLogReader({ dir }) {
  const cache = new Map();

  function events(username) {
    if (!dir || typeof username !== "string" || !USERNAME_PATTERN.test(username)) {
      return [];
    }
    const file = path.join(dir, username, "session-monitoring.jsonl");
    let stat;
    try {
      stat = fs.statSync(file);
    } catch {
      cache.delete(username);
      return [];
    }
    const cached = cache.get(username);
    if (cached && cached.mtimeMs === stat.mtimeMs && cached.size === stat.size) {
      return cached.events;
    }
    let parsed = [];
    try {
      parsed = parseSessionLog(fs.readFileSync(file, "utf8"));
    } catch {
      // Unreadable now; try again on the next request.
    }
    cache.set(username, { mtimeMs: stat.mtimeMs, size: stat.size, events: parsed });
    return parsed;
  }

  return {
    available: () => Boolean(dir) && fs.existsSync(dir),
    events,
  };
}

module.exports = { parseSessionLog, summarizeSession, createSessionLogReader };
