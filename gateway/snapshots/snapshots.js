const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");
const { finishInfo } = require("../checkin/store");
const { ASSIGNMENT_ID_PATTERN } = require("../checkin/constants");

// Layout: <dir>/<assignment>/<username>/<finish stamp>-<reason>.tar.gz plus a
// .json sidecar. The stamp is the finish time, so a reopened student who
// finishes again gets a second snapshot instead of overwriting the first.
const USERNAME_PATTERN = /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/u;
const SNAPSHOT_FILE_PATTERN = /^(\d{8}T\d{6}Z)-(submitted|expired)\.tar\.gz$/u;

function isSafeUsername(username) {
  return typeof username === "string" && USERNAME_PATTERN.test(username);
}

function snapshotFileName(finishedAtIso, reason) {
  const stamp = new Date(finishedAtIso).toISOString().replace(/[-:]/gu, "").replace(/\.\d{3}/u, "");
  return `${stamp}-${reason}.tar.gz`;
}

function studentDir(dir, assignmentId, username) {
  if (!ASSIGNMENT_ID_PATTERN.test(assignmentId) || !isSafeUsername(username)) {
    throw new Error("Invalid snapshot path");
  }
  return path.join(dir, assignmentId, username);
}

/** Absolute path of one snapshot archive, or null if the name is not a snapshot. */
function snapshotPath(dir, assignmentId, username, file) {
  if (!SNAPSHOT_FILE_PATTERN.test(String(file))) {
    return null;
  }
  try {
    return path.join(studentDir(dir, assignmentId, username), file);
  } catch {
    return null;
  }
}

/** @returns {Record<string, {file, reason, finishedAt, takenAt, bytes}[]>} newest first */
function listSnapshots(dir, assignmentId) {
  const result = {};
  if (!dir || !ASSIGNMENT_ID_PATTERN.test(assignmentId)) {
    return result;
  }
  let users;
  try {
    users = fs.readdirSync(path.join(dir, assignmentId));
  } catch {
    return result;
  }
  for (const username of users) {
    if (!isSafeUsername(username)) {
      continue;
    }
    const userDir = path.join(dir, assignmentId, username);
    let files;
    try {
      files = fs.readdirSync(userDir);
    } catch {
      continue;
    }
    const snapshots = files
      .filter((file) => SNAPSHOT_FILE_PATTERN.test(file))
      .map((file) => {
        let meta = {};
        try {
          meta = JSON.parse(fs.readFileSync(path.join(userDir, `${file}.json`), "utf8"));
        } catch {
          // Archive without metadata: fall back to the name.
        }
        const [, , reason] = SNAPSHOT_FILE_PATTERN.exec(file);
        return {
          file,
          reason,
          finishedAt: meta.finishedAt || null,
          takenAt: meta.takenAt || null,
          bytes: Number.isInteger(meta.bytes) ? meta.bytes : null,
        };
      })
      .sort((a, b) => b.file.localeCompare(a.file));
    if (snapshots.length) {
      result[username] = snapshots;
    }
  }
  return result;
}

function runTar(args, input) {
  return new Promise((resolve, reject) => {
    const child = spawn("tar", args, { stdio: ["pipe", "ignore", "pipe"] });
    let stderr = "";
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });
    child.on("error", reject);
    child.on("close", (code) => {
      // GNU tar exits 1 when a file changed while it was read; the container
      // may still be stopping, and the archive is complete either way.
      if (code === 0 || code === 1) {
        resolve();
      } else {
        reject(new Error(`tar exited ${code}: ${stderr.trim().slice(0, 500)}`));
      }
    });
    child.stdin.end(input);
  });
}

/**
 * Archive a student's home, skipping top-level dot entries (editor state,
 * caches, shell history). Symlinks are stored as links, never followed.
 */
async function archiveHome(homeDir, outFile) {
  const entries = fs.readdirSync(homeDir).filter((name) => !name.startsWith("."));
  fs.mkdirSync(path.dirname(outFile), { recursive: true });
  const tmp = `${outFile}.${process.pid}.tmp`;
  try {
    await runTar(["-czf", tmp, "-C", homeDir, "--null", "-T", "-"], entries.map((name) => `${name}\0`).join(""));
    fs.renameSync(tmp, outFile);
  } catch (err) {
    fs.rmSync(tmp, { force: true });
    throw err;
  }
  return fs.statSync(outFile).size;
}

/**
 * Snapshots due for every student who finished (submitted or ran out of time)
 * within the last maxLagMs and has none for that finish yet. Older finishes are
 * skipped: their home may have changed since, so a snapshot would mislabel it.
 */
function dueSnapshots({ state, dir, nowMs, maxLagMs }) {
  const due = [];
  for (const [assignmentId, assignment] of Object.entries(state.assignments)) {
    if (!ASSIGNMENT_ID_PATTERN.test(assignmentId)) {
      continue;
    }
    for (const username of Object.keys(assignment.checkins || {})) {
      if (!isSafeUsername(username)) {
        continue;
      }
      const finish = finishInfo(assignment, username, nowMs);
      if (!finish || nowMs - Date.parse(finish.at) > maxLagMs) {
        continue;
      }
      const file = snapshotFileName(finish.at, finish.reason);
      const target = path.join(dir, assignmentId, username, file);
      if (!fs.existsSync(target)) {
        due.push({ assignmentId, username, reason: finish.reason, finishedAt: finish.at, file: target });
      }
    }
  }
  return due;
}

async function takeSnapshot({ homesDir, job, nowMs }) {
  const homeDir = path.join(homesDir, job.username);
  const bytes = await archiveHome(homeDir, job.file);
  const meta = {
    assignment: job.assignmentId,
    username: job.username,
    reason: job.reason,
    finishedAt: job.finishedAt,
    takenAt: new Date(nowMs).toISOString(),
    bytes,
  };
  fs.writeFileSync(`${job.file}.json`, `${JSON.stringify(meta, null, 2)}\n`);
  return meta;
}

module.exports = {
  isSafeUsername,
  snapshotFileName,
  snapshotPath,
  listSnapshots,
  archiveHome,
  dueSnapshots,
  takeSnapshot,
};
