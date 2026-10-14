const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test } = require("node:test");
const { createCheckinStore } = require("./store");

function tempFile() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "checkin-store-"));
  return path.join(dir, "state", "checkins.json");
}

test("everyone is locked when no assignment is active", () => {
  const store = createCheckinStore({ file: tempFile() });
  assert.equal(store.isUnlocked("alice"), false);
  store.createAssignment({ id: "lab1", title: "Lab 1", by: "prof" });
  store.checkIn("lab1", "alice", { by: "prof", method: "manual" });
  assert.equal(store.isUnlocked("alice"), false);
  store.setActive("lab1");
  assert.equal(store.isUnlocked("alice"), true);
  store.setActive(null);
  assert.equal(store.isUnlocked("alice"), false);
});

test("only check-ins for the active assignment unlock", () => {
  const store = createCheckinStore({ file: tempFile() });
  store.createAssignment({ id: "lab1", title: "", by: "prof" });
  store.createAssignment({ id: "exam-1", title: "Exam 1", by: "prof" });
  store.checkIn("lab1", "alice", { by: "prof", method: "qr" });
  store.setActive("exam-1");
  assert.equal(store.isUnlocked("alice"), false);
  assert.equal(store.isUnlocked("bob"), false);
  store.checkIn("exam-1", "bob", { by: "ta", method: "passcode" });
  assert.equal(store.isUnlocked("bob"), true);
  assert.equal(store.snapshot().assignments.lab1.title, "lab1");
});

test("check-in is idempotent and undo / clear relock", () => {
  const store = createCheckinStore({ file: tempFile(), now: () => 0 });
  store.createAssignment({ id: "lab1", title: "Lab 1", by: "prof" });
  store.setActive("lab1");
  const first = store.checkIn("lab1", "alice", { by: "prof", method: "qr" });
  const second = store.checkIn("lab1", "alice", { by: "ta", method: "manual" });
  assert.equal(first.alreadyCheckedIn, false);
  assert.equal(second.alreadyCheckedIn, true);
  assert.deepEqual(second.record, { at: "1970-01-01T00:00:00.000Z", by: "prof", method: "qr" });
  assert.deepEqual(store.undoCheckIn("lab1", "alice"), { removed: true });
  assert.deepEqual(store.undoCheckIn("lab1", "alice"), { removed: false });
  assert.equal(store.isUnlocked("alice"), false);
  store.checkIn("lab1", "alice", { by: "prof", method: "qr" });
  store.checkIn("lab1", "bob", { by: "prof", method: "qr" });
  assert.deepEqual(store.clearAssignment("lab1").removed.sort(), ["alice", "bob"]);
  assert.equal(store.isUnlocked("bob"), false);
});

test("time limits run from each student's check-in and lock at the deadline", () => {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "exam", title: "Exam", by: "prof", timeLimitMinutes: 30 });
  store.setActive("exam");
  store.checkIn("exam", "alice", { by: "prof", method: "qr" });
  clock = 10 * 60_000;
  store.checkIn("exam", "bob", { by: "prof", method: "qr" });

  assert.deepEqual(store.getSession("bob"), {
    assignment: { id: "exam", title: "Exam" },
    checkedInAt: "1970-01-01T00:10:00.000Z",
    deadline: "1970-01-01T00:40:00.000Z",
    submittedAt: null,
    finished: null,
  });
  clock = 30 * 60_000 - 1;
  assert.equal(store.isUnlocked("alice"), true);
  clock = 30 * 60_000;
  assert.equal(store.isUnlocked("alice"), false);
  assert.equal(store.getSession("alice").finished, "expired");
  assert.equal(store.isUnlocked("bob"), true);
  assert.equal(store.getSession("carol"), null);
});

test("submitting locks the workspace once and is not reversible by the student", () => {
  const store = createCheckinStore({ file: tempFile(), now: () => 5000 });
  store.createAssignment({ id: "lab1", title: "Lab 1", by: "prof" });
  store.setActive("lab1");
  assert.throws(() => store.submit("lab1", "alice"), /Not checked in/u);
  store.checkIn("lab1", "alice", { by: "prof", method: "qr" });
  assert.equal(store.getSession("alice").deadline, null);

  const first = store.submit("lab1", "alice");
  assert.equal(first.submitted, true);
  assert.equal(first.record.submittedAt, "1970-01-01T00:00:05.000Z");
  assert.equal(store.isUnlocked("alice"), false);
  assert.equal(store.getSession("alice").finished, "submitted");
  assert.equal(store.checkIn("lab1", "alice", { by: "ta", method: "manual" }).alreadyCheckedIn, true);
  assert.equal(store.isUnlocked("alice"), false);
  assert.equal(store.submit("lab1", "alice").submitted, false);

  store.undoCheckIn("lab1", "alice");
  store.checkIn("lab1", "alice", { by: "ta", method: "manual" });
  assert.equal(store.isUnlocked("alice"), true);
});

test("submitting after the deadline does not overwrite the timeout", () => {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "exam", title: "", by: "prof", timeLimitMinutes: 1 });
  store.setActive("exam");
  store.checkIn("exam", "alice", { by: "prof", method: "qr" });
  clock = 60_000;
  assert.deepEqual(
    { ...store.submit("exam", "alice"), record: undefined },
    { submitted: false, finished: "expired", record: undefined },
  );
  assert.equal(store.getSession("alice").submittedAt, null);
});

test("invalid input is rejected explicitly", () => {
  const store = createCheckinStore({ file: tempFile() });
  for (const id of ["", "Lab1", "-lab", "lab-", "lab_1", "a:b", "__proto__", "x".repeat(65)]) {
    assert.throws(() => store.createAssignment({ id, title: "", by: "prof" }), /Assignment ID/u);
  }
  store.createAssignment({ id: "lab1", title: "", by: "prof" });
  assert.throws(() => store.createAssignment({ id: "lab1", title: "", by: "prof" }), /already exists/u);
  assert.throws(
    () => store.createAssignment({ id: "lab2", title: "x".repeat(121), by: "prof" }),
    /Title/u,
  );
  for (const timeLimitMinutes of [0, -5, 1.5, "30", 24 * 60 + 1]) {
    assert.throws(
      () => store.createAssignment({ id: "timed", title: "", by: "prof", timeLimitMinutes }),
      /Time limit/u,
    );
  }
  assert.throws(() => store.setActive("missing"), /Unknown assignment/u);
  assert.throws(() => store.setActive("toString"), /Unknown assignment/u);
  assert.throws(() => store.checkIn("missing", "alice", { by: "p", method: "qr" }), /Unknown/u);
  assert.throws(() => store.checkIn("lab1", "alice", { by: "p", method: "magic" }), /method/u);
  assert.equal(store.isUnlocked("alice"), false);
});

test("state persists across restarts and corrupt files fail loudly", () => {
  const file = tempFile();
  const store = createCheckinStore({ file });
  store.createAssignment({ id: "lab1", title: "Lab 1", by: "prof" });
  store.setActive("lab1");
  store.checkIn("lab1", "alice", { by: "prof", method: "manual" });

  const reloaded = createCheckinStore({ file });
  assert.equal(reloaded.getActiveAssignmentId(), "lab1");
  assert.equal(reloaded.isUnlocked("alice"), true);
  assert.deepEqual(fs.readdirSync(path.dirname(file)), ["checkins.json"]);

  fs.writeFileSync(file, "{not json");
  assert.throws(() => createCheckinStore({ file }), SyntaxError);
});

test("snapshots are copies", () => {
  const store = createCheckinStore({ file: tempFile() });
  store.createAssignment({ id: "lab1", title: "Lab 1", by: "prof" });
  store.setActive("lab1");
  store.snapshot().assignments.lab1.checkins.alice = { at: "", by: "", method: "qr" };
  assert.equal(store.isUnlocked("alice"), false);
});

test("extra minutes move one student's deadline and can be set before check-in", () => {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "exam", title: "Exam", by: "prof", timeLimitMinutes: 60 });
  store.setActive("exam");
  assert.deepEqual(store.setExtraMinutes("exam", "alice", 30), { previous: 0, extraMinutes: 30 });
  store.checkIn("exam", "alice", { by: "prof", method: "qr" });
  store.checkIn("exam", "bob", { by: "prof", method: "qr" });
  assert.equal(store.getSession("alice").deadline, "1970-01-01T01:30:00.000Z");

  clock = 60 * 60_000;
  assert.equal(store.isUnlocked("bob"), false);
  assert.equal(store.isUnlocked("alice"), true);
  // Extending a student whose time ran out reopens their workspace.
  store.setExtraMinutes("exam", "bob", 5);
  assert.equal(store.isUnlocked("bob"), true);
  store.setExtraMinutes("exam", "bob", 0);
  assert.equal(store.isUnlocked("bob"), false);
  assert.equal(store.snapshot().assignments.exam.extraMinutes.bob, undefined);

  for (const minutes of [-1, 1.5, "5", 24 * 60 + 1]) {
    assert.throws(() => store.setExtraMinutes("exam", "alice", minutes), /Extra time/u);
  }
});

test("extend all adds time only for students still working", () => {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "untimed", title: "", by: "prof" });
  assert.throws(() => store.extendAll("untimed", 5), /no time limit/u);
  store.createAssignment({ id: "exam", title: "", by: "prof", timeLimitMinutes: 10 });
  store.setActive("exam");
  for (const user of ["alice", "bob", "carol"]) {
    store.checkIn("exam", user, { by: "prof", method: "qr" });
  }
  store.setExtraMinutes("exam", "carol", 20);
  store.submit("exam", "alice");
  clock = 15 * 60_000;
  assert.deepEqual(store.extendAll("exam", 10), { extended: ["carol"] });
  assert.deepEqual(store.snapshot().assignments.exam.extraMinutes, { carol: 30 });
  assert.throws(() => store.extendAll("exam", 0), /Extension/u);
});

test("reopening a submission keeps its history", () => {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "lab1", title: "", by: "prof" });
  store.setActive("lab1");
  store.checkIn("lab1", "alice", { by: "prof", method: "qr" });
  assert.throws(() => store.reopen("lab1", "alice", { by: "ta" }), /Not submitted/u);
  assert.throws(() => store.reopen("lab1", "bob", { by: "ta" }), /Not checked in/u);
  clock = 1000;
  store.submit("lab1", "alice");
  clock = 2000;
  const result = store.reopen("lab1", "alice", { by: "ta" });
  assert.equal(result.finished, null);
  assert.deepEqual(result.record.reopened, [
    { submittedAt: "1970-01-01T00:00:01.000Z", at: "1970-01-01T00:00:02.000Z", by: "ta" },
  ]);
  assert.equal(store.isUnlocked("alice"), true);
  assert.equal(store.getSession("alice").checkedInAt, "1970-01-01T00:00:00.000Z");
});

test("assignments can be renamed, retimed and archived", () => {
  let clock = 0;
  const store = createCheckinStore({ file: tempFile(), now: () => clock });
  store.createAssignment({ id: "exam", title: "Exam", by: "prof", timeLimitMinutes: 10 });
  store.setActive("exam");
  store.checkIn("exam", "alice", { by: "prof", method: "qr" });
  clock = 10 * 60_000;
  assert.equal(store.isUnlocked("alice"), false);
  store.updateAssignment("exam", { title: "  Final exam ", timeLimitMinutes: 20 });
  assert.equal(store.isUnlocked("alice"), true);
  assert.equal(store.getAssignment("exam").title, "Final exam");
  store.updateAssignment("exam", { title: "", timeLimitMinutes: null });
  assert.equal(store.getAssignment("exam").title, "exam");
  assert.equal(store.getSession("alice").deadline, null);

  assert.throws(() => store.updateAssignment("exam", { archived: true }), /Close the session/u);
  assert.throws(() => store.updateAssignment("exam", { archived: "yes" }), /archived/u);
  assert.throws(() => store.updateAssignment("exam", { timeLimitMinutes: 0 }), /Time limit/u);
  store.setActive(null);
  store.updateAssignment("exam", { archived: true });
  assert.equal(store.getAssignment("exam").archived, true);
  store.setActive("exam");
  assert.equal(store.getAssignment("exam").archived, undefined);
});
