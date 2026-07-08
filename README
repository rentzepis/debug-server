# Debug Server

## Setup

To build the Docker image, run:

```bash
cd code-server-image
./build.sh
```

## Create per-user environment

```bash
./create_codeserver.sh <USERNAME> <PORT> [clean]
```

This (re)creates a container for the user and prints a new password. Students open
`http://<SERVER_IP>:<PORT>` and log in with that password.

- Assign a unique port per student (e.g. 9001–9099).
- Without `clean`, only the code-server config is reset; the user's home directory is kept.
- With `clean`, the entire user environment is wiped and recreated from starter files.
- Containers use `--restart unless-stopped` and come back automatically after a host reboot.

Example:

```bash
./create_codeserver.sh alice 9001
./create_codeserver.sh bob   9002
./create_codeserver.sh alice 9001 clean   # full reset for alice
```

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

- URL: `http://192.168.1.50:9001` (replace with your host IP and the student's port)
- Password: printed by the script

### Firewall

If `ufw` or a similar firewall is enabled, allow the student port range from the LAN only:

```bash
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
