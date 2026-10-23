import glob
import json
import os
import tempfile
import shutil
import signal
import subprocess

IGNORE = shutil.ignore_patterns(
    "*.o", "*.d", "logs", "results.log", "proxy", "proxy.c",
    "__pycache__", "submissions", "scratch_work", ".DS_Store",
)

def apply_edit(path, edits):
    """Replace one line in `path`. Returns the original file text so you can revert."""
    for edit in edits:
        n = edit["line"]                                   # 1-based, absolute

        with open(path, "r", newline="") as f:             # newline="" keeps \r\n / \n as-is
            original = f.read()
        lines = original.splitlines(keepends=True)

        if not 1 <= n <= len(lines):
            raise ValueError(f"line {n} is out of range for {path} ({len(lines)} lines)")

        raw = lines[n - 1]
        body = raw.rstrip("\r\n")
        eol = raw[len(body):]                              # preserve this line's own ending

        if body != edit["old_line"]:
            raise ValueError(
                f"{path}:{n} no longer matches old_line (file changed since chunking?)\n"
                f"  on disk: {body!r}\n  expected: {edit['old_line']!r}"
            )
        if "\n" in edit["new_line"] or "\r" in edit["new_line"]:
            raise ValueError("new_line contains a real newline; must be a single line")

        lines[n - 1] = edit["new_line"] + eol

    # atomic write: temp file in the same dir, then replace
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path) or ".")
    try:
        with os.fdopen(fd, "w", newline="") as f:
            f.write("".join(lines))
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise
    return original


def builds(file, timeout=120):
    repo_dir = "starter/proxylab-handout/submissions/"
    r = subprocess.run(
        ["make", "-B", "-C", repo_dir, "CC=gcc -Werror", file],
        capture_output=True, text=True, timeout=timeout,
    )
    return r.returncode == 0, r.stderr

def apply_patch(path, edit):
    original = apply_edit(path, edit)
    ok, err = builds(path.name)
    if not ok:
        with open(path, "w", newline="") as f:
            f.write(original)
        raise ValueError(f"mutation doesn't compile:\n{err}")
    

def make_workspace(handout_dir, src_path, scratch_root):
    """Copy the handout into scratch_root/<student id> and drop the student's file in as proxy.c."""
    handout_dir, src_path, scratch_root = map(os.path.abspath, (handout_dir, src_path, scratch_root))
    stem = os.path.splitext(os.path.basename(src_path))[0]
    ws = os.path.join(scratch_root, stem)
    shutil.rmtree(ws, ignore_errors=True)                       # fresh every time
    shutil.copytree(handout_dir, ws, copy_function=shutil.copy2, ignore=IGNORE)
    shutil.copy2(src_path, os.path.join(ws, "proxy.c"))
    return ws, stem


def per_test(ws):
    results = {}
    for path in sorted(glob.glob(os.path.join(ws, "logs", "*.log"))):
        with open(path, "rb") as f:
            lines = [l for l in f.read().splitlines() if l.strip()]
        last = lines[-1].decode(errors="replace") if lines else ""
        results[os.path.basename(path)] = (last == "ALL TESTS PASSED")
    return results


def run_tests(ws, timeout=1800):
    """Run driver.sh inside workspace `ws`.
    Returns {"timed_out": bool, "summary": dict|None, "tests": {log: passed}}."""
    ws = os.path.abspath(ws)

    # fresh logs so nothing stale gets scored
    shutil.rmtree(os.path.join(ws, "logs"), ignore_errors=True)
    try:
        os.remove(os.path.join(ws, "results.log"))
    except FileNotFoundError:
        pass

    p = subprocess.Popen(
        [os.path.join(ws, "driver.sh"), "proxy"], cwd=ws, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)       # kills pxyregress and any proxy it spawned
        p.communicate()
        return {"timed_out": True, "summary": None, "tests": per_test(ws)}

    tests = per_test(ws)
    if not tests:
        raise RuntimeError(f"no logs produced in {ws}; harness failed, not a mutation result\n{out[-2000:]}")

    lines = [l for l in out.splitlines() if l.strip()]
    try:
        summary = json.loads(lines[-1])
    except (IndexError, json.JSONDecodeError):
        summary = None
    return {"timed_out": False, "summary": summary, "tests": tests}


def builds(ws, timeout=120):
    ws = os.path.abspath(ws)
    r = subprocess.run(
        ["make", "-B", "-C", ws, "CC=gcc -Werror", "proxy"],
        capture_output=True, text=True, timeout=timeout,
    )
    return r.returncode == 0, r.stderr


def mutate_and_test(handout_dir, src_path, scratch_root, edit):
    ws, stem = make_workspace(handout_dir, src_path, scratch_root)
    target = os.path.join(ws, "proxy.c")

    apply_edit(target, edit)                   # edits the workspace copy only
    ok, err = builds(ws)
    if not ok:
        return {"student": stem, "built": False, "build_error": err}

    result = run_tests(ws)
    result.update({"student": stem, "built": True})
    return result