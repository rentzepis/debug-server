# Debug Server

## Features

### Session monitoring

Each student container records browser window and tab activity to a per-user log file on
the host. This tracks when students sign in, focus or leave the editor, switch browser
tabs, or close the page — not VS Code editor tab switches inside the IDE.

Logs are written to `logs/<username>-session-monitoring.jsonl` and are enabled
automatically by `create_codeserver.sh`. Tail a student's log in real time:

```bash
tail -f logs/alice-session-monitoring.jsonl
```

Example lines:

```
2026-07-08 17:33:22  LOGIN        [8ff0ae27]  User signed in  (172.17.0.1)
2026-07-08 17:33:22  FOCUS        [8ff0ae27]  Window focused — actively using the editor  (172.17.0.1)
2026-07-08 17:33:37  BLUR         [8ff0ae27]  Window unfocused — switched to another application  (172.17.0.1)
2026-07-08 17:36:18  TAB HIDDEN   [8ff0ae27]  Browser tab hidden — switched away or minimized  (172.17.0.1)
2026-07-08 17:37:46  TAB VISIBLE  [8ff0ae27]  Browser tab visible — editor is active  (172.17.0.1)
2026-07-08 17:37:47  DISCONNECT   [8ff0ae27]  Session ending — page closing or navigating away  (172.17.0.1)
2026-07-08 17:37:51  LOGOUT       [dcc46225]  User signed out  (172.17.0.1)
```

| Label | Meaning |
|-------|---------|
| `LOGIN` | Student authenticated (via gateway Google SSO) |
| `LOGOUT` | Student signed out |
| `FOCUS` | Browser window gained focus |
| `BLUR` | Browser window lost focus (another app focused) |
| `TAB HIDDEN` | Student switched away from the code-server browser tab |
| `TAB VISIBLE` | Student returned to the code-server browser tab |
| `DISCONNECT` | Page closing or navigating away |

To rebuild only code-server sources after changes (without a full VS Code build):

```bash
cd code-server-image
./build.sh --fast
```

This recompiles everything under `code-server/src/` (routes, HTTP layer, login pages,
service worker, i18n, etc.) on top of the existing image. It does **not** re-download
or rebuild VS Code.

Equivalent:

```bash
docker build -f Dockerfile.routes -t code-server-image .
```

Then recreate the student's container with `create_codeserver.sh`.

The insecure-context warning is suppressed automatically (via an injected dismiss script on
current images, and by removing the upstream notification patch in future full builds).

## Setup

To build the Docker image, run:

```bash
cd code-server-image
./build.sh
```

For code-server-only changes (see [Session monitoring](#session-monitoring)), use the fast rebuild:

```bash
./build.sh --fast
```

## Google SSO setup

Students sign in with their `@andrew.cmu.edu` Google account. Only Andrew IDs that you
have provisioned with `create_codeserver.sh` can open a workspace (that file is the
allowlist). code-server password auth is disabled.

### 1. Create a Google OAuth client

1. Open [Google Cloud Console](https://console.cloud.google.com/) → **APIs & Services** → **Credentials**
2. **Create credentials** → **OAuth client ID** → Application type **Web application**
3. Add **Authorized redirect URIs**:
   - `https://213-debug.com/auth/google/callback` (production via Cloudflare)
   - `http://localhost/auth/google/callback` (local testing with port 80)
4. Copy the client ID and client secret

Configure the OAuth consent screen for your project (External or Internal). Students
must be able to sign in with Google Workspace accounts on `andrew.cmu.edu`.

### 2. Store credentials

```bash
cp gateway/.env.example gateway/.env
# edit gateway/.env and set GOOGLE_CLIENT_ID and GOOGLE_CLIENT_SECRET
```

`gateway/.env` is gitignored. Optional: `GOOGLE_ALLOWED_DOMAIN=andrew.cmu.edu` (default).

### 3. Start the gateway

```bash
./start-gateway.sh
```

The gateway refuses to start if Google credentials are missing.

## Create per-user environment

```bash
./create_codeserver.sh <ANDREW_ID> [clean]
```

Use the student's Andrew ID as the username (e.g. `jsmith` for `jsmith@andrew.cmu.edu`).
This (re)creates their container with `auth: none` and registers them in
`gateway/users.json` (the SSO allowlist). No host port is published — students reach the
workspace only through the gateway on the Docker network.

- Without `clean`, only the code-server config is reset; the user's home directory is kept.
- With `clean`, the entire user environment is wiped and recreated from starter files.
- Containers use `--restart unless-stopped` and come back automatically after a host reboot.
- Containers join `debug-server-net` so the gateway can proxy `https://<andrewid>.213-debug.com/`.
- Session monitoring is enabled by default; logs go to `logs/<andrewid>-session-monitoring.jsonl`
  (see [Session monitoring](#session-monitoring)).
- A bash terminal opens automatically as an editor tab when the workspace loads.
- **Recreate existing containers** after this change (or after enabling SSO) so they have
  `auth: none` and no published host ports.

Example:

```bash
./create_codeserver.sh jsmith
./create_codeserver.sh ada
./create_codeserver.sh jsmith clean   # full reset for jsmith
```

## Gateway login (Google SSO)

Start the shared login screen on port 80 (or override with `GATEWAY_PORT`):

```bash
./start-gateway.sh
```

The public domain is read from `gateway/domain` (currently `213-debug.com`), or from
`PUBLIC_BASE_DOMAIN` if set in the environment.

Student flow:

1. Open `https://213-debug.com/` (or `http://<SERVER_IP>/` on LAN)
2. **Sign in with Google** using `@andrew.cmu.edu`
3. Land in their own workspace (`https://<andrewid>.213-debug.com/` publicly, or the
   gateway apex on LAN)

Rules enforced by the gateway:

- Google ID token must have hosted domain `andrew.cmu.edu` and a verified email
- Email local-part (Andrew ID) must exist in `gateway/users.json`
- Session cookie is bound to that Andrew ID — students cannot open someone else's workspace

Google’s `hd` UI hint is not enough on its own; the gateway verifies the ID token claims.

## Public access (Cloudflare Tunnel)

Use a tunnel so the laptop does not need a public IP or port-forwarding. The site is
only reachable while the laptop is awake and `cloudflared` is running.

### 1. Set the domain

`gateway/domain` should contain your apex domain:

```
213-debug.com
```

Rebuild/restart the gateway after changing it:

```bash
./start-gateway.sh
```

### 2. Create a tunnel in Cloudflare

1. [Cloudflare Dashboard](https://one.dash.cloudflare.com/) → **Zero Trust** → **Networks** → **Tunnels**
2. **Create a tunnel** → **Cloudflared** → name it (e.g. `debug-laptop`)
3. Install with the token command Cloudflare shows (runs as a service on the laptop)

### 3. Add public hostnames

In the tunnel’s **Public Hostname** tab, add:

| Subdomain | Domain | Type | URL |
|-----------|--------|------|-----|
| *(empty)* | `213-debug.com` | HTTP | `http://localhost:80` |
| `*` | `213-debug.com` | HTTP | `http://localhost:80` |

Cloudflare auto-creates DNS for the apex hostname, but **not** for the wildcard
(you’ll see a yellow warning). Add it yourself:

1. **DNS** → **Records** → **Add record**
2. Type **CNAME**, Name `*`, Proxy **on** (orange cloud)
3. Target = the same `….cfargotunnel.com` value used by the existing `213-debug.com`
   CNAME (visible in **DNS → Records**, or under **Zero Trust → Tunnels → your tunnel**)

Confirm resolution before testing login:

```bash
dig +short user1.213-debug.com   # should return Cloudflare IPs, not NXDOMAIN
```

See `cloudflare-tunnel.example.yml` if you prefer a config-file tunnel instead of the
token installer.

### 4. Create students and share URLs

```bash
./create_codeserver.sh jsmith
```

Share:

- Gateway: `https://213-debug.com/` (Sign in with Google)
- Workspace (after SSO): `https://jsmith.213-debug.com/`

No password — Google SSO is the only login.

### Notes

- Keep `./start-gateway.sh` running (port 80) whenever you want the public site up.
- After changing the gateway code or `gateway/domain`, rerun `./start-gateway.sh`.
- Student Andrew IDs become subdomains — use letters, numbers, and hyphens only.
- Existing student containers must be on `debug-server-net` (re-run `create_codeserver.sh`)
  for public subdomain proxying to work.
- Prefer Cloudflare HTTPS for Google OAuth; raw LAN IPs are awkward as Google redirect URIs.
- When you move off the laptop later, install the same tunnel token on the always-on host.

### Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Firefox can’t connect to … user1.213-debug.com` | Wildcard DNS missing — add the `*` CNAME (step 3) |
| `dig` shows `NXDOMAIN` for a student subdomain | Same as above |
| Gateway fails to start | Set `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` in `gateway/.env` |
| “Not enrolled” after Google sign-in | Run `create_codeserver.sh <andrewid>` for that student |
| Sign-in fails with redirect URI mismatch | Add the exact callback URL in Google Cloud Console |
| Apex works, subdomain asks to sign in again | Cookie domain / HTTPS — ensure tunnel terminates TLS and gateway was rebuilt |
## LAN hosting

Student containers are not published on the host network. Classroom access goes through
the gateway on port 80, which enforces Google SSO and proxies each signed-in user to
their container on the Docker network.

Google OAuth works best over HTTPS (Cloudflare Tunnel). For a LAN-only host, register
`http://localhost/auth/google/callback` (or the LAN URL if Google allows it) as a
redirect URI, or use the Cloudflare tunnel from the classroom network.

### Host requirements

- A Linux machine on the same network as students (classroom server, lab PC, etc.)
- Docker installed and running
- Google OAuth credentials in `gateway/.env` (see [Google SSO setup](#google-sso-setup))
- Enough RAM and disk for one shared image (~2 GB) plus per-student home dirs under `/home/<andrewid>`

### Find the host LAN IP

On the host machine:

```bash
hostname -I | awk '{print $1}'    # first IPv4 address
# or
ip -4 addr show scope global
```

Use the address on the **LAN interface** (e.g. `192.168.1.50`), not `127.0.0.1` and
not Docker bridge IPs like `172.17.0.1`.

### Give students access

After running `create_codeserver.sh` and `./start-gateway.sh`, share:

- Gateway URL: `http://<SERVER_IP>/` (Sign in with Google)

There are no per-student host ports to share.

### Firewall

If `ufw` or a similar firewall is enabled, allow the gateway only:

```bash
sudo ufw allow 80/tcp
```

### Stay LAN-only

- Do **not** set up router port-forwarding.
- Auth is Google SSO via the gateway (`auth: none` inside each container).
- Prefer Cloudflare HTTPS when possible so Google redirect URIs stay simple.

### Troubleshooting

| Symptom | Check |
|---------|-------|
| Works on host, not from other devices | Wrong IP (used localhost or a Docker bridge IP); firewall blocking port 80 |
| Connection refused | Gateway not running: `docker ps`; restart with `./start-gateway.sh` |
| Page loads but IDE is broken | Students should use `http://` on LAN, or `https://` via Cloudflare |
| Google redirect_uri_mismatch | Register the exact callback URL students hit in Google Cloud Console |

To confirm a student container has no published host ports:

```bash
docker port code-<ANDREW_ID>
# should print nothing
```

### Verify access

1. On the host: `./create_codeserver.sh testuser`
2. Ensure `gateway/.env` has Google credentials; run `./start-gateway.sh`
3. From a browser, open the gateway URL and sign in with a provisioned `@andrew.cmu.edu` account

## Notes

Starter code is currently compiled for ARM machines.
