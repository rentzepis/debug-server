#!/bin/bash
# Run prescreen.py inside the same Docker container used for evaluation.
# Usage: bash prescreen.sh

#!/bin/bash
# Run confirm_compilation.py inside the Docker container used for evaluation.
# Usage: bash prescreen.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

docker run --rm \
  -v "$PROJECT_DIR/starter:/work/starter" \
  -v "$SCRIPT_DIR:/work/public_code_cleaning" \
  -w /work \
  proxylab-evaluator:latest \
  python3 public_code_cleaning/confirm_compilation.py