import os
import re
import subprocess
import tempfile

def apply_edit(path, edits):
    """Replace specified lines in `path`. Returns original file text for reverting."""
    # Read original file once
    with open(path, "r", newline="") as f:
        original = f.read()

    lines = original.splitlines(keepends=True)

    # Sort edits in reverse line order so line replacements don't scramble indices
    sorted_edits = sorted(edits, key=lambda x: x["line"], reverse=True)

    for edit in sorted_edits:
        n = edit["line"]  # 1-based index

        if not 1 <= n <= len(lines):
            raise ValueError(f"line {n} is out of range for {path} ({len(lines)} lines)")

        raw = lines[n - 1]
        body = raw.rstrip("\r\n")
        eol = raw[len(body):]  # preserve original line ending

        if body.strip() != edit["old_line"].strip(): # changed to remove whitespace
            raise ValueError(
                f"{path}:{n} no longer matches old_line\n"
                f"  on disk:  {body!r}\n"
                f"  expected: {edit['old_line']!r}"
            )
        if "\n" in edit["new_line"] or "\r" in edit["new_line"]:
            raise ValueError("new_line contains a real newline; must be a single line")

        lines[n - 1] = edit["new_line"] + eol

    # Atomic write to temporary file, then replace
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path) or ".")
    try:
        with os.fdopen(fd, "w", newline="") as f:
            f.write("".join(lines))
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise

    return original


# def builds(file, timeout=120):
#     repo_dir = "starter/proxylab-handout/submissions/"
#     r = subprocess.run(
#         ["make", "-B", "-C", repo_dir, "CC=gcc -Werror", file],
#         capture_output=True, text=True, timeout=timeout,
#     )
#     return r.returncode == 0, r.stderr

def collapse_blank_lines(text: str, max_consecutive: int = 1) -> str:
    """
    Collapses multiple consecutive blank/whitespace-only lines down to `max_consecutive`.
    Preserves original line endings (\r\n or \n).
    """
    # Pattern matches 2 or more consecutive whitespace-only lines
    eol = "\r\n" if "\r\n" in text else "\n"
    target_replacement = eol * (max_consecutive + 1)
    
    # Replace sequences of 2 or more empty lines with 1 empty line
    pattern = r'(?:[ \t]*\r?\n){' + str(max_consecutive + 2) + r',}'
    return re.sub(pattern, target_replacement, text)


def remove_excess_blank_lines_from_file(path, max_consecutive=1):
    """Reads file, cleans up excess whitespace lines, and writes back."""
    with open(path, "r", newline="") as f:
        content = f.read()

    cleaned = collapse_blank_lines(content, max_consecutive=max_consecutive)

    if cleaned != content:
        with open(path, "w", newline="") as f:
            f.write(cleaned)


def apply_patch(path, edits):
    """Accepts a single dict or a list of edit dicts."""
    if isinstance(edits, dict):
        edits = [edits]
    original = apply_edit(path, edits)
    remove_excess_blank_lines_from_file(path)
    # ok, err = builds(path.name if hasattr(path, "name") else str(path))
    # if not ok:
    #     with open(path, "w", newline="") as f:
    #         f.write(original)
    #     raise ValueError(f"mutation doesn't compile:\n{err}")