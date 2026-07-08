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
  timestamp: string;
  sessionId?: string;
  visibility?: "visible" | "hidden";
  focused?: boolean;
  active?: boolean;
  href?: string;
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
}

const monitoringEnabled = (): boolean => {
  const value = process.env.CODE_SERVER_SESSION_MONITORING;
  return value === "1" || value === "true" || value === "yes";
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

const getRequestContext = (
  req: Request,
): Pick<
  SessionEvent,
  "userAgent" | "xForwardedFor" | "remoteAddress" | "href"
> => {
  return {
    href: req.headers.referer || req.headers.origin,
    userAgent: req.headers["user-agent"],
    xForwardedFor: req.headers["x-forwarded-for"],
    remoteAddress: req.connection.remoteAddress,
  };
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

  public async record(req: Request, event: SessionEvent): Promise<void> {
    if (!this.enabled) {
      return;
    }

    const line = `${JSON.stringify({
      schemaVersion: 1,
      ...event,
      sessionId: event.sessionId || getSessionId(req),
      ...getRequestContext(req),
    })}\n`;

    this.writeQueue = this.writeQueue
      .then(async () => {
        await fs.mkdir(path.dirname(this.filePath), { recursive: true });
        await fs.appendFile(this.filePath, line);
      })
      .catch((error) => {
        logger.error("Failed to write session monitoring event", error);
      });

    await this.writeQueue;
  }

  public buildBrowserScriptTag(req: Request): string {
    if (!this.enabled) {
      return "";
    }
    // External same-origin script is allowed by script-src 'self' on all VS Code
    // builds. Inline injection requires a nonce/hash that varies by version.
    const src = replaceTemplates(req, "{{BASE}}/session/monitor.js");
    return `<script src="${src}"></script>`;
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
      timestamp: new Date().toString(),
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

  emitVisibility();
  emitFocus();
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
  await getSessionMonitoringSink(req.args).record(req, event);
};

export const buildSessionMonitoringBootstrap = (req: Request): string => {
  return getSessionMonitoringSink(req.args).buildBrowserScriptTag(req);
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
  res.setHeader("Cache-Control", "no-store");
  if (!sink.enabled) {
    res.status(204).end();
    return;
  }
  res.end(sink.buildBrowserBootstrap(req));
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

  await sink.record(req, {
    event: body.event || "visibility",
    timestamp: body.timestamp || new Date().toString(),
    visibility: body.visibility,
    focused: typeof body.focused !== "undefined" ? body.focused : undefined,
    active: typeof body.active !== "undefined" ? body.active : undefined,
    href: body.href || req.headers.referer || req.headers.origin,
  });

  res.status(204).end();
});
