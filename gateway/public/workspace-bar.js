(() => {
  "use strict";

  if (window.__workspaceBar) return;
  window.__workspaceBar = true;

  const BAR_HEIGHT = 32;
  const POLL_MS = 10_000;
  const RETRY_AFTER_ERROR_MS = 5000;
  const WARN_MS = 5 * 60_000;
  const URGENT_MS = 60_000;

  let session = null;
  let checkinUrl = "/checkin";
  let clockOffsetMs = 0;
  let pollTimer = null;
  let tickTimer = null;
  let submitting = false;
  let leaving = false;
  let bar = null;
  let timeEl = null;
  let submitBtn = null;

  async function api(method, url) {
    const init = { method, credentials: "same-origin", cache: "no-store" };
    if (method !== "GET") {
      init.headers = { "Content-Type": "application/json" };
      init.body = "{}";
    }
    const res = await fetch(url, init);
    let data = null;
    try {
      data = await res.json();
    } catch {
      // handled below
    }
    if (!res.ok) {
      const err = new Error((data && data.error) || `Request failed (${res.status})`);
      err.status = res.status;
      throw err;
    }
    return data;
  }

  function leave(url) {
    if (leaving) return;
    leaving = true;
    window.clearTimeout(pollTimer);
    window.clearInterval(tickTimer);
    window.location.assign(url);
  }

  // VS Code sizes its workbench from window.innerHeight when mounted on
  // <body>, so report a shorter viewport and let body padding make room.
  function reserveSpace() {
    const own = Object.getOwnPropertyDescriptor(window, "innerHeight");
    const base = own || Object.getOwnPropertyDescriptor(Window.prototype, "innerHeight");
    if (base && base.get) {
      Object.defineProperty(window, "innerHeight", {
        configurable: true,
        get: () => Math.max(0, base.get.call(window) - BAR_HEIGHT),
      });
    }
    document.documentElement.classList.add("has-workspace-bar");
    window.dispatchEvent(new Event("resize"));
  }

  function formatRemaining(ms) {
    const total = Math.max(0, Math.ceil(ms / 1000));
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    const s = total % 60;
    const mm = String(m).padStart(h ? 2 : 1, "0");
    return `${h ? `${h}:` : ""}${mm}:${String(s).padStart(2, "0")}`;
  }

  function tick() {
    if (!session || !timeEl) return;
    if (!session.deadline) {
      timeEl.textContent = "No time limit";
      bar.dataset.urgency = "";
      return;
    }
    const remaining = Date.parse(session.deadline) - (Date.now() + clockOffsetMs);
    timeEl.textContent = remaining > 0 ? `${formatRemaining(remaining)} left` : "Time's up";
    bar.dataset.urgency = remaining <= URGENT_MS ? "urgent" : remaining <= WARN_MS ? "warn" : "";
    if (remaining <= 0) {
      submitBtn.disabled = true;
      window.clearInterval(tickTimer);
      tickTimer = null;
      schedule(1000);
    }
  }

  function render() {
    if (!bar) {
      const link = document.createElement("link");
      link.rel = "stylesheet";
      link.href = "/_gateway/workspace-bar.css";
      document.head.append(link);

      bar = document.createElement("div");
      bar.className = "workspace-bar";
      bar.setAttribute("role", "banner");

      const title = document.createElement("span");
      title.className = "workspace-bar-title";

      timeEl = document.createElement("span");
      timeEl.className = "workspace-bar-time";
      timeEl.setAttribute("role", "timer");

      submitBtn = document.createElement("button");
      submitBtn.type = "button";
      submitBtn.className = "workspace-bar-submit";
      submitBtn.textContent = "Submit";
      submitBtn.addEventListener("click", submit);

      bar.append(title, timeEl, submitBtn);
      document.body.prepend(bar);
      reserveSpace();
    }
    bar.querySelector(".workspace-bar-title").textContent = session.assignment.title;
    submitBtn.disabled = submitting;
    submitBtn.textContent = submitting ? "Submitting…" : "Submit";
    tick();
    if (!tickTimer && session.deadline) tickTimer = window.setInterval(tick, 500);
  }

  function schedule(ms) {
    window.clearTimeout(pollTimer);
    if (!leaving) pollTimer = window.setTimeout(refresh, ms);
  }

  async function refresh() {
    try {
      const data = await api("GET", "/_gateway/api/checkin/session");
      checkinUrl = data.checkinUrl || checkinUrl;
      clockOffsetMs = Date.parse(data.serverNow) - Date.now();
      if (!data.session) {
        // Staff (and never-checked-in users) get no bar; a student whose
        // check-in disappeared has been locked out by staff.
        if (session) leave(checkinUrl);
        return;
      }
      if (data.session.finished) {
        leave(checkinUrl);
        return;
      }
      session = data.session;
      render();
      schedule(POLL_MS);
    } catch (err) {
      if (err.status === 401 || err.status === 403) return;
      schedule(RETRY_AFTER_ERROR_MS);
    }
  }

  async function submit() {
    if (submitting || !session) return;
    const unsaved = document.querySelector(".monaco-workbench .tab.dirty");
    const message = [
      `Submit ${session.assignment.title}?`,
      unsaved ? "\nYou have UNSAVED files. Unsaved changes are not submitted — cancel and save first (Ctrl+S)." : "",
      "\nYour workspace will close and you cannot make further changes.",
    ].join("");
    if (!window.confirm(message)) return;
    submitting = true;
    render();
    try {
      const data = await api("POST", "/_gateway/api/checkin/submit");
      leave(data.redirect || checkinUrl);
    } catch (err) {
      window.alert(err instanceof Error ? err.message : "Unable to submit. Try again.");
    } finally {
      submitting = false;
      if (!leaving) render();
    }
  }

  refresh();
})();
