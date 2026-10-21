#!/bin/bash
# run_bug_gen.sh
# Full bug-injection pipeline: download → ingest → prescreen → generate bugs → zip edits
#
# Usage:
#   bash run_bug_gen.sh [local|remote] [conda_env_name] [--setup]
#
# Modes:
#   local   — use small GGUF model (default, for testing on laptop)
#   remote  — use large GGUF model (for GPU on PSC/AWS)
#
# Flags:
#   --setup — create conda env and install dependencies (run once on PSC/AWS)
#
# Examples:
#   bash run_bug_gen.sh local llm-debug
#   bash run_bug_gen.sh remote llm-debug --setup   # first time on PSC
#   bash run_bug_gen.sh remote llm-debug           # subsequent runs

set -euo pipefail

MODE="${1:-local}"
CONDA_ENV="${2:-llm-debug}"
SETUP=false
for arg in "$@"; do
    if [ "$arg" = "--setup" ]; then
        SETUP=true
    fi
done
DATA="public"
SUBMISSIONS_DIR="starter/proxylab-handout/submissions"
PROXY_DOWNLOADS="proxy_downloads"
ZIP_OUT="edits_${DATA}"

# ── Load .env ──────────────────────────────────────────────────────────────────
if [ -f .env ]; then
    export $(grep -v '^#' .env | xargs)
    echo "Loaded .env"
fi

# ── Activate conda env ─────────────────────────────────────────────────────────
if command -v conda &> /dev/null; then
    source "$(conda info --base)/etc/profile.d/conda.sh"

    # First-time setup on remote
    if [ "$SETUP" = true ] && [ "$MODE" = "remote" ]; then
        echo "Setting up conda environment: $CONDA_ENV..."
        module load anaconda3 2>/dev/null || true  # PSC module load, ignore if not available
        conda create -n "$CONDA_ENV" python=3.11 -y
        conda activate "$CONDA_ENV"

        # Install all deps except llama-cpp-python
        if [ -f requirements.txt ]; then
            grep -v "llama.cpp\|llama_cpp" requirements.txt > /tmp/reqs_no_llama.txt
            pip install -r /tmp/reqs_no_llama.txt
        fi

        # Install llama-cpp-python with CUDA support
        echo "Installing llama-cpp-python with CUDA..."
        CMAKE_ARGS="-DGGML_CUDA=on" pip install llama-cpp-python --no-cache-dir

        echo "Environment setup complete."
    else
        conda activate "$CONDA_ENV"
    fi

    echo "Activated conda env: $CONDA_ENV"
    PYTHON="python"
elif [ -n "${VIRTUAL_ENV:-}" ]; then
    # Already inside a venv (common on PSC/AWS)
    PYTHON="python"
    echo "Using active venv: $VIRTUAL_ENV"
else
    PYTHON="python3"
    echo "No conda or venv detected, using: $PYTHON"
fi

# ── Model selection ────────────────────────────────────────────────────────────
if [ "$MODE" = "local" ]; then
    MODEL="unsloth/Qwen3-Coder-30B-A3B-Instruct-GGUF"
    FILENAME="Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf"
else
    MODEL="unsloth/Qwen3-Coder-480B-A30B-Instruct-GGUF"
    FILENAME="Qwen3-Coder-480B-A30B-Instruct-Q4_K_M.gguf"
fi

echo ""
echo "=================================================="
echo "Bug Generation Pipeline"
echo "Mode: $MODE | Model: $FILENAME"
echo "Python: $($PYTHON --version)"
echo "=================================================="

# ── Step 1: Download proxy submissions ─────────────────────────────────────────
echo ""
echo "[1/5] Downloading proxy submissions..."
$PYTHON public_code_cleaning/get_public_proxies.py

# ── Step 2: Ingest into submissions dir ────────────────────────────────────────
echo ""
echo "[2/5] Ingesting into submissions dir..."
$PYTHON - <<PYEOF
import shutil
from pathlib import Path

PROXY_DOWNLOADS = Path("$PROXY_DOWNLOADS")
SUBMISSIONS_DIR = Path("$SUBMISSIONS_DIR")
SUBMISSIONS_DIR.mkdir(exist_ok=True)

copied = 0
for proxy_file in PROXY_DOWNLOADS.rglob("proxy.c"):
    repo_name = proxy_file.parent.name
    dest = SUBMISSIONS_DIR / f"{repo_name}.c"
    shutil.copy(proxy_file, dest)
    copied += 1

    repo_sub = SUBMISSIONS_DIR / repo_name
    repo_sub.mkdir(exist_ok=True)

    for extra in ("cache.c", "cache.h"):
        src = proxy_file.parent / extra
        if src.exists():
            shutil.copy(src, repo_sub / extra)

print(f"Ingested {copied} submissions into {SUBMISSIONS_DIR}")
PYEOF

# ── Step 3: Prescreen ──────────────────────────────────────────────────────────
echo ""
echo "[3/5] Prescreening submissions..."
chmod +x public_code_cleaning/prescreen.sh
bash public_code_cleaning/prescreen.sh

# ── Step 4: Generate bugs for all mutation types ───────────────────────────────
echo ""
echo "[4/5] Generating bugs (all mutation types)..."
$PYTHON - <<PYEOF
from pathlib import Path
from prompt_engineering.bug_step_from_chunk import generate_bugs_from_chunk
from prompt_engineering.utils import MutationType, load_model

MODEL    = "$MODEL"
FILENAME = "$FILENAME"
DATA     = "$DATA"

HELPER_FILES    = {"csapp.c", "port-for-user.c", "cache.c"}
SUBMISSIONS_DIR = Path("$SUBMISSIONS_DIR").resolve()

print(f"Loading model {FILENAME}...")
llm = load_model(MODEL, FILENAME)

for mutation in MutationType:
    print(f"\n── Mutation: {mutation} ──")
    failed = generate_bugs_from_chunk(llm, SUBMISSIONS_DIR, HELPER_FILES, mutation, DATA)
    print(f"  Failed: {len(failed)}")
PYEOF

# ── Step 5: Zip edits ──────────────────────────────────────────────────────────
echo ""
echo "[5/5] Zipping edits..."
$PYTHON - <<PYEOF
import shutil

data      = "$DATA"
edits_dir = f"edits/{data}"
zip_path  = f"edits_{data}"
shutil.make_archive(zip_path, "zip", edits_dir)
print(f"Zipped edits to {zip_path}.zip")
print(f"To download: scp user@host:\$(pwd)/{zip_path}.zip .")
PYEOF

echo ""
echo "=================================================="
echo "Pipeline complete. Output: ${ZIP_OUT}.zip"
echo "=================================================="