# Copyright (c) 2026 Nelaric Contributors
"""Install one verified Unreal Engine package from the private producer."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import tempfile

REPO = "Nelaric/UnrealEngine-CI"
VERSION = "5.6.1"
BLOCK = 1024 * 1024


def package_tag(system, architecture, target, revision):
    if system not in ("windows", "macos", "linux") or architecture not in ("x64", "arm64"):
        raise ValueError("Unknown package platform or architecture")
    if target not in ("Game", "Editor") or not re.fullmatch("[0-9a-f]{40}", revision):
        raise ValueError("Expected Game or Editor and a full producer commit SHA")
    return f"ue-{VERSION}-{system}-{architecture}-{target.lower()}-{revision[:12]}"


def safe_name(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or "\0" in name
            or PurePosixPath(name).as_posix() != name or name.startswith("/")
            or any(s in ("", ".", "..") for s in name.split("/"))):
        raise ValueError(f"Invalid package path: {name!r}")
    return name


def api(endpoint):
    return json.loads(subprocess.check_output(["gh", "api", f"repos/{REPO}/{endpoint}"]))


def fetch_asset(asset_id, path):
    url = f"https://api.github.com/repos/{REPO}/releases/assets/{int(asset_id)}"
    token = os.environ.get("GH_TOKEN") or subprocess.check_output(["gh", "auth", "token"], text=True).strip()
    if not re.fullmatch(r"[A-Za-z0-9_.=-]+", token):
        raise ValueError("Invalid GitHub installation token")
    # curl drops the Authorization header when the GitHub asset redirects to a different host.
    # Supplying the header on stdin keeps the token out of the process arguments.
    config = f'header = "Authorization: Bearer {token}"\n'
    subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location", "--retry", "3",
                    "--proto", "=https", "--proto-redir", "=https", "--config", "-",
                    "--header", "Accept: application/octet-stream", "--output", str(path), url],
                   input=config.encode(), check=True)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class ReleaseParts(io.RawIOBase):
    """Verify each part on disk before exposing its bytes to the decompressor."""

    def __init__(self, parts, assets, work, digest):
        self.parts = iter(parts)
        self.assets = assets
        self.path = work / "current.part"
        self.digest = digest
        self.combined = hashlib.sha256()
        self.part = None
        self.finished = False

    def readable(self):
        return True

    def readinto(self, buf):
        while not self.finished:
            if self.part is not None:
                count = self.part.readinto(buf)
                if count:
                    self.combined.update(memoryview(buf)[:count])
                    return count
                self.part.close()
                self.part = None
                self.path.unlink()
            part = next(self.parts, None)
            if part is None:
                if self.combined.hexdigest() != self.digest:
                    raise ValueError("Combined archive SHA-256 mismatch")
                self.finished = True
                break
            asset = self.assets[part["name"]]
            if asset["size"] != part["bytes"]:
                raise ValueError(f"Unexpected published size: {part['name']}")
            fetch_asset(asset["id"], self.path)
            if self.path.stat().st_size != part["bytes"] or sha256(self.path) != part["sha256"]:
                raise ValueError(f"Package part SHA-256 mismatch: {part['name']}")
            self.part = self.path.open("rb")
        return 0

    def close(self):
        if self.part is not None:
            self.part.close()
        super().close()


def inventory(path):
    import zstandard
    rows = {}
    with path.open("rb") as compressed, zstandard.ZstdDecompressor().stream_reader(compressed) as reader:
        for line in io.TextIOWrapper(reader, encoding="utf-8"):
            row = json.loads(line)
            name = safe_name(row["path"])
            if name in rows or not row.get("reason") or row["kind"] not in ("file", "symlink"):
                raise ValueError(f"Invalid inventory entry: {name}")
            rows[name] = row
    return rows


def unpack(source, directory, rows):
    import zstandard
    seen = set()
    with zstandard.ZstdDecompressor().stream_reader(source, closefd=False) as zstream:
        with tarfile.open(fileobj=zstream, mode="r|") as archive:
            for member in archive:
                name = safe_name(member.name)
                row = rows.get(name)
                if not row or name in seen:
                    raise ValueError(f"Unlisted or duplicate archive member: {name}")
                if member.issym():
                    if row["kind"] != "symlink" or member.linkname != row["link"]:
                        raise ValueError(f"Unexpected symbolic link: {name}")
                elif not member.isfile() or row["kind"] != "file" or member.size != row["bytes"]:
                    raise ValueError(f"Unexpected archive entry: {name}")
                if any(parent.as_posix() in rows for parent in PurePosixPath(name).parents):
                    raise ValueError(f"Package member traverses a file or symlink: {name}")
                archive.extract(member, directory, filter="data")
                if member.issym() and not (directory / name).is_symlink():
                    raise ValueError(f"The filesystem did not preserve the symbolic link: {name}")
                if not member.issym() and sha256(directory / name) != row["sha256"]:
                    raise ValueError(f"File SHA-256 mismatch: {name}")
                seen.add(name)
        # Finish the zstd frame, including the tar end padding.
        while zstream.read(BLOCK):
            pass
    if seen != rows.keys():
        raise ValueError(f"Archive omitted {len(rows.keys() - seen)} inventoried files")
    if not (directory / "Engine/Build/Build.version").is_file():
        raise ValueError("Missing Engine/Build/Build.version")


def install(root, system, arch, target, revision, draft_id=None):
    tag = package_tag(system, arch, target, revision)
    if draft_id and os.environ.get("GITHUB_REPOSITORY") != REPO:
        raise ValueError("Draft verification runs in the private producer")
    release = api(f"releases/{int(draft_id)}" if draft_id else f"releases/tags/{tag}")
    if release["tag_name"] != tag or (release["draft"] and not draft_id):
        raise ValueError("The release does not match the requested published package")
    if not draft_id and (not release.get("immutable") or "verification.json" not in {a["name"] for a in release["assets"]}):
        raise ValueError("The package requires a verified immutable release")
    assets = {a["name"]: a for a in release["assets"]}
    root = root.absolute()
    if root.exists():
        raise ValueError(f"Engine destination already exists: {root}")
    root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root.parent) as tmp:
        work = Path(tmp)
        meta = work / "manifest.json"
        fetch_asset(assets["manifest.json"]["id"], meta)
        manifest = json.loads(meta.read_bytes())
        expected = {"format": "Nelaric.UEPackage.v1", "version": VERSION, "system": system,
                    "architecture": arch, "target": target, "configuration": "Development", "revision": revision}
        if any(manifest.get(k) != v for k, v in expected.items()):
            raise ValueError("Package identity mismatch")
        parts = manifest["parts"]
        if not parts or [p["name"] for p in parts] != [f"engine.tar.zst.{i:03d}" for i in range(len(parts))]:
            raise ValueError("Invalid package part sequence")
        indexed = work / "files.jsonl.zst"
        fetch_asset(assets["files.jsonl.zst"]["id"], indexed)
        if sha256(indexed) != manifest["inventorySha256"]:
            raise ValueError("Inventory SHA-256 mismatch")
        rows = inventory(indexed)
        if len(rows) != manifest["files"] or sum(r["bytes"] for r in rows.values()) != manifest["bytes"]:
            raise ValueError("Incorrect inventory file count or byte total")
        manifest_digest = sha256(meta)
        if not draft_id:
            verification = work / "verification.json"
            fetch_asset(assets["verification.json"]["id"], verification)
            verified = json.loads(verification.read_bytes())
            if verified.get("result") != "passed" or verified.get("manifestSha256") != manifest_digest:
                raise ValueError("Clean-run verification does not match this package")
        output = work / "payload"
        output.mkdir()
        with io.BufferedReader(ReleaseParts(parts, assets, work, manifest["archiveSha256"])) as source:
            unpack(source, output, rows)
            while source.read(BLOCK):
                pass
        output.rename(root)
    if "GITHUB_OUTPUT" in os.environ:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as outputs:
            outputs.write(f"manifest_sha={manifest_digest}\n")
    print(f"Installed {tag}: {manifest['files']} files, {manifest['bytes']} bytes", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("system")
    parser.add_argument("architecture")
    parser.add_argument("target")
    parser.add_argument("revision")
    parser.add_argument("--draft-id", type=int)
    a = parser.parse_args()
    try:
        install(a.destination, a.system, a.architecture, a.target, a.revision, a.draft_id)
    except (KeyError, ValueError, OSError, subprocess.SubprocessError) as error:
        sys.exit(f"::error::{error}")
