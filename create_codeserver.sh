#!/bin/bash
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"

usage() {
  echo "Usage: $0 <andrew-id> [--clean|--stop]" >&2
  echo "       $0 --all [--clean|--stop]" >&2
  echo "       $0 --student-code [--clean|--stop]" >&2
  echo "  <andrew-id> must match the student's @andrew.cmu.edu Google account local-part." >&2
  echo "  --all           apply the action to every enrolled user in gateway/users.json." >&2
  echo "  --student-code  apply the action to every Andrew ID with a .c file in student-code/." >&2
  echo "  --clean         wipe the user's home and recreate the container from starter files." >&2
  echo "  --stop          stop and remove the container (home directory is left alone)." >&2
  exit 1
}

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
USERS_FILE="$SCRIPT_DIR/gateway/users.json"
STUDENT_CODE_DIR="$SCRIPT_DIR/student-code"

DO_ALL=0
DO_STUDENT_CODE=0
CLEAN=0
STOP=0
USERNAME=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --all|-a)
      DO_ALL=1
      ;;
    --student-code)
      DO_STUDENT_CODE=1
      ;;
    --clean|-c|clean)
      CLEAN=1
      ;;
    --stop|-s)
      STOP=1
      ;;
    -h|--help)
      usage
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage
      ;;
    *)
      if [[ -n "$USERNAME" ]]; then
        # Back-compat: old calls were <andrew-id> <port> [clean]
        if [[ "$1" =~ ^[0-9]+$ ]]; then
          echo "Warning: port argument is no longer used; containers are reached only via the gateway." >&2
          shift
          continue
        fi
        echo "Unexpected argument: $1" >&2
        usage
      fi
      USERNAME=$1
      ;;
  esac
  shift
done

if [[ "$CLEAN" -eq 1 && "$STOP" -eq 1 ]]; then
  echo "Use either --clean or --stop, not both." >&2
  usage
fi

bulk_modes=$((DO_ALL + DO_STUDENT_CODE))
if [[ "$bulk_modes" -gt 1 ]]; then
  echo "Use either --all or --student-code, not both." >&2
  usage
fi

if [[ "$bulk_modes" -eq 1 && -n "$USERNAME" ]]; then
  echo "Use either a bulk flag (--all/--student-code) or <andrew-id>, not both." >&2
  usage
fi

if [[ "$bulk_modes" -eq 0 && -z "$USERNAME" ]]; then
  usage
fi

enrolled_users() {
  python3 - "$USERS_FILE" <<'PY'
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
}

student_code_users() {
  python3 - "$STUDENT_CODE_DIR" <<'PY'
import sys
from pathlib import Path

root = Path(sys.argv[1])
if not root.is_dir():
    sys.exit(0)
ids = sorted(
    {p.stem for p in root.glob("*.c") if p.is_file()},
    key=str.lower,
)
for andrew_id in ids:
    print(andrew_id)
PY
}

run_bulk() {
  local source_label=$1
  shift
  local -a ALL_USERS=("$@")

  if [[ ${#ALL_USERS[@]} -eq 0 ]]; then
    echo "No users found from $source_label" >&2
    exit 1
  fi

  EXTRA_FLAGS=()
  if [[ "$STOP" -eq 1 ]]; then
    EXTRA_FLAGS+=(--stop)
    echo "Stopping ${#ALL_USERS[@]} user container(s) from $source_label..."
  elif [[ "$CLEAN" -eq 1 ]]; then
    EXTRA_FLAGS+=(--clean)
    echo "Regenerating ${#ALL_USERS[@]} user(s) from $source_label (clean)..."
  else
    echo "Regenerating ${#ALL_USERS[@]} user(s) from $source_label..."
  fi
  failed=0
  for andrew_id in "${ALL_USERS[@]}"; do
    echo ""
    echo "========== $andrew_id =========="
    if ! "$0" "$andrew_id" "${EXTRA_FLAGS[@]}"; then
      echo "FAILED: $andrew_id" >&2
      failed=$((failed + 1))
    fi
  done
  echo ""
  if [[ "$failed" -gt 0 ]]; then
    echo "Finished with $failed failure(s) out of ${#ALL_USERS[@]}." >&2
    exit 1
  fi
  if [[ "$STOP" -eq 1 ]]; then
    echo "Finished stopping ${#ALL_USERS[@]} user container(s)."
  else
    echo "Finished regenerating ${#ALL_USERS[@]} user(s)."
  fi
  exit 0
}

# Apply the same action to every enrolled Andrew ID from gateway/users.json
if [[ "$DO_ALL" -eq 1 ]]; then
  if [[ ! -s "$USERS_FILE" ]]; then
    echo "No enrolled users found in $USERS_FILE" >&2
    exit 1
  fi
  mapfile -t ALL_USERS < <(enrolled_users)
  run_bulk "$USERS_FILE" "${ALL_USERS[@]}"
fi

# Apply the same action to every Andrew ID that has a .c file in student-code/
if [[ "$DO_STUDENT_CODE" -eq 1 ]]; then
  if [[ ! -d "$STUDENT_CODE_DIR" ]]; then
    echo "No student-code directory at $STUDENT_CODE_DIR" >&2
    exit 1
  fi
  mapfile -t ALL_USERS < <(student_code_users)
  run_bulk "$STUDENT_CODE_DIR" "${ALL_USERS[@]}"
fi

LOG_DIR="$SCRIPT_DIR/logs"
KEYSTROKE_DIR="$SCRIPT_DIR/keystrokes"
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

if [[ "$STOP" -eq 1 ]]; then
  if docker rm -f "code-$USERNAME" >/dev/null 2>&1; then
    echo "Stopped and removed container code-$USERNAME"
  else
    echo "No container code-$USERNAME to stop"
  fi
  exit 0
fi

docker network create "$NETWORK" 2>/dev/null || true

# reset this user's container
docker rm -f "code-$USERNAME" 2>/dev/null || true

if [[ "$CLEAN" -eq 1 ]]; then
  # full reset: wipe the user's entire environment and code
  echo "Resetting entire environment for $USERNAME..."
  rm -rf "$HOME_DIR"
  rm -rf "${LOG_DIR:?}/$USERNAME"
  rm -f "$LOG_DIR/$USERNAME-session-monitoring.jsonl"
  rm -rf "${KEYSTROKE_DIR:?}/$USERNAME"
else
  # keep the rest of the user's environment intact, only reset the code-server config/workspace
  rm -rf "$HOME_DIR/.local/share/code-server"
  # drop stale config so auth: none is what code-server picks up next
  rm -f "$HOME_DIR/.config/code-server/config.yaml"
fi

# Session logs: each container mounts just its own folder, so students cannot
# read or rewrite each other's browser-activity logs.
mkdir -p "$LOG_DIR/$USERNAME"
chown root:root "$LOG_DIR"
chmod 755 "$LOG_DIR"
# Move a log from the old flat layout (logs/<user>-session-monitoring.jsonl).
if [[ -f "$LOG_DIR/$USERNAME-session-monitoring.jsonl" && ! -e "$LOG_DIR/$USERNAME/session-monitoring.jsonl" ]]; then
  mv "$LOG_DIR/$USERNAME-session-monitoring.jsonl" "$LOG_DIR/$USERNAME/session-monitoring.jsonl"
fi
touch "$LOG_DIR/$USERNAME/session-monitoring.jsonl"
chown -R 1000:1000 "$LOG_DIR/$USERNAME"

# Terminal keystrokes hold student work: the parent is root-only and each
# container mounts just its own folder.
mkdir -p "$KEYSTROKE_DIR/$USERNAME"
chmod 700 "$KEYSTROKE_DIR"
chown 1000:1000 "$KEYSTROKE_DIR/$USERNAME"

# copy starter files
cp -r starter/proxylab-handout/. "$HOME_DIR/"

STUDENT_CODE="$STUDENT_CODE_DIR/$USERNAME.c"
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
  "editor.selectionClipboard": false,
  "task.allowAutomaticTasks": "on",
  "terminal.integrated.defaultProfile.linux": "bash",
  "terminal.integrated.defaultLocation": "editor",
  "terminal.integrated.allowInUntrustedWorkspace": true,
  "security.workspace.trust.enabled": false,
  "chat.disableAIFeatures": true,
  "workbench.secondarySideBar.defaultVisibility": "hidden",
  "workbench.localHistory.enabled": false,
  "remote.otherPortsAttributes": {
    "onAutoForward": "silent"
  },
  "git.enabled": false,
  "git.autoRepositoryDetection": false,
  "extensions.autoCheckUpdates": false,
  "extensions.autoUpdate": false,
  "update.mode": "none",
  "telemetry.telemetryLevel": "off",
  "typescript.tsserver.enable": false,
  "typescript.validate.enable": false,
  "javascript.validate.enable": false,
  "search.followSymlinks": false,
  "files.watcherExclude": {
    "**/.local/**": true,
    "**/.config/**": true,
    "**/.cache/**": true,
    "**/*.o": true,
    "**/*.so": true,
    "**/proxy": true,
    "**/proxy-ref": true
  },
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
cat > "$CODE_SERVER_USER_DIR/keybindings.json" <<'EOF'
[
  { "key": "ctrl+c", "command": "-editor.action.clipboardCopyAction" },
  { "key": "cmd+c", "command": "-editor.action.clipboardCopyAction" },
  { "key": "ctrl+v", "command": "-editor.action.clipboardPasteAction" },
  { "key": "cmd+v", "command": "-editor.action.clipboardPasteAction" },
  { "key": "ctrl+x", "command": "-editor.action.clipboardCutAction" },
  { "key": "cmd+x", "command": "-editor.action.clipboardCutAction" },
  { "key": "ctrl+shift+c", "command": "-workbench.action.terminal.copySelection" },
  { "key": "cmd+shift+c", "command": "-workbench.action.terminal.copySelection" },
  { "key": "ctrl+shift+v", "command": "-workbench.action.terminal.paste" },
  { "key": "cmd+shift+v", "command": "-workbench.action.terminal.paste" },
  { "key": "shift+insert", "command": "-editor.action.clipboardPasteAction" },
  { "key": "ctrl+insert", "command": "-editor.action.clipboardCopyAction" }
]
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

# Created stopped: gateway-reconciler starts it once the student checks in
# and stops it after they submit. --init lets `docker stop` end it promptly.
if ! docker create \
  --name "code-$USERNAME" \
  --init \
  --network "$NETWORK" \
  --memory=512m \
  --memory-swap=512m \
  --cpus=1.0 \
  --pids-limit=256 \
  --security-opt=no-new-privileges:true \
  --restart unless-stopped \
  -e CODE_SERVER_USERNAME="$USERNAME" \
  -e NODE_OPTIONS="--max-old-space-size=256" \
  -e CODE_SERVER_RECONNECTION_GRACE_TIME=300 \
  -e CODE_SERVER_SESSION_MONITORING=1 \
  -e CODE_SERVER_SESSION_MONITORING_FILE=/var/log/code-server/session-monitoring.jsonl \
  -e CODE_SERVER_KEYSTROKE_LOG_DIR=/var/log/keystrokes \
  -e CODE_SERVER_DISABLE_CLIPBOARD=1 \
  -e CODE_SERVER_HIDE_AGENT_SIDEBAR=1 \
  -e BASH_ENV=/etc/bash.bashrc \
  -v "$SCRIPT_DIR/student-bash.bashrc":/etc/bash.bashrc:ro \
  -v "$HOME_DIR":/home/coder \
  -v "$LOG_DIR/$USERNAME":/var/log/code-server \
  -v "$KEYSTROKE_DIR/$USERNAME":/var/log/keystrokes \
  code-server-image \
  node /opt/code-server/out/node/entry.js --bind-addr 0.0.0.0:8080 --auth none; then
  echo "Failed to create container for $USERNAME" >&2
  exit 1
fi

LAN_IP=$(hostname -I | awk '{print $1}')
echo "USERNAME (Andrew ID): $USERNAME"
echo "Auth: Google SSO via gateway (no password)"
echo "Network: docker-only (no host port published)"
echo "Container: created stopped; starts when the student is checked in"
echo "Gateway URL (LAN): http://${LAN_IP}/"
if [[ -n "$PUBLIC_BASE_DOMAIN" ]]; then
  echo "Public gateway: https://${PUBLIC_BASE_DOMAIN}/"
  echo "Public workspace: https://${USERNAME}.${PUBLIC_BASE_DOMAIN}/"
fi
echo "Note: only students provisioned here (and @andrew.cmu.edu) can sign in."
