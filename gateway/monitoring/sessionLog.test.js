const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test } = require("node:test");
const { parseSessionLog, summarizeSession, createSessionLogReader } = require("./sessionLog");

const LOG = [
  "2026-07-27 16:00:00  LOGIN         [abcdef12]  User signed in  (172.18.0.3)",
  "2026-07-27 16:00:01  TAB VISIBLE   [abcdef12]  Browser tab visible — editor is active  (172.18.0.3)",
  "2026-07-27 16:00:01  FOCUS         [abcdef12]  Window focused — actively using the editor  (172.18.0.3)",
  "2026-07-27 16:10:00  BLUR          [abcdef12]  Window unfocused — switched to another application  (172.18.0.3)",
  "2026-07-27 16:10:01  TAB HIDDEN    [abcdef12]  Browser tab hidden — switched away or minimized  (172.18.0.3)",
  "2026-07-27 16:12:00  TAB VISIBLE   [abcdef12]  Browser tab visible — window not focused  (172.18.0.3)",
  "2026-07-27 16:12:05  FOCUS         [abcdef12]  Window focused — actively using the editor  (172.18.0.3)",
  "garbage line",
  "2026-07-27 16:20:00  BLUR          [abcdef12]  Window unfocused — switched to another application  (172.18.0.3)",
  "2026-07-27 16:20:30  DISCONNECT    [abcdef12]  Session ending — page closing or navigating away  (172.18.0.3)",
  "2026-07-27 16:21:00  FOCUS         [--------]  Window focused — actively using the editor  (172.18.0.3)",
  "2026-07-27 16:21:00  TAB VISIBLE   [--------]  Browser tab visible — editor is active  (172.18.0.3)",
  "",
].join("\n");

const t = (hms) => Date.parse(`2026-07-27T${hms}Z`);

test("parses labels, details and UTC timestamps, skipping junk", () => {
  const events = parseSessionLog(LOG);
  assert.equal(events.length, 11);
  assert.deepEqual(events[4], {
    at: t("16:10:01"),
    type: "hidden",
    label: "TAB HIDDEN",
    detail: "Browser tab hidden — switched away or minimized",
  });
  assert.equal(events[0].type, "login");
});

test("summarizes away time and disconnects within the window", () => {
  const events = parseSessionLog(LOG);
  assert.deepEqual(summarizeSession(events, t("16:00:00"), t("16:30:00")), {
    awayCount: 2,
    // 16:10:00-16:12:05 and 16:20:00-16:20:30 (the disconnect ends it)
    awayMs: 125_000 + 30_000,
    longestAwayMs: 125_000,
    disconnects: 1,
    lastEventAt: t("16:21:00"),
  });
  // Window ending while the student is away counts up to the end.
  assert.deepEqual(summarizeSession(events, t("16:00:00"), t("16:11:00")), {
    awayCount: 1,
    awayMs: 60_000,
    longestAwayMs: 60_000,
    disconnects: 0,
    lastEventAt: t("16:10:01"),
  });
  assert.equal(summarizeSession(events, t("17:00:00"), t("18:00:00")).lastEventAt, null);
});

test("a page loaded in the background is away time but not a departure", () => {
  const events = parseSessionLog(
    [
      "2026-07-27 16:00:00  BLUR          [--------]  x  (1)",
      "2026-07-27 16:01:00  FOCUS         [--------]  x  (1)",
      "2026-07-27 16:01:00  TAB VISIBLE   [--------]  x  (1)",
    ].join("\n"),
  );
  const summary = summarizeSession(events, t("16:00:00"), t("16:05:00"));
  assert.equal(summary.awayCount, 0);
  assert.equal(summary.awayMs, 60_000);
});

test("reader caches by file stat and rejects unsafe usernames", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "session-log-"));
  const reader = createSessionLogReader({ dir });
  assert.equal(reader.available(), true);
  assert.deepEqual(reader.events("alice"), []);
  fs.writeFileSync(path.join(dir, "alice-session-monitoring.jsonl"), LOG);
  assert.equal(reader.events("alice").length, 11);
  assert.equal(reader.events("alice"), reader.events("alice"));
  assert.deepEqual(reader.events("../alice"), []);
  assert.equal(createSessionLogReader({ dir: "" }).available(), false);
});
