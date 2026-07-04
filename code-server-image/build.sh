#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

IMAGE="${1:-code-server-image}"
shift || true

use_buildkit() {
  docker buildx version >/dev/null 2>&1
}

reclaim_disk() {
  echo "Build failed; reclaiming Docker cache and dangling images..."
  if use_buildkit; then
    docker builder prune -af >/dev/null 2>&1 || true
  else
    docker system prune -af >/dev/null 2>&1 || true
  fi
  docker image prune -af >/dev/null 2>&1 || true
  echo "Disk reclaimed. Retry with: ./build.sh ${IMAGE}"
}

echo "Building ${IMAGE}..."
if use_buildkit; then
  export DOCKER_BUILDKIT=1
  echo "Using BuildKit (buildx available)."
else
  unset DOCKER_BUILDKIT
  echo "Using classic docker build (buildx not available)."
fi

if docker build -t "${IMAGE}" "$@" .; then
  docker image prune -f >/dev/null 2>&1 || true
  echo "Build succeeded."
  exit 0
fi

reclaim_disk
exit 1
