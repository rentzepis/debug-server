const fs = require("fs");
const path = require("path");
const { ASSIGNMENT_ID_PATTERN } = require("./constants");

const CHECKIN_METHODS = new Set(["qr", "passcode", "manual"]);
const MAX_TITLE_LENGTH = 120;
const MAX_TIME_LIMIT_MINUTES = 24 * 60;

function hasOwn(object, key) {
  return Object.prototype.hasOwnProperty.call(object, key);
}

function extraMinutesFor(assignment, username) {
  const extra = assignment.extraMinutes && hasOwn(assignment.extraMinutes, username)
    ? assignment.extraMinutes[username]
    : 0;
  return Number.isInteger(extra) ? extra : 0;
}

/**
 * Epoch ms when this student's time runs out, or null for untimed assignments
 * and students who are not checked in. Includes their extra minutes.
 */
function deadlineMs(assignment, username) {
  const minutes = assignment.timeLimitMinutes;
  if (!(Number.isInteger(minutes) && minutes > 0) || !hasOwn(assignment.checkins, username)) {
    return null;
  }
  const record = assignment.checkins[username];
  return Date.parse(record.at) + (minutes + extraMinutesFor(assignment, username)) * 60_000;
}

/** When and why this student's work ended, or null while they may still work. */
function finishInfo(assignment, username, nowMs) {
  if (!hasOwn(assignment.checkins, username)) {
    return null;
  }
  const record = assignment.checkins[username];
  if (record.submittedAt) {
    return { reason: "submitted", at: record.submittedAt };
  }
  const deadline = deadlineMs(assignment, username);
  return deadline !== null && nowMs >= deadline
    ? { reason: "expired", at: new Date(deadline).toISOString() }
    : null;
}

function validateTimeLimit(timeLimitMinutes) {
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
}

function cleanTitleOrThrow(title) {
  const cleanTitle = typeof title === "string" ? title.trim() : "";
  if (cleanTitle.length > MAX_TITLE_LENGTH) {
    throw new Error(`Title must be at most ${MAX_TITLE_LENGTH} characters`);
  }
  return cleanTitle;
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
  function finishedReason(assignment, username) {
    const finish = finishInfo(assignment, username, now());
    return finish ? finish.reason : null;
  }

  function requireCheckedIn(assignment, username) {
    if (!hasOwn(assignment.checkins, username)) {
      throw new Error("Not checked in");
    }
    return assignment.checkins[username];
  }

  function validateExtraMinutes(minutes) {
    if (!Number.isInteger(minutes) || minutes < 0 || minutes > MAX_TIME_LIMIT_MINUTES) {
      throw new Error(`Extra time must be a whole number of minutes from 0 to ${MAX_TIME_LIMIT_MINUTES}`);
    }
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
      return finishedReason(assignment, username) === null;
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
      const deadline = deadlineMs(assignment, username);
      return {
        assignment: { id: activeId, title: assignment.title },
        checkedInAt: record.at,
        deadline: deadline === null ? null : new Date(deadline).toISOString(),
        submittedAt: record.submittedAt || null,
        finished: finishedReason(assignment, username),
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
      const cleanTitle = cleanTitleOrThrow(title);
      validateTimeLimit(timeLimitMinutes);
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

    /** Change the title, time limit or archived flag. The ID never changes. */
    updateAssignment(assignmentId, changes) {
      const assignment = requireAssignment(assignmentId);
      const next = {};
      if (hasOwn(changes, "title")) {
        next.title = cleanTitleOrThrow(changes.title) || assignmentId;
      }
      if (hasOwn(changes, "timeLimitMinutes")) {
        validateTimeLimit(changes.timeLimitMinutes);
        next.timeLimitMinutes = changes.timeLimitMinutes;
      }
      if (hasOwn(changes, "archived")) {
        if (typeof changes.archived !== "boolean") {
          throw new Error("archived must be true or false");
        }
        if (changes.archived && state.activeAssignmentId === assignmentId) {
          throw new Error("Close the session before archiving the active assignment");
        }
        next.archived = changes.archived;
      }
      Object.assign(assignment, next);
      if (assignment.archived === false) {
        delete assignment.archived;
      }
      save();
      return structuredClone(assignment);
    },

    /** Extra minutes on top of the time limit; may be set before the student checks in. */
    setExtraMinutes(assignmentId, username, minutes) {
      const assignment = requireAssignment(assignmentId);
      validateExtraMinutes(minutes);
      assignment.extraMinutes = assignment.extraMinutes || {};
      const previous = extraMinutesFor(assignment, username);
      if (minutes === 0) {
        delete assignment.extraMinutes[username];
      } else {
        assignment.extraMinutes[username] = minutes;
      }
      save();
      return { previous, extraMinutes: minutes };
    },

    /** Add minutes for every student still working, e.g. after an outage. */
    extendAll(assignmentId, minutes) {
      const assignment = requireAssignment(assignmentId);
      if (!assignment.timeLimitMinutes) {
        throw new Error("This assignment has no time limit");
      }
      if (!Number.isInteger(minutes) || minutes < 1 || minutes > MAX_TIME_LIMIT_MINUTES) {
        throw new Error(`Extension must be a whole number of minutes from 1 to ${MAX_TIME_LIMIT_MINUTES}`);
      }
      const extended = Object.keys(assignment.checkins).filter(
        (username) => finishedReason(assignment, username) === null,
      );
      assignment.extraMinutes = assignment.extraMinutes || {};
      for (const username of extended) {
        assignment.extraMinutes[username] = Math.min(
          MAX_TIME_LIMIT_MINUTES,
          extraMinutesFor(assignment, username) + minutes,
        );
      }
      save();
      return { extended };
    },

    /** Undo a submission, keeping its time (and who reopened it) in the record. */
    reopen(assignmentId, username, { by }) {
      const assignment = requireAssignment(assignmentId);
      const record = requireCheckedIn(assignment, username);
      if (!record.submittedAt) {
        throw new Error("Not submitted");
      }
      record.reopened = [
        ...(Array.isArray(record.reopened) ? record.reopened : []),
        { submittedAt: record.submittedAt, at: new Date(now()).toISOString(), by },
      ];
      delete record.submittedAt;
      save();
      return { record: structuredClone(record), finished: finishedReason(assignment, username) };
    },

    setActive(assignmentId) {
      if (assignmentId !== null) {
        requireAssignment(assignmentId);
        // Activating an archived assignment brings it back.
        delete state.assignments[assignmentId].archived;
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
      const record = requireCheckedIn(assignment, username);
      const finished = finishedReason(assignment, username);
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

module.exports = { createCheckinStore, deadlineMs, finishInfo, extraMinutesFor };
