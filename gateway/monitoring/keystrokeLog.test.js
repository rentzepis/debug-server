const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test } = require("node:test");
const {
  parseKeystrokeSession,
  decodeKeys,
  replayLine,
  keystrokeLines,
  createKeystrokeLogReader,
} = require("./keystrokeLog");

// As written by util-linux 2.38 `script -q -f -m advanced -I x.in -T x.timing`.
const TIMING = [
  "H 0.000000 START_TIME 2026-10-14 11:29:06+00:00",
  "H 0.000000 TTY /dev/pts/0",
  "H 0.000000 SHELL /bin/bash",
  "I 1.008822 10",
  "I 0.615188 11",
  "I 0.618912 7",
  "I 0.637888 1",
  "H 0.000000 DURATION 4.151365",
  "H 0.000000 EXIT_CODE 130",
  "",
].join("\n");
const INPUT = Buffer.from(
  'Script started on 2026-10-14 11:29:06+00:00 [TTY="/dev/pts/0" COLUMNS="-1" LINES="-1"]\n' +
    "echo live\r" +
    "ls\x1b[D\x1b[Dx\x7f\r" +
    "gdb -q\r" +
    "\x03" +
    '\nScript done on 2026-10-14 11:29:10+00:00 [COMMAND_EXIT_CODE="130"]\n',
  "latin1",
);

const start = Date.parse("2026-10-14T11:29:06Z");

test("pairs timing entries with typed bytes and absolute times", () => {
  const session = parseKeystrokeSession(TIMING, INPUT);
  assert.equal(session.startedAt, start);
  assert.equal(session.endedAt, start + 4151);
  assert.deepEqual(
    session.chunks.map((c) => [c.at - start, c.bytes.toString("latin1")]),
    [
      [1009, "echo live\r"],
      [1624, "ls\x1b[D\x1b[Dx\x7f\r"],
      [2243, "gdb -q\r"],
      [2881, "\x03"],
    ],
  );
});

test("a live terminal has no end and stops at bytes not yet flushed", () => {
  const timing = TIMING.split("\n").slice(0, 6).join("\n");
  const session = parseKeystrokeSession(timing, INPUT.subarray(0, INPUT.indexOf("ls") + 4));
  assert.equal(session.endedAt, null);
  assert.equal(session.chunks.length, 1);
});

test("without a start time nothing is placed on the timeline", () => {
  assert.deepEqual(parseKeystrokeSession("I 0.5 3\n", Buffer.from("abc")), { startedAt: null, endedAt: null, chunks: [] });
});

test("names special keys and drops terminal replies", () => {
  assert.deepEqual(decodeKeys(Buffer.from("ab\x1b[A\x1bOB\x1b[1;5C\x1b[3~\x7f\t\x03\x1bb")), [
    "ab",
    { key: "↑" },
    { key: "↓" },
    { key: "Ctrl+→" },
    { key: "Delete" },
    { key: "Backspace" },
    { key: "Tab" },
    { key: "Ctrl+C" },
    { key: "Alt+b" },
  ]);
  assert.deepEqual(decodeKeys(Buffer.from("x\x1b[12;1R\x1b[?1;2c\x1b]11;rgb:0000/0000/0000\x1b\\\x1b[Iy")), ["xy"]);
  assert.deepEqual(decodeKeys(Buffer.from("\x1b[200~int\x1b[201~")), [{ key: "Paste start" }, "int", { key: "Paste end" }]);
  assert.deepEqual(decodeKeys(Buffer.from("\x1b")), [{ key: "Esc" }]);
  assert.deepEqual(decodeKeys(Buffer.from("é", "utf8")), ["é"]);
});

test("replays line editing and flags completion and history", () => {
  const replay = (s) => replayLine(decodeKeys(Buffer.from(s)));
  assert.deepEqual(replay("ls\x1b[D\x1b[Dx\x7f"), { text: "ls", exact: true });
  assert.deepEqual(replay("mkae\x7f\x7f\x7fake"), { text: "make", exact: true });
  assert.deepEqual(replay("gcc a.c\x01sudo \x05 -g"), { text: "sudo gcc a.c -g", exact: true });
  assert.deepEqual(replay("rm -rf build\x17proxy\x15ls"), { text: "ls", exact: true });
  assert.deepEqual(replay("./pro\t"), { text: "./pro", exact: false });
  assert.deepEqual(replay("\x1b[A"), { text: "", exact: false });
});

test("splits lines on Enter, Ctrl+C and Ctrl+D", () => {
  const lines = keystrokeLines(parseKeystrokeSession(TIMING, INPUT).chunks);
  assert.deepEqual(
    lines.map((l) => [l.at - start, l.text, l.end]),
    [
      [1009, "echo live", "Enter"],
      [1624, "ls", "Enter"],
      [2243, "gdb -q", "Enter"],
      [2881, "", "Ctrl+C"],
    ],
  );
  assert.deepEqual(lines[1].keys, ["ls", { key: "←" }, { key: "←" }, "x", { key: "Backspace" }]);
  const open = keystrokeLines([{ at: 5, bytes: Buffer.from("make") }, { at: 6, bytes: Buffer.from(" test") }]);
  assert.deepEqual(open, [{ at: 5, keys: ["make test"], text: "make test", exact: true, end: null }]);
});

test("reader lists a user's terminals oldest first and ignores stray files", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "keylog-"));
  const userDir = path.join(dir, "jsmith");
  fs.mkdirSync(userDir);
  fs.writeFileSync(path.join(userDir, "20261014T112906Z-7.timing"), TIMING);
  fs.writeFileSync(path.join(userDir, "20261014T112906Z-7.in"), INPUT);
  fs.writeFileSync(path.join(userDir, "20261014T100000Z-9.timing"), "H 0.000000 START_TIME 2026-10-14 10:00:00+00:00\nI 0.5 3\n");
  fs.writeFileSync(path.join(userDir, "20261014T100000Z-9.in"), "Script started on x\nls\r");
  fs.writeFileSync(path.join(userDir, "20261014T110000Z-8.timing"), TIMING); // no .in
  fs.writeFileSync(path.join(userDir, "notes.timing"), TIMING);

  const reader = createKeystrokeLogReader({ dir });
  assert.equal(reader.available(), true);
  const sessions = reader.sessions("jsmith");
  assert.deepEqual(
    sessions.map((s) => [s.id, s.lines.map((l) => l.text)]),
    [
      ["20261014T100000Z-9", ["ls"]],
      ["20261014T112906Z-7", ["echo live", "ls", "gdb -q", ""]],
    ],
  );
  assert.equal(reader.sessions("jsmith")[1].lines, sessions[1].lines); // cached
  fs.appendFileSync(path.join(userDir, "20261014T100000Z-9.in"), "pwd\r");
  fs.appendFileSync(path.join(userDir, "20261014T100000Z-9.timing"), "I 1.0 4\n");
  assert.deepEqual(
    reader.sessions("jsmith")[0].lines.map((l) => l.text),
    ["ls", "pwd"],
  );
  assert.deepEqual(reader.sessions("../etc"), []);
  assert.deepEqual(reader.sessions("nobody"), []);
  assert.equal(createKeystrokeLogReader({ dir: "" }).available(), false);
  fs.rmSync(dir, { recursive: true, force: true });
});
