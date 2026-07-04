(function () {
  function sendEvent(evt) {
    try {
      // Use fetch with credentials so cookies are sent.
      fetch("/session/event", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "include",
        body: JSON.stringify(evt),
        keepalive: true,
      }).catch(() => {});
    } catch (e) {}
  }

  function makeEvent(type, extra) {
    return Object.assign(
      {
        event: type,
        timestamp: Math.floor(Date.now() / 1000),
        visibility:
          typeof document !== "undefined"
            ? document.visibilityState
            : undefined,
        focused:
          typeof document !== "undefined" &&
          typeof document.hasFocus === "function"
            ? document.hasFocus()
            : undefined,
        href: typeof location !== "undefined" ? location.href : undefined,
      },
      extra || {},
    );
  }

  if (typeof document !== "undefined") {
    document.addEventListener("visibilitychange", function () {
      sendEvent(makeEvent("visibility"));
    });
  }
  if (typeof window !== "undefined") {
    window.addEventListener("focus", function () {
      sendEvent(makeEvent("focus"));
    });
    window.addEventListener("blur", function () {
      sendEvent(makeEvent("blur"));
    });
    window.addEventListener("beforeunload", function () {
      sendEvent(makeEvent("unload"));
    });
  }

  // Initial load event
  try {
    sendEvent(makeEvent("page_load"));
  } catch (e) {}
})();
