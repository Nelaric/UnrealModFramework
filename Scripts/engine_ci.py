# Copyright (c) 2026 Nelaric Contributors
"""Install a locked UMFCI SDK, or run the same MBT stages used by local builds."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "UnrealModFramework/UnrealModFrameworkDev.uproject"
TOOLS = PROJECT.parent / "Plugins/ModRuntime/Tools"
LOCK = json.loads((ROOT / ".github/engine-lock.json").read_text(encoding="utf-8"))


def target_settings(slug: str) -> dict:
    entry = LOCK["targets"][slug]
    if not entry["release"] or not entry["recipe"] or not re.fullmatch(r"[0-9a-f]{64}", entry["manifestSha256"] or ""):
        raise ValueError(f"{slug} has no independently verified UMFCI release in the engine lock")
    return entry


def git_head(directory: Path) -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=directory, text=True).strip()


def require_toolchain(entry: dict, reported: dict) -> None:
    requirements = LOCK["toolchains"][entry["toolchain"]]
    for key, value in requirements.items():
        if key == "allowedCompilers":
            if reported.get("compiler") not in value:
                raise ValueError("The Windows compiler version is outside the engine lock")
        elif reported.get(key) != value:
            raise ValueError(f"The toolchain does not match the engine lock: {key}")


def install_sdk(umfci: Path, slug: str, work: Path) -> None:
    entry = target_settings(slug)
    if git_head(umfci) != LOCK["installerCommit"]:
        raise ValueError("UMFCI installer checkout does not match the lock")
    sys.path.insert(0, str(umfci / "Scripts"))
    from install import PrivateRelease, install, metadata
    from platforms import configure
    from targets import select

    target = select(slug)
    target.require_native_host()
    expected = {"platform": target.platform, "architecture": entry["architecture"],
                "profile": entry["profile"], "configuration": LOCK["configuration"]}
    if target.as_dict() != expected or target.tag(entry["recipe"]) != entry["release"]:
        raise ValueError("UMFCI target or release does not match the engine lock")
    release = PrivateRelease(LOCK["repository"], entry["release"])
    if release.release.get("immutable") is not True or release.release.get("draft") or release.release.get("prerelease"):
        raise ValueError("The SDK release must be immutable, published and final")
    manifest_bytes = metadata(release.open, "manifest.json")
    if hashlib.sha256(manifest_bytes).hexdigest() != entry["manifestSha256"]:
        raise ValueError("SDK manifest SHA-256 does not match the engine lock")
    manifest = json.loads(manifest_bytes)
    if (manifest["epic_upstream_commit"] != LOCK["upstreamCommit"] or manifest["ue_version"] != LOCK["ueVersion"]
            or manifest["dotnet_sdk_version"] != LOCK["dotnetSdk"] or manifest["target"] != expected
            or manifest["installed_bytes"] > LOCK["maxInstalledBytes"]):
        raise ValueError("SDK identity does not match the engine lock")
    require_toolchain(entry, manifest["toolchain"])
    os.environ.pop("GH_TOKEN", None)
    os.environ.pop("GITHUB_TOKEN", None)

    # The immutable SDK installer receives the verified manifest bytes and streams
    # the other assets through the release client's existing authenticated handle.
    def open_asset(name):
        return io.BytesIO(manifest_bytes) if name == "manifest.json" else release.open(name)

    work.mkdir(parents=True, exist_ok=True)
    _, timings = install(open_asset, target, entry["recipe"], work / "sdk")
    print("UMF timing SDK installation", json.dumps(timings), flush=True)
    started = time.monotonic()
    toolchain, _ = configure(target, work)
    require_toolchain(entry, toolchain)
    if entry["toolchain"] == "windows" and toolchain["compiler"] < manifest["toolchain"]["compiler"]:
        raise ValueError("The Windows compiler is older than the SDK producer compiler")
    env = {key: os.environ[key] for key in ("LINUX_MULTIARCH_ROOT", "DEVELOPER_DIR") if key in os.environ}
    if entry["toolchain"] == "windows":
        env.update(UMF_MSVC_VERSION=toolchain["compiler"], UMF_WINDOWS_SDK_VERSION=toolchain["sdk"])
    dotnet = Path(shutil.which("dotnet")).resolve()
    if subprocess.check_output([dotnet, "--version"], cwd=umfci, text=True).strip() != LOCK["dotnetSdk"]:
        raise ValueError("The active .NET SDK does not match the engine lock")
    env.update(DOTNET_ROOT=str(dotnet.parent), UE_USE_SYSTEM_DOTNET="1")
    (work / "toolchain.json").write_text(json.dumps(env) + "\n", encoding="utf-8")
    print(f"UMF timing toolchain {time.monotonic() - started:.3f} s", flush=True)


def timed(label: str, command: list, env: dict | None = None) -> None:
    started = time.monotonic()
    subprocess.run(command, check=True, cwd=ROOT, env=env)
    print(f"UMF timing {label} {time.monotonic() - started:.3f} s", flush=True)


def native_tools(target: str, env: dict) -> Path:
    if os.name == "nt":
        build = ["cmd.exe", "/d", "/c", "call", str(TOOLS / "Build.bat")]
    else:
        build = ["sh", TOOLS / "Build.sh"]
    timed("native tools", [*build, "--target", target, "--jobs", "4"], env)
    timed("host libclang", ["cmake", "-P", TOOLS / "SetupLibClang.cmake"])
    return TOOLS / "build"


def run_target(slug: str, work: Path, commit: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or git_head(ROOT) != commit:
        raise ValueError("Engine job checkout does not match the reviewed source commit")
    work = work.resolve()
    entry = target_settings(slug)
    env = {**os.environ, **json.loads((work / "toolchain.json").read_text(encoding="utf-8"))}
    tools = native_tools("ModRuntimeUhtTools", env)
    mbt = tools / "ModBuildTool" / ("ModBuildTool.exe" if os.name == "nt" else "ModBuildTool")
    editor = entry["profile"] == "Editor"
    name = "UnrealModFrameworkDevEditor" if editor else "UnrealModFrameworkDev"
    args = ["--engine", work / "sdk", "--project", PROJECT, "--target", name,
            "--platform", entry["ubtPlatform"], "--architecture", entry["ubtArchitecture"]]
    identity = ["--commit", commit, "--engine-commit", LOCK["upstreamCommit"]]
    metadata = work / "Metadata"
    if editor:
        timed("prepare metadata", [mbt, "prepare-metadata", *args, *identity, "--output", metadata], env)
    timed(f"build {entry['profile']}", [mbt, "build-ue-target", *args, *identity, "--configuration",
                                      "Development", "--mode", "normal", "--metadata", metadata], env)
    if editor:
        timed("automation", [mbt, "test-ue-target", *args, "--report", work / "Automation"], env)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    config = sub.add_parser("config")
    config.add_argument("target", choices=LOCK["targets"])
    installer = sub.add_parser("install")
    installer.add_argument("target", choices=LOCK["targets"])
    installer.add_argument("umfci", type=Path)
    installer.add_argument("work", type=Path)
    builder = sub.add_parser("build")
    builder.add_argument("target", choices=LOCK["targets"])
    builder.add_argument("work", type=Path)
    builder.add_argument("commit")
    args = parser.parse_args()
    if args.command == "config":
        entry = target_settings(args.target)
        print(f"installer={LOCK['installerCommit']}")
        print(f"dotnet={LOCK['dotnetSdk']}")
        print(f"runner={entry['runner']}")
    elif args.command == "install":
        install_sdk(args.umfci.resolve(), args.target, args.work.resolve())
    else:
        run_target(args.target, args.work, args.commit)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
