const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test } = require("node:test");
const { createCheckinStore } = require("../checkin/store");
const { plan, createRunPolicy } = require("./reconcile");

function tempFile() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "reconcile-"));
  return path.join(dir, "state", "checkins.json");
}

function setup({ timeLimitMinutes = null } = {}) {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "lab1", title: "Lab 1", by: "prof", timeLimitMinutes });
  store.setActive("lab1");
  const policy = createRunPolicy({
    users: { alice: true, bob: true },
    staff: ["Prof"],
    store,
  });
  return { store, policy, advance: (ms) => (clock += ms) };
}

test("plan starts wanted containers and stops unwanted ones", () => {
  const containers = [
    { name: "code-alice", running: false },
    { name: "code-bob", running: true },
    { name: "code-carol", running: true },
    { name: "code-dave", running: false },
  ];
  const wanted = new Set(["alice", "carol"]);
  assert.deepEqual(plan(containers, (u) => wanted.has(u)), {
    start: ["code-alice"],
    stop: ["code-bob"],
  });
});

test("containers run only while checked in and not submitted", () => {
  const { store, policy } = setup();
  assert.equal(policy("alice"), false);
  store.checkIn("lab1", "alice", { by: "prof", method: "manual" });
  assert.equal(policy("alice"), true);
  assert.equal(policy("bob"), false);
  store.submit("lab1", "alice");
  assert.equal(policy("alice"), false);
});

test("containers stop when time runs out or the session closes", () => {
  const { store, policy, advance } = setup({ timeLimitMinutes: 1 });
  store.checkIn("lab1", "alice", { by: "prof", method: "manual" });
  store.checkIn("lab1", "bob", { by: "prof", method: "manual" });
  advance(60_000);
  assert.equal(policy("alice"), false);
  const fresh = setup();
  fresh.store.checkIn("lab1", "bob", { by: "prof", method: "manual" });
  fresh.store.setActive(null);
  assert.equal(fresh.policy("bob"), false);
});

test("staff containers always run; unenrolled check-ins do not", () => {
  const { store, policy } = setup();
  assert.equal(policy("prof"), true);
  store.checkIn("lab1", "mallory", { by: "prof", method: "manual" });
  assert.equal(policy("mallory"), false);
});
