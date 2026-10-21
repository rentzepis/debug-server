"""Step 1: find regex matches in a C source file and cut out a context window.

For every matching line, returns that line plus up to `context` lines before and
after it (clamped at the file boundaries). Each line keeps its ORIGINAL line
number, so step 2's edits can be applied back to the full file.
"""

import random
import re
from prompt_engineering.utils import Chunk, MutationType

# Default patterns per mutation type. Tune these against your own submissions.
MUTATION_PATTERNS = {
    MutationType.PROTOCOL: {
        "regex": r'(?:\b(?:rio_writen|Rio_writen|strcat|snprintf|sprintf|sio_dprintf|strncat|dprintf)\s*\([\s\S]*?,\s*(?:\([^)]+\)\s*)?&?"(?:\\r\\n|\\r\\n\\r\\n|[\s\S]*?close\\r\\n\\r\\n)"\s*[,)]|(?:\([^)]+\)\s*)?&?"(?:\\r\\n|\\r\\n\\r\\n|[\s\S]*?close\\r\\n\\r\\n)"\s*,)', # r'\b(?:rio_writen|strcat|snprintf|sprintf|sio_dprintf|strncat)\s*\([^;"]*,\s*"\\r\\n"\s*[,)]',
        "lo": 4,
        "hi": 10,
    },
    MutationType.ROBUSTIO: {
        "regex": r"\b[Rr]io_readlineb\s*\(",
        "lo": 4,
        "hi": 10,
    },
    MutationType.BUFLEN: {
        "regex": r"\b([A-Za-z_]\w*)\s*=\s*(?:\(\s*)*(?:[Rr]io_(?:readlineb|readnb|readn)|read)\b[\s\S]*?\b[Rr]io_writen\s*\(\s*[^,]+\s*,\s*[^,]+\s*,\s*(?:\([^)]+\)\s*)?\1\s*\)",
        "lo": 4,
        "hi": 10,
    },
    MutationType.PARSING: {
        "regex": r'\bstr(?:n|case)?cmp\s*\([^)]*"\\r\\n"[^)]*\)|\b(\w+)\s*\[\s*0\s*\]\s*==\s*\'\\r\'\s*&&\s*\1\s*\[\s*1\s*\]\s*==\s*\'\\n\'',
        "lo": 4,
        "hi": 4,
    },
}

def _is_comment(line: str) -> bool:
    s = line.strip()
    return s.startswith(("//", "/*", "*"))


def read_lines(path: str) -> list:
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    lines = [l.rstrip("\r") for l in text.split("\n")]
    if lines and lines[-1] == "":  # file ended with a newline
        lines.pop()
    return lines


def find_chunks(
    lines: list,
    pattern: str,
    lo_context: int = 10,
    hi_contex: int = 10,
    flags: int = 0,
    skip_comments: bool = True,
) -> list:
    """Return one Chunk per matching line."""
    rx = re.compile(pattern, flags)
    chunks = []
    for idx, line in enumerate(lines):  # idx is 0-indexed
        if skip_comments and _is_comment(line):
            continue
        if not rx.search(line):
            continue
        lo = max(0, idx - lo_context)
        hi = min(len(lines), idx + hi_contex + 1)  # exclusive
        chunks.append(
            Chunk(
                match_line=idx + 1,
                start_line=lo + 1,
                end_line=hi,
                lines=lines[lo:hi],
            )
        )
    return chunks

def find_chunks_multiline(
    lines: list,
    pattern: str, 
    lo_context: int = 10,
    hi_context: int = 10,
    flags: int = 0,
    skip_comments: bool = True,
) -> list:
    """Return one Chunk per match spanning across lines."""
    # Pattern matching read assignment into a variable, followed by rio_writen using that same variable (\1)
    rx = re.compile(pattern, flags)
    
    # Pre-calculate character start index for each line to convert match.start() back to line numbers
    line_starts = []
    current_pos = 0
    for line in lines:
        line_starts.append(current_pos)
        current_pos += len(line)

    def get_line_idx(char_offset: int) -> int:
        """Binary search or list scan to find 0-indexed line number for a character offset."""
        import bisect
        return bisect.bisect_right(line_starts, char_offset) - 1

    full_text = "".join(lines)
    chunks = []

    for match in rx.finditer(full_text):
        # Use match.end() - 1 to target the rio_writen line instead of the start assignment line
        match_end_pos = max(0, match.end() - 1)
        match_line_idx = get_line_idx(match_end_pos) # 0-indexed line of rio_writen
        
        if skip_comments and _is_comment(lines[match_line_idx]):
            continue

        lo = max(0, match_line_idx - lo_context)
        hi = min(len(lines), match_line_idx + hi_context + 1) # exclusive

        chunks.append(
            Chunk(
                match_line=match_line_idx + 1, # 1-indexed line for rio_writen
                start_line=lo + 1,
                end_line=hi,
                lines=lines[lo:hi],
            )
        )

    return chunks


def pick_chunk(chunks: list, seed=None):
    """Choose one chunk at random (None if there were no matches)."""
    num = random.randint(0, len(chunks)-1)
    return chunks[num]


def run_step1(file, mutation):

    pat = MUTATION_PATTERNS[mutation]["regex"]
    lo_context = MUTATION_PATTERNS[mutation]["lo"]
    hi_context = MUTATION_PATTERNS[mutation]["hi"]
    if mutation == MutationType.BUFLEN:
        found = find_chunks_multiline(read_lines(file), pat, lo_context, hi_context)
    else:
        found = find_chunks(read_lines(file), pat, lo_context, hi_context)

    if found:
        chunk = pick_chunk(found)
        return chunk
    else:
        # print(f"File: {file.name}, No matches found.")
        return None