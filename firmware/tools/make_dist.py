#!/usr/bin/env python3
"""Collects a finished build into a folder the web page can flash from, with a manifest.

Run after `idf.py build`. Writes the bootloader, partition table, app and status pack, plus
manifest.json: the firmware version (as the board reports it in `fw=`), and each file's flash
offset, size, SHA-256 (checked by the page after downloading) and MD5 (checked by the board after
writing), plus the version's notes from CHANGELOG.md. The page never writes the nvs partition, so settings and the custom status survive.

Usage: tools/make_dist.py [output, default ../firmware-dist]
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "firmware-dist"
# flasher_args.json's names for the parts, and what the page calls them.
ROLES = {"bootloader": "bootloader", "partition-table": "partitions", "app": "app", "status": "status"}


def notes(version: str) -> list[str]:
    """The bullet points under "## <version>" in CHANGELOG.md."""
    log = (ROOT.parent / "CHANGELOG.md").read_text()
    m = re.search(rf"^## {re.escape(version)}\s*$(.*?)(?=^## |\Z)", log, re.M | re.S)
    items: list[str] = []
    for line in m.group(1).splitlines() if m else []:
        if line.startswith("- "):
            items.append(line[2:].strip())
        elif line.startswith("  ") and items:
            items[-1] += " " + line.strip()  # a bullet wrapped onto the next line
    return items


def main() -> None:
    args = json.loads((BUILD / "flasher_args.json").read_text())
    desc = json.loads((BUILD / "project_description.json").read_text())
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    files = []
    for key, role in ROLES.items():
        src = (BUILD / args[key]["file"]).resolve()
        data = src.read_bytes()
        shutil.copyfile(src, OUT / src.name)
        files.append({
            "role": role,
            "file": src.name,
            "offset": int(args[key]["offset"], 16),
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "md5": hashlib.md5(data).hexdigest(),
        })
    version = desc["project_version"]
    manifest = {
        "version": version,
        "notes": notes(version),
        "chip": args["extra_esptool_args"]["chip"],
        "files": files,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{OUT}: {manifest['version']}, " + ", ".join(f"{f['file']} @ {f['offset']:#x}" for f in files))


if __name__ == "__main__":
    main()
