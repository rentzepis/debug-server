const fs = require("fs");
const path = require("path");
const { ASSIGNMENT_ID_PATTERN } = require("./constants");

const CHECKIN_METHODS = new Set(["qr", "passcode", "manual"]);
const MAX_TITLE_LENGTH = 120;
const MAX_TIME_LIMIT_MINUTES = 24 * 60;

function hasOwn(object, key) {
  return Object.prototype.hasOwnProperty.call(object, key);
}

/** Epoch ms when this check-in's time runs out, or null for untimed assignments. */
function deadlineMs(assignment, record) {
  const minutes = assignment.timeLimitMinutes;
  return Number.isInteger(minutes) && minutes > 0
    ? Date.parse(record.at) + minutes * 60_000
    : null;
}

function emptyState() {
  return { activeAssignmentId: null, assignments: {} };
}

function loadState(file) {
  let raw;
  try {
    raw = fs.readFileSync(file, "utf8");
  } catch (err) {
    if (err.code === "ENOENT") {
      return emptyState();
    }
    throw err;
  }
  if (!raw.trim()) {
    return emptyState();
  }
  // A corrupt file must stop startup rather than silently wiping attendance.
  const parsed = JSON.parse(raw);
  return {
    activeAssignmentId:
      typeof parsed.activeAssignmentId === "string" ? parsed.activeAssignmentId : null,
    assignments:
      parsed.assignments && typeof parsed.assignments === "object" ? parsed.assignments : {},
  };
}

function createCheckinStore({ file, now = Date.now }) {
  const state = loadState(file);
  if (state.activeAssignmentId && !state.assignments[state.activeAssignmentId]) {
    state.activeAssignmentId = null;
  }

  function save() {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = `${file}.${process.pid}.tmp`;
    fs.writeFileSync(tmp, `${JSON.stringify(state, null, 2)}\n`, { mode: 0o600 });
    fs.renameSync(tmp, file);
  }

  function requireAssignment(assignmentId) {
    const assignment =
      typeof assignmentId === "string" &&
      Object.prototype.hasOwnProperty.call(state.assignments, assignmentId)
        ? state.assignments[assignmentId]
        : null;
    if (!assignment) {
      throw new Error("Unknown assignment");
    }
    return assignment;
  }

  /** "submitted", "expired", or null while the student may still work. */
  function finishedReason(assignment, record) {
    if (record.submittedAt) {
      return "submitted";
    }
    const deadline = deadlineMs(assignment, record);
    return deadline !== null && now() >= deadline ? "expired" : null;
  }

  return {
    snapshot() {
      return structuredClone(state);
    },

    getActiveAssignmentId() {
      return state.activeAssignmentId;
    },

    getAssignment(assignmentId) {
      const assignment = requireAssignment(assignmentId);
      return structuredClone(assignment);
    },

    hasAssignment(assignmentId) {
      return (
        typeof assignmentId === "string" &&
        Object.prototype.hasOwnProperty.call(state.assignments, assignmentId)
      );
    },

    isUnlocked(username) {
      const activeId = state.activeAssignmentId;
      if (!activeId || !username) {
        return false;
      }
      const assignment = state.assignments[activeId];
      if (!assignment || !hasOwn(assignment.checkins, username)) {
        return false;
      }
      return finishedReason(assignment, assignment.checkins[username]) === null;
    },

    /**
     * The student's check-in for the active assignment, or null if they have none.
     * `finished` is "submitted" or "expired" once the workspace is closed to them.
     */
    getSession(username) {
      const activeId = state.activeAssignmentId;
      const assignment = activeId ? state.assignments[activeId] : null;
      if (!assignment || !username || !hasOwn(assignment.checkins, username)) {
        return null;
      }
      const record = assignment.checkins[username];
      const deadline = deadlineMs(assignment, record);
      return {
        assignment: { id: activeId, title: assignment.title },
        checkedInAt: record.at,
        deadline: deadline === null ? null : new Date(deadline).toISOString(),
        submittedAt: record.submittedAt || null,
        finished: finishedReason(assignment, record),
      };
    },

    createAssignment({ id, title, by, timeLimitMinutes = null }) {
      if (typeof id !== "string" || !ASSIGNMENT_ID_PATTERN.test(id)) {
        throw new Error(
          "Assignment ID must be 1-64 lowercase letters, digits or dashes, not starting or ending with a dash",
        );
      }
      if (Object.prototype.hasOwnProperty.call(state.assignments, id)) {
        throw new Error("An assignment with this ID already exists");
      }
      const cleanTitle = typeof title === "string" ? title.trim() : "";
      if (cleanTitle.length > MAX_TITLE_LENGTH) {
        throw new Error(`Title must be at most ${MAX_TITLE_LENGTH} characters`);
      }
      if (
        timeLimitMinutes !== null &&
        !(
          Number.isInteger(timeLimitMinutes) &&
          timeLimitMinutes >= 1 &&
          timeLimitMinutes <= MAX_TIME_LIMIT_MINUTES
        )
      ) {
        throw new Error(`Time limit must be a whole number of minutes from 1 to ${MAX_TIME_LIMIT_MINUTES}`);
      }
      state.assignments[id] = {
        title: cleanTitle || id,
        timeLimitMinutes,
        createdAt: new Date(now()).toISOString(),
        createdBy: by,
        checkins: {},
      };
      save();
      return structuredClone(state.assignments[id]);
    },

    setActive(assignmentId) {
      if (assignmentId !== null) {
        requireAssignment(assignmentId);
      }
      const previous = state.activeAssignmentId;
      state.activeAssignmentId = assignmentId;
      save();
      return { previous, current: assignmentId };
    },

    checkIn(assignmentId, username, { by, method }) {
      const assignment = requireAssignment(assignmentId);
      if (!CHECKIN_METHODS.has(method)) {
        throw new Error("Unknown check-in method");
      }
      if (Object.prototype.hasOwnProperty.call(assignment.checkins, username)) {
        return { alreadyCheckedIn: true, record: structuredClone(assignment.checkins[username]) };
      }
      const record = { at: new Date(now()).toISOString(), by, method };
      assignment.checkins[username] = record;
      save();
      return { alreadyCheckedIn: false, record: structuredClone(record) };
    },

    submit(assignmentId, username) {
      const assignment = requireAssignment(assignmentId);
      if (!hasOwn(assignment.checkins, username)) {
        throw new Error("Not checked in");
      }
      const record = assignment.checkins[username];
      const finished = finishedReason(assignment, record);
      if (finished) {
        return { submitted: false, finished, record: structuredClone(record) };
      }
      record.submittedAt = new Date(now()).toISOString();
      save();
      return { submitted: true, finished: "submitted", record: structuredClone(record) };
    },

    undoCheckIn(assignmentId, username) {
      const assignment = requireAssignment(assignmentId);
      if (!Object.prototype.hasOwnProperty.call(assignment.checkins, username)) {
        return { removed: false };
      }
      delete assignment.checkins[username];
      save();
      return { removed: true };
    },

    clearAssignment(assignmentId) {
      const assignment = requireAssignment(assignmentId);
      const removed = Object.keys(assignment.checkins);
      assignment.checkins = {};
      save();
      return { removed };
    },
  };
}

module.exports = { createCheckinStore, deadlineMs };
