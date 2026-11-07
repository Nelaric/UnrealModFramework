#!/usr/bin/env python3
# Copyright (c) 2026 Nelaric Contributors
"""Check C++ and Unreal build-script format with pinned external tools."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from check_text import ROOT, repository_files

CPP_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}


def main() -> int:
    files = repository_files()
    cpp = [p for p in files if p.is_file() and p.suffix.lower() in CPP_SUFFIXES]
    build = [p for p in files if p.is_file() and (p.name.endswith(".Build.cs") or p.name.endswith(".Target.cs"))]
    failed = False

    if cpp:
        if not shutil.which("clang-format"):
            print("clang-format is required for C++ files (pin: 18.1.8).", file=sys.stderr)
            return 1
        version = subprocess.run(["clang-format", "--version"], capture_output=True, text=True, check=True).stdout
        if "18.1.8" not in version:
            print(f"Expected clang-format 18.1.8, found {version.strip()}", file=sys.stderr)
            return 1
        result = subprocess.run(["clang-format", "--dry-run", "--Werror", *map(str, cpp)], cwd=ROOT)
        failed |= result.returncode != 0
    else:
        print("No C++ files yet; clang-format check has no targets.")

    if build:
        if not shutil.which("dotnet"):
            print("dotnet is required for Unreal build scripts.", file=sys.stderr)
            return 1
        result = subprocess.run(["dotnet", "tool", "run", "csharpier", "check", *map(str, build)], cwd=ROOT)
        failed |= result.returncode != 0
    else:
        print("No .Build.cs or .Target.cs files yet; CSharpier check has no targets.")

    print(f"Checked {len(cpp)} C++ and {len(build)} Unreal build-script files.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
