const { spawn } = require("child_process");
const express = require("express");
const fs = require("fs");
const path = require("path");
const QRCode = require("qrcode");
const { CHECKIN_QR_PAYLOAD_PREFIX } = require("./constants");
const { deadlineMs, finishInfo, extraMinutesFor } = require("./store");
const { summarizeSession } = require("../monitoring/sessionLog");
const { listSnapshots, snapshotPath } = require("../snapshots/snapshots");

const PASSCODE_FAILURE_WINDOW_MS = 60_000;
const PASSCODE_MAX_FAILURES = 5;
const NO_ACTIVE_POLL_MS = 3_000;

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function audit(event, fields) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), event, ...fields }));
}

function csvCell(value) {
  const text = String(value ?? "");
  const guarded = /^[=+\-@\t\r]/u.test(text) ? `'${text}` : text;
  return `"${guarded.replace(/"/g, '""')}"`;
}

/**
 * Student check-in and instructor dashboard routes. Mounted on the apex host
 * only; subdomain requests are proxied before these routes run.
 */
function registerCheckinRoutes(app, deps) {
  const {
    store,
    tokens,
    loadUsers,
    findUsername,
    isStaff,
    sessionUser,
    requestOrigin,
    workspaceRedirectUrl,
    checkinPageUrl,
    pageShell,
    escapeHtml,
    onLocksChanged,
    sessionLogs,
    snapshotsDir,
  } = deps;

  const passcodeIssuerByAssignment = new Map();
  const passcodeFailuresByUser = new Map();

  function enrolledUsername(user) {
    return user ? findUsername(loadUsers(), user) : null;
  }

  function canonicalEnrolled(rawUser) {
    const username = enrolledUsername(rawUser);
    return username && username === rawUser ? username : null;
  }

  function handle(fn) {
    return async (req, res) => {
      try {
        await fn(req, res);
      } catch (err) {
        if (err instanceof HttpError) {
          res.status(err.status).json({ error: err.message });
          return;
        }
        console.error("check-in route failed:", err);
        res.status(500).json({ error: "Internal error. Please try again." });
      }
    };
  }

  function requireUser(req) {
    const user = sessionUser(req);
    if (!user) {
      throw new HttpError(401, "Authentication required");
    }
    return user;
  }

  function requireStudent(req) {
    const user = requireUser(req);
    const username = enrolledUsername(user);
    if (!username) {
      throw new HttpError(403, "You are not enrolled for this course workspace");
    }
    return username;
  }

  function requireStaff(req) {
    const user = requireUser(req);
    if (!isStaff(user)) {
      throw new HttpError(403, "A staff role is required");
    }
    return user;
  }

  /** Check-in to finish (or now): the span a student's activity is judged over. */
  function activityWindow(assignment, username, nowMs) {
    const record = assignment.checkins[username];
    if (!record) {
      return null;
    }
    const finish = finishInfo(assignment, username, nowMs);
    // Log lines have whole-second timestamps; keep events from the check-in second.
    const from = Math.floor(Date.parse(record.at) / 1000) * 1000;
    return { from, to: finish ? Date.parse(finish.at) : nowMs };
  }

  function activitySummary(assignment, username, nowMs) {
    const window = activityWindow(assignment, username, nowMs);
    return window ? summarizeSession(sessionLogs.events(username), window.from, window.to) : null;
  }

  function requireAssignmentParam(req) {
    const { id } = req.params;
    if (!store.hasAssignment(id)) {
      throw new HttpError(404, "Unknown assignment");
    }
    return store.getAssignment(id);
  }

  function wholeMinutes(value, label) {
    if (!Number.isInteger(value)) {
      throw new HttpError(400, `${label} must be a whole number of minutes`);
    }
    return value;
  }

  function recentPasscodeFailures(username, now) {
    const recent = (passcodeFailuresByUser.get(username) || []).filter(
      (at) => now - at < PASSCODE_FAILURE_WINDOW_MS,
    );
    if (recent.length) {
      passcodeFailuresByUser.set(username, recent);
    } else {
      passcodeFailuresByUser.delete(username);
    }
    return recent;
  }

  const api = express.Router();
  api.use(express.json({ limit: "4kb" }));
  api.use((req, res, next) => {
    res.set("Cache-Control", "private, no-store");
    if (req.method === "GET" || req.method === "HEAD") {
      return next();
    }
    if (!req.is("application/json")) {
      return res.status(415).json({ error: "Content-Type must be application/json" });
    }
    if (req.get("origin") !== requestOrigin(req)) {
      return res.status(403).json({ error: "Cross-origin request rejected" });
    }
    return next();
  });

  // ---------------------------------------------------------------- student

  api.get(
    "/checkin/status",
    handle(async (req, res) => {
      const username = requireStudent(req);
      if (isStaff(username) || store.isUnlocked(username)) {
        return res.json({ checkedIn: true, redirect: workspaceRedirectUrl(username) });
      }
      const activeId = store.getActiveAssignmentId();
      if (!activeId) {
        return res.json({
          checkedIn: false,
          noActiveAssignment: true,
          pollMs: NO_ACTIVE_POLL_MS,
        });
      }
      const session = store.getSession(username);
      if (session && session.finished) {
        return res.json({ checkedIn: false, ...session, pollMs: NO_ACTIVE_POLL_MS });
      }
      const assignment = store.getAssignment(activeId);
      const { token, expiresAt } = tokens.getCurrentToken(activeId, username);
      const qrSvg = await QRCode.toString(
        `${CHECKIN_QR_PAYLOAD_PREFIX}${activeId}:${username}:${token}`,
        { type: "svg", margin: 1, color: { dark: "#000000", light: "#ffffff" } },
      );
      return res.json({
        checkedIn: false,
        username,
        assignment: { id: activeId, title: assignment.title },
        qrSvg,
        pollMs: Math.max(50, expiresAt - Date.now() + 25),
      });
    }),
  );

  api.post(
    "/checkin/passcode",
    handle(async (req, res) => {
      const username = requireStudent(req);
      const now = Date.now();
      if (recentPasscodeFailures(username, now).length >= PASSCODE_MAX_FAILURES) {
        throw new HttpError(429, "Too many incorrect passcodes. Wait a minute and try again.");
      }
      const passcode = typeof req.body?.passcode === "string" ? req.body.passcode.trim() : "";
      if (!/^\d{4}$/u.test(passcode)) {
        throw new HttpError(400, "Enter the four-digit passcode shown by staff.");
      }
      const activeId = store.getActiveAssignmentId();
      if (!activeId) {
        throw new HttpError(409, "No check-in session is open");
      }
      if (!tokens.isValidPasscode(activeId, passcode)) {
        passcodeFailuresByUser.set(username, [...recentPasscodeFailures(username, now), now]);
        audit("checkin.passcode_rejected", { actor: username, assignment: activeId });
        throw new HttpError(400, "Invalid or expired passcode");
      }
      const issuer = passcodeIssuerByAssignment.get(activeId) || "passcode";
      const result = store.checkIn(activeId, username, { by: issuer, method: "passcode" });
      passcodeFailuresByUser.delete(username);
      audit("checkin.passcode", {
        actor: username,
        target: username,
        assignment: activeId,
        issuer,
        alreadyCheckedIn: result.alreadyCheckedIn,
      });
      return res.json({ checkedIn: true, redirect: workspaceRedirectUrl(username) });
    }),
  );

  // Polled by the workspace bar, which may run on the student's subdomain.
  api.get(
    "/checkin/session",
    handle(async (req, res) => {
      const username = requireStudent(req);
      return res.json({
        session: store.getSession(username),
        serverNow: new Date().toISOString(),
        checkinUrl: checkinPageUrl(req),
      });
    }),
  );

  api.post(
    "/checkin/submit",
    handle(async (req, res) => {
      const username = requireStudent(req);
      const activeId = store.getActiveAssignmentId();
      if (!activeId || !store.getSession(username)) {
        throw new HttpError(409, "You are not checked in to an open assignment");
      }
      const result = store.submit(activeId, username);
      if (result.submitted) {
        onLocksChanged();
        audit("checkin.submitted", { actor: username, assignment: activeId });
      }
      return res.json({ finished: result.finished, redirect: checkinPageUrl(req) });
    }),
  );

  // ---------------------------------------------------------------- staff

  api.get(
    "/instructor/state",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const snapshot = store.snapshot();
      const assignments = Object.entries(snapshot.assignments)
        .map(([id, a]) => {
          const checkins = {};
          for (const [user, record] of Object.entries(a.checkins)) {
            const deadline = deadlineMs(a, user);
            checkins[user] = {
              ...record,
              deadline: deadline === null ? null : new Date(deadline).toISOString(),
            };
          }
          return { id, ...a, extraMinutes: a.extraMinutes || {}, checkins };
        })
        .sort((a, b) => String(b.createdAt).localeCompare(String(a.createdAt)));
      const roster = Object.keys(loadUsers()).sort((a, b) =>
        a.toLowerCase().localeCompare(b.toLowerCase()),
      );
      return res.json({
        me,
        meWorkspaceUrl: enrolledUsername(me) ? workspaceRedirectUrl(enrolledUsername(me)) : null,
        activeAssignmentId: snapshot.activeAssignmentId,
        assignments,
        roster,
        serverNow: new Date().toISOString(),
      });
    }),
  );

  api.post(
    "/instructor/assignments",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const id = typeof req.body?.id === "string" ? req.body.id.trim() : "";
      const timeLimitMinutes = req.body?.timeLimitMinutes ?? null;
      try {
        store.createAssignment({ id, title: req.body?.title, by: me, timeLimitMinutes });
      } catch (err) {
        throw new HttpError(400, err.message);
      }
      audit("checkin.assignment_created", { actor: me, assignment: id, timeLimitMinutes });
      return res.status(201).json({ id });
    }),
  );

  api.patch(
    "/instructor/assignments/:id",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const { id } = req.params;
      requireAssignmentParam(req);
      const changes = {};
      for (const key of ["title", "timeLimitMinutes", "archived"]) {
        if (req.body && Object.prototype.hasOwnProperty.call(req.body, key)) {
          changes[key] = req.body[key];
        }
      }
      let updated;
      try {
        updated = store.updateAssignment(id, changes);
      } catch (err) {
        throw new HttpError(400, err.message);
      }
      // A new time limit can lock (or unlock) students who are working now.
      onLocksChanged();
      audit("checkin.assignment_updated", { actor: me, assignment: id, ...changes });
      return res.json({ id, ...updated });
    }),
  );

  api.post(
    "/instructor/assignments/:id/extend",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const { id } = req.params;
      requireAssignmentParam(req);
      const minutes = wholeMinutes(req.body?.minutes, "Extension");
      let result;
      try {
        result = store.extendAll(id, minutes);
      } catch (err) {
        throw new HttpError(400, err.message);
      }
      audit("checkin.extended_all", { actor: me, assignment: id, minutes, count: result.extended.length });
      return res.json({ extended: result.extended.length });
    }),
  );

  api.put(
    "/instructor/assignments/:id/extra-minutes/:user",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const assignment = requireAssignmentParam(req);
      const rawUser = req.params.user;
      // Former students keep their record, so their time can still be corrected.
      const username =
        canonicalEnrolled(rawUser) ||
        (Object.prototype.hasOwnProperty.call(assignment.checkins, rawUser) ? rawUser : null);
      if (!username) {
        throw new HttpError(404, `${rawUser} is not enrolled`);
      }
      const minutes = wholeMinutes(req.body?.minutes, "Extra time");
      let result;
      try {
        result = store.setExtraMinutes(req.params.id, username, minutes);
      } catch (err) {
        throw new HttpError(400, err.message);
      }
      onLocksChanged();
      audit("checkin.extra_minutes", { actor: me, target: username, assignment: req.params.id, ...result });
      return res.json({ username, ...result });
    }),
  );

  api.post(
    "/instructor/assignments/:id/checkins/:user/reopen",
    handle(async (req, res) => {
      const me = requireStaff(req);
      requireAssignmentParam(req);
      const { id, user } = req.params;
      let result;
      try {
        result = store.reopen(id, user, { by: me });
      } catch (err) {
        throw new HttpError(409, err.message);
      }
      audit("checkin.reopened", { actor: me, target: user, assignment: id, finished: result.finished });
      return res.json({ username: user, finished: result.finished });
    }),
  );

  api.get(
    "/instructor/assignments/:id/activity",
    handle(async (req, res) => {
      requireStaff(req);
      const assignment = requireAssignmentParam(req);
      const nowMs = Date.now();
      const monitoring = {};
      const monitoringAvailable = sessionLogs.available();
      if (monitoringAvailable) {
        for (const user of Object.keys(assignment.checkins)) {
          monitoring[user] = activitySummary(assignment, user, nowMs);
        }
      }
      return res.json({
        monitoringAvailable,
        monitoring,
        snapshotsAvailable: Boolean(snapshotsDir) && fs.existsSync(snapshotsDir),
        snapshots: listSnapshots(snapshotsDir, req.params.id),
      });
    }),
  );

  api.get(
    "/instructor/assignments/:id/activity/:user",
    handle(async (req, res) => {
      requireStaff(req);
      const assignment = requireAssignmentParam(req);
      const window = activityWindow(assignment, req.params.user, Date.now());
      if (!window) {
        throw new HttpError(404, "Not checked in");
      }
      const events = sessionLogs
        .events(req.params.user)
        .filter((event) => event.at >= window.from && event.at <= window.to)
        .map((event) => ({ at: new Date(event.at).toISOString(), label: event.label, detail: event.detail }));
      return res.json({
        from: new Date(window.from).toISOString(),
        to: new Date(window.to).toISOString(),
        events,
      });
    }),
  );

  api.get(
    "/instructor/assignments/:id/snapshots/:user/:file",
    handle(async (req, res) => {
      requireStaff(req);
      requireAssignmentParam(req);
      const { id, user, file } = req.params;
      const target = snapshotsDir ? snapshotPath(snapshotsDir, id, user, file) : null;
      if (!target || !fs.existsSync(target)) {
        throw new HttpError(404, "No such snapshot");
      }
      audit("snapshot.downloaded", { actor: sessionUser(req), target: user, assignment: id, file });
      return res.download(target, `${id}-${user}-${file}`);
    }),
  );

  api.get(
    "/instructor/assignments/:id/snapshots.tar.gz",
    handle(async (req, res) => {
      requireStaff(req);
      requireAssignmentParam(req);
      const { id } = req.params;
      if (!snapshotsDir || !fs.existsSync(path.join(snapshotsDir, id))) {
        throw new HttpError(404, "No snapshots for this assignment yet");
      }
      audit("snapshot.downloaded_all", { actor: sessionUser(req), assignment: id });
      res.set("Content-Type", "application/gzip");
      res.set("Content-Disposition", `attachment; filename="snapshots-${id}.tar.gz"`);
      const tar = spawn("tar", ["-czf", "-", "-C", snapshotsDir, id], { stdio: ["ignore", "pipe", "ignore"] });
      tar.on("error", () => res.destroy());
      tar.on("close", (code) => {
        if (code !== 0) {
          res.destroy();
        }
      });
      res.on("close", () => tar.kill());
      tar.stdout.pipe(res);
    }),
  );

  api.post(
    "/instructor/active",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const assignmentId = req.body?.assignmentId ?? null;
      if (assignmentId !== null && !store.hasAssignment(assignmentId)) {
        throw new HttpError(404, "Unknown assignment");
      }
      const { previous } = store.setActive(assignmentId);
      if (previous !== assignmentId) {
        onLocksChanged();
      }
      audit("checkin.active_changed", { actor: me, previous, assignment: assignmentId });
      return res.json({ activeAssignmentId: assignmentId });
    }),
  );

  api.post(
    "/instructor/scan",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const payload = req.body?.payload;
      if (typeof payload !== "string" || !payload.startsWith(CHECKIN_QR_PAYLOAD_PREFIX)) {
        throw new HttpError(400, "Not a check-in QR code");
      }
      const parts = payload.slice(CHECKIN_QR_PAYLOAD_PREFIX.length).split(":");
      const [assignmentId, rawUser, token] = parts;
      if (parts.length !== 3 || !tokens.isValidToken(assignmentId, rawUser, token)) {
        throw new HttpError(400, "Invalid or expired check-in code");
      }
      const activeId = store.getActiveAssignmentId();
      if (assignmentId !== activeId) {
        throw new HttpError(409, `This code is for "${assignmentId}", which is not the active assignment`);
      }
      const username = canonicalEnrolled(rawUser);
      if (!username) {
        throw new HttpError(404, `${rawUser} is not enrolled`);
      }
      const result = store.checkIn(activeId, username, { by: me, method: "qr" });
      audit("checkin.qr", {
        actor: me,
        target: username,
        assignment: activeId,
        alreadyCheckedIn: result.alreadyCheckedIn,
      });
      return res.json({ username, ...result });
    }),
  );

  api.post(
    "/instructor/passcode",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const activeId = store.getActiveAssignmentId();
      if (!activeId) {
        throw new HttpError(409, "Activate an assignment before revealing a passcode");
      }
      const issued = tokens.issuePasscode(activeId);
      passcodeIssuerByAssignment.set(activeId, me);
      audit("checkin.passcode_issued", { actor: me, assignment: activeId });
      return res.json({
        assignmentId: activeId,
        passcode: issued.passcode,
        periodMs: issued.periodMs,
        remainingMs: issued.expiresAt - Date.now(),
      });
    }),
  );

  function requireAssignmentAndStudent(req) {
    const { id, user } = req.params;
    if (!store.hasAssignment(id)) {
      throw new HttpError(404, "Unknown assignment");
    }
    const username = canonicalEnrolled(user);
    return { assignmentId: id, username, rawUser: user };
  }

  api.post(
    "/instructor/assignments/:id/checkins/:user",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const { assignmentId, username, rawUser } = requireAssignmentAndStudent(req);
      if (!username) {
        throw new HttpError(404, `${rawUser} is not enrolled`);
      }
      const result = store.checkIn(assignmentId, username, { by: me, method: "manual" });
      audit("checkin.manual", {
        actor: me,
        target: username,
        assignment: assignmentId,
        alreadyCheckedIn: result.alreadyCheckedIn,
      });
      return res.json({ username, ...result });
    }),
  );

  api.delete(
    "/instructor/assignments/:id/checkins/:user",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const { assignmentId, rawUser } = requireAssignmentAndStudent(req);
      // Undo also works for students who have since been removed from users.json.
      const result = store.undoCheckIn(assignmentId, rawUser);
      if (result.removed) {
        onLocksChanged();
      }
      audit("checkin.undo", { actor: me, target: rawUser, assignment: assignmentId, ...result });
      return res.json(result);
    }),
  );

  api.post(
    "/instructor/assignments/:id/clear",
    handle(async (req, res) => {
      const me = requireStaff(req);
      const { id } = req.params;
      if (!store.hasAssignment(id)) {
        throw new HttpError(404, "Unknown assignment");
      }
      const { removed } = store.clearAssignment(id);
      if (removed.length) {
        onLocksChanged();
      }
      audit("checkin.cleared", { actor: me, assignment: id, count: removed.length });
      return res.json({ removed: removed.length });
    }),
  );

  api.get(
    "/instructor/assignments/:id.csv",
    handle(async (req, res) => {
      requireStaff(req);
      const { id } = req.params;
      if (!store.hasAssignment(id)) {
        throw new HttpError(404, "Unknown assignment");
      }
      const assignment = store.getAssignment(id);
      const { checkins } = assignment;
      const users = new Set([...Object.keys(loadUsers()), ...Object.keys(checkins)]);
      const nowMs = Date.now();
      const monitored = sessionLogs.available();
      const snapshots = listSnapshots(snapshotsDir, id);
      const seconds = (ms) => (ms === undefined ? "" : Math.round(ms / 1000));
      const rows = [
        [
          "andrew_id",
          "checked_in",
          "checked_in_at",
          "checked_in_by",
          "method",
          "extra_minutes",
          "deadline",
          "submitted_at",
          "times_reopened",
          "times_left_editor",
          "seconds_away",
          "longest_away_seconds",
          "disconnects",
          "snapshots",
        ],
      ];
      for (const user of [...users].sort((a, b) => a.toLowerCase().localeCompare(b.toLowerCase()))) {
        const record = checkins[user];
        const deadlineAt = deadlineMs(assignment, user);
        const deadline = deadlineAt === null ? "" : new Date(deadlineAt).toISOString();
        const activity = record && monitored ? activitySummary(assignment, user, nowMs) : null;
        rows.push([
          user,
          record ? "yes" : "no",
          record?.at,
          record?.by,
          record?.method,
          extraMinutesFor(assignment, user) || "",
          deadline,
          record?.submittedAt,
          record?.reopened?.length || "",
          activity?.awayCount,
          seconds(activity?.awayMs),
          seconds(activity?.longestAwayMs),
          activity?.disconnects,
          (snapshots[user] || []).map((s) => s.file).join(" "),
        ]);
      }
      res.set("Content-Type", "text/csv; charset=utf-8");
      res.set("Content-Disposition", `attachment; filename="checkins-${id}.csv"`);
      return res.send(`${rows.map((row) => row.map(csvCell).join(",")).join("\r\n")}\r\n`);
    }),
  );

  api.use((_req, res) => res.status(404).json({ error: "Not found" }));
  // eslint-disable-next-line no-unused-vars -- Express needs the 4-argument signature
  api.use((err, _req, res, _next) => {
    res.status(err.status && err.status < 500 ? err.status : 400).json({
      error: "Malformed request",
    });
  });

  app.use("/_gateway/api", api);

  // ---------------------------------------------------------------- assets

  app.get("/_gateway/jsQR.js", (_req, res) => {
    res.sendFile(path.join(path.dirname(require.resolve("jsqr")), "jsQR.js"));
  });
  app.use("/_gateway", express.static(path.join(__dirname, "..", "public")));
  app.use("/_gateway", (_req, res) => res.status(404).type("text").send("Not found"));

  // ---------------------------------------------------------------- pages

  app.get("/checkin", (req, res) => {
    const user = sessionUser(req);
    if (!user) {
      return res.redirect(302, "/login");
    }
    if (isStaff(user)) {
      return res.redirect(302, "/instructor");
    }
    const username = enrolledUsername(user);
    if (!username) {
      req.session = null;
      return res.redirect(302, "/login");
    }
    if (store.isUnlocked(username)) {
      return res.redirect(302, workspaceRedirectUrl(username));
    }
    res.set("Cache-Control", "private, no-store");
    return res.type("html").send(
      pageShell({
        title: "Check in",
        heading: "Check in",
        scripts: ["/_gateway/checkin.js"],
        bodyHtml: `
          <p class="hint" id="assignment-title" style="margin: -10px 0 20px;">Loading…</p>
          <div class="success" id="finished" role="status" hidden></div>
          <div id="checkin-flow">
            <div class="qr-box" id="qr" aria-live="polite"></div>
            <div class="error" id="qr-error" role="alert" hidden></div>
            <p class="hint">Show this QR code to a staff member, or enter the passcode they reveal.</p>
            <form class="passcode-form" id="passcode-form" autocomplete="off">
              <label for="passcode">Staff passcode</label>
              <div class="row">
                <input id="passcode" name="passcode" type="text" inputmode="numeric"
                       pattern="[0-9]{4}" maxlength="4" placeholder="0000" disabled />
                <button type="submit" id="passcode-submit" disabled>Check in</button>
              </div>
            </form>
            <div class="error" id="passcode-error" role="alert" hidden></div>
          </div>
          <div class="success" id="done" role="status" hidden>Checked in. Opening your workspace…</div>
          <p class="hint footer">Signed in as ${escapeHtml(username)} · <a href="/logout">Sign out</a></p>
        `,
      }),
    );
  });

  app.get("/instructor", (req, res) => {
    const user = sessionUser(req);
    if (!user) {
      return res.redirect(302, "/login");
    }
    if (!isStaff(user)) {
      return res.redirect(302, "/");
    }
    res.set("Cache-Control", "private, no-store");
    return res.type("html").send(
      pageShell({
        title: "Instructor dashboard",
        heading: "Instructor dashboard",
        wide: true,
        scripts: ["/_gateway/instructor.js"],
        bodyHtml: `
          <p class="hint" style="margin: -10px 0 20px;">
            Signed in as ${escapeHtml(user)}
            <span id="my-workspace"></span>
            · <a href="/logout">Sign out</a>
          </p>
          <div class="dashboard">
          <div class="error" id="error" role="alert" hidden></div>

          <div class="dash-grid">
          <section class="panel">
            <h2>Session</h2>
            <p id="active-status" class="status-line">Loading…</p>
            <div class="row wrap">
              <select id="assignment-select" aria-label="Assignment"></select>
              <button type="button" id="activate">Make active</button>
              <button type="button" id="deactivate" class="secondary">Close session</button>
            </div>
            <div class="row wrap session-options">
              <label class="hint checkbox"><input type="checkbox" id="show-archived" /> Show archived</label>
              <button type="button" id="extend-all" class="secondary small">Extend everyone…</button>
            </div>
            <details id="edit-details">
              <summary>Edit selected assignment</summary>
              <form id="edit-form" class="row wrap" autocomplete="off">
                <input id="edit-title" placeholder="Title" maxlength="120" aria-label="Title" />
                <input id="edit-minutes" type="number" min="1" max="1440" step="1"
                       placeholder="Time limit, minutes (blank = none)" aria-label="Time limit in minutes" />
                <button type="submit">Save</button>
                <button type="button" id="archive" class="secondary">Archive</button>
              </form>
              <p class="hint form-note">The ID cannot change. A new time limit applies to everyone already checked in.</p>
            </details>
            <details>
              <summary>New assignment</summary>
              <form id="create-form" class="row wrap" autocomplete="off">
                <input id="new-id" placeholder="id, e.g. proxylab-checkoff" required
                       pattern="[a-z0-9](?:[a-z0-9\\-]{0,62}[a-z0-9])?" maxlength="64" />
                <input id="new-title" placeholder="Title (optional)" maxlength="120" />
                <input id="new-minutes" type="number" min="1" max="1440" step="1"
                       placeholder="Time limit, minutes (optional)" aria-label="Time limit in minutes" />
                <button type="submit">Create</button>
              </form>
            </details>
          </section>

          <section class="panel">
            <h2>Check in students</h2>
            <div class="row wrap">
              <button type="button" id="scan-toggle">Start scanning</button>
              <button type="button" id="reveal" class="secondary">Reveal passcode</button>
              <span id="passcode-display" class="passcode" hidden></span>
            </div>
            <div id="scanner" class="scanner" hidden>
              <video id="scanner-video" playsinline muted></video>
              <canvas id="scanner-canvas" hidden></canvas>
              <div id="scanner-busy" class="scanner-busy" hidden>Checking in…</div>
            </div>
            <p id="scan-status" class="hint" aria-live="polite"></p>
            <div id="flash" class="flash" role="status" hidden></div>
          </section>
          </div>

          <section class="panel">
            <h2>Roster <span id="roster-count" class="hint"></span></h2>
            <div id="roster-stats" class="stats" hidden></div>
            <div class="row wrap">
              <input id="filter" type="search" placeholder="Filter Andrew IDs" />
              <a id="csv" class="button secondary small" href="#">Export CSV</a>
              <a id="snapshots-all" class="button secondary small" href="#">Download snapshots</a>
              <button type="button" id="clear" class="danger small">Clear check-ins</button>
            </div>
            <div class="table-wrap">
              <table>
                <thead>
                  <tr>
                    <th>Andrew ID</th><th>Status</th><th>Checked in</th><th>By</th><th>Finished</th>
                    <th title="Times the student left the editor window or tab, and total time away">Away</th>
                    <th title="Browser disconnects (page closed or reloaded)">Disc.</th>
                    <th>Snapshot</th><th></th>
                  </tr>
                </thead>
                <tbody id="roster"></tbody>
              </table>
            </div>
            <p class="hint" id="activity-note"></p>
          </section>
          </div>
        `,
      }),
    );
  });
}

module.exports = { registerCheckinRoutes };
