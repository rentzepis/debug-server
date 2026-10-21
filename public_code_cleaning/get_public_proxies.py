#!/usr/bin/env python3
"""
Download all proxy.c files from GitHub repos matching:
  path:**/proxy.c "proxylab-handout"

Usage:
    export GITHUB_TOKEN=your_token_here
    python download_proxy.py

Results are saved to ./proxy_downloads/<owner>__<repo>/proxy.c
"""

import time
import requests
from pathlib import Path
from typing import Optional
import os
from dotenv import load_dotenv

# ── config ────────────────────────────────────────────────────────────────────
load_dotenv()
TOKEN = os.environ["GITHUB"]
QUERY = 'filename:proxy.c csapp'
OUT_DIR = Path("proxy_downloads")
PER_PAGE = 100
MAX_PAGES = 10          # GitHub code search caps at 1000 results (10 × 100)
SLEEP_BETWEEN = 2.0     # seconds between API calls to avoid rate-limiting
# ──────────────────────────────────────────────────────────────────────────────

if not TOKEN:
    raise SystemExit("Set GITHUB_TOKEN environment variable before running.")

HEADERS = {
    "Authorization": f"Bearer {TOKEN}",
    "Accept": "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
}

def search_code(query: str, page: int) -> dict:
    resp = requests.get(
        "https://api.github.com/search/code",
        headers=HEADERS,
        params={"q": query, "per_page": PER_PAGE, "page": page},
        timeout=30,
    )
    if resp.status_code == 403:
        reset = int(resp.headers.get("X-RateLimit-Reset", time.time() + 60))
        wait = max(reset - time.time(), 1)
        print(f"  Rate limited — waiting {wait:.0f}s …")
        time.sleep(wait)
        return search_code(query, page)
    resp.raise_for_status()
    return resp.json()


def download_raw(owner: str, repo: str, path: str, ref: str = "HEAD") -> Optional[bytes]:
    url = f"https://raw.githubusercontent.com/{owner}/{repo}/{ref}/{path}"
    resp = requests.get(url, headers=HEADERS, timeout=30)
    if resp.status_code == 200:
        return resp.content
    # fallback: use contents API
    api_url = f"https://api.github.com/repos/{owner}/{repo}/contents/{path}"
    resp = requests.get(api_url, headers=HEADERS, timeout=30)
    if resp.status_code == 200:
        import base64
        data = resp.json()
        if isinstance(data, list):
            # path resolved to a directory listing — shouldn't happen but skip
            return None
        if data.get("encoding") == "base64":
            return base64.b64decode(data["content"])
    print(f"  ✗ Could not download {owner}/{repo}/{path} (HTTP {resp.status_code})")
    return None


def main():
    OUT_DIR.mkdir(exist_ok=True)
    seen: set[str] = set()
    total_downloaded = 0

    for page in range(1, MAX_PAGES + 1):
        print(f"\n── Page {page} ──────────────────────────────")
        data = search_code(QUERY, page)
        items = data.get("items", [])
        if not items:
            print("No more results.")
            break

        total_count = data.get("total_count", "?")
        print(f"Total matches reported by GitHub: {total_count}")
        print(f"Items returned this page: {len(items)}")
        if items:
            print(f"First result: {items[0]['repository']['full_name']} / {items[0]['path']}")

        for item in items:
            repo_full = item["repository"]["full_name"]   # e.g. "alice/proxylab"
            file_path = item["path"]                       # e.g. "proxy.c" or "src/proxy.c"
            sha       = item.get("sha", "HEAD")

            key = f"{repo_full}/{file_path}"
            if key in seen:
                continue
            seen.add(key)

            owner, repo = repo_full.split("/", 1)
            safe_name   = repo_full.replace("/", "__")    # "alice__proxylab"
            dest_dir    = OUT_DIR / safe_name
            dest_dir.mkdir(exist_ok=True)

            # preserve sub-path if proxy.c isn't at root
            dest_file = dest_dir / Path(file_path).name
            if dest_file.exists():
                print(f"  ↩ Already have {key}")
                continue

            # skip editor temp files e.g. #proxy.c#
            if Path(file_path).name != "proxy.c":
                print(f"  ✗ Skipping non-proxy.c file: {key}")
                continue

            print(f"  ↓ {key}", end=" … ", flush=True)
            content = download_raw(owner, repo, file_path)
            if content:
                dest_file.write_bytes(content)
                total_downloaded += 1
                print(f"saved ({len(content):,} bytes)")

                # Try to grab cache.c and cache.h from the same directory
                proxy_dir = str(Path(file_path).parent)
                for extra in ("cache.c", "cache.h"):
                    extra_path = f"{proxy_dir}/{extra}".lstrip("./")
                    extra_dest = dest_dir / extra
                    if extra_dest.exists():
                        continue
                    extra_content = download_raw(owner, repo, extra_path)
                    if extra_content:
                        extra_dest.write_bytes(extra_content)
                        print(f"      + {extra} ({len(extra_content):,} bytes)")
                    time.sleep(0.5)

            time.sleep(SLEEP_BETWEEN)

        if len(items) < PER_PAGE:
            print("Last page reached.")
            break

        time.sleep(SLEEP_BETWEEN)

    print(f"\n✓ Done — downloaded {total_downloaded} file(s) into {OUT_DIR}/")


if __name__ == "__main__":
    main()
