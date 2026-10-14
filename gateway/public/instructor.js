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
  const filterEl = $("filter");
  const csvLink = $("csv");
  const clearBtn = $("clear");
  const rosterBody = $("roster");

  let state = null;
  let selectedId = null;
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

  /** Status text, CSS class and finish time for one roster row. */
  function progress(assignment, record) {
    if (!record) return { text: "Not checked in", className: "out", finishedAt: "" };
    if (record.submittedAt) return { text: "Submitted", className: "done", finishedAt: record.submittedAt };
    if (!assignment.timeLimitMinutes) return { text: "Checked in", className: "in", finishedAt: "" };
    const deadline = Date.parse(record.at) + assignment.timeLimitMinutes * 60_000;
    const remaining = deadline - Date.now();
    if (remaining <= 0) {
      return { text: "Time up", className: "done", finishedAt: new Date(deadline).toISOString() };
    }
    return { text: `Working · ${Math.ceil(remaining / 60_000)}m left`, className: "in", finishedAt: "" };
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
      if (!selectedId || !assignmentById(selectedId)) {
        selectedId = state.activeAssignmentId || (state.assignments[0] && state.assignments[0].id) || null;
      }
      render();
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

    // Assignment picker
    const options = state.assignments.map((a) => {
      const option = document.createElement("option");
      option.value = a.id;
      option.textContent = `${a.title}${a.title !== a.id ? ` (${a.id})` : ""}${a.timeLimitMinutes ? ` · ${a.timeLimitMinutes} min` : ""}${a.id === state.activeAssignmentId ? " [active]" : ""}`;
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

  function renderRoster() {
    const assignment = assignmentById(selectedId);
    csvLink.hidden = !assignment;
    clearBtn.hidden = !assignment;
    clearBtn.disabled = mutating || !assignment || !Object.keys(assignment.checkins).length;
    if (!assignment) {
      rosterCountEl.textContent = "";
      rosterBody.replaceChildren();
      return;
    }
    csvLink.href = `/_gateway/api/instructor/assignments/${encodeURIComponent(assignment.id)}.csv`;

    const checkins = assignment.checkins;
    const enrolled = new Set(state.roster);
    const users = [...state.roster, ...Object.keys(checkins).filter((u) => !enrolled.has(u))];
    const checkedInCount = Object.keys(checkins).length;
    rosterCountEl.textContent = `${assignment.title}: ${checkedInCount} of ${state.roster.length} checked in`;

    const filter = filterEl.value.trim().toLowerCase();
    const rows = users
      .filter((u) => !filter || u.toLowerCase().includes(filter))
      .sort((a, b) => {
        const ai = checkins[a] ? 1 : 0;
        const bi = checkins[b] ? 1 : 0;
        return ai - bi || a.toLowerCase().localeCompare(b.toLowerCase());
      })
      .map((user) => {
        const record = checkins[user];
        const status = progress(assignment, record);
        const tr = document.createElement("tr");
        const cells = [
          user,
          status.text,
          record ? formatTime(record.at) : "",
          record ? record.by : "",
          record ? record.method : "",
          formatTime(status.finishedAt),
        ].map((text) => {
          const td = document.createElement("td");
          td.textContent = text;
          return td;
        });
        cells[1].className = status.className;
        if (!enrolled.has(user)) {
          const note = document.createElement("span");
          note.className = "muted";
          note.textContent = " (no longer enrolled)";
          cells[0].append(note);
        }
        const actionTd = document.createElement("td");
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = record ? "secondary small" : "small";
        btn.textContent = record ? "Undo" : "Check in";
        btn.disabled = mutating || (!record && !enrolled.has(user));
        btn.addEventListener("click", () => (record ? undoCheckIn(assignment, user) : manualCheckIn(assignment, user)));
        actionTd.append(btn);
        tr.append(...cells, actionTd);
        return tr;
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

  selectEl.addEventListener("change", () => {
    selectedId = selectEl.value;
    render();
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
