(() => {
  "use strict";

  const QR_PREFIX = "debug-server-checkin:";
  const STATE_POLL_MS = 3000;
  const SCAN_REQUEST_TIMEOUT_MS = 10_000;
  const SAME_CODE_COOLDOWN_MS = 2500;
  const SAME_STUDENT_COOLDOWN_MS = 3000;
  const FLASH_MS = 4000;

  const $ = (id) => document.getElementById(id);
  const errorEl = $("error");
  const myWorkspaceEl = $("my-workspace");
  const activeStatusEl = $("active-status");
  const selectEl = $("assignment-select");
  const activateBtn = $("activate");
  const deactivateBtn = $("deactivate");
  const createForm = $("create-form");
  const newIdEl = $("new-id");
  const newTitleEl = $("new-title");
  const newMinutesEl = $("new-minutes");
  const scanToggleBtn = $("scan-toggle");
  const revealBtn = $("reveal");
  const passcodeEl = $("passcode-display");
  const scannerEl = $("scanner");
  const video = $("scanner-video");
  const canvas = $("scanner-canvas");
  const scannerBusyEl = $("scanner-busy");
  const scanStatusEl = $("scan-status");
  const flashEl = $("flash");
  const rosterCountEl = $("roster-count");
  const rosterStatsEl = $("roster-stats");
  const filterEl = $("filter");
  const csvLink = $("csv");
  const snapshotsAllLink = $("snapshots-all");
  const clearBtn = $("clear");
  const rosterBody = $("roster");
  const activityNoteEl = $("activity-note");
  const showArchivedEl = $("show-archived");
  const extendAllBtn = $("extend-all");
  const editDetails = $("edit-details");
  const editForm = $("edit-form");
  const editTitleEl = $("edit-title");
  const editMinutesEl = $("edit-minutes");
  const archiveBtn = $("archive");

  let state = null;
  let clockOffsetMs = 0;
  let selectedId = null;
  let editFormFor = null;

  // Monitoring summaries and snapshots for the selected assignment.
  let activity = null;
  let activityFor = null;
  let expandedUser = null;
  let timeline = null;
  // Scroll position of each terminal's keystroke list, kept across re-renders.
  const keylogScroll = new Map();
  let loadingState = false;
  let reloadQueued = false;
  let mutating = false;

  let issuing = false;
  let passcodeVisible = false;
  let passcodeTimer = null;
  let passcodeTick = null;

  const scanner = {
    running: false,
    generation: 0,
    stream: null,
    frame: null,
    detector: null,
    busy: false,
    lastRaw: "",
    lastRawAt: 0,
    lastUser: "",
    lastUserAt: 0,
  };

  // ------------------------------------------------------------ helpers

  function showError(message) {
    errorEl.textContent = message || "";
    errorEl.hidden = !message;
  }

  let flashTimer = null;
  function flash(kind, message) {
    flashEl.className = `flash ${kind}`;
    flashEl.textContent = message;
    flashEl.hidden = false;
    window.clearTimeout(flashTimer);
    flashTimer = window.setTimeout(() => {
      flashEl.hidden = true;
    }, FLASH_MS);
  }

  async function api(method, url, body) {
    const init = { method, credentials: "same-origin", cache: "no-store" };
    if (body !== undefined) {
      init.headers = { "Content-Type": "application/json" };
      init.body = JSON.stringify(body);
    }
    const res = await fetch(url, init);
    if (res.status === 401) {
      window.location.assign("/login");
      throw new Error("Your session expired. Sign in again.");
    }
    let data = null;
    try {
      data = await res.json();
    } catch {
      // handled below
    }
    if (!res.ok) {
      throw new Error((data && data.error) || `Request failed (${res.status})`);
    }
    return data;
  }

  function withTimeout(promise, ms, message) {
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => reject(new Error(message)), ms);
      promise.then(
        (value) => {
          window.clearTimeout(timer);
          resolve(value);
        },
        (err) => {
          window.clearTimeout(timer);
          reject(err);
        },
      );
    });
  }

  function formatTime(iso) {
    if (!iso) return "";
    const date = new Date(iso);
    const sameDay = date.toDateString() === new Date().toDateString();
    return sameDay
      ? date.toLocaleTimeString([], { hour: "numeric", minute: "2-digit", second: "2-digit" })
      : date.toLocaleString([], { month: "short", day: "numeric", hour: "numeric", minute: "2-digit" });
  }

  function assignmentById(id) {
    return state ? state.assignments.find((a) => a.id === id) || null : null;
  }

  function timeLimitLabel(assignment) {
    return assignment.timeLimitMinutes ? `${assignment.timeLimitMinutes} min limit` : "no time limit";
  }

  function serverNow() {
    return Date.now() + clockOffsetMs;
  }

  function formatDuration(ms) {
    const total = Math.round(ms / 1000);
    if (total < 60) return `${total}s`;
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    return h ? `${h}h ${m}m` : `${m}m ${String(total % 60).padStart(2, "0")}s`;
  }

  function formatBytes(bytes) {
    if (bytes === null || bytes === undefined) return "";
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  }

  function extraMinutes(assignment, user) {
    return assignment.extraMinutes[user] || 0;
  }

  /** Status text, CSS class and finish time for one roster row. */
  function progress(assignment, user) {
    const record = assignment.checkins[user];
    const extra = extraMinutes(assignment, user);
    const extraNote = extra && assignment.timeLimitMinutes ? ` (+${extra}m)` : "";
    if (!record) return { text: `Not checked in${extraNote}`, className: "out", finishedAt: "" };
    if (record.submittedAt) return { text: "Submitted", className: "done", finishedAt: record.submittedAt };
    if (!record.deadline) {
      return { text: record.reopened ? "Reopened" : "Checked in", className: "in", finishedAt: "" };
    }
    const remaining = Date.parse(record.deadline) - serverNow();
    if (remaining <= 0) {
      return { text: `Time up${extraNote}`, className: "done", finishedAt: record.deadline };
    }
    return {
      text: `Working · ${Math.ceil(remaining / 60_000)}m left${extraNote}`,
      className: "in",
      finishedAt: "",
    };
  }

  async function mutate(fn) {
    if (mutating) return;
    mutating = true;
    showError("");
    render();
    try {
      await fn();
    } catch (err) {
      showError(err instanceof Error ? err.message : "Request failed.");
    } finally {
      mutating = false;
      render();
      await loadState();
    }
  }

  // ------------------------------------------------------------ state + render

  async function loadState() {
    if (loadingState) {
      reloadQueued = true;
      return;
    }
    loadingState = true;
    try {
      state = await api("GET", "/_gateway/api/instructor/state");
      clockOffsetMs = Date.parse(state.serverNow) - Date.now();
      if (!selectedId || !assignmentById(selectedId)) {
        const firstVisible = state.assignments.find((a) => !a.archived) || state.assignments[0];
        selectedId = state.activeAssignmentId || (firstVisible && firstVisible.id) || null;
      }
      render();
      await loadActivity();
    } catch (err) {
      showError(err instanceof Error ? err.message : "Unable to load dashboard.");
    } finally {
      loadingState = false;
    }
    if (reloadQueued) {
      reloadQueued = false;
      await loadState();
    }
  }

  function activityUrl(assignmentId) {
    return `/_gateway/api/instructor/assignments/${encodeURIComponent(assignmentId)}/activity`;
  }

  async function loadActivity() {
    const id = selectedId;
    if (!id) {
      activity = null;
      activityFor = null;
      return;
    }
    try {
      const next = await api("GET", activityUrl(id));
      let nextTimeline = null;
      if (expandedUser && assignmentById(id) && assignmentById(id).checkins[expandedUser]) {
        nextTimeline = await api("GET", `${activityUrl(id)}/${encodeURIComponent(expandedUser)}`);
      }
      if (id !== selectedId) return;
      activity = next;
      activityFor = id;
      timeline = nextTimeline ? { user: expandedUser, ...nextTimeline } : null;
      renderRoster();
    } catch (err) {
      showError(err instanceof Error ? err.message : "Unable to load activity.");
    }
  }

  function render() {
    if (!state) return;
    const active = assignmentById(state.activeAssignmentId);

    if (state.meWorkspaceUrl && !myWorkspaceEl.firstChild) {
      const link = document.createElement("a");
      link.href = state.meWorkspaceUrl;
      link.textContent = "Open my workspace";
      myWorkspaceEl.append(" · ", link);
    }

    // Session status
    if (active) {
      const count = Object.keys(active.checkins).length;
      activeStatusEl.className = "status-line";
      activeStatusEl.textContent = `Active: ${active.title} (${active.id}), ${timeLimitLabel(active)}. ${count} of ${state.roster.length} checked in. Students not checked in are locked out.`;
    } else {
      activeStatusEl.className = "status-line locked";
      activeStatusEl.textContent = state.assignments.length
        ? "No active session. All student workspaces are locked."
        : "No assignments yet. Create one below, then make it active.";
    }

    // Assignment picker (archived ones only on request, or while selected)
    const options = state.assignments
      .filter((a) => !a.archived || showArchivedEl.checked || a.id === selectedId)
      .map((a) => {
        const option = document.createElement("option");
        option.value = a.id;
        option.textContent = `${a.title}${a.title !== a.id ? ` (${a.id})` : ""}${a.timeLimitMinutes ? ` · ${a.timeLimitMinutes} min` : ""}${a.id === state.activeAssignmentId ? " [active]" : ""}${a.archived ? " [archived]" : ""}`;
        option.selected = a.id === selectedId;
        return option;
      });
    if (!options.length) {
      const option = document.createElement("option");
      option.textContent = "No assignments";
      option.disabled = true;
      options.push(option);
    }
    if (document.activeElement !== selectEl) selectEl.replaceChildren(...options);
    selectEl.disabled = !state.assignments.length;
    activateBtn.disabled = mutating || !selectedId || selectedId === state.activeAssignmentId;
    deactivateBtn.disabled = mutating || !state.activeAssignmentId;
    extendAllBtn.disabled = mutating || !active || !active.timeLimitMinutes;

    // Edit form follows the selection, but never overwrites what staff are typing.
    const selected = assignmentById(selectedId);
    editDetails.hidden = !selected;
    if (selected && editFormFor !== selected.id) {
      editFormFor = selected.id;
      editTitleEl.value = selected.title;
      editMinutesEl.value = selected.timeLimitMinutes || "";
    }
    if (selected) {
      archiveBtn.textContent = selected.archived ? "Unarchive" : "Archive";
      archiveBtn.disabled = mutating || (!selected.archived && selected.id === state.activeAssignmentId);
      archiveBtn.title = archiveBtn.disabled && !mutating ? "Close the session before archiving it" : "";
    }

    // Check-in controls apply to the active assignment only
    scanToggleBtn.disabled = !active && !scanner.running;
    revealBtn.disabled = !active || issuing || passcodeVisible;
    if (!active && !scanner.running) {
      scanStatusEl.textContent = "Make an assignment active to check students in.";
    } else if (!scanner.running && !scanStatusEl.dataset.sticky) {
      scanStatusEl.textContent = "";
    }

    renderRoster();
  }

  function cell(text, className) {
    const td = document.createElement("td");
    td.textContent = text;
    if (className) td.className = className;
    return td;
  }

  function statusCell(status) {
    const td = document.createElement("td");
    const pill = document.createElement("span");
    pill.className = `pill ${status.className}`;
    pill.textContent = status.text;
    td.append(pill);
    return td;
  }

  function renderStats(assignment, users) {
    const counts = { in: 0, done: 0, out: 0 };
    for (const user of users) counts[progress(assignment, user).className] += 1;
    const stats = [
      ["Working", counts.in, "in"],
      ["Finished", counts.done, "done"],
      ["Not checked in", counts.out, "out"],
    ].map(([label, value, kind]) => {
      const stat = document.createElement("div");
      stat.className = `stat ${kind}`;
      const number = document.createElement("strong");
      number.textContent = String(value);
      stat.append(number, document.createTextNode(label));
      return stat;
    });
    rosterStatsEl.replaceChildren(...stats);
    rosterStatsEl.hidden = false;
  }

  function smallButton(label, className, onClick, disabled) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = `${className} small`;
    btn.textContent = label;
    btn.disabled = disabled;
    btn.addEventListener("click", onClick);
    return btn;
  }

  function awayCell(summary) {
    if (!summary) return cell("");
    if (!summary.awayCount && !summary.awayMs) return cell("—", "muted-cell");
    const td = cell(`${summary.awayCount}× · ${formatDuration(summary.awayMs)}`, "flag");
    td.title = `Left the editor ${summary.awayCount} time(s); ${formatDuration(summary.awayMs)} away in total, longest ${formatDuration(summary.longestAwayMs)}`;
    return td;
  }

  function snapshotCell(assignment, user) {
    const td = document.createElement("td");
    const list = (activity && activity.snapshots[user]) || [];
    list.forEach((snap, i) => {
      if (i) td.append(document.createElement("br"));
      const link = document.createElement("a");
      link.href = `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}/snapshots/${encodeURIComponent(user)}/${encodeURIComponent(snap.file)}`;
      link.textContent = `${snap.reason === "submitted" ? "Submitted" : "Time up"} · ${formatBytes(snap.bytes)}`;
      link.title = `Work at ${formatTime(snap.finishedAt)}, archived ${formatTime(snap.takenAt)}`;
      td.append(link);
    });
    return td;
  }

  function timelineRow(assignment, user, columns) {
    const tr = document.createElement("tr");
    tr.className = "timeline-row";
    const td = document.createElement("td");
    td.colSpan = columns;
    const record = assignment.checkins[user];
    const parts = [];
    if (record && record.reopened) {
      for (const r of record.reopened) {
        parts.push(`Submitted ${formatTime(r.submittedAt)}, reopened by ${r.by} at ${formatTime(r.at)}.`);
      }
    }
    if (parts.length) {
      const note = document.createElement("p");
      note.className = "hint";
      note.textContent = parts.join(" ");
      td.append(note);
    }
    if (!timeline || timeline.user !== user) {
      td.append(hint("Loading activity…"));
      tr.append(td);
      return tr;
    }
    td.append(heading("Browser"));
    if (!timeline.events.length) {
      td.append(hint("No browser activity recorded since check-in."));
    } else {
      const list = document.createElement("ol");
      list.className = "timeline";
      for (const event of timeline.events) {
        const li = document.createElement("li");
        const time = document.createElement("span");
        time.className = "timeline-time";
        time.textContent = formatTime(event.at);
        const label = document.createElement("span");
        label.className = `timeline-label ${/BLUR|HIDDEN|DISCONNECT/u.test(event.label) ? "away" : ""}`;
        label.textContent = event.label;
        li.append(time, label, document.createTextNode(event.detail));
        list.append(li);
      }
      td.append(list);
    }
    td.append(...terminalInput(timeline));
    tr.append(td);
    return tr;
  }

  const hint = (textContent) => Object.assign(document.createElement("p"), { className: "hint", textContent });
  const heading = (textContent) => Object.assign(document.createElement("h4"), { className: "timeline-heading", textContent });

  function terminalInput(data) {
    const out = [heading("Terminal input")];
    if (!data.keystrokesAvailable) return [...out, hint("Keystroke logs are not mounted.")];
    if (!data.terminals.length) return [...out, hint("Nothing typed in a terminal since check-in.")];
    if (data.terminalsTruncated) out.push(hint("Long log: showing the most recent 2,000 lines."));
    for (const terminal of data.terminals) {
      // No end is recorded if the container stops under it (deadline, submit).
      const closed = terminal.endedAt ? `, closed ${formatTime(terminal.endedAt)}` : "";
      out.push(hint(`Terminal opened ${formatTime(terminal.startedAt)}${closed}`));
      const list = document.createElement("ol");
      list.className = "timeline keylog";
      for (const line of terminal.lines) list.append(keylogLine(line));
      out.push(list);
      // The roster re-renders on every poll: follow new input like a terminal
      // unless staff scrolled up to read.
      const saved = keylogScroll.get(`${data.user}/${terminal.id}`);
      requestAnimationFrame(() => {
        list.scrollTop = saved && !saved.atBottom ? saved.top : list.scrollHeight;
      });
      list.addEventListener("scroll", () => {
        const atBottom = list.scrollTop + list.clientHeight >= list.scrollHeight - 4;
        keylogScroll.set(`${data.user}/${terminal.id}`, { top: list.scrollTop, atBottom });
      });
    }
    return out;
  }

  function keylogLine(line) {
    const li = document.createElement("li");
    const time = Object.assign(document.createElement("span"), { className: "timeline-time", textContent: formatTime(line.at) });
    const text = Object.assign(document.createElement("code"), { className: "keylog-text", textContent: line.text });
    li.append(time, text);
    if (!line.exact) {
      text.classList.add("inexact");
      text.title = "Tab completion, history or a full-screen program was involved, so what ran may differ.";
    }
    if (line.end && line.end !== "Enter") li.append(kbd(line.end));
    if (line.keys.some((k) => typeof k !== "string")) {
      const raw = document.createElement("span");
      raw.className = "keylog-raw";
      raw.title = "Keys as typed";
      for (const k of line.keys) raw.append(typeof k === "string" ? k : kbd(k.key));
      li.append(raw);
    }
    return li;
  }

  function kbd(key) {
    return Object.assign(document.createElement("kbd"), { textContent: key });
  }

  function toggleTimeline(user) {
    expandedUser = expandedUser === user ? null : user;
    timeline = null;
    renderRoster();
    if (expandedUser) loadActivity();
  }

  function renderRoster() {
    const assignment = assignmentById(selectedId);
    csvLink.hidden = !assignment;
    clearBtn.hidden = !assignment;
    clearBtn.disabled = mutating || !assignment || !Object.keys(assignment.checkins).length;
    const currentActivity = activity && activityFor === selectedId ? activity : null;
    const hasSnapshots = Boolean(currentActivity && Object.keys(currentActivity.snapshots).length);
    snapshotsAllLink.hidden = !assignment || !hasSnapshots;
    if (!assignment) {
      rosterCountEl.textContent = "";
      rosterStatsEl.hidden = true;
      activityNoteEl.textContent = "";
      rosterBody.replaceChildren();
      return;
    }
    csvLink.href = `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}.csv`;
    snapshotsAllLink.href = `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}/snapshots.tar.gz`;

    const notes = [];
    if (currentActivity && !currentActivity.monitoringAvailable) {
      notes.push("Browser activity logs are not mounted, so Away and Disc. are empty.");
    }
    if (currentActivity && !currentActivity.snapshotsAvailable) {
      notes.push("The snapshot folder is not mounted, so no work snapshots are taken.");
    }
    notes.push("Click an Andrew ID to see that student's browser activity and terminal input.");
    activityNoteEl.textContent = notes.join(" ");

    const checkins = assignment.checkins;
    const enrolled = new Set(state.roster);
    const users = [...state.roster, ...Object.keys(checkins).filter((u) => !enrolled.has(u))];
    const checkedInCount = Object.keys(checkins).length;
    rosterCountEl.textContent = `${assignment.title}: ${checkedInCount} of ${state.roster.length} checked in`;
    renderStats(assignment, users);

    const filter = filterEl.value.trim().toLowerCase();
    const rows = [];
    users
      .filter((u) => !filter || u.toLowerCase().includes(filter))
      .sort((a, b) => {
        const ai = checkins[a] ? 1 : 0;
        const bi = checkins[b] ? 1 : 0;
        return ai - bi || a.toLowerCase().localeCompare(b.toLowerCase());
      })
      .forEach((user) => {
        const record = checkins[user];
        const status = progress(assignment, user);
        const summary = currentActivity && currentActivity.monitoring[user];
        const tr = document.createElement("tr");

        const userTd = document.createElement("td");
        if (record) {
          const toggle = document.createElement("button");
          toggle.type = "button";
          toggle.className = "link-button";
          toggle.textContent = `${expandedUser === user ? "▾" : "▸"} ${user}`;
          toggle.setAttribute("aria-expanded", String(expandedUser === user));
          toggle.addEventListener("click", () => toggleTimeline(user));
          userTd.append(toggle);
        } else {
          userTd.textContent = user;
        }
        if (!enrolled.has(user)) {
          const note = document.createElement("span");
          note.className = "muted";
          note.textContent = " (no longer enrolled)";
          userTd.append(note);
        }

        const actionTd = document.createElement("td");
        actionTd.className = "actions-cell";
        actionTd.append(
          smallButton(
            record ? "Undo" : "Check in",
            record ? "secondary" : "",
            () => (record ? undoCheckIn(assignment, user) : manualCheckIn(assignment, user)),
            mutating || (!record && !enrolled.has(user)),
          ),
        );
        if (assignment.timeLimitMinutes) {
          actionTd.append(
            smallButton("Time…", "secondary", () => setExtraTime(assignment, user), mutating),
          );
        }
        if (record && record.submittedAt) {
          actionTd.append(smallButton("Reopen", "secondary", () => reopen(assignment, user), mutating));
        }

        tr.append(
          userTd,
          statusCell(status),
          cell(record ? formatTime(record.at) : ""),
          cell(record ? `${record.by} (${record.method})` : ""),
          cell(formatTime(status.finishedAt)),
          awayCell(summary),
          summary ? cell(String(summary.disconnects), summary.disconnects ? "flag" : "muted-cell") : cell(""),
          snapshotCell(assignment, user),
          actionTd,
        );
        rows.push(tr);
        if (record && expandedUser === user) rows.push(timelineRow(assignment, user, tr.children.length));
      });
    rosterBody.replaceChildren(...rows);
  }

  // ------------------------------------------------------------ actions

  function checkinUrl(assignmentId, user) {
    return `/_gateway/api/instructor/assignments/${encodeURIComponent(assignmentId)}/checkins/${encodeURIComponent(user)}`;
  }

  function manualCheckIn(assignment, user) {
    mutate(async () => {
      const result = await api("POST", checkinUrl(assignment.id, user), {});
      flash(result.alreadyCheckedIn ? "warn" : "ok", `${user} ${result.alreadyCheckedIn ? "was already checked in" : "checked in"}.`);
    });
  }

  function undoCheckIn(assignment, user) {
    const warning =
      assignment.id === state.activeAssignmentId
        ? ` ${user} will be locked out and disconnected immediately.`
        : "";
    const timer = assignment.timeLimitMinutes ? " Checking them in again starts a fresh timer." : "";
    if (!window.confirm(`Undo ${user}'s check-in for ${assignment.title}?${warning}${timer}`)) return;
    mutate(async () => {
      await api("DELETE", checkinUrl(assignment.id, user), {});
    });
  }

  function setExtraTime(assignment, user) {
    const current = extraMinutes(assignment, user);
    const answer = window.prompt(
      `Extra minutes for ${user} on ${assignment.title}, on top of the ${assignment.timeLimitMinutes} min limit.\n` +
        `Use this for accommodations (e.g. ${Math.round(assignment.timeLimitMinutes * 0.5)} for 1.5× time) or to give back lost time. ` +
        "It can be set before the student checks in. 0 removes it.",
      String(current),
    );
    if (answer === null) return;
    const minutes = Number(answer.trim());
    if (!Number.isInteger(minutes) || minutes < 0) {
      showError("Extra time must be a whole number of minutes, 0 or more.");
      return;
    }
    mutate(async () => {
      await api(
        "PUT",
        `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}/extra-minutes/${encodeURIComponent(user)}`,
        { minutes },
      );
      flash("ok", minutes ? `${user} now has ${minutes} extra minutes.` : `Removed ${user}'s extra time.`);
    });
  }

  function reopen(assignment, user) {
    if (
      !window.confirm(
        `Reopen ${user}'s workspace for ${assignment.title}? Their submission time is kept in the history and their timer keeps running from the original check-in.`,
      )
    ) {
      return;
    }
    mutate(async () => {
      const result = await api("POST", `${checkinUrl(assignment.id, user)}/reopen`, {});
      if (result.finished === "expired") {
        flash("warn", `${user} was reopened but their time is up. Use Time… to give them more.`);
      } else if (assignment.id !== state.activeAssignmentId) {
        flash("warn", `${user} was reopened, but ${assignment.title} is not the active session.`);
      } else {
        flash("ok", `${user} can work again.`);
      }
    });
  }

  selectEl.addEventListener("change", () => {
    selectedId = selectEl.value;
    expandedUser = null;
    timeline = null;
    activity = null;
    render();
    loadActivity();
  });

  showArchivedEl.addEventListener("change", render);

  extendAllBtn.addEventListener("click", () => {
    const active = assignmentById(state.activeAssignmentId);
    if (!active || !active.timeLimitMinutes) return;
    const answer = window.prompt(
      `Give every student still working on ${active.title} how many more minutes? Students who already submitted or ran out of time are not changed.`,
      "10",
    );
    if (answer === null) return;
    const minutes = Number(answer.trim());
    if (!Number.isInteger(minutes) || minutes < 1) {
      showError("The extension must be a whole number of minutes, 1 or more.");
      return;
    }
    mutate(async () => {
      const result = await api(
        "POST",
        `/_gateway/api/instructor/assignments/${encodeURIComponent(active.id)}/extend`,
        { minutes },
      );
      flash("ok", `Gave ${result.extended} student(s) ${minutes} more minutes.`);
    });
  });

  editForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const assignment = assignmentById(selectedId);
    if (!assignment) return;
    const minutesText = editMinutesEl.value.trim();
    const timeLimitMinutes = minutesText ? Number(minutesText) : null;
    const title = editTitleEl.value.trim();
    if (
      timeLimitMinutes !== assignment.timeLimitMinutes &&
      Object.keys(assignment.checkins).length &&
      !window.confirm(
        `Change the time limit of ${assignment.title} to ${timeLimitMinutes ? `${timeLimitMinutes} minutes` : "none"}? ` +
          "It applies to everyone already checked in; anyone past the new limit is closed out immediately.",
      )
    ) {
      return;
    }
    mutate(async () => {
      await api("PATCH", `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}`, {
        title,
        timeLimitMinutes,
      });
      editFormFor = null;
      flash("ok", `Saved ${title || assignment.id}.`);
    });
  });

  archiveBtn.addEventListener("click", () => {
    const assignment = assignmentById(selectedId);
    if (!assignment) return;
    const archived = !assignment.archived;
    mutate(async () => {
      await api("PATCH", `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}`, {
        archived,
      });
      flash(
        "ok",
        archived
          ? `Archived ${assignment.title}. Tick "Show archived" to find it again.`
          : `Unarchived ${assignment.title}.`,
      );
    });
  });

  activateBtn.addEventListener("click", () => {
    const next = assignmentById(selectedId);
    if (!next) return;
    const current = assignmentById(state.activeAssignmentId);
    const message = current
      ? `Switch the active session from ${current.title} to ${next.title}? Students are re-locked until they check in to ${next.title}.`
      : `Open ${next.title}? Students must check in to it to use their workspaces.`;
    if (!window.confirm(message)) return;
    mutate(() => api("POST", "/_gateway/api/instructor/active", { assignmentId: next.id }));
  });

  deactivateBtn.addEventListener("click", () => {
    if (!window.confirm("Close the session? All student workspaces will be locked and disconnected immediately.")) return;
    stopScanner();
    mutate(() => api("POST", "/_gateway/api/instructor/active", { assignmentId: null }));
  });

  createForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const id = newIdEl.value.trim();
    const title = newTitleEl.value.trim();
    const minutes = newMinutesEl.value.trim();
    const timeLimitMinutes = minutes ? Number(minutes) : null;
    mutate(async () => {
      await api("POST", "/_gateway/api/instructor/assignments", { id, title, timeLimitMinutes });
      newIdEl.value = "";
      newTitleEl.value = "";
      newMinutesEl.value = "";
      selectedId = id;
      flash("ok", `Created ${title || id}. Make it active to start checking students in.`);
    });
  });

  clearBtn.addEventListener("click", () => {
    const assignment = assignmentById(selectedId);
    if (!assignment) return;
    const warning = assignment.id === state.activeAssignmentId ? " Everyone will be locked out and disconnected." : "";
    if (!window.confirm(`Clear all check-ins for ${assignment.title}?${warning}`)) return;
    mutate(() => api("POST", `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}/clear`, {}));
  });

  filterEl.addEventListener("input", renderRoster);

  // ------------------------------------------------------------ passcode

  function hidePasscode() {
    window.clearTimeout(passcodeTimer);
    window.clearInterval(passcodeTick);
    passcodeVisible = false;
    passcodeEl.hidden = true;
    passcodeEl.replaceChildren();
    render();
  }

  revealBtn.addEventListener("click", async () => {
    if (issuing || passcodeVisible) return;
    issuing = true;
    revealBtn.textContent = "Issuing…";
    render();
    showError("");
    try {
      const result = await api("POST", "/_gateway/api/instructor/passcode", {});
      const hideAt = performance.now() + result.remainingMs;
      if (result.remainingMs <= 0) {
        throw new Error("Passcode expired before it arrived. Reveal a new code.");
      }
      passcodeVisible = true;
      const digits = document.createTextNode(result.passcode);
      const countdown = document.createElement("small");
      const tick = () => {
        countdown.textContent = ` ${Math.max(0, Math.ceil((hideAt - performance.now()) / 1000))}s`;
      };
      tick();
      passcodeEl.replaceChildren(digits, countdown);
      passcodeEl.hidden = false;
      passcodeTick = window.setInterval(tick, 200);
      passcodeTimer = window.setTimeout(hidePasscode, result.remainingMs);
    } catch (err) {
      showError(err instanceof Error ? err.message : "Unable to issue passcode.");
    } finally {
      issuing = false;
      revealBtn.textContent = "Reveal passcode";
      render();
    }
  });

  // ------------------------------------------------------------ scanner

  function setScanStatus(message, sticky = false) {
    scanStatusEl.textContent = message;
    if (sticky) scanStatusEl.dataset.sticky = "1";
    else delete scanStatusEl.dataset.sticky;
  }

  function loadJsQr() {
    if (window.jsQR) return Promise.resolve(window.jsQR);
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = "/_gateway/jsQR.js";
      script.onload = () => (window.jsQR ? resolve(window.jsQR) : reject(new Error("QR decoder failed to load.")));
      script.onerror = () => reject(new Error("QR decoder failed to load."));
      document.head.append(script);
    });
  }

  function stopScanner() {
    scanner.generation += 1;
    if (scanner.frame !== null) cancelAnimationFrame(scanner.frame);
    scanner.frame = null;
    if (scanner.stream) scanner.stream.getTracks().forEach((track) => track.stop());
    scanner.stream = null;
    video.srcObject = null;
    scanner.running = false;
    scanner.busy = false;
    scannerBusyEl.hidden = true;
    scannerEl.hidden = true;
    scanToggleBtn.textContent = "Start scanning";
    render();
  }

  async function startScanner() {
    if (scanner.running) return;
    const generation = ++scanner.generation;
    scanner.running = true;
    scanToggleBtn.textContent = "Stop scanning";
    scannerEl.hidden = false;
    setScanStatus("Starting camera…");
    render();
    try {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        throw new Error(
          window.isSecureContext
            ? "This browser does not support camera access. Use the passcode or manual check-in instead."
            : "Camera access requires HTTPS. Use the passcode or manual check-in instead.",
        );
      }
      const stream = await navigator.mediaDevices.getUserMedia({
        video: { facingMode: "environment" },
        audio: false,
      });
      if (generation !== scanner.generation) {
        stream.getTracks().forEach((track) => track.stop());
        return;
      }
      scanner.stream = stream;
      video.srcObject = stream;
      await video.play();
      if (generation !== scanner.generation) return;

      scanner.detector = null;
      if ("BarcodeDetector" in window) {
        try {
          const formats = await window.BarcodeDetector.getSupportedFormats?.();
          if (!formats || formats.includes("qr_code")) {
            scanner.detector = new window.BarcodeDetector({ formats: ["qr_code"] });
          }
        } catch (err) {
          console.warn("Native QR detection unavailable; using jsQR:", err);
        }
      }
      await loadJsQr();
      if (generation !== scanner.generation) return;

      setScanStatus("Point the camera at a student's check-in QR code.");
      scanner.frame = requestAnimationFrame(() => scanFrame(generation));
    } catch (err) {
      if (generation !== scanner.generation) return;
      stopScanner();
      setScanStatus(err instanceof Error ? err.message : "Unable to access the camera.", true);
    }
  }

  async function scanFrame(generation) {
    if (generation !== scanner.generation) return;
    try {
      if (video.readyState >= 2 && !scanner.busy) {
        let rawValue;
        if (scanner.detector) {
          try {
            const codes = await scanner.detector.detect(video);
            rawValue = codes[0] && codes[0].rawValue;
          } catch (err) {
            console.warn("Native QR detection failed; using jsQR:", err);
            scanner.detector = null;
          }
        }
        if (!scanner.detector) {
          const context = canvas.getContext("2d", { willReadFrequently: true });
          if (!context) throw new Error("QR decoding canvas is unavailable.");
          canvas.width = video.videoWidth;
          canvas.height = video.videoHeight;
          context.drawImage(video, 0, 0);
          const image = context.getImageData(0, 0, canvas.width, canvas.height);
          const code = window.jsQR(image.data, image.width, image.height, { inversionAttempts: "dontInvert" });
          rawValue = code && code.data;
        }
        if (generation === scanner.generation && rawValue) await handleDecoded(rawValue, generation);
      }
    } catch (err) {
      if (generation === scanner.generation) {
        stopScanner();
        setScanStatus(err instanceof Error ? err.message : "Unable to scan QR code.", true);
      }
      return;
    }
    if (generation === scanner.generation) {
      scanner.frame = requestAnimationFrame(() => scanFrame(generation));
    }
  }

  async function handleDecoded(rawValue, generation) {
    if (scanner.busy || !rawValue.startsWith(QR_PREFIX)) return;
    const now = performance.now();
    const user = rawValue.slice(QR_PREFIX.length).split(":")[1] || "";
    if (rawValue === scanner.lastRaw && now - scanner.lastRawAt < SAME_CODE_COOLDOWN_MS) return;
    if (user && user === scanner.lastUser && now - scanner.lastUserAt < SAME_STUDENT_COOLDOWN_MS) return;
    scanner.lastRaw = rawValue;
    scanner.lastRawAt = now;

    scanner.busy = true;
    scannerBusyEl.hidden = false;
    try {
      const result = await withTimeout(
        api("POST", "/_gateway/api/instructor/scan", { payload: rawValue }),
        SCAN_REQUEST_TIMEOUT_MS,
        "Check-in timed out. Check the roster before retrying.",
      );
      if (generation !== scanner.generation) return;
      scanner.lastUser = result.username;
      scanner.lastUserAt = performance.now();
      flash(
        result.alreadyCheckedIn ? "warn" : "ok",
        result.alreadyCheckedIn ? `${result.username} was already checked in.` : `${result.username} checked in.`,
      );
      loadState();
    } catch (err) {
      if (generation === scanner.generation) {
        flash("bad", err instanceof Error ? err.message : "Check-in failed.");
      }
    } finally {
      if (generation === scanner.generation) {
        scanner.busy = false;
        scannerBusyEl.hidden = true;
      }
    }
  }

  scanToggleBtn.addEventListener("click", () => {
    if (scanner.running) {
      stopScanner();
      setScanStatus("");
    } else {
      startScanner();
    }
  });

  window.addEventListener("pagehide", stopScanner);
  document.addEventListener("visibilitychange", () => {
    if (document.hidden && scanner.running) {
      stopScanner();
      setScanStatus("Scanning paused while the page was hidden.", true);
    }
  });

  // ------------------------------------------------------------ boot

  loadState();
  window.setInterval(() => {
    if (!document.hidden) loadState();
  }, STATE_POLL_MS);
})();
