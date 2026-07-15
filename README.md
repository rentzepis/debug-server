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
| `LOGIN` | Student authenticated to code-server |
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

## Create per-user environment

```bash
./create_codeserver.sh <USERNAME> <PORT> [clean]
```

This (re)creates a container for the user and prints a new password. Students can open
`http://<SERVER_IP>:<PORT>` and log in with that password, or use the gateway login
screen at `http://<SERVER_IP>/` (LAN) or `https://213-debug.com/` (Cloudflare) and
enter their username to be redirected.

- Assign a unique port per student (e.g. 9001–9099).
- Reassigning a port to a different user frees the old container on that port, updates the
  gateway mapping, and prints a new password for the new user.
- Without `clean`, only the code-server config is reset; the user's home directory is kept.
- With `clean`, the entire user environment is wiped and recreated from starter files.
- Containers use `--restart unless-stopped` and come back automatically after a host reboot.
- `create_codeserver.sh` registers each username in `gateway/users.json` for gateway routing.
- Containers join `debug-server-net` so the gateway can proxy `https://<user>.213-debug.com/`.
- Session monitoring is enabled by default; logs go to `logs/<username>-session-monitoring.jsonl`
  (see [Session monitoring](#session-monitoring)).
- A bash terminal opens automatically as an editor tab when the workspace loads. Re-running `create_codeserver.sh`
  deploys the auto-terminal extension, `.vscode/tasks.json`, and settings needed for this behavior
  (workspace trust is disabled so Restricted Mode does not block startup terminals).

Example:

```bash
./create_codeserver.sh alice 9001
./create_codeserver.sh bob   9002
./create_codeserver.sh alice 9001 clean   # full reset for alice
```

## Gateway login

Start the shared login screen on port 80 (or override with `GATEWAY_PORT`):

```bash
./start-gateway.sh
```

The public domain is read from `gateway/domain` (currently `213-debug.com`), or from
`PUBLIC_BASE_DOMAIN` if set in the environment.

- **LAN:** Students visit `http://<SERVER_IP>/`, enter their username, and are redirected
  to `http://<SERVER_IP>:<PORT>/`.
- **Public (Cloudflare):** Students visit `https://213-debug.com/`, enter their username,
  and are redirected to `https://<username>.213-debug.com/`. The gateway reverse-proxies
  that subdomain to the student's container (including WebSockets).

They still log in to code-server with the password printed by `create_codeserver.sh`.

The gateway reads username-to-port mappings from `gateway/users.json`, which is updated
automatically when you run `create_codeserver.sh`.

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
./create_codeserver.sh alice 9001
```

Share:

- Gateway: `https://213-debug.com/`
- Workspace: `https://alice.213-debug.com/`
- Password: printed by the script

### Notes

- Keep `./start-gateway.sh` running (port 80) whenever you want the public site up.
- After changing the gateway code or `gateway/domain`, rerun `./start-gateway.sh`.
- Student usernames become subdomains — use letters, numbers, and hyphens only.
- Existing student containers must be on `debug-server-net` (re-run `create_codeserver.sh`)
  for public subdomain proxying to work.
- LAN URLs (`http://<ip>:<port>`) still work on the classroom network alongside Cloudflare.
- When you move off the laptop later, install the same tunnel token on the always-on host.

### Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Firefox can’t connect to … user1.213-debug.com` | Wildcard DNS missing — add the `*` CNAME (step 3) |
| `dig` shows `NXDOMAIN` for a student subdomain | Same as above |
| Apex works, subdomain hangs on password login | Ensure gateway was rebuilt after the subdomain-proxy fix (`./start-gateway.sh`) |

## LAN hosting

The setup is already LAN-capable: Docker publishes each student's port on all host
interfaces, and code-server listens on `0.0.0.0` inside the container. No extra
networking configuration is required.

### Host requirements

- A Linux machine on the same network as students (classroom server, lab PC, etc.)
- Docker installed and running
- Enough RAM and disk for one shared image (~2 GB) plus per-student home dirs under `/home/<username>`

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

After running `create_codeserver.sh`, share:

- Gateway URL: `http://<SERVER_IP>/` (students enter their username, then log in)
- Direct URL: `http://<SERVER_IP>:9001` (replace with your host IP and the student's port)
- Password: printed by the script

Start the gateway once per host with `./start-gateway.sh`.

### Firewall

If `ufw` or a similar firewall is enabled, allow the student port range from the LAN only:

```bash
sudo ufw allow 80/tcp
sudo ufw allow from 192.168.0.0/16 to any port 9001:9099 proto tcp
```

Adjust the subnet to match your network (e.g. `10.0.0.0/8`).

### Stay LAN-only

- Do **not** set up router port-forwarding for these ports.
- Password auth is enabled by default.
- HTTP on a trusted classroom LAN is fine; HTTPS is optional and adds certificate complexity.

### Troubleshooting

| Symptom | Check |
|---------|-------|
| Works on host, not from other devices | Wrong IP (used localhost or a Docker bridge IP); firewall blocking the port |
| Connection refused | Container not running: `docker ps`; recreate with `create_codeserver.sh` |
| Page loads but IDE is broken | Students should use `http://`, not `https://`, unless TLS is configured |

To confirm a port is published:

```bash
docker port code-<USERNAME>
# should show 0.0.0.0:<PORT>
```

### Verify LAN access

1. On the host: `./create_codeserver.sh testuser 9001`
2. From another device on the same network, open `http://<host-lan-ip>:9001`
3. Log in with the printed password

## Notes

Starter code is currently compiled for ARM machines.
