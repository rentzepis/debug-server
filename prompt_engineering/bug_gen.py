"""Step 2: ask the local model for bug edits. Returns the JSON edits only; nothing is patched.

Input : a Chunk from step1_extract (numbered code) + a mutation type.
Output: a list like [{"line": 219, "old_line": "...", "new_line": "..."}].
        []   -> the model said the mutation doesn't apply to this chunk.
        None -> every attempt was malformed or didn't match the chunk (try another chunk).
"""
import json
import random
import re

from prompt_engineering.utils import MutationType
from prompt_engineering.prompts import main_prompt, few_shot_prompt

def call_model(llm, chunk, mutation: MutationType):
    if mutation is None or type(mutation) != MutationType:
        print(f"Mutation type: {mutation} not found. Randomly selecting...")
        mutation = random.choice(MutationType)
    mutation_prompt = few_shot_prompt[mutation]
    prompt = main_prompt + mutation_prompt + chunk
    messages = [
        {"role": "user", "content": prompt}
    ]
    response = llm.create_chat_completion(
        messages=messages,
        max_tokens=1024,
        temperature=0.1,
        seed=random.randint(0, 2**31 - 1)
    )
    raw = response["choices"][0]["message"]["content"].strip()
    return re.sub(r"^```(?:json)?\s*|\s*```$", "", raw.strip())

def parse_edits(text):
    """Parse the model output into a list of edit dicts, or raise ValueError."""
    text = text.strip()
    text = re.sub(r"^```(?:json)?\s*|\s*```$", "", text)  # defensive; prompt forbids fences
    try:
        edits = json.loads(text)
    except json.JSONDecodeError as e:
        raise ValueError(f"not valid JSON: {e}")
    if not isinstance(edits, list):
        raise ValueError("top-level value is not an array")
    for e in edits:
        if not isinstance(e, dict) or set(e) != {"line", "old_line", "new_line"}:
            raise ValueError(f"bad edit object: {e!r}")
        if not isinstance(e["line"], int) or isinstance(e["line"], bool):
            raise ValueError(f"'line' must be an integer: {e!r}")
        if not isinstance(e["old_line"], str) or not isinstance(e["new_line"], str):
            raise ValueError(f"'old_line'/'new_line' must be strings: {e!r}")
    return edits

def resolve_lines(edits, chunk):
    """Fix up each edit's 'line' by locating old_line in the chunk."""
    for e in edits:
        hits = [chunk.start_line + i
                for i, l in enumerate(chunk.lines) if l.strip() == e["old_line"].strip()]
        if len(hits) == 1:
            e["line"] = hits[0]        # override the model's number
        elif len(hits) == 0:
            raise ValueError(f"old_line not found in chunk: {e['old_line']!r}")
        else:
            # ambiguous: keep the model's line only if it's one of the hits
            if e["line"] not in hits:
                raise ValueError(f"old_line is ambiguous and 'line' is not one of {hits}")
    return edits

def check_against_chunk(edits, chunk):
    """
    Validates that:
    1. At least one edit targets chunk.match_line.
    2. Any edit matching chunk.match_line has correct old_line/new_line validation.
    """
    if not edits:
        raise ValueError("edits list cannot be empty")

    matching_edits = [e for e in edits if e["line"] == chunk.match_line]

    if not matching_edits:
        target_lines = [e["line"] for e in edits]
        raise ValueError(
            f"No edit targets line {chunk.match_line}. "
            f"Edits target line(s): {target_lines}"
        )

    actual = chunk.lines[chunk.match_line - chunk.start_line]

    for e in matching_edits:
        if e["old_line"].strip() != actual.strip():
            raise ValueError(
                f"old_line doesn't match line {chunk.match_line}:\n"
                f"  on disk:  {actual!r}\n"
                f"  expected: {e['old_line']!r}"
            )
        if e["old_line"] != actual:
            e["old_line"] = actual
        if e["new_line"].strip() == actual.strip():
            raise ValueError(f"new_line on line {chunk.match_line} is identical to the original")

def render_chunk(chunk):
    out = []
    for i, l in enumerate(chunk.lines):
        n = chunk.start_line + i
        marker = ">>" if n == chunk.match_line else "  "
        out.append(f"{marker} {n}: {l}")
    return "\n".join(out)

def generate_edits(llm, chunk, mutation):
    """Return the edit list for one chunk ([] = doesn't apply, None = all attempts failed)."""
    text = call_model(llm, render_chunk(chunk), mutation)
    try:
        edits = parse_edits(text)
        edits = resolve_lines(edits, chunk)
        check_against_chunk(edits, chunk)
        return edits
    except Exception as e:
        print(f"Error suggesting/generating bug: {e}")
        return None


def run_step2(llm, chunk, mutation):
    if chunk is None:
        raise ValueError(f"Cannot insert bug into chunk with None value")
    edits = json.dumps(generate_edits(llm, chunk, mutation), indent=2)
    return edits