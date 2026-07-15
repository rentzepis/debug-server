const express = require("express");
const fs = require("fs");
const httpProxy = require("http-proxy");

const PORT = Number(process.env.PORT || 8080);
const USERS_FILE = process.env.USERS_FILE || "/data/users.json";
const PUBLIC_BASE_DOMAIN = (process.env.PUBLIC_BASE_DOMAIN || "").trim().toLowerCase();

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

function getPortForUser(users, username) {
  const key = findUsername(users, username);
  if (key == null) {
    return null;
  }
  const entry = users[key];
  if (typeof entry === "number") {
    return entry;
  }
  if (typeof entry === "object" && entry != null && entry.port != null) {
    return Number(entry.port);
  }
  return null;
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

function renderLoginPage({ error } = {}) {
  const errorHtml = error
    ? `<div class="error" role="alert">${escapeHtml(error)}</div>`
    : "";

  return `<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta name="color-scheme" content="light dark" />
    <title>Sign in</title>
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
      .login-form { display: flex; flex-direction: column; gap: 16px; }
      .field { display: flex; gap: 12px; }
      input[type="text"] {
        flex: 1;
        box-sizing: border-box;
        border: 1px solid light-dark(#ccc, #374151);
        border-radius: 5px;
        background: light-dark(#fff, #191a1b);
        color: inherit;
        padding: 16px;
        font-size: 1rem;
      }
      button {
        border: none;
        border-radius: 5px;
        background: light-dark(rgb(87, 114, 245), rgb(26, 86, 219));
        color: #fff;
        cursor: pointer;
        font-size: 1rem;
        font-weight: 500;
        padding: 16px 20px;
        white-space: nowrap;
      }
      .error {
        background: light-dark(#fee2e2, #7f1d1d);
        border-radius: 5px;
        color: light-dark(#991b1b, #fecaca);
        padding: 12px 16px;
      }
    </style>
  </head>
  <body>
    <div class="center-container">
      <div class="card-box">
        <div class="header">
          <h1>Welcome</h1>
          <p>Enter your username to open your workspace.</p>
        </div>
        <div class="content">
          <form class="login-form" method="post" action="/login">
            ${errorHtml}
            <div class="field">
              <input
                required
                autofocus
                type="text"
                name="username"
                autocomplete="username"
                placeholder="Username"
              />
              <button type="submit">Continue</button>
            </div>
          </form>
        </div>
      </div>
    </div>
  </body>
</html>`;
}

const app = express();
app.set("trust proxy", true);

// Subdomain → student container (Cloudflare Tunnel path).
// Must run BEFORE body parsers — otherwise POST bodies (e.g. code-server
// password login) are consumed and the proxied request hangs forever.
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

  proxy.web(req, res, { target: workspaceTarget(username) });
});

// Body parser only for apex gateway routes (not proxied subdomains).
app.use(express.urlencoded({ extended: false }));

app.get("/", (_req, res) => {
  res.redirect("/login");
});

app.get("/login", (_req, res) => {
  res.type("html").send(renderLoginPage());
});

app.post("/login", (req, res) => {
  const requested = String(req.body.username || "").trim();
  if (!requested) {
    return res
      .status(400)
      .type("html")
      .send(renderLoginPage({ error: "Please enter a username." }));
  }

  const users = loadUsers();
  const username = findUsername(users, requested);
  const port = username ? getPortForUser(users, username) : null;
  if (!username || port == null || Number.isNaN(port)) {
    return res
      .status(404)
      .type("html")
      .send(renderLoginPage({ error: "Unknown username." }));
  }

  const query = `folder=/home/coder&user=${encodeURIComponent(username)}`;

  if (PUBLIC_BASE_DOMAIN) {
    return res.redirect(302, publicWorkspaceUrl(username, query));
  }

  const hostname = req.hostname;
  const target = `${req.protocol}://${hostname}:${port}/?${query}`;
  res.redirect(302, target);
});

const server = app.listen(PORT, "0.0.0.0", () => {
  const mode = PUBLIC_BASE_DOMAIN
    ? `public domain ${PUBLIC_BASE_DOMAIN} (subdomain proxy enabled)`
    : "LAN mode (port redirects)";
  console.log(`gateway listening on ${PORT} — ${mode}`);
});

server.on("upgrade", (req, socket, head) => {
  const host = (req.headers.host || "").split(":")[0];
  const sub = getSubdomainUser(host);
  if (!sub) {
    socket.destroy();
    return;
  }

  const users = loadUsers();
  const username = findUsername(users, sub);
  if (!username) {
    socket.destroy();
    return;
  }

  proxy.ws(req, socket, head, { target: workspaceTarget(username) });
});
