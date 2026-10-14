(() => {
  "use strict";

  const RETRY_AFTER_ERROR_MS = 3000;

  const titleEl = document.getElementById("assignment-title");
  const qrEl = document.getElementById("qr");
  const qrErrorEl = document.getElementById("qr-error");
  const form = document.getElementById("passcode-form");
  const input = document.getElementById("passcode");
  const submit = document.getElementById("passcode-submit");
  const passcodeErrorEl = document.getElementById("passcode-error");
  const doneEl = document.getElementById("done");
  const finishedEl = document.getElementById("finished");
  const flowEl = document.getElementById("checkin-flow");

  let pollTimer = null;
  let submitting = false;
  let completed = false;
  let sessionOpen = false;

  function formatClock(iso) {
    return new Date(iso).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
  }

  function showFinished(data) {
    finishedEl.textContent =
      data.finished === "submitted"
        ? `Submitted at ${formatClock(data.submittedAt)}. Your workspace is closed.`
        : `Time ran out at ${formatClock(data.deadline)}. Your workspace is closed.`;
    finishedEl.hidden = false;
    flowEl.hidden = true;
  }

  function showError(el, message) {
    el.textContent = message;
    el.hidden = !message;
  }

  function updateFormState() {
    input.disabled = !sessionOpen || submitting || completed;
    submit.disabled = !sessionOpen || submitting || completed;
    submit.textContent = submitting ? "Checking in…" : "Check in";
  }

  async function readJson(res) {
    if (res.status === 401) {
      window.location.assign("/login");
      throw new Error("Your session expired. Sign in again.");
    }
    let data = null;
    try {
      data = await res.json();
    } catch {
      // fall through to the generic message
    }
    if (!res.ok) {
      throw new Error((data && data.error) || `Request failed (${res.status})`);
    }
    return data;
  }

  function complete(redirect) {
    if (completed) return;
    completed = true;
    window.clearTimeout(pollTimer);
    showError(qrErrorEl, "");
    showError(passcodeErrorEl, "");
    doneEl.hidden = false;
    updateFormState();
    window.location.assign(redirect || "/");
  }

  function schedule(ms) {
    window.clearTimeout(pollTimer);
    if (!completed) pollTimer = window.setTimeout(refresh, ms);
  }

  function showWaiting(message) {
    const div = document.createElement("div");
    div.className = "waiting";
    div.textContent = message;
    qrEl.replaceChildren(div);
  }

  async function refresh() {
    try {
      const res = await fetch("/_gateway/api/checkin/status", {
        cache: "no-store",
        credentials: "same-origin",
      });
      const data = await readJson(res);
      if (completed) return;
      if (data.checkedIn) {
        complete(data.redirect);
        return;
      }
      showError(qrErrorEl, "");
      finishedEl.hidden = true;
      flowEl.hidden = false;
      if (data.finished) {
        sessionOpen = false;
        titleEl.textContent = data.assignment.title;
        showFinished(data);
      } else if (data.noActiveAssignment) {
        sessionOpen = false;
        titleEl.textContent = "No check-in session is open yet.";
        showWaiting("Wait for your instructor to open a session. This page updates automatically.");
      } else {
        sessionOpen = true;
        titleEl.textContent = data.assignment.title;
        const img = document.createElement("img");
        img.alt = "Check-in QR code";
        img.width = 304;
        img.height = 304;
        img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(data.qrSvg)}`;
        qrEl.replaceChildren(img);
      }
      updateFormState();
      schedule(data.pollMs);
    } catch (err) {
      if (completed) return;
      qrEl.replaceChildren();
      showError(qrErrorEl, err instanceof Error ? err.message : "Unable to load the check-in code.");
      schedule(RETRY_AFTER_ERROR_MS);
    }
  }

  input.addEventListener("input", () => {
    showError(passcodeErrorEl, "");
    updateFormState();
  });

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (submitting || completed) return;
    const passcode = input.value.trim();
    if (!/^\d{4}$/u.test(passcode)) {
      showError(passcodeErrorEl, "Enter the four-digit passcode shown by staff.");
      return;
    }
    submitting = true;
    showError(passcodeErrorEl, "");
    updateFormState();
    try {
      const res = await fetch("/_gateway/api/checkin/passcode", {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ passcode }),
      });
      const data = await readJson(res);
      complete(data.redirect);
    } catch (err) {
      showError(passcodeErrorEl, err instanceof Error ? err.message : "Unable to check in.");
    } finally {
      submitting = false;
      updateFormState();
      if (!completed) input.focus();
    }
  });

  updateFormState();
  refresh();
})();
