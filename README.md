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

### Clipboard restrictions

Copy and paste are disabled in student containers by default. The restriction
uses three layers:

1. A browser script injected into the workbench that blocks clipboard events,
   keyboard shortcuts, and `navigator.clipboard` access.
2. VS Code keybinding overrides that unbind editor and terminal copy/paste
   shortcuts.
3. A VS Code clipboard-service patch (via `CODE_SERVER_DISABLE_CLIPBOARD=1`,
   set automatically by `create_codeserver.sh`) that no-ops internal clipboard
   read/write, including menu and command-palette actions.

To disable clipboard restrictions for a container, omit or set
`CODE_SERVER_DISABLE_CLIPBOARD=0` when running the container.

**Rebuild notes:**

- Code-server source changes (`code-server/src/**`): `./build.sh --fast`
- Clipboard-service or VS Code patch changes: full `./build.sh`

Recreate affected student containers after rebuilding.

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
screen at `http://<SERVER_IP>/` and enter their username to be redirected.

- Assign a unique port per student (e.g. 9001–9099).
- Without `clean`, only the code-server config is reset; the user's home directory is kept.
- With `clean`, the entire user environment is wiped and recreated from starter files.
- Containers use `--restart unless-stopped` and come back automatically after a host reboot.
- `create_codeserver.sh` registers each username in `gateway/users.json` for gateway routing.
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

Students visit `http://<SERVER_IP>/`, enter their username, and are redirected to their
assigned port. They still log in to code-server with the password printed by
`create_codeserver.sh`.

The gateway reads username-to-port mappings from `gateway/users.json`, which is updated
automatically when you run `create_codeserver.sh`.

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
