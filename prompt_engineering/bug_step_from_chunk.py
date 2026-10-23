import json
from pathlib import Path
import importlib.util

from prompt_engineering.bug_gen import run_step2

def load_chunks_for_mutation(mutation, chunks_file):
    """Load the pre-computed chunks list from chunks/<mutation>.py."""
    chunks_file = Path(chunks_file)
    if not chunks_file.exists():
        raise FileNotFoundError(
            f"No chunks file found at {chunks_file}. "
            f"Run the chunking step first."
        )
    import sys
    mod_name = f"chunks_mod_{mutation}"
    sys.modules.pop(mod_name, None)
    spec = importlib.util.spec_from_file_location(mod_name, chunks_file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.chunks  # list of Chunk objects


def generate_bugs_from_chunk(llm, folder_path, helper_files, mutation, data):
    failed = []
    all_edits = []

    # Load only the chunks that passed step 1 for this mutation
    chunks_file = f"chunks/{data}_{mutation}.py"
    print(chunks_file)
    chunks = load_chunks_for_mutation(mutation, chunks_file)
    chunks_by_id = {
        (c['id'] if isinstance(c, dict) else c.id):
        (c['chunk'] if isinstance(c, dict) else c)
        for c in chunks
    }

    print(f"Loaded {len(chunks_by_id)} pre-computed chunks for mutation '{mutation}'")

    for file_path in sorted(folder_path.iterdir()):
        if not file_path.is_file() or file_path.suffix != ".c":
            continue
        if file_path.name in helper_files:
            continue
        if file_path.name not in chunks_by_id:
            # Not in the passing chunks for this mutation — skip silently
            continue

        chunk = chunks_by_id[file_path.name]
        raw_edits = run_step2(llm, chunk, mutation)
        edits = json.loads(raw_edits)
        print(edits)

        if edits:
            # Guard against model returning multiline new_line values
            bad = any("\n" in e.get("new_line", "") for e in edits)
            if bad:
                print(f"Skipping {file_path.name}: model returned multiline new_line")
                failed.append(file_path.name)
                continue
            else:
                edits_formatted = {"id": file_path.name, "edits": edits}
                all_edits.append(edits_formatted)
        else:
            print(f"Unable to parse edits for bug type {mutation} in file {file_path.name}")
            failed.append(file_path.name)

    Path("edits").mkdir(exist_ok=True)
    with open(f"edits/public_testset/{mutation}.py", "w") as f:
        f.write("edits = [\n")
        for e in all_edits:
            f.write(f"{str(e)},\n")
        f.write("]")

    return failed