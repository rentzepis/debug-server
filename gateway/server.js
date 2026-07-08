const express = require("express");
const fs = require("fs");

const PORT = Number(process.env.PORT || 8080);
const USERS_FILE = process.env.USERS_FILE || "/data/users.json";

function loadUsers() {
  try {
    const raw = fs.readFileSync(USERS_FILE, "utf8");
    return JSON.parse(raw || "{}");
  } catch {
    return {};
  }
}

function getPortForUser(users, username) {
  const entry = users[username];
  if (entry == null) {
    return null;
  }
  if (typeof entry === "number") {
    return entry;
  }
  if (typeof entry === "object" && entry.port != null) {
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
        background: light-dark(rgb(244, 247, 252), #111827);
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
        background: light-dark(rgb(250, 253, 258), #1f2937);
        border-radius: 5px;
        box-shadow: light-dark(rgba(60, 66, 87, 0.12), rgba(10, 10, 10, 0.62)) 0 7px 14px 0,
          rgba(0, 0, 0, 0.12) 0 3px 6px 0;
        max-width: 520px;
        width: 100%;
      }
      .header {
        border-bottom: 1px solid light-dark(#ddd, #111827);
        padding: 30px;
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
      .content { padding: 40px; }
      .login-form { display: flex; flex-direction: column; gap: 16px; }
      .field { display: flex; gap: 12px; }
      input[type="text"] {
        flex: 1;
        box-sizing: border-box;
        border: 1px solid light-dark(#ccc, #374151);
        border-radius: 5px;
        background: light-dark(#fff, #111827);
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
app.use(express.urlencoded({ extended: false }));

app.get("/", (_req, res) => {
  res.redirect("/login");
});

app.get("/login", (_req, res) => {
  res.type("html").send(renderLoginPage());
});

app.post("/login", (req, res) => {
  const username = String(req.body.username || "").trim();
  if (!username) {
    return res
      .status(400)
      .type("html")
      .send(renderLoginPage({ error: "Please enter a username." }));
  }

  const port = getPortForUser(loadUsers(), username);
  if (port == null || Number.isNaN(port)) {
    return res
      .status(404)
      .type("html")
      .send(renderLoginPage({ error: "Unknown username." }));
  }

  const hostname = req.hostname;
  const target = `${req.protocol}://${hostname}:${port}/?folder=/home/coder`;
  res.redirect(302, target);
});

app.listen(PORT, "0.0.0.0", () => {
  console.log(`gateway listening on ${PORT}`);
});
