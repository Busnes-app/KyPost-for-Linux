#!/usr/bin/env python3
"""Pin Go module proxy artifacts for Flatpak's network-isolated source build.

Run after go.mod/go.sum changes. Uses Go's verified persistent module cache.
"""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
raw = subprocess.check_output(["go", "mod", "download", "-json", "all"], cwd=root / "core/pgp/gobridge", text=True)
decoder = json.JSONDecoder()
modules = []
while raw.strip():
    value, end = decoder.raw_decode(raw.lstrip())
    modules.append(value)
    raw = raw.lstrip()[end:]
sources = []
for module in sorted(modules, key=lambda m: m["Path"]):
    if "Error" in module:
        raise RuntimeError(module["Error"])
    escaped = "".join("!" + c.lower() if c.isupper() else c for c in module["Path"])
    for suffix, field in [("info", "Info"), ("mod", "GoMod"), ("zip", "Zip")]:
        path = Path(module[field])
        sources.append({"type": "file", "url": f"https://proxy.golang.org/{escaped}/@v/{module['Version']}.{suffix}",
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "dest": f".go-mod-cache/cache/download/{escaped}/@v",
                        "dest-filename": f"{module['Version']}.{suffix}"})
(root / "packaging/flatpak/go-sources.json").write_text(json.dumps(sources, indent=2) + "\n")
