import { Router } from "express";
import { getCookieOptions, redirect } from "../http";
import { sanitizeString } from "../util";
import {
  clearSessionMonitoringCookie,
  getSessionMonitoringSessionId,
  getSessionMonitoringSink,
  recordSessionEvent,
} from "./session";

export const router = Router();

router.get<{}, undefined, undefined, { base?: string; to?: string }>(
  "/",
  async (req, res) => {
    const sessionMonitor = getSessionMonitoringSink(req.args);
    const sessionId = getSessionMonitoringSessionId(req);
    if (sessionMonitor.enabled && sessionId) {
      await recordSessionEvent(req, {
        event: "logout",
        timestamp: new Date().toISOString(),
        sessionId,
        active: false,
      });
    }

    // Must use the *identical* properties used to set the cookie.
    res.clearCookie(req.cookieSessionName, getCookieOptions(req));
    clearSessionMonitoringCookie(req, res);

    const to = sanitizeString(req.query.to) || "/";
    return redirect(req, res, to, {
      to: undefined,
      base: undefined,
      href: undefined,
    });
  },
);
