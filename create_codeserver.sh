#!/bin/bash
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"

USERNAME=$1
PORT=$2
CLEAN=$3

if [[ -z "$USERNAME" || -z "$PORT" ]]; then
  echo "Usage: $0 <username> <port> [clean]" >&2
  exit 1
fi

PASSWORD=$(openssl rand -base64 16)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LOG_DIR="$SCRIPT_DIR/logs"
USERS_FILE="$SCRIPT_DIR/gateway/users.json"
HOME_DIR="/home/$USERNAME"

# reset this user's container
docker rm -f "code-$USERNAME" 2>/dev/null || true

# free the port if another container (e.g. a previous user) still owns it
for cid in $(docker ps -aq --filter "publish=${PORT}" 2>/dev/null); do
  cname=$(docker inspect -f '{{.Name}}' "$cid" 2>/dev/null | sed 's#^/##')
  echo "Port $PORT is in use by $cname; removing it so $USERNAME can take over..."
  docker rm -f "$cid" 2>/dev/null || true
done

if [[ "$CLEAN" == "clean" ]]; then
  # full reset: wipe the user's entire environment and code
  echo "Resetting entire environment for $USERNAME..."
  rm -rf "$HOME_DIR"
  rm -f "$LOG_DIR/$USERNAME-session-monitoring.jsonl"
else
  # keep the rest of the user's environment intact, only reset the code-server config/workspace
  rm -rf "$HOME_DIR/.local/share/code-server"
  # drop stale password so the new PASSWORD env is what code-server persists next
  rm -f "$HOME_DIR/.config/code-server/config.yaml"
fi

mkdir -p "$LOG_DIR"
touch "$LOG_DIR/$USERNAME-session-monitoring.jsonl"
chown -R 1000:1000 "$LOG_DIR"

# copy starter files
cp -r starter/proxylab-handout/. "$HOME_DIR/"

STUDENT_CODE="$SCRIPT_DIR/student-code/$USERNAME.c"
if [[ -f "$STUDENT_CODE" ]]; then
  cp "$STUDENT_CODE" "$HOME_DIR/proxy.c"
else
  echo "Warning: no student code at $STUDENT_CODE, using starter proxy.c" >&2
fi

mkdir -p "$HOME_DIR/.vscode"
cp "$SCRIPT_DIR/tasks-master.json" "$HOME_DIR/.vscode/tasks.json"
cp "$SCRIPT_DIR/vscode-settings-master.json" "$HOME_DIR/.vscode/settings.json"

CODE_SERVER_USER_DIR="$HOME_DIR/.local/share/code-server/User"
mkdir -p "$CODE_SERVER_USER_DIR"
EXT_DIR="$HOME_DIR/.local/share/code-server/extensions/debug-server.auto-terminal-0.0.1"
mkdir -p "$EXT_DIR"
cp "$SCRIPT_DIR/extensions/auto-terminal/package.json" "$EXT_DIR/"
cp "$SCRIPT_DIR/extensions/auto-terminal/extension.js" "$EXT_DIR/"

cat > "$CODE_SERVER_USER_DIR/settings.json" <<'EOF'
{
  "workbench.colorTheme": "Dark 2026",
  "task.allowAutomaticTasks": "on",
  "terminal.integrated.defaultProfile.linux": "bash",
  "terminal.integrated.defaultLocation": "editor",
  "terminal.integrated.allowInUntrustedWorkspace": true,
  "security.workspace.trust.enabled": false,
  "chat.disableAIFeatures": true,
  "workbench.secondarySideBar.defaultVisibility": "hidden",
  "extensions.allowed": {
    "*": false,
    "debug-server.auto-terminal": true
  }
}
EOF

chown -R 1000:1000 "$HOME_DIR"

mkdir -p "$(dirname "$USERS_FILE")"
python3 - "$USERS_FILE" "$USERNAME" "$PORT" <<'PY'
import json
import sys
from pathlib import Path

users_file = Path(sys.argv[1])
username = sys.argv[2]
port = int(sys.argv[3])

users = {}
if users_file.exists() and users_file.stat().st_size > 0:
    users = json.loads(users_file.read_text())

# drop anyone else previously mapped to this port, then claim it
users = {u: p for u, p in users.items() if p != port and u != username}
users[username] = port
users_file.write_text(json.dumps(users, indent=2) + "\n")
PY

if ! docker run -d \
  --name "code-$USERNAME" \
  --memory=768m \
  --memory-swap=768m \
  --cpus=1.0 \
  --security-opt=no-new-privileges:true \
  --restart unless-stopped \
  -e PASSWORD="$PASSWORD" \
  -e CODE_SERVER_USERNAME="$USERNAME" \
  -e NODE_OPTIONS="--max-old-space-size=384" \
  -e CODE_SERVER_SESSION_MONITORING=1 \
  -e CODE_SERVER_SESSION_MONITORING_FILE="/var/log/code-server/$USERNAME-session-monitoring.jsonl" \
  -e CODE_SERVER_HIDE_AGENT_SIDEBAR=1 \
  -v "$HOME_DIR":/home/coder \
  -v "$LOG_DIR":/var/log/code-server \
  -p "$PORT":8080 \
  code-server-image; then
  echo "Failed to start container for $USERNAME on port $PORT" >&2
  exit 1
fi

LAN_IP=$(hostname -I | awk '{print $1}')
echo "USERNAME: $USERNAME"
echo "Port: $PORT"
echo "Password: $PASSWORD"
echo "Direct URL: http://${LAN_IP}:${PORT}/"
echo "Gateway URL: http://${LAN_IP}/ (enter username on login screen)"
