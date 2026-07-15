import { logger } from "@coder/logger";
import { randomUUID } from "crypto";
import { Router, type Request, type Response } from "express";
import { promises as fs } from "fs";
import * as path from "path";
import { type HttpCode, HttpError } from "../../common/http";
import { type DefaultedArgs } from "../cli";
import { authenticated, getCookieOptions, replaceTemplates } from "../http";

export interface SessionEvent {
  event: string;
  timestamp?: string | number;
  sessionId?: string;
  visibility?: "visible" | "hidden";
  focused?: boolean;
  active?: boolean;
  href?: string;
  reason?: string;
  userAgent?: string;
  xForwardedFor?: string | string[];
  remoteAddress?: string;
}

interface SessionMonitoringRequestBody {
  event?: string;
  timestamp?: string;
  visibility?: "visible" | "hidden";
  focused?: boolean;
  active?: boolean;
  href?: string;
  reason?: string;
}

interface NormalizedSessionEvent {
  event: string;
  timestamp?: string | number;
  sessionId?: string;
  visibility?: "visible" | "hidden";
  focused?: boolean;
  active?: boolean;
  reason?: string;
  remoteAddress?: string;
}

const EVENT_LABELS: Record<string, string> = {
  login: "LOGIN",
  logout: "LOGOUT",
  "window-focus": "FOCUS",
  "window-blur": "BLUR",
  "visibility-hidden": "TAB HIDDEN",
  "visibility-visible": "TAB VISIBLE",
  disconnect: "DISCONNECT",
};

const pad2 = (value: number): string => String(value).padStart(2, "0");

const formatLogTimestamp = (timestamp?: string | number): string => {
  let date: Date;
  if (typeof timestamp === "number") {
    date = new Date(timestamp < 1_000_000_000_000 ? timestamp * 1000 : timestamp);
  } else if (typeof timestamp === "string" && timestamp) {
    date = new Date(timestamp);
  } else {
    date = new Date();
  }

  if (Number.isNaN(date.getTime())) {
    return String(timestamp ?? new Date().toISOString());
  }

  return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())} ${pad2(date.getHours())}:${pad2(date.getMinutes())}:${pad2(date.getSeconds())}`;
};

const shortSessionId = (sessionId?: string): string => {
  return sessionId ? sessionId.slice(0, 8) : "--------";
};

const describeEvent = (event: NormalizedSessionEvent): string => {
  switch (event.event) {
    case "login":
      return "User signed in";
    case "logout":
      return "User signed out";
    case "window-focus":
      if (event.visibility === "hidden") {
        return "Window focused while tab is hidden (background)";
      }
      return event.active
        ? "Window focused — actively using the editor"
        : "Window focused";
    case "window-blur":
      return event.visibility === "hidden"
        ? "Window unfocused — tab already hidden"
        : "Window unfocused — switched to another application";
    case "visibility-hidden":
      return event.focused
        ? "Browser tab hidden — editor still has window focus"
        : "Browser tab hidden — switched away or minimized";
    case "visibility-visible":
      return event.focused
        ? "Browser tab visible — editor is active"
        : "Browser tab visible — window not focused";
    case "disconnect":
      return event.reason === "pagehide"
        ? "Session ending — page closing or navigating away"
        : "Session ending — browser disconnected";
    default:
      return event.event.replace(/-/g, " ");
  }
};

const formatSessionLogLine = (event: NormalizedSessionEvent): string => {
  const label = (EVENT_LABELS[event.event] || event.event.toUpperCase()).padEnd(
    12,
    " ",
  );
  const session = shortSessionId(event.sessionId);
  const summary = describeEvent(event);
  const client = (event.remoteAddress || "?").replace(/^::ffff:/, "");

  return `${formatLogTimestamp(event.timestamp)}  ${label}  [${session}]  ${summary}  (${client})`;
};

const monitoringEnabled = (): boolean => {
  const value = process.env.CODE_SERVER_SESSION_MONITORING;
  return value === "1" || value === "true" || value === "yes";
};

const clipboardDisabled = (): boolean => {
  const value = process.env.CODE_SERVER_DISABLE_CLIPBOARD;
  return value === "1" || value === "true" || value === "yes";
};

const agentSidebarHidden = (): boolean => {
  const value = process.env.CODE_SERVER_HIDE_AGENT_SIDEBAR;
  return value !== "0" && value !== "false" && value !== "no";
};

const monitoringPath = (args: DefaultedArgs): string => {
  return (
    process.env.CODE_SERVER_SESSION_MONITORING_FILE ||
    path.join(args["user-data-dir"], "session-monitoring.jsonl")
  );
};

export const getMonitoringSessionCookieName = (suffix?: string): string => {
  return suffix
    ? `code-server-session-monitor-${suffix.replace(/[^a-zA-Z0-9-]/g, "-")}`
    : "code-server-session-monitor";
};

const getSessionId = (req: Request): string | undefined => {
  const cookieName = getMonitoringSessionCookieName(req.args["cookie-suffix"]);
  return typeof req.cookies?.[cookieName] === "string"
    ? req.cookies[cookieName]
    : undefined;
};

class SessionMonitoringSink {
  private writeQueue: Promise<void> = Promise.resolve();

  public constructor(
    private readonly args: DefaultedArgs,
    private readonly filePath: string,
    public readonly enabled: boolean,
  ) {}

  public createSessionId(): string {
    return randomUUID();
  }

  public getCookieName(): string {
    return getMonitoringSessionCookieName(this.args["cookie-suffix"]);
  }

  public getCookieOptions(req: Request): ReturnType<typeof getCookieOptions> {
    return getCookieOptions(req);
  }

  public record(req: Request, event: SessionEvent): void {
    if (!this.enabled) {
      return;
    }

    const sessionId = event.sessionId || getSessionId(req);
    const remoteAddress =
      event.remoteAddress || req.connection.remoteAddress || undefined;
    const line = `${formatSessionLogLine({
      event: event.event,
      timestamp: event.timestamp,
      sessionId,
      visibility: event.visibility,
      focused: event.focused,
      active: event.active,
      reason: event.reason,
      remoteAddress,
    })}\n`;

    this.writeQueue = this.writeQueue
      .then(async () => {
        await fs.mkdir(path.dirname(this.filePath), { recursive: true });
        await fs.appendFile(this.filePath, line);
      })
      .catch((error) => {
        logger.error("Failed to write session monitoring event", error);
      });
  }

  public async flush(): Promise<void> {
    await this.writeQueue;
  }

  public buildBrowserScriptTag(req: Request): string {
    if (!this.enabled) {
      return "";
    }
    // External same-origin script is allowed by script-src 'self' on all VS Code
    // builds. Inline injection requires a nonce/hash that varies by version.
    const src = replaceTemplates(req, "{{BASE}}/session/monitor.js");
    return `<script defer src="${src}"></script>`;
  }

  public buildBrowserBootstrap(req: Request): string {
    if (!this.enabled) {
      return "";
    }

    const endpoint = replaceTemplates(req, "{{BASE}}/session/event");
    return `(() => {
  const endpoint = new URL(${JSON.stringify(endpoint)}, window.location.href).href;
  let lastVisibility;
  let lastFocused;

  const currentState = () => ({
    visibility: document.hidden ? "hidden" : "visible",
    focused: document.hasFocus(),
  });

  const send = (event, extras = {}) => {
    const state = currentState();
    const payload = JSON.stringify({
      event,
      timestamp: new Date().toISOString(),
      visibility: state.visibility,
      focused: state.focused,
      active: state.visibility === "visible" && state.focused,
      href: window.location.href,
      ...extras,
    });

    fetch(endpoint, {
      body: payload,
      credentials: "include",
      headers: { "content-type": "application/json" },
      keepalive: true,
      method: "POST",
    }).catch(() => {
      try {
        navigator.sendBeacon(
          endpoint,
          new Blob([payload], { type: "application/json" }),
        );
      } catch (e) {
        // Ignore unload-time failures.
      }
    });
  };

  const emitVisibility = () => {
    const state = currentState();
    if (state.visibility === lastVisibility) {
      return;
    }
    lastVisibility = state.visibility;
    send(state.visibility === "hidden" ? "visibility-hidden" : "visibility-visible");
  };

  const emitFocus = () => {
    const state = currentState();
    if (state.focused === lastFocused) {
      return;
    }
    lastFocused = state.focused;
    send(state.focused ? "window-focus" : "window-blur");
  };

  document.addEventListener("visibilitychange", emitVisibility, { passive: true });
  window.addEventListener("focus", emitFocus, { passive: true });
  window.addEventListener("blur", emitFocus, { passive: true });
  window.addEventListener("pagehide", () => send("disconnect", { reason: "pagehide" }), { passive: true });

  const bootstrap = () => {
    emitVisibility();
    emitFocus();
  };
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", bootstrap, { once: true });
  } else if (typeof requestIdleCallback === "function") {
    requestIdleCallback(bootstrap);
  } else {
    setTimeout(bootstrap, 0);
  }
})();`;
  }
}

let sessionMonitoringSink: SessionMonitoringSink | undefined;

export const getSessionMonitoringSink = (
  args: DefaultedArgs,
): SessionMonitoringSink => {
  if (!sessionMonitoringSink) {
    sessionMonitoringSink = new SessionMonitoringSink(
      args,
      monitoringPath(args),
      monitoringEnabled(),
    );
    if (sessionMonitoringSink.enabled) {
      logger.info(`Session monitoring enabled: ${monitoringPath(args)}`);
    }
  }
  return sessionMonitoringSink;
};

export const getSessionMonitoringSessionId = (
  req: Request,
): string | undefined => {
  return getSessionId(req);
};

export const setSessionMonitoringCookie = (
  req: Request,
  res: Response,
  sessionId: string,
): void => {
  res.cookie(
    getMonitoringSessionCookieName(req.args["cookie-suffix"]),
    sessionId,
    getCookieOptions(req),
  );
};

export const clearSessionMonitoringCookie = (
  req: Request,
  res: Response,
): void => {
  res.clearCookie(
    getMonitoringSessionCookieName(req.args["cookie-suffix"]),
    getCookieOptions(req),
  );
};

export const recordSessionEvent = async (
  req: Request,
  event: SessionEvent,
): Promise<void> => {
  getSessionMonitoringSink(req.args).record(req, event);
  await getSessionMonitoringSink(req.args).flush();
};

export const buildSessionMonitoringBootstrap = (req: Request): string => {
  return getSessionMonitoringSink(req.args).buildBrowserScriptTag(req);
};

export const buildClipboardDisableBootstrap = (req: Request): string => {
  if (!clipboardDisabled()) {
    return "";
  }
  const src = replaceTemplates(req, "{{BASE}}/session/clipboard.js");
  return `<script defer src="${src}"></script>`;
};

export const buildInsecureNotificationDismissBootstrap = (req: Request): string => {
  const src = replaceTemplates(req, "{{BASE}}/session/dismiss-insecure.js");
  return `<script defer src="${src}"></script>`;
};

export const buildAgentSidebarHideBootstrap = (req: Request): string => {
  if (!agentSidebarHidden()) {
    return "";
  }
  const src = replaceTemplates(req, "{{BASE}}/session/hide-agent-sidebar.js");
  return `<script defer src="${src}"></script>`;
};

const buildDismissInsecureNotificationScript = (): string => {
  return `(() => {
  const pattern = /insecure context/i;
  const dismiss = () => {
    for (const el of document.querySelectorAll(
      ".notification-list-item-message, .monaco-list-row"
    )) {
      const text = el.textContent || "";
      if (!pattern.test(text)) {
        continue;
      }
      const item = el.closest(".notification-list-item") || el.closest(".monaco-list-row");
      if (!item) {
        continue;
      }
      const understand = Array.from(item.querySelectorAll(".monaco-button")).find(
        (button) => button.textContent?.trim() === "I understand",
      );
      if (understand) {
        understand.click();
        continue;
      }
      item.querySelector(".codicon-close")?.click();
    }
  };
  const start = () => {
    dismiss();
    new MutationObserver(dismiss).observe(document.body, {
      childList: true,
      subtree: true,
    });
    setTimeout(dismiss, 1000);
    setTimeout(dismiss, 3000);
  };
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start, { once: true });
  } else {
    start();
  }
})();`;
};

const buildClipboardDisableScript = (): string => {
  return `(() => {
  const block = (event) => {
    event.preventDefault();
    event.stopImmediatePropagation();
    return false;
  };

  ["copy", "cut", "paste"].forEach((type) => {
    document.addEventListener(type, block, true);
    window.addEventListener(type, block, true);
  });

  // Do not swallow bare Ctrl/Cmd+C — terminals need it for interrupt.
  // Editor/menu copy is blocked via the copy/cut/paste listeners above.
  document.addEventListener("keydown", (event) => {
    const key = event.key.toLowerCase();
    const mod = event.ctrlKey || event.metaKey;
    if (mod && (key === "v" || key === "x")) {
      block(event);
    }
    if (mod && event.shiftKey && (key === "c" || key === "v")) {
      block(event);
    }
    if (event.shiftKey && key === "insert") {
      block(event);
    }
    if (event.ctrlKey && key === "insert") {
      block(event);
    }
  }, true);

  const emptyClipboard = {
    read: async () => [],
    readText: async () => "",
    write: async () => {},
    writeText: async () => {},
  };

  if (navigator.clipboard) {
    try {
      Object.defineProperty(navigator, "clipboard", {
        value: emptyClipboard,
        configurable: false,
      });
    } catch (error) {
      navigator.clipboard.readText = emptyClipboard.readText;
      navigator.clipboard.writeText = emptyClipboard.writeText;
      navigator.clipboard.read = emptyClipboard.read;
      navigator.clipboard.write = emptyClipboard.write;
    }
  }
})();`;
};

const buildAgentSidebarHideScript = (): string => {
  return `(() => {
  const style = document.createElement("style");
  style.textContent = [
    ".monaco-workbench .part.auxiliarybar { display: none !important; }",
    ".monaco-workbench .titlebar-right .action-label[aria-label*='Agents'] { display: none !important; }",
    ".monaco-workbench .titlebar-right .action-label[aria-label*='Chat'] { display: none !important; }",
    ".monaco-workbench .activitybar .action-label[aria-label*='Chat'] { display: none !important; }",
    ".monaco-workbench .activitybar .action-label[aria-label*='Copilot'] { display: none !important; }",
  ].join("\\n");
  document.head.appendChild(style);

  const hideAgentSidebar = () => {
    for (const el of document.querySelectorAll(
      ".monaco-workbench .part.auxiliarybar, .monaco-workbench .auxiliarybar"
    )) {
      el.style.display = "none";
    }
    for (const el of document.querySelectorAll(
      ".monaco-workbench .titlebar-right .action-label, .monaco-workbench .activitybar .action-label"
    )) {
      const label = (el.getAttribute("aria-label") || el.textContent || "").trim();
      if (/\\b(chat|copilot|agents?)\\b/i.test(label)) {
        el.style.display = "none";
      }
    }
    for (const el of document.querySelectorAll(
      ".monaco-workbench .pane-header, .monaco-workbench .composite.title"
    )) {
      const text = (el.textContent || "").trim();
      if (/build with agents?/i.test(text)) {
        const pane = el.closest(".part.auxiliarybar, .pane-composite-part");
        if (pane) {
          pane.style.display = "none";
        }
      }
    }
  };

  const start = () => {
    hideAgentSidebar();
    new MutationObserver(hideAgentSidebar).observe(document.body, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ["class", "style", "aria-label"],
    });
    setTimeout(hideAgentSidebar, 500);
    setTimeout(hideAgentSidebar, 2000);
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start, { once: true });
  } else {
    start();
  }
})();`;
};

const canRecordSessionEvent = async (req: Request): Promise<boolean> => {
  if (await authenticated(req)) {
    return true;
  }
  return !!getSessionId(req);
};

const parseSessionEventBody = (
  req: Request,
): SessionMonitoringRequestBody => {
  const body = req.body;
  if (body && typeof body === "object" && !Array.isArray(body)) {
    return body as SessionMonitoringRequestBody;
  }
  if (typeof body === "string" && body) {
    try {
      return JSON.parse(body) as SessionMonitoringRequestBody;
    } catch {
      return {};
    }
  }
  return {};
};

export const router = Router();

router.get("/monitor.js", (req, res) => {
  const sink = getSessionMonitoringSink(req.args);
  res.setHeader("Content-Type", "application/javascript; charset=utf-8");
  res.setHeader("Cache-Control", "private, max-age=3600");
  if (!sink.enabled) {
    res.status(204).end();
    return;
  }
  res.end(sink.buildBrowserBootstrap(req));
});

router.get("/clipboard.js", (req, res) => {
  res.setHeader("Content-Type", "application/javascript; charset=utf-8");
  res.setHeader("Cache-Control", "private, max-age=3600");
  if (!clipboardDisabled()) {
    res.status(204).end();
    return;
  }
  res.end(buildClipboardDisableScript());
});

router.get("/dismiss-insecure.js", (req, res) => {
  res.setHeader("Content-Type", "application/javascript; charset=utf-8");
  res.setHeader("Cache-Control", "private, max-age=3600");
  res.end(buildDismissInsecureNotificationScript());
});

router.get("/hide-agent-sidebar.js", (req, res) => {
  res.setHeader("Content-Type", "application/javascript; charset=utf-8");
  res.setHeader("Cache-Control", "private, max-age=3600");
  if (!agentSidebarHidden()) {
    res.status(204).end();
    return;
  }
  res.end(buildAgentSidebarHideScript());
});

router.post("/event", async (req, res) => {
  const sink = getSessionMonitoringSink(req.args);
  if (!(await canRecordSessionEvent(req))) {
    throw new HttpError("Unauthorized", 401 as HttpCode);
  }

  if (!sink.enabled) {
    res.status(204).end();
    return;
  }

  const body = parseSessionEventBody(req);

  sink.record(req, {
    event: body.event || "visibility",
    timestamp: body.timestamp || new Date().toISOString(),
    visibility: body.visibility,
    focused: typeof body.focused !== "undefined" ? body.focused : undefined,
    active: typeof body.active !== "undefined" ? body.active : undefined,
    href: body.href || req.headers.referer || req.headers.origin,
    reason: body.reason,
  });

  res.status(204).end();
});
