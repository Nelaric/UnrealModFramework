# Copyright (c) 2026 Nelaric Contributors
"""Install a pinned, access-controlled UE build on a disposable Actions runner."""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit


def install(root: Path, key: str) -> None:
    url = os.environ.pop("UE_ARCHIVE_URL", "")
    digest = os.environ.get("UE_ARCHIVE_SHA256", "").lower()
    print(f"Available space for {key}: {shutil.disk_usage(root.parent).free / 2**30:.1f} GiB", flush=True)
    if not url or not digest:
        raise ValueError(f"Missing Actions secret UE_561_{key}_URL or variable UE_561_{key}_SHA256. "
                         "This job needs a licensed UE 5.6.1 installed build for this host.")
    if urlsplit(url).scheme != "https" or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("The engine archive requires HTTPS and a 64-character SHA-256 digest.")
    # Only a verified archive may supply executable files. A temporary directory also
    # keeps partial downloads and extractions out of the engine location on failure.
    with tempfile.TemporaryDirectory(dir=root.parent) as temp:
        archive = Path(temp, "engine.tar")
        payload = Path(temp, "payload")
        payload.mkdir()
        status = subprocess.call(["curl", "--fail", "--silent", "--show-error", "--location", "--retry", "3",
                                  "--proto", "=https", "--proto-redir", "=https", "--output", archive, url])
        if status:
            raise ValueError(f"Engine archive download failed (curl status {status}).")
        with archive.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != digest:
                raise ValueError("Engine archive SHA-256 mismatch.")
        subprocess.run(["tar", "-xf", archive, "-C", payload], check=True)
        if not (payload / "Engine/Build/Build.version").is_file():
            raise ValueError("The archive must contain Engine/Build/Build.version at its root.")
        payload.rename(root)


if __name__ == "__main__":
    try:
        install(Path(sys.argv[1]), sys.argv[2])
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
