const assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test } = require("node:test");
const { createCheckinStore } = require("../checkin/store");
const {
  snapshotFileName,
  snapshotPath,
  listSnapshots,
  dueSnapshots,
  takeSnapshot,
} = require("./snapshots");

function tempDir(prefix) {
  return fs.mkdtempSync(path.join(os.tmpdir(), prefix));
}

test("file names encode the finish time and reason", () => {
  assert.equal(snapshotFileName("2026-10-14T15:30:12.345Z", "submitted"), "20261014T153012Z-submitted.tar.gz");
  const dir = "/snaps";
  assert.equal(
    snapshotPath(dir, "lab1", "alice", "20261014T153012Z-expired.tar.gz"),
    "/snaps/lab1/alice/20261014T153012Z-expired.tar.gz",
  );
  for (const [assignment, user, file] of [
    ["lab1", "alice", "../x.tar.gz"],
    ["lab1", "alice", "20261014T153012Z-submitted.tar.gz.json"],
    ["lab1", "..", "20261014T153012Z-submitted.tar.gz"],
    ["../lab1", "alice", "20261014T153012Z-submitted.tar.gz"],
  ]) {
    assert.equal(snapshotPath(dir, assignment, user, file), null);
  }
});

test("recent finishes are due once, and old ones are skipped", async () => {
  let clock = Date.parse("2026-10-14T15:00:00Z");
  const store = createCheckinStore({ file: path.join(tempDir("snap-state-"), "c.json"), now: () => clock });
  store.createAssignment({ id: "exam", title: "", by: "prof", timeLimitMinutes: 10 });
  store.setActive("exam");
  for (const user of ["alice", "bob", "carol"]) {
    store.checkIn("exam", user, { by: "prof", method: "qr" });
  }
  clock += 60_000;
  store.submit("exam", "alice");
  const dir = tempDir("snaps-");
  const homes = tempDir("homes-");
  fs.mkdirSync(path.join(homes, "alice", ".local"), { recursive: true });
  fs.writeFileSync(path.join(homes, "alice", "proxy.c"), "int main(void) { return 0; }\n");
  fs.writeFileSync(path.join(homes, "alice", ".bash_history"), "secret\n");
  fs.writeFileSync(path.join(homes, "alice", ".local", "state"), "x");
  fs.symlinkSync("/etc/passwd", path.join(homes, "alice", "link"));

  const due = dueSnapshots({ state: store.snapshot(), dir, nowMs: clock, maxLagMs: 30 * 60_000 });
  assert.deepEqual(
    due.map((job) => [job.username, job.reason, path.basename(job.file)]),
    [["alice", "submitted", "20261014T150100Z-submitted.tar.gz"]],
  );
  const meta = await takeSnapshot({ homesDir: homes, job: due[0], nowMs: clock + 1000 });
  assert.equal(meta.takenAt, "2026-10-14T15:01:01.000Z");
  const listing = execFileSync("tar", ["-tzvf", due[0].file], { encoding: "utf8" });
  assert.match(listing, /proxy\.c/u);
  assert.match(listing, /link -> \/etc\/passwd/u);
  assert.doesNotMatch(listing, /bash_history|\.local/u);
  assert.deepEqual(dueSnapshots({ state: store.snapshot(), dir, nowMs: clock, maxLagMs: 30 * 60_000 }), []);

  // bob and carol time out; only carol is still within the lag window.
  store.setExtraMinutes("exam", "carol", 60);
  clock = Date.parse("2026-10-14T16:20:00Z");
  assert.deepEqual(
    dueSnapshots({ state: store.snapshot(), dir, nowMs: clock, maxLagMs: 30 * 60_000 }).map((j) => [
      j.username,
      j.reason,
      j.finishedAt,
    ]),
    [["carol", "expired", "2026-10-14T16:10:00.000Z"]],
  );

  assert.deepEqual(listSnapshots(dir, "exam"), {
    alice: [
      {
        file: "20261014T150100Z-submitted.tar.gz",
        reason: "submitted",
        finishedAt: "2026-10-14T15:01:00.000Z",
        takenAt: "2026-10-14T15:01:01.000Z",
        bytes: meta.bytes,
      },
    ],
  });
  assert.deepEqual(listSnapshots(dir, "missing"), {});
});
