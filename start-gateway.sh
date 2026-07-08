#!/usr/bin/env bash
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
NETWORK="debug-server-net"
USERS_FILE="$SCRIPT_DIR/gateway/users.json"
SECRET_FILE="$SCRIPT_DIR/gateway/.session_secret"
IMAGE="gateway-image"
GATEWAY_PORT="${GATEWAY_PORT:-80}"

docker network create "$NETWORK" 2>/dev/null || true

mkdir -p "$(dirname "$USERS_FILE")"
if [[ ! -s "$USERS_FILE" ]]; then
  echo '{}' > "$USERS_FILE"
fi

if [[ ! -f "$SECRET_FILE" ]]; then
  openssl rand -hex 32 > "$SECRET_FILE"
fi
SESSION_SECRET=$(cat "$SECRET_FILE")

echo "Building ${IMAGE}..."
docker build -t "${IMAGE}" "$SCRIPT_DIR/gateway"

docker rm -f gateway 2>/dev/null || true

docker run -d \
  --name gateway \
  --restart unless-stopped \
  --network "$NETWORK" \
  -p "${GATEWAY_PORT}:8080" \
  -e SESSION_SECRET="$SESSION_SECRET" \
  -e USERS_FILE=/data/users.json \
  -v "$USERS_FILE:/data/users.json" \
  "${IMAGE}"

LAN_IP=$(hostname -I | awk '{print $1}')
if [[ "$GATEWAY_PORT" == "80" ]]; then
  PUBLIC_URL="http://${LAN_IP}/"
  LOCAL_URL="http://127.0.0.1/"
else
  PUBLIC_URL="http://${LAN_IP}:${GATEWAY_PORT}/"
  LOCAL_URL="http://127.0.0.1:${GATEWAY_PORT}/"
fi

healthy=0
for _ in 1 2 3 4 5; do
  if curl -fsS --max-time 3 "${LOCAL_URL}login" >/dev/null; then
    healthy=1
    break
  fi
  sleep 1
done

if [[ "$healthy" -eq 1 ]]; then
  echo "Gateway is running."
  echo "  URL (LAN):      ${PUBLIC_URL}"
  echo "  URL (local):    ${LOCAL_URL}"
  echo "  Listening port: ${GATEWAY_PORT} (not 8080 — that is a separate service on this host)"
else
  echo "Gateway container started but health check failed on ${LOCAL_URL}" >&2
  docker logs gateway 2>&1 | tail -10 >&2
  exit 1
fi

if [[ "$LAN_IP" == 10.0.2.* ]]; then
  echo ""
  echo "Note: ${LAN_IP} is a QEMU/NAT address and is usually not reachable from your"
  echo "host machine's browser. Use ${LOCAL_URL} from inside this VM, forward port"
  echo "${GATEWAY_PORT} in your SSH/editor port forwarding, or switch the VM to bridged networking."
fi
