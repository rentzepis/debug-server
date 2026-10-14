  # Debug Server

## About

Self-hosted per-student browser IDE for CMU 15-213 / 15-513 (Introduction to Computer Systems), used for proxylab and similar C labs.

Each enrolled user runs in an isolated Docker container (custom code-server). A gateway provides Google Workspace SSO and reverse-proxies to that container. Access is via classroom LAN (port 80) or Cloudflare Tunnel (`213-debug.com`).

Design constraints:


| Area       | Behavior                                                                                     |
| ---------- | -------------------------------------------------------------------------------------------- |
| Isolation  | One container per user; no published host ports; memory/CPU limits; no privilege escalation  |
| Auth       | Google SSO only; code-server `auth: none`; enrollment via `gateway/users.json`               |
| Integrity  | Clipboard disabled by default; AI chat / Copilot and third-party extensions blocked          |
| Monitoring | Per-user browser focus / tab / login logs under `logs/`                                      |
| Check-in   | Workspaces locked until staff check the student in to the active assignment (QR / passcode)  |
| Ops        | Provision, reset, or stop containers with `create_codeserver.sh`; `--restart unless-stopped` |


## Architecture

```
Student browser
    │  https://213-debug.com  |  http://<LAN_IP>/
    ▼
Gateway (Google SSO + reverse proxy)
    │  debug-server-net
    ▼
code-<username>  (auth: none, no host ports)
```


| Path                                       | Description                                                 |
| ------------------------------------------ | ----------------------------------------------------------- |
| `code-server-image/`                       | Custom code-server + VS Code image                          |
| `gateway/`                                 | SSO gateway and subdomain proxy                             |
| `gateway/checkin/`, `gateway/public/`      | Check-in protocol, store, routes and dashboard assets       |
| `gateway/staff.json`                       | Staff allowlist for the instructor dashboard                |
| `gateway/data/checkins.json`               | Assignments, active assignment and check-in records         |
| `create_codeserver.sh`                     | Create / reset / stop student containers; updates allowlist |
| `start-gateway.sh`                         | Build and run the gateway                                   |
| `starter/`                                 | Handout files copied into student homes                     |
| `student-code/<username>.c`                | Optional seed for `proxy.c`                                 |
| `logs/<username>-session-monitoring.jsonl` | Session activity log                                        |


## Requirements

- Linux host with Docker
- Google Cloud OAuth client (Web application)
- Optional: Cloudflare Tunnel for public HTTPS

## Quick start

```bash
cd code-server-image && ./build.sh
cp gateway/.env.example gateway/.env   # set GOOGLE_CLIENT_ID / GOOGLE_CLIENT_SECRET
./start-gateway.sh
./create_codeserver.sh jsmith
```

URLs:

- Public: `https://213-debug.com/`
- LAN: `http://<SERVER_IP>/`

## Build

```bash
cd code-server-image
./build.sh          # full image (VS Code + code-server)
./build.sh --fast   # code-server sources only (Dockerfile.routes)
```

`--fast` does not rebuild VS Code or re-apply VS Code patches.

After rebuilding the image, recreate containers:

```bash
./create_codeserver.sh jsmith
./create_codeserver.sh --all
```


| Change                              | Rebuild                    | Follow-up           |
| ----------------------------------- | -------------------------- | ------------------- |
| `code-server/src/**`                | `./build.sh --fast`        | Recreate containers |
| VS Code patches / clipboard service | `./build.sh`               | Recreate containers |
| Gateway / `gateway/domain`          | `./start-gateway.sh`       | —                   |
| Student home / enrollment           | `./create_codeserver.sh …` | —                   |


Bump `ASSET_COMMIT` in `code-server-image/Dockerfile` when client assets change, otherwise browsers may keep a year-cached bundle.

## Google SSO

Allowed Google Workspace domain defaults to `andrew.cmu.edu` (`GOOGLE_ALLOWED_DOMAIN`). Username is the email local-part (e.g. `jsmith` for `jsmith@andrew.cmu.edu`). Only usernames listed in `gateway/users.json` may open a workspace.

### OAuth client

1. [Google Cloud Console](https://console.cloud.google.com/) → **APIs & Services** → **Credentials**
2. Create **OAuth client ID** (Web application)
3. Redirect URIs:
  - `https://213-debug.com/auth/google/callback`
  - `http://localhost/auth/google/callback`
4. Consent screen must allow accounts on the configured Workspace domain

### Credentials

```bash
cp gateway/.env.example gateway/.env
```

Required: `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET`. File is gitignored. Gateway exits if either is missing.

### Checks

- ID token `hd` matches `GOOGLE_ALLOWED_DOMAIN`; email verified
- Email local-part is enrolled in `gateway/users.json`
- Session cookie is bound to that username

## Student containers

```bash
./create_codeserver.sh <username> [--clean|--stop]
./create_codeserver.sh --all [--clean|--stop]
./create_codeserver.sh --student-code [--clean|--stop]
```


| Flag             | Effect                                                                             |
| ---------------- | ---------------------------------------------------------------------------------- |
| *(default)*      | Recreate container; keep home; reset code-server config/workspace                  |
| `--clean`        | Delete home + session log; recreate from `starter/` (+ `student-code/` if present) |
| `--stop`         | Remove container; keep home                                                        |
| `--all`          | Apply to every username in `gateway/users.json`                                    |
| `--student-code` | Apply to every Andrew ID with a `.c` file in `student-code/`                       |


**Default Configuration:**

- Network: `debug-server-net` (no published ports)
- Limits: 512 MB RAM, 1 CPU
- Restart: `unless-stopped`
- Session monitoring and clipboard restrictions enabled
- Auto-open bash terminal in an editor tab
- Username must be a valid DNS label (subdomain routing)
- Legacy port arguments are ignored

## Cloudflare Tunnel

Public hostname routing requires `cloudflared` running on the host and the gateway listening on port 80.

### Domain

Apex domain is read from `gateway/domain` (default `213-debug.com`), or `PUBLIC_BASE_DOMAIN`. Restart gateway after changes:

```bash
./start-gateway.sh
```

### Setup

1. [Cloudflare Zero Trust](https://one.dash.cloudflare.com/) → **Networks** → **Tunnels**
2. Create a Cloudflared tunnel; install with the provided token
3. Public hostnames → `http://localhost:80`:


| Subdomain | Domain          |
| --------- | --------------- |
| *(empty)* | `213-debug.com` |
| `*`       | `213-debug.com` |


Add a proxied DNS CNAME for the wildcard (not auto-created):

- Name: `*`
- Target: same `….cfargotunnel.com` hostname as the apex record

```bash
dig +short user1.213-debug.com
```

Config-file alternative: `cloudflare-tunnel.example.yml`.

### URLs


| Role      | URL                                 |
| --------- | ----------------------------------- |
| Gateway   | `https://213-debug.com/`            |
| Workspace | `https://<username>.213-debug.com/` |


## LAN

Gateway publishes port 80 (`GATEWAY_PORT` to override). Student containers are reachable only on the Docker network.

```bash
hostname -I | awk '{print $1}'
sudo ufw allow 80/tcp    # if ufw is enabled
docker port code-<username>   # expect empty output
```

Student URL: `http://<SERVER_IP>/`. Prefer Cloudflare for Google OAuth redirect URIs when possible.

## Features

### Session monitoring

Log path: `logs/<username>-session-monitoring.jsonl` (enabled by `create_codeserver.sh`). Tracks browser window/tab events, not VS Code editor tabs.

```bash
tail -f logs/jsmith-session-monitoring.jsonl
```


| Label                        | Meaning                       |
| ---------------------------- | ----------------------------- |
| `LOGIN` / `LOGOUT`           | SSO sign-in / sign-out        |
| `FOCUS` / `BLUR`             | Window focus change           |
| `TAB HIDDEN` / `TAB VISIBLE` | Browser tab visibility        |
| `DISCONNECT`                 | Page close or navigation away |


### Check-in

Students cannot open their workspace until a staff member checks them in to the **active assignment**. With no active assignment every student is locked out (staff are exempt).

**Staff.** List instructor/TA Andrew IDs in `gateway/staff.json` (created as `[]` by `start-gateway.sh`):

```json
["prof1", "ta2"]
```

Staff do not need to be in `users.json`. The file is read on every request; it is bind-mounted as a single file, so edit it in place (or restart the gateway after replacing it).

**Workflow** (`https://213-debug.com/instructor`, or `http://<SERVER_IP>/instructor`):

1. Create an assignment (e.g. `proxylab-checkoff`, optionally with a time limit in minutes) and click **Make active**. Only one assignment is active at a time; older ones remain as attendance history.
2. Students who sign in see `/checkin`: a QR code that rotates every 2 seconds plus a passcode field.
3. Check them in by any of:
   - **Start scanning** and point the camera at the student's screen.
   - **Reveal passcode**: a four-digit code valid for 5 seconds that the student types in.
   - **Check in** next to the student in the roster.
4. **Undo**, **Clear check-ins**, switching the active assignment or **Close session** re-lock the affected students immediately and disconnect open editors.
5. **Export CSV** downloads attendance, deadlines and submission times for the selected assignment.

**Workspace bar.** Checked-in students see a bar above the editor with the assignment title, the time left and a **Submit** button. The gateway injects it into the code-server page, so no image rebuild is needed.

- Each student's timer starts at *their* check-in, and untimed assignments show "No time limit".
- **Submit** asks for confirmation (and warns about unsaved tabs), then closes the workspace.
- When time runs out, the workspace closes on its own at the deadline.
- Either way, the student lands on `/checkin`, which says why their workspace is closed. Saved files stay in their home directory.
- To reopen a student, **Undo** their check-in and check them in again. This starts a fresh timer.

Audit lines (`checkin.*`, never containing codes) go to `docker logs gateway`. Tests: `cd gateway && npm test`.

Limitations:

- The camera needs HTTPS (or `localhost`). On plain-HTTP LAN use the passcode or manual check-in.
- Passcodes live in gateway memory and are lost on restart; QR tokens are signed with `gateway/.checkin_secret` (regenerate it to invalidate all codes).
- Codes do not prove physical presence: a screenshot or relayed passcode works while valid.

### Clipboard

Disabled by default (`CODE_SERVER_DISABLE_CLIPBOARD=1`):

1. Injected script (clipboard events, paste/cut shortcuts, `navigator.clipboard`; Ctrl+C left for SIGINT)
2. VS Code keybinding overrides
3. Clipboard-service patch

Set `CODE_SERVER_DISABLE_CLIPBOARD=0` to allow clipboard for a container.

### Other restrictions

- AI chat / Copilot UI hidden
- Extensions blocked except `debug-server.auto-terminal`
- `su` / `sudo` removed; setuid cleared; `--security-opt=no-new-privileges`
- Insecure-context warning suppressed

## Troubleshooting


| Symptom                          | Action                                                            |
| -------------------------------- | ----------------------------------------------------------------- |
| Gateway fails to start           | Set `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` in `gateway/.env` |
| “Not enrolled”                   | `./create_codeserver.sh <username>`                               |
| Student stuck on check-in page   | Make an assignment active and check them in at `/instructor`      |
| Instructor dashboard redirects   | Add the Andrew ID to `gateway/staff.json`                         |
| `redirect_uri_mismatch`          | Add exact callback URI in Google Cloud Console                    |
| `NXDOMAIN` for `*.213-debug.com` | Add proxied `*` CNAME                                             |
| Subdomain re-prompts login       | Confirm TLS at tunnel; restart gateway                            |
| Reachable on host only           | Check LAN IP / firewall port 80                                   |
| Connection refused               | `docker ps`; `./start-gateway.sh`                                 |
| Stale UI after rebuild           | Bump `ASSET_COMMIT` in Dockerfile                                 |


## Notes

- Default public domain: `gateway/domain` → `213-debug.com`
- Default gateway port: `80` (`GATEWAY_PORT`)
- Default SSO domain: `GOOGLE_ALLOWED_DOMAIN=andrew.cmu.edu`
- Starter binaries under `starter/` are built for ARM

