"""
Pre-screen all submissions for clean compilation before bug injection.
Moves failing submissions to submissions/failed_compile/ so they are
excluded from chunking and evaluation.

Reuses the exact same build_proxy_wrapper logic as evaluate.py.

Usage:
    python confirm_compilation.py
"""

import shutil
from pathlib import Path

# Import build logic directly from evaluate so compilation is identical
from evaluate_public import build_proxy_wrapper, ensure_cache_stubs, HANDOUT_DIR, SUBMISSIONS_DIR

FAILED_DIR = SUBMISSIONS_DIR / "failed_compile"
HELPER_FILES = {"csapp.c", "csapp.h", "cache.c", "cache.h", "port-for-user.c"}


def main():
    FAILED_DIR.mkdir(exist_ok=True)
    ensure_cache_stubs()

    submission_files = sorted([
        f for f in SUBMISSIONS_DIR.glob("*.c")
        if f.name not in HELPER_FILES
    ])

    if not submission_files:
        print(f"No submissions found in {SUBMISSIONS_DIR}")
        return

    passed, failed = [], []
    print(f"Screening {len(submission_files)} submissions...\n")

    for sub in submission_files:
        ok = build_proxy_wrapper(sub)
        if ok:
            passed.append(sub.name)
            print(f"  ✓ {sub.name}")
        else:
            failed.append(sub.name)
            print(f"  ✗ {sub.name}")
            shutil.move(str(sub), FAILED_DIR / sub.name)
            repo_sub = SUBMISSIONS_DIR / sub.stem
            if repo_sub.exists():
                shutil.move(str(repo_sub), FAILED_DIR / sub.stem)

    print(f"\n{'='*60}")
    print(f"RESULTS: {len(passed)} passed, {len(failed)} failed")
    print(f"{'='*60}")
    print(f"\nPassed ({len(passed)}):")
    for name in passed:
        print(f"  {name}")
    print(f"\nFailed ({len(failed)}) — moved to {FAILED_DIR.relative_to(Path.cwd())}:")
    for name in failed:
        print(f"  {name}")


if __name__ == "__main__":
    main()