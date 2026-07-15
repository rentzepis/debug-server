const crypto = require("crypto");
const express = require("express");
const fs = require("fs");
const httpProxy = require("http-proxy");
const cookieSession = require("cookie-session");
const { OAuth2Client } = require("google-auth-library");

const PORT = Number(process.env.PORT || 8080);
const USERS_FILE = process.env.USERS_FILE || "/data/users.json";
const PUBLIC_BASE_DOMAIN = (process.env.PUBLIC_BASE_DOMAIN || "").trim().toLowerCase();
const SESSION_SECRET = process.env.SESSION_SECRET || "";
const GOOGLE_CLIENT_ID = (process.env.GOOGLE_CLIENT_ID || "").trim();
const GOOGLE_CLIENT_SECRET = (process.env.GOOGLE_CLIENT_SECRET || "").trim();
const GOOGLE_ALLOWED_DOMAIN = (
  process.env.GOOGLE_ALLOWED_DOMAIN || "andrew.cmu.edu"
)
  .trim()
  .toLowerCase();

if (!SESSION_SECRET) {
  console.error("SESSION_SECRET is required");
  process.exit(1);
}
if (!GOOGLE_CLIENT_ID || !GOOGLE_CLIENT_SECRET) {
  console.error("GOOGLE_CLIENT_ID and GOOGLE_CLIENT_SECRET are required");
  process.exit(1);
}

const googleClient = new OAuth2Client(GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET);

const proxy = httpProxy.createProxyServer({
  xfwd: true,
  // Keep the browser Host header (alice.example.com) so code-server
  // generates correct absolute URLs behind Cloudflare Tunnel.
  changeOrigin: false,
  ws: true,
});

proxy.on("error", (err, _req, res) => {
  console.error("proxy error:", err.message);
  if (res && !res.headersSent && typeof res.writeHead === "function") {
    res.writeHead(502, { "Content-Type": "text/plain" });
    res.end("Workspace unavailable. Is the student container running?");
  } else if (res && typeof res.destroy === "function") {
    res.destroy();
  }
});

function loadUsers() {
  try {
    const raw = fs.readFileSync(USERS_FILE, "utf8");
    return JSON.parse(raw || "{}");
  } catch {
    return {};
  }
}

function findUsername(users, username) {
  if (!username) {
    return null;
  }
  if (Object.prototype.hasOwnProperty.call(users, username)) {
    return username;
  }
  const lower = username.toLowerCase();
  return Object.keys(users).find((u) => u.toLowerCase() === lower) || null;
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** @returns {string|null} subdomain username, or null for apex / non-public hosts */
function getSubdomainUser(hostname) {
  if (!PUBLIC_BASE_DOMAIN) {
    return null;
  }
  const host = String(hostname || "")
    .toLowerCase()
    .split(":")[0];
  if (!host || host === PUBLIC_BASE_DOMAIN || host === `www.${PUBLIC_BASE_DOMAIN}`) {
    return null;
  }
  const suffix = `.${PUBLIC_BASE_DOMAIN}`;
  if (!host.endsWith(suffix)) {
    return null;
  }
  const sub = host.slice(0, -suffix.length);
  // Only single-label student subdomains (alice.domain.com)
  if (!sub || sub.includes(".")) {
    return null;
  }
  return sub;
}

function workspaceTarget(username) {
  return `http://code-${username}:8080`;
}

function publicWorkspaceUrl(username, query) {
  const qs = query ? `?${query}` : "";
  return `https://${username}.${PUBLIC_BASE_DOMAIN}/${qs}`;
}

function workspaceRedirectUrl(username) {
  const query = `folder=/home/coder&user=${encodeURIComponent(username)}`;
  if (PUBLIC_BASE_DOMAIN) {
    return publicWorkspaceUrl(username, query);
  }
  // code-server cannot be served from a URL subpath; on LAN keep the
  // session on the gateway apex and proxy everything to that user's container.
  return `/?${query}`;
}

function sessionUser(req) {
  const user = req.session && req.session.user;
  return typeof user === "string" && user ? user : null;
}

function usersMatch(a, b) {
  return String(a || "").toLowerCase() === String(b || "").toLowerCase();
}

function requestIsHttps(req) {
  if (req.secure) {
    return true;
  }
  const proto = String(req.headers["x-forwarded-proto"] || "")
    .split(",")[0]
    .trim()
    .toLowerCase();
  return proto === "https";
}

function absoluteUrl(req, pathname) {
  const host = req.get("host");
  const proto = requestIsHttps(req) ? "https" : "http";
  return `${proto}://${host}${pathname}`;
}

function googleRedirectUri(req) {
  return absoluteUrl(req, "/auth/google/callback");
}

function pageShell({ title, heading, bodyHtml }) {
  return `<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta name="color-scheme" content="light dark" />
    <title>${escapeHtml(title)}</title>
    <style>
      :root { color-scheme: light dark; }
      html, body { height: 100%; margin: 0; }
      body {
        background: light-dark(rgb(244, 247, 252), #191a1b);
        color: light-dark(#111, #ddd);
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      }
      .center-container {
        align-items: center;
        box-sizing: border-box;
        display: flex;
        justify-content: center;
        min-height: 100%;
        padding: 20px;
      }
      .card-box {
        background: light-dark(rgb(250, 253, 258), #2C2D2E);
        border-radius: 5px;
        max-width: 520px;
        width: 100%;
      }
      .header {
        padding: 40px 40px 20px 40px;
      }
      .header h1 {
        margin: 0;
        font-size: 1.5rem;
        color: light-dark(#444, #fff);
      }
      .header p {
        margin: 10px 0 0;
        color: light-dark(#555, #9ca3af);
      }
      .content { padding: 20px 40px 40px 40px; }
      .actions { display: flex; flex-direction: column; gap: 16px; }
      a.button, button {
        display: inline-block;
        box-sizing: border-box;
        border: none;
        border-radius: 5px;
        background: light-dark(rgb(87, 114, 245), rgb(26, 86, 219));
        color: #fff;
        cursor: pointer;
        font-size: 1rem;
        font-weight: 500;
        padding: 16px 20px;
        text-align: center;
        text-decoration: none;
        white-space: nowrap;
      }
      .error {
        background: light-dark(#fee2e2, #7f1d1d);
        border-radius: 5px;
        color: light-dark(#991b1b, #fecaca);
        padding: 12px 16px;
        margin-bottom: 16px;
      }
      .hint {
        color: light-dark(#555, #9ca3af);
        font-size: 0.95rem;
        margin: 0;
      }
    </style>
  </head>
  <body>
    <div class="center-container">
      <div class="card-box">
        <div class="header">
          <h1>${escapeHtml(heading)}</h1>
        </div>
        <div class="content">
          ${bodyHtml}
        </div>
      </div>
    </div>
  </body>
</html>`;
}

function renderLoginPage({ error } = {}) {
  const errorHtml = error
    ? `<div class="error" role="alert">${escapeHtml(error)}</div>`
    : "";

  return pageShell({
    title: "Sign in",
    heading: "Welcome",
    bodyHtml: `
      <p class="hint" style="margin: -10px 0 20px;">
        Sign in with your @${escapeHtml(GOOGLE_ALLOWED_DOMAIN)} Google account.
      </p>
      ${errorHtml}
      <div class="actions">
        <a class="button" href="/auth/google">Sign in with Google</a>
      </div>
    `,
  });
}

function renderNotEnrolledPage(andrewId) {
  return pageShell({
    title: "Not enrolled",
    heading: "Not enrolled",
    bodyHtml: `
      <div class="error" role="alert">
        Your Andrew ID (${escapeHtml(andrewId)}) is not enrolled for this course workspace.
        Contact your instructor if you think this is a mistake.
      </div>
      <div class="actions">
        <a class="button" href="/login">Back to sign in</a>
      </div>
    `,
  });
}

function renderForbiddenPage({ message } = {}) {
  return pageShell({
    title: "Access denied",
    heading: "Access denied",
    bodyHtml: `
      <div class="error" role="alert">${escapeHtml(message || "You do not have access to this workspace.")}</div>
      <div class="actions">
        <a class="button" href="/login">Sign in</a>
      </div>
    `,
  });
}

/**
 * Gateway-owned routes that must never be proxied to code-server.
 */
function isGatewayRoute(pathname) {
  return (
    pathname === "/login" ||
    pathname === "/logout" ||
    pathname === "/auth/google" ||
    pathname === "/auth/google/callback" ||
    pathname.startsWith("/auth/google/")
  );
}

function loginUrl(req) {
  if (PUBLIC_BASE_DOMAIN && getSubdomainUser(req.hostname)) {
    return `https://${PUBLIC_BASE_DOMAIN}/login`;
  }
  return "/login";
}

function requireWorkspaceSession(req, res, username) {
  const loggedIn = sessionUser(req);
  if (!loggedIn) {
    if (req.accepts("html")) {
      res.redirect(302, loginUrl(req));
    } else {
      res.status(401).type("text").send("Authentication required");
    }
    return null;
  }
  if (!usersMatch(loggedIn, username)) {
    res
      .status(403)
      .type("html")
      .send(
        renderForbiddenPage({
          message: `Signed in as ${loggedIn}, but this workspace belongs to ${username}.`,
        }),
      );
    return null;
  }
  return loggedIn;
}

function stripCookieHeaderForProxy(req) {
  // Avoid forwarding the gateway session cookie to code-server.
  if (!req.headers.cookie) {
    return;
  }
  const parts = String(req.headers.cookie)
    .split(";")
    .map((c) => c.trim())
    .filter((c) => c && !c.startsWith("session=") && !c.startsWith("session.sig="));
  if (parts.length) {
    req.headers.cookie = parts.join("; ");
  } else {
    delete req.headers.cookie;
  }
}

const app = express();
app.set("trust proxy", true);

const sessionOpts = {
  name: "session",
  keys: [SESSION_SECRET],
  maxAge: 7 * 24 * 60 * 60 * 1000,
  httpOnly: true,
  sameSite: "lax",
  // Secure is set per-request below via cookie options middleware.
};
if (PUBLIC_BASE_DOMAIN) {
  sessionOpts.domain = `.${PUBLIC_BASE_DOMAIN}`;
}

app.use(
  cookieSession({
    ...sessionOpts,
  }),
);

// Mark Secure when the request arrived over HTTPS (e.g. Cloudflare Tunnel).
app.use((req, _res, next) => {
  if (req.sessionOptions) {
    req.sessionOptions.secure = requestIsHttps(req);
  }
  next();
});

// Subdomain → student container (Cloudflare Tunnel path).
// Must run BEFORE body parsers — otherwise POST bodies are consumed and the
// proxied request hangs forever.
app.use((req, res, next) => {
  const sub = getSubdomainUser(req.hostname);
  if (!sub) {
    return next();
  }

  const users = loadUsers();
  const username = findUsername(users, sub);
  if (!username) {
    return res
      .status(404)
      .type("text")
      .send(`Unknown workspace "${sub}".`);
  }

  if (!requireWorkspaceSession(req, res, username)) {
    return;
  }

  stripCookieHeaderForProxy(req);
  proxy.web(req, res, { target: workspaceTarget(username) });
});

// Body parser only for apex gateway routes (not proxied subdomains).
app.use(express.urlencoded({ extended: false }));

app.get("/", (req, res, next) => {
  const user = sessionUser(req);
  if (user) {
    const users = loadUsers();
    const username = findUsername(users, user);
    if (username) {
      if (PUBLIC_BASE_DOMAIN) {
        return res.redirect(302, workspaceRedirectUrl(username));
      }
      // LAN: fall through to the session proxy below.
      return next();
    }
  }
  res.redirect("/login");
});

app.get("/login", (req, res) => {
  const user = sessionUser(req);
  if (user) {
    const users = loadUsers();
    const username = findUsername(users, user);
    if (username) {
      return res.redirect(302, workspaceRedirectUrl(username));
    }
  }
  const error = typeof req.query.error === "string" ? req.query.error : undefined;
  res.type("html").send(renderLoginPage({ error }));
});

app.get("/auth/google", (req, res) => {
  const state = crypto.randomBytes(24).toString("hex");
  req.session.oauthState = state;

  const params = new URLSearchParams({
    client_id: GOOGLE_CLIENT_ID,
    redirect_uri: googleRedirectUri(req),
    response_type: "code",
    scope: "openid email profile",
    state,
    hd: GOOGLE_ALLOWED_DOMAIN,
    prompt: "select_account",
  });

  res.redirect(302, `https://accounts.google.com/o/oauth2/v2/auth?${params}`);
});

app.get("/auth/google/callback", async (req, res) => {
  const { code, state, error } = req.query;

  if (error) {
    return res
      .status(400)
      .type("html")
      .send(renderLoginPage({ error: `Google sign-in was cancelled or failed (${error}).` }));
  }

  if (!code || typeof code !== "string") {
    return res
      .status(400)
      .type("html")
      .send(renderLoginPage({ error: "Missing authorization code from Google." }));
  }

  if (!state || state !== req.session.oauthState) {
    req.session.oauthState = undefined;
    return res
      .status(400)
      .type("html")
      .send(renderLoginPage({ error: "Invalid sign-in state. Please try again." }));
  }
  req.session.oauthState = undefined;

  try {
    googleClient.redirectUri = googleRedirectUri(req);
    const { tokens } = await googleClient.getToken(code);
    if (!tokens.id_token) {
      throw new Error("Google did not return an ID token");
    }

    const ticket = await googleClient.verifyIdToken({
      idToken: tokens.id_token,
      audience: GOOGLE_CLIENT_ID,
    });
    const payload = ticket.getPayload() || {};

    const email = String(payload.email || "")
      .trim()
      .toLowerCase();
    const hd = String(payload.hd || "")
      .trim()
      .toLowerCase();
    const emailVerified = payload.email_verified === true;

    if (!email || !emailVerified) {
      return res
        .status(403)
        .type("html")
        .send(renderLoginPage({ error: "Your Google account email could not be verified." }));
    }

    // hd is authoritative for Workspace domain membership; also require email suffix.
    if (hd !== GOOGLE_ALLOWED_DOMAIN || !email.endsWith(`@${GOOGLE_ALLOWED_DOMAIN}`)) {
      return res
        .status(403)
        .type("html")
        .send(
          renderLoginPage({
            error: `Only @${GOOGLE_ALLOWED_DOMAIN} Google accounts are allowed.`,
          }),
        );
    }

    const andrewId = email.slice(0, -(GOOGLE_ALLOWED_DOMAIN.length + 1));
    if (!andrewId || andrewId.includes("@")) {
      return res
        .status(403)
        .type("html")
        .send(renderLoginPage({ error: "Could not determine your Andrew ID from email." }));
    }

    const users = loadUsers();
    const username = findUsername(users, andrewId);
    if (!username) {
      req.session = null;
      return res.status(403).type("html").send(renderNotEnrolledPage(andrewId));
    }

    req.session.user = username;
    return res.redirect(302, workspaceRedirectUrl(username));
  } catch (err) {
    console.error("Google OAuth callback failed:", err.message);
    return res
      .status(500)
      .type("html")
      .send(renderLoginPage({ error: "Sign-in failed. Please try again." }));
  }
});

app.post("/logout", (req, res) => {
  req.session = null;
  res.redirect(302, "/login");
});

app.get("/logout", (req, res) => {
  req.session = null;
  res.redirect(302, "/login");
});

// LAN mode: after SSO, proxy the gateway apex to the signed-in user's
// container (code-server cannot live under a /w/<user> subpath).
app.use((req, res, next) => {
  if (PUBLIC_BASE_DOMAIN || isGatewayRoute(req.path)) {
    return next();
  }

  const loggedIn = sessionUser(req);
  if (!loggedIn) {
    return next();
  }

  const users = loadUsers();
  const username = findUsername(users, loggedIn);
  if (!username) {
    req.session = null;
    return res.redirect(302, "/login");
  }

  stripCookieHeaderForProxy(req);
  proxy.web(req, res, { target: workspaceTarget(username) });
});

app.use((_req, res) => {
  res.redirect(302, "/login");
});

const server = app.listen(PORT, "0.0.0.0", () => {
  const mode = PUBLIC_BASE_DOMAIN
    ? `public domain ${PUBLIC_BASE_DOMAIN} (subdomain proxy + Google SSO)`
    : "LAN mode (apex session proxy + Google SSO)";
  console.log(`gateway listening on ${PORT} — ${mode}`);
  console.log(`allowed Google domain: @${GOOGLE_ALLOWED_DOMAIN}`);
});

function getSessionFromUpgrade(req) {
  // cookie-session is Express middleware; for raw upgrades, decode the cookie
  // the same way cookie-session would (signed with SESSION_SECRET).
  try {
    const Cookies = require("cookies");
    const Keygrip = require("keygrip");
    const cookies = new Cookies(req, null, { keys: new Keygrip([SESSION_SECRET]) });
    const raw = cookies.get("session", { signed: true });
    if (!raw) {
      return null;
    }
    return JSON.parse(Buffer.from(raw, "base64").toString("utf8"));
  } catch {
    return null;
  }
}

server.on("upgrade", (req, socket, head) => {
  const host = (req.headers.host || "").split(":")[0];
  const sub = getSubdomainUser(host);
  const session = getSessionFromUpgrade(req);
  const loggedIn = session && typeof session.user === "string" ? session.user : null;

  let workspaceUser = null;

  if (sub) {
    workspaceUser = sub;
  } else if (!PUBLIC_BASE_DOMAIN && loggedIn) {
    // LAN: WebSocket upgrades on the gateway apex go to the signed-in workspace.
    workspaceUser = loggedIn;
  }

  if (!workspaceUser) {
    socket.destroy();
    return;
  }

  const users = loadUsers();
  const username = findUsername(users, workspaceUser);
  if (!username) {
    socket.destroy();
    return;
  }

  if (!loggedIn || !usersMatch(loggedIn, username)) {
    socket.destroy();
    return;
  }

  if (req.headers.cookie) {
    const parts = String(req.headers.cookie)
      .split(";")
      .map((c) => c.trim())
      .filter((c) => c && !c.startsWith("session=") && !c.startsWith("session.sig="));
    if (parts.length) {
      req.headers.cookie = parts.join("; ");
    } else {
      delete req.headers.cookie;
    }
  }

  proxy.ws(req, socket, head, { target: workspaceTarget(username) });
});
