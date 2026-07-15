#!/bin/bash
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"

usage() {
  echo "Usage: $0 <andrew-id> [clean]" >&2
  echo "       $0 --all [clean]" >&2
  echo "  <andrew-id> must match the student's @andrew.cmu.edu Google account local-part." >&2
  echo "  --all recreates every enrolled user in gateway/users.json." >&2
  exit 1
}

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
USERS_FILE="$SCRIPT_DIR/gateway/users.json"

# Recreate every enrolled Andrew ID from gateway/users.json
if [[ "$1" == "--all" || "$1" == "-a" ]]; then
  CLEAN_ALL="${2:-}"
  if [[ -n "$CLEAN_ALL" && "$CLEAN_ALL" != "clean" ]]; then
    usage
  fi
  if [[ ! -s "$USERS_FILE" ]]; then
    echo "No enrolled users found in $USERS_FILE" >&2
    exit 1
  fi
  mapfile -t ALL_USERS < <(python3 - "$USERS_FILE" <<'PY'
import json
import sys
from pathlib import Path

users = json.loads(Path(sys.argv[1]).read_text() or "{}")
for key, value in sorted(users.items(), key=lambda item: item[0].lower()):
    if isinstance(value, bool):
        if value:
            print(key)
    elif isinstance(value, (int, float)) or value:
        print(key)
PY
)
  if [[ ${#ALL_USERS[@]} -eq 0 ]]; then
    echo "No enrolled users found in $USERS_FILE" >&2
    exit 1
  fi
  echo "Regenerating ${#ALL_USERS[@]} user(s) from $USERS_FILE..."
  failed=0
  for andrew_id in "${ALL_USERS[@]}"; do
    echo ""
    echo "========== $andrew_id =========="
    if ! "$0" "$andrew_id" ${CLEAN_ALL:+"$CLEAN_ALL"}; then
      echo "FAILED: $andrew_id" >&2
      failed=$((failed + 1))
    fi
  done
  echo ""
  if [[ "$failed" -gt 0 ]]; then
    echo "Finished with $failed failure(s) out of ${#ALL_USERS[@]}." >&2
    exit 1
  fi
  echo "Finished regenerating ${#ALL_USERS[@]} user(s)."
  exit 0
fi

USERNAME=$1
CLEAN=$2

if [[ -z "$USERNAME" ]]; then
  usage
fi

# Back-compat: old calls were <andrew-id> <port> [clean]
if [[ "$CLEAN" =~ ^[0-9]+$ ]]; then
  echo "Warning: port argument is no longer used; containers are reached only via the gateway." >&2
  CLEAN=$3
fi

if [[ -n "$CLEAN" && "$CLEAN" != "clean" ]]; then
  usage
fi

LOG_DIR="$SCRIPT_DIR/logs"
DOMAIN_FILE="$SCRIPT_DIR/gateway/domain"
NETWORK="debug-server-net"
HOME_DIR="/home/$USERNAME"

PUBLIC_BASE_DOMAIN="${PUBLIC_BASE_DOMAIN:-}"
if [[ -z "$PUBLIC_BASE_DOMAIN" && -f "$DOMAIN_FILE" ]]; then
  PUBLIC_BASE_DOMAIN=$(tr -d '[:space:]' < "$DOMAIN_FILE")
fi
PUBLIC_BASE_DOMAIN=$(echo "$PUBLIC_BASE_DOMAIN" | tr '[:upper:]' '[:lower:]')

# Subdomains must be a single DNS label
if [[ -n "$PUBLIC_BASE_DOMAIN" && ! "$USERNAME" =~ ^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?$ ]]; then
  echo "Username '$USERNAME' is not valid as a subdomain of ${PUBLIC_BASE_DOMAIN}." >&2
  echo "Use the student's Andrew ID (letters, numbers, and hyphens only)." >&2
  exit 1
fi

docker network create "$NETWORK" 2>/dev/null || true

# reset this user's container
docker rm -f "code-$USERNAME" 2>/dev/null || true

if [[ "$CLEAN" == "clean" ]]; then

  # full reset: wipe the user's entire environment and code
  echo "Resetting entire environment for $USERNAME..."
  rm -rf "$HOME_DIR"
  rm -f "$LOG_DIR/$USERNAME-session-monitoring.jsonl"
else
  # keep the rest of the user's environment intact, only reset the code-server config/workspace
  rm -rf "$HOME_DIR/.local/share/code-server"
  # drop stale config so auth: none is what code-server picks up next
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
  "workbench.localHistory.enabled": false,
  "files.exclude": {
    "**/.local": true,
    "**/.config": true
  },
  "extensions.allowed": {
    "*": false,
    "debug-server.auto-terminal": true
  }
}
EOF

# Gateway Google SSO is the only auth gate; disable code-server passwords.
mkdir -p "$HOME_DIR/.config/code-server"
cat > "$HOME_DIR/.config/code-server/config.yaml" <<'EOF'
bind-addr: 0.0.0.0:8080
auth: none
cert: false
EOF

chown -R 1000:1000 "$HOME_DIR"

mkdir -p "$(dirname "$USERS_FILE")"
python3 - "$USERS_FILE" "$USERNAME" <<'PY'
import json
import sys
from pathlib import Path

users_file = Path(sys.argv[1])
username = sys.argv[2]

users = {}
if users_file.exists() and users_file.stat().st_size > 0:
    users = json.loads(users_file.read_text())

# Normalize legacy username→port maps to an enrollment allowlist.
normalized = {}
for key, value in users.items():
    if isinstance(value, bool):
        if value:
            normalized[key] = True
    elif isinstance(value, (int, float)) or value:
        normalized[key] = True

normalized[username] = True
users_file.write_text(json.dumps(normalized, indent=2, sort_keys=True) + "\n")
PY

if ! docker run -d \
  --name "code-$USERNAME" \
  --network "$NETWORK" \
  --memory=768m \
  --memory-swap=768m \
  --cpus=1.0 \
  --security-opt=no-new-privileges:true \
  --restart unless-stopped \
  -e CODE_SERVER_USERNAME="$USERNAME" \
  -e NODE_OPTIONS="--max-old-space-size=384" \
  -e CODE_SERVER_SESSION_MONITORING=1 \
  -e CODE_SERVER_SESSION_MONITORING_FILE="/var/log/code-server/$USERNAME-session-monitoring.jsonl" \
  -e CODE_SERVER_HIDE_AGENT_SIDEBAR=1 \
  -v "$HOME_DIR":/home/coder \
  -v "$LOG_DIR":/var/log/code-server \
  code-server-image \
  node /opt/code-server/out/node/entry.js --bind-addr 0.0.0.0:8080 --auth none; then
  echo "Failed to start container for $USERNAME" >&2
  exit 1
fi

LAN_IP=$(hostname -I | awk '{print $1}')
echo "USERNAME (Andrew ID): $USERNAME"
echo "Auth: Google SSO via gateway (no password)"
echo "Network: docker-only (no host port published)"
echo "Gateway URL (LAN): http://${LAN_IP}/"
if [[ -n "$PUBLIC_BASE_DOMAIN" ]]; then
  echo "Public gateway: https://${PUBLIC_BASE_DOMAIN}/"
  echo "Public workspace: https://${USERNAME}.${PUBLIC_BASE_DOMAIN}/"
fi
echo "Note: only students provisioned here (and @andrew.cmu.edu) can sign in."
