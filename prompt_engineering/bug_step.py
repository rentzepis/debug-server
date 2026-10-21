import json

from prompt_engineering.bug_gen import run_step2
from prompt_engineering.regex import run_step1


def generate_bugs(llm, folder_path, helper_files, mutation):
    failed = []
    all_edits = []
    all_chunks = []

    for file_path in folder_path.iterdir():
        if file_path.is_file() and file_path.suffix == ".c":
            if file_path.name in helper_files:
                continue

            chunk = run_step1(file_path, mutation)
            if chunk:
                all_chunks.append(chunk)
                raw_edits = run_step2(llm, chunk, mutation)
                edits = json.loads(raw_edits)
                print(edits)
                if not edits:
                    print(f"Unable to parse edits for bug type {mutation} in file {file_path.name}")
                    failed.append(file_path.name)
                else:
                    all_edits.append(raw_edits)
            else:
                print(f"Unable to find code for bug type {mutation} in file {file_path.name}")
                failed.append(file_path.name)

    with open(f"chunks/{mutation}.py", "w") as f:
        f.write("from prompt_engineering.utils import Chunk\n\n")
        f.write("chunks = [\n")
        for c in all_chunks:
            f.write(f"{ str(c)},\n")
        f.write("]")

    with open(f"edits/{mutation}.py", "w") as f:
        f.write("edits = [\n")
        for e in all_edits:
            f.write(f"{ str(e)},\n")
        f.write("]")

    return failed