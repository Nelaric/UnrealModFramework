#!/usr/bin/env python3
# Copyright (c) 2026 Nelaric Contributors
"""Reject Hook witnesses left to dyld by Unreal's -undefined dynamic_lookup links."""

import argparse
import json
import plistlib
import subprocess
import sys
from pathlib import Path


def verify_imports(binary, nm):
    names = subprocess.check_output([*nm, "-u", "-j", str(binary)], text=True)
    unresolved = {name for line in names.splitlines() if (name := line.strip()).startswith("_UMF_ModHook_")}
    if unresolved:
        raise ValueError(f"{binary} imports a missing Hook source witness: {', '.join(sorted(unresolved))}")


def verify_target(project, receipt, generated, nm=("xcrun", "nm", "-arch", "all")):
    source_map = generated / "Diagnostics/HookSources.json"
    if not source_map.is_file():
        return
    units = json.loads(source_map.read_text(encoding="utf-8-sig"))["units"]
    if not units:
        return
    products = json.loads(receipt.read_text(encoding="utf-8-sig"))["BuildProducts"]
    images = set()
    for item in products:
        if item["Type"] not in ("Executable", "DynamicLibrary"):
            continue
        path = item["Path"]
        if path.startswith("$(ProjectDir)/"):
            binary = project / path[len("$(ProjectDir)/") :]
        elif Path(path).is_absolute():
            binary = Path(path)
        else:
            continue
        if binary.is_dir() and binary.suffix == ".app":
            contents = binary / "Contents"
            info = contents / "Info.plist" if contents.is_dir() else binary / "Info.plist"
            name = plistlib.loads(info.read_bytes())["CFBundleExecutable"]
            binary = (contents / "MacOS" if contents.is_dir() else binary) / name
        images.add(binary)
    if not images:
        raise ValueError(f"No project Mach-O image in {receipt}")
    for binary in sorted(images):
        verify_imports(binary, nm)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", required=True, type=Path)
    parser.add_argument("--target-receipt", required=True, type=Path)
    parser.add_argument("--generated", required=True, type=Path)
    args = parser.parse_args()
    try:
        verify_target(args.project_dir, args.target_receipt, args.generated)
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
