#!/usr/bin/env python3
"""Restore defaultChatAgent required by VS Code onboarding.

Without it, workbench.js throws at module load and browsers show a blank white screen.

Also rewrites the embedded VS Code commit hash: static asset URLs are
/stable-<commit>/static/... and are cached for a year by Cloudflare and
browsers, so the patched JS is never picked up unless the URLs change.
The replacement hash has the same length (40 hex chars), keeping minified
bundles and sourcemap offsets valid.
"""
from pathlib import Path
import hashlib
import json
import sys

OLD_COMMIT = "a22d00300655c17490ce63dffc28bcdcedcd82c4"
# Deterministic new commit hash for cache busting (any 40-char hex works).
NEW_COMMIT = hashlib.sha1(b"debug-server hotfix defaultChatAgent 2026-07-21").hexdigest()

VSCODE_ROOT = Path("/opt/code-server/lib/vscode")

workbench = VSCODE_ROOT / "out/vs/code/browser/workbench/workbench.js"
text = workbench.read_text(encoding="utf-8")

needle = (
    'configurationDefaults:{"chat.disableAIFeatures":!0,'
    '"workbench.secondarySideBar.defaultVisibility":"hidden",'
    '"workbench.colorTheme":"Dark 2026"},version:"1.127.0"'
)
insert = (
    'configurationDefaults:{"chat.disableAIFeatures":!0,'
    '"workbench.secondarySideBar.defaultVisibility":"hidden",'
    '"workbench.colorTheme":"Dark 2026"},'
    'defaultChatAgent:{extensionId:"GitHub.copilot",'
    'chatExtensionId:"GitHub.copilot-chat",'
    'provider:{default:{id:"github",name:"GitHub"},'
    'enterprise:{id:"github-enterprise",name:"GitHub Enterprise"}},'
    'providerScopes:[]},version:"1.127.0"'
)

if needle not in text:
    marker = '"workbench.colorTheme":"Dark 2026"},defaultChatAgent:{extensionId:"GitHub.copilot"'
    if marker in text:
        print("workbench.js already has defaultChatAgent; skipping")
    else:
        print("hotpatch needle not found in workbench.js", file=sys.stderr)
        sys.exit(1)
else:
    workbench.write_text(text.replace(needle, insert, 1), encoding="utf-8")
    print("patched workbench.js defaultChatAgent")

product_path = VSCODE_ROOT / "product.json"
product = json.loads(product_path.read_text(encoding="utf-8"))
if "defaultChatAgent" not in product:
    product["defaultChatAgent"] = {
        "extensionId": "GitHub.copilot",
        "chatExtensionId": "GitHub.copilot-chat",
        "provider": {
            "default": {"id": "github", "name": "GitHub"},
            "enterprise": {"id": "github-enterprise", "name": "GitHub Enterprise"},
        },
        "providerScopes": [],
    }
    product_path.write_text(json.dumps(product, indent=2) + "\n", encoding="utf-8")
    print("patched product.json defaultChatAgent")
else:
    print("product.json already has defaultChatAgent; skipping")

# Cache-bust: replace the commit everywhere it is embedded so /stable-<commit>/
# asset URLs change and stale Cloudflare/browser caches are skipped.
replaced = 0
for path in [product_path, *sorted((VSCODE_ROOT / "out").rglob("*"))]:
    if not path.is_file() or path.suffix not in {".js", ".json", ".css", ".html"}:
        continue
    try:
        content = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    if OLD_COMMIT in content:
        path.write_text(content.replace(OLD_COMMIT, NEW_COMMIT), encoding="utf-8")
        replaced += 1
        print(f"cache-bust commit in {path.relative_to(VSCODE_ROOT)}")

if replaced == 0 and NEW_COMMIT not in product_path.read_text(encoding="utf-8"):
    print("commit not found anywhere; unexpected", file=sys.stderr)
    sys.exit(1)
print(f"done; new commit {NEW_COMMIT}")
