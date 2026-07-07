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
HOME_DIR="/home/$USERNAME"

# reset user's container
docker rm -f "code-$USERNAME" 2>/dev/null

if [[ "$CLEAN" == "clean" ]]; then
  # full reset: wipe the user's entire environment and code
  echo "Resetting entire environment for $USERNAME..."
  rm -rf "$HOME_DIR"
else
  # keep the rest of the user's environment intact, only reset the code-server config/workspace
  rm -rf "$HOME_DIR/.code-server"
  rm -rf "$HOME_DIR/project"
fi

mkdir -p "$HOME_DIR/project"
mkdir -p "$LOG_DIR"
touch "$LOG_DIR/$USERNAME-session-monitoring.jsonl"
chown -R 1000:1000 "$LOG_DIR"

# copy starter files
cp -r starter/proxylab/proxylab-handout/ "$HOME_DIR/"

# If the user has a saved starter file, use it in place of the default proxy.c
STUDENT_CODE="$SCRIPT_DIR/student-code/$USERNAME.c"
if [[ -f "$STUDENT_CODE" ]]; then
  echo "Loading starter code for $USERNAME from $STUDENT_CODE"
  cp "$STUDENT_CODE" "$HOME_DIR/proxylab-handout/proxy.c"
fi

chown -R 1000:1000 "$HOME_DIR"

docker run -d \
  --name "code-$USERNAME" \
  --restart unless-stopped \
  -e PASSWORD="$PASSWORD" \
  -e CODE_SERVER_SESSION_MONITORING=1 \
  -e CODE_SERVER_SESSION_MONITORING_FILE="/var/log/code-server/$USERNAME-session-monitoring.jsonl" \
  -v "$HOME_DIR":/home/coder/ \
  -v "$LOG_DIR":/var/log/code-server \
  -p "$PORT":8080 \
  code-server-image

echo "USERNAME: $USERNAME"
echo "Port: $PORT"
echo "Password: $PASSWORD"
