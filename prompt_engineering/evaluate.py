import os
import re
import shutil
import subprocess
import signal
from pathlib import Path

HANDOUT_DIR = Path("starter/proxylab-handout").resolve()
SUBMISSIONS_DIR = HANDOUT_DIR / "submissions"

def release_harness_ports():
    """Frees specific local ports used by proxylab test binaries without killing system services."""
    proxy_bin = HANDOUT_DIR / "proxy"
    if proxy_bin.exists():
        subprocess.run(["fuser", "-k", "-9", str(proxy_bin)], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)

def fix_driver_environment():
    """Forces native recompilation of pxy/file_generator regardless of location."""
    release_harness_ports()

    # Find any source file for file_generator.c
    file_gen_sources = list(HANDOUT_DIR.rglob("file_generator.c"))

    if file_gen_sources:
        src = file_gen_sources[0]

        # Target paths where driver/pxydrive might look for it
        targets = [
            HANDOUT_DIR / "file_generator",
            HANDOUT_DIR / "pxy" / "file_generator"
        ]

        for target in targets:
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                target.unlink()

            res = subprocess.run(["gcc", "-O2", str(src), "-o", str(target)], capture_output=True, text=True)
            if res.returncode == 0:
                target.chmod(0o755)
                print(f"[RECOMPILED NATIVE] {target.relative_to(HANDOUT_DIR)}")
            else:
                print(f"[ERROR] Failed building {target}: {res.stderr}")

    # Same target logic for port-for-user
    port_sources = list(HANDOUT_DIR.rglob("port-for-user.c"))
    if port_sources:
        src = port_sources[0]
        targets = [HANDOUT_DIR / "port-for-user", HANDOUT_DIR / "pxy" / "port-for-user"]
        for target in targets:
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                target.unlink()
            subprocess.run(["gcc", "-O2", str(src), "-o", str(target)], check=False)
            if target.exists():
                target.chmod(0o755)

    # Set script permissions
    for script in HANDOUT_DIR.rglob("*.sh"):
        script.chmod(0o755)
    for script in HANDOUT_DIR.rglob("*.py"):
        script.chmod(0o755)
        
def sanitize_c_file(file_path: Path):
    """Safely fixes physical multiline strings without altering valid syntax or breaking escaped quotes."""
    content = file_path.read_text(encoding="utf-8", errors="ignore")
    
    # Correctly handles inner escaped quotes (\") inside string literals
    pattern = re.compile(r'"((?:[^"\\]|\\.)*)"', re.DOTALL)

    def fix_multiline(match):
        raw_inner = match.group(1)
        if "\n" in raw_inner or "\r" in raw_inner:
            cleaned = raw_inner.replace("\r\n", "\\n").replace("\n", "\\n")
            return f'"{cleaned}"'
        return match.group(0)

    sanitized = pattern.sub(fix_multiline, content)
    if sanitized != content:
        file_path.write_text(sanitized, encoding="utf-8")
        # print(f"[FIXED] Sanitized string literals in: {file_path.name}")
        
def build_proxy_wrapper(submission_file: Path) -> bool:
    """Compiles student submission natively and creates unbuffered execution wrappers."""
    raw_bin = (HANDOUT_DIR / "proxy_bin").resolve()
    if raw_bin.exists():
        raw_bin.unlink()

    csapp_c = HANDOUT_DIR / "csapp.c"
    csapp_h = HANDOUT_DIR / "csapp.h"
    so_file = HANDOUT_DIR / "libhttp_parser.so"

    # Use native gcc instead of cross-compiler
    cmd = ['gcc', '-Wall', '-g', '-Og']

    include_dirs = {submission_file.parent, HANDOUT_DIR}
    if csapp_h.exists():
        include_dirs.add(csapp_h.parent)

    for inc in include_dirs:
        cmd.extend(['-I', str(inc)])

    cmd.append(str(submission_file.resolve()))
    if csapp_c.exists():
        cmd.append(str(csapp_c.resolve()))
    if so_file.exists():
        cmd.append(str(so_file.resolve()))

    cmd.extend(['-o', str(raw_bin), '-lpthread', '-lrt'])

    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = f"{HANDOUT_DIR}:{env.get('LD_LIBRARY_PATH', '')}"

    res = subprocess.run(cmd, cwd=HANDOUT_DIR, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        # print(f"[FAIL] Compilation Error for {submission_file.name}:\n{res.stderr}\n")
        return False

    # Execute directly without qemu-aarch64 wrapper
    wrapper_content = f"""#!/bin/bash
export LD_LIBRARY_PATH="{HANDOUT_DIR}:$LD_LIBRARY_PATH"
exec stdbuf -i0 -o0 -e0 "{raw_bin}" "$@"
"""
    wrapper_path = HANDOUT_DIR / "proxy"
    wrapper_path.write_text(wrapper_content)
    wrapper_path.chmod(0o755)

    proxy_c_link = HANDOUT_DIR / "proxy.c"
    if proxy_c_link.exists() or proxy_c_link.is_symlink():
        proxy_c_link.unlink()
    os.symlink(wrapper_path, proxy_c_link)

    return True

def evaluate_all_submissions():
    fix_driver_environment()

    submission_files = [
        f for f in sorted(SUBMISSIONS_DIR.glob("*.c"))
        if f.name not in ("csapp.c", "csapp.h")
    ]

    if not submission_files:
        print(f"No valid student submission .c files found in {SUBMISSIONS_DIR}")
        return

    results = {}
    print(f"Found {len(submission_files)} student submissions in {SUBMISSIONS_DIR.name}\n" + "="*60)

    for sub_file in submission_files:
        # print(f"\n[+] Testing: {sub_file.name}")

        release_harness_ports()

        log_dir = HANDOUT_DIR / "logs"
        if log_dir.exists():
            shutil.rmtree(log_dir)

        ''' new '''
        fmt_res = subprocess.run(
            ["make", "-C", str(HANDOUT_DIR), "format"],
            capture_output=True,
            text=True
        )
        if fmt_res.returncode != 0:
            print(f"[WARN] 'make format' failed on {sub_file.name}: {fmt_res.stderr}")
        ''' end new '''

        if not build_proxy_wrapper(sub_file):
            results[sub_file.name] = "COMPILATION_FAILED"
            continue

        env = os.environ.copy()
        env['PYTHONUNBUFFERED'] = '1'
        env['LD_LIBRARY_PATH'] = f"{HANDOUT_DIR}:{env.get('LD_LIBRARY_PATH', '')}"

        driver_sh = HANDOUT_DIR / "driver_AB.sh"

        proc = subprocess.Popen(
            [str(driver_sh)],
            cwd=HANDOUT_DIR,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            preexec_fn=os.setsid
        )

        try:
            stdout, _ = proc.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            stdout, _ = proc.communicate()
            stdout += "\n[DRIVER TIMED OUT]"

        passed = "SUCCESS COUNT = 25/25" in stdout or "25 (100.0%) passed" in stdout

        if passed:
            results[sub_file.name] = "PASSED (25/25)"
            # print(f"    Result: PASSED")
        else:
            summary_line = [line for line in stdout.splitlines() if "passed" in line or "SUCCESS" in line or "failed" in line]
            status_text = summary_line[0] if summary_line else "FAILED"
            results[sub_file.name] = f"FAILED ({status_text.strip()})"
            # print(f"    Result: {results[sub_file.name]}")
            # print(f"--- STDOUT LOG EXCERPT ---\n{stdout[-1200:]}\n-------------------------")

    print("\n" + "="*60)
    print("FINAL SUBMISSION RESULTS")
    print("="*60)
    for name, status in results.items():
        print(f"{name:<30} | {status}")

if __name__ == "__main__":
    evaluate_all_submissions()