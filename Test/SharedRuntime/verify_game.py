# Copyright (c) 2026 Nelaric Contributors
"""Run the packaged two-Mod test against a staged Win64 Development game."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import time
from pathlib import Path

GAME = "UnrealModFrameworkDev"
TEST = "Nelaric.UnrealModFramework.SharedRuntime.TwoMods"
PLUGIN = "UMFDevSDK"
RUNTIME = PLUGIN + "Runtime"


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_mod(archive):
    with archive.open("rb") as stream:
        magic, version, length, offset, size, index, index_size, total, reserved = struct.unpack(
            "<8sII6Q", stream.read(64))
        if magic != b"UMFMOD\0\0" or version != 3 or length != offset or offset != 64 or reserved or total != archive.stat().st_size or index + index_size != total:
            raise ValueError(f"Invalid native Mod header in {archive}")
        manifest = json.loads(stream.read(size))
        stream.seek(index)
        entries = json.loads(stream.read(index_size))["entries"]
    payloads = [entry["path"] for entry in entries if entry["path"].startswith("Native/")]
    library = manifest["extensions"]["nativeExtension"]
    if "nativeCode" in manifest or payloads != [f"Native/{library}"] or not library.endswith(".dll"):
        raise ValueError(f"Expected a single independent extension in {archive}: {payloads}")
    return {"modId": manifest["modId"], "sha256": digest(archive), "nativeExtension": payloads[0]}


def verify(args):
    args.report.mkdir(parents=True, exist_ok=True)
    report_file = args.report / "verification.json"
    report_file.unlink(missing_ok=True)
    manifest = read_json(args.sdk / "Manifest.json")
    link = read_json(args.link)
    if digest(args.receipt) != link["ueTargetReceipt"]["contentHash"]:
        raise ValueError("Host link proof belongs to a different UBT receipt")
    for field in ("contractHash", "projectionHash", "nativeABIHash"):
        if link[field] != manifest[field]:
            raise ValueError(f"Host and released SDK disagree on {field}")
    profile = next((target for target in manifest["targets"] if all(target[key] == link[key] for key in
                    ("target", "platform", "configuration", "architecture"))), None)
    game_batch = read_json(args.host_manifest)
    if not profile or game_batch["generationId"] != link["generationId"]:
        raise ValueError("Game build profile or final generation disagrees with the link proof")
    client = [item["contentHash"] for item in game_batch["generatedFiles"] if item["role"] == "ClientSource"]
    if client != [manifest["sharedRuntime"]["clientSourceHash"]]:
        raise ValueError("Installed game Client implementation differs from the published SDK")
    for item in game_batch["generatedFiles"]:
        if item["role"] in ("SdkHeader", "SdkResource"):
            artifact = (args.sdk / "Native/Include" if item["role"] == "SdkHeader" else args.sdk) / item["path"]
            if digest(artifact) != item["contentHash"]:
                raise ValueError(f"Installed game and released SDK disagree on {item['path']}")
    receipt = read_json(args.receipt)
    project_files = {item["Path"][len("$(ProjectDir)/"):]: item["Type"] for item in receipt["BuildProducts"]
                     if item["Path"].startswith("$(ProjectDir)/")}
    installed_game = args.host_project.resolve().parent
    plugin_path = Path("Plugins") / PLUGIN
    published = args.sdk / "Host" / plugin_path
    host_files = [file for file in published.rglob("*") if file.is_file()]
    if not host_files or any(digest(file) != digest(installed_game / plugin_path / file.relative_to(published)) for file in host_files):
        raise ValueError("Installed game plugin differs from the published Host plugin")
    game_dir = args.stage / GAME
    launch_root, launch_path = receipt["Launch"].split("/", 1)
    stage_roots = {"$(ProjectDir)": game_dir, "$(EngineDir)": args.stage / "Engine"}
    executable = stage_roots[launch_root] / launch_path
    project_arguments = [GAME] if launch_root == "$(EngineDir)" else []
    build_products = {item["Path"]: item["Type"] for item in receipt["BuildProducts"]}
    if build_products.get(receipt["Launch"]) != "Executable" or not executable.is_file():
        raise ValueError(f"The staged launch executable does not match the Game receipt: {executable}")
    if project_arguments:
        runtime_dir = game_dir / "Plugins" / PLUGIN / "Binaries/Win64"
        runtime_map = read_json(runtime_dir / "UnrealGame.modules")
        runtime = runtime_dir / runtime_map["Modules"][RUNTIME]
        if (project_files.get(runtime.relative_to(game_dir).as_posix()) != "DynamicLibrary" or
                runtime_map["BuildId"] != receipt["Version"]["BuildId"]):
            raise ValueError("Staged shared module does not belong to this Game build")
    else:
        runtime = executable
    if digest(runtime) != digest(installed_game / runtime.relative_to(game_dir)):
        raise ValueError("The staged Runtime differs from the image built for the installed game")
    for image in link["binaries"]:
        candidates = [game_dir / name for name, kind in project_files.items()
                      if Path(name).name == Path(image["path"]).name and kind == image["kind"]]
        if len(candidates) != 1 or digest(candidates[0]) != image["contentHash"]:
            raise ValueError(f"Staged image does not match the verified link for {image['ownerModule']}")
    archive_paths = [args.test_mod, args.other_mod]
    mods = [read_mod(path) for path in archive_paths]
    if [mod["modId"] for mod in mods] != ["testmod", "sharedcaller"]:
        raise ValueError("The test requires the TestMod and SharedCaller packages")
    directory = executable.parent / "Mods"
    directory.mkdir(exist_ok=True)
    if any(directory.glob("*.mod")):
        raise ValueError(f"Use a clean staged test game for installation: {directory}")
    log = args.report / "game.log"
    log.unlink(missing_ok=True)
    installed = [directory / (name + ".mod") for name in ("TestMod", "SharedCaller")]
    process = None
    registry = digest(args.registry)
    base = args.registry.parent.parent.name
    try:
        for source, destination in zip(archive_paths, installed):
            shutil.copy2(source, destination)
        command = [executable, *project_arguments, "/Game/Levels/Basic/L_Basic?game=/Script/SharedRuntimeChecks.SharedRuntimeTestMode",
                   "-nullrhi", "-nosound", "-unattended", "-NoCrashReports", "-NoSplash", "-NoTraceServer",
                   "-stdout", "-FullStdOutLogOutput", f"-UserDir={args.report / 'GameUser'}", f"-abslog={log}",
                   f"-ini:Game:[ModLoader]:BaseReleaseVersion={base},[ModLoader]:BaseRegistrySha256={registry},[ModLoader]:bAllowNativeExtensions=True",
                   f"-SharedRemovePath={installed[0]}",
                   f"-ExecCmds=Automation RunTests {TEST};SoftQuit"]
        with (args.report / "console.log").open("wb") as console:
            process = subprocess.Popen(command, stdout=console, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 240
            removed = False
            while process.poll() is None and time.monotonic() < deadline:
                if not removed and log.is_file() and "UMF_SHARED_BOTH_CALLED" in log.read_text(encoding="utf-8", errors="replace"):
                    installed[0].unlink()
                    removed = True
                time.sleep(0.5)
            if process.poll() is None:
                raise TimeoutError(f"Game did not finish. See {log}")
        events = log.read_text(encoding="utf-8", errors="replace").splitlines()
        expected = ["UMF_SHARED_BOTH_CALLED", "Unmounted mod testmod", "UMF_SHARED_AFTER_UNLOAD health=41 revoked=-1",
                    f"Result={{Success}} Name={{TwoMods}} Path={{{TEST}}}"]
        evidence = [next((line for line in events if token in line), None) for token in expected]
        if process.returncode or not removed or not all(evidence):
            raise RuntimeError(f"Missing game evidence or failing exit code {process.returncode}. See {log}")
        result = {"format": "UMF.SharedRuntimeVerification", "schemaVersion": 1, "status": "Succeeded",
                  "sdkFingerprint": manifest["inputFingerprint"], "sdkManifestHash": digest(args.sdk / "Manifest.json"),
                  "profile": {name: link[name] for name in ("target", "platform", "configuration", "architecture", "generationId", "nativeABIHash")},
                  "sdkProducerGenerationId": profile["generationId"], "baseRegistryHash": registry,
                  "hostPluginFilesVerified": len(host_files),
                  "capabilities": {"hostFinalLinkVerified": True, "gameSdkRuntimeBuilt": True, "runtimeBindingVerified": True}, "ueTargetReceiptHash": digest(args.receipt),
                  "hostBinaries": link["binaries"], "sharedRuntime": {"module": RUNTIME, "sha256": digest(runtime)},
                  "mods": mods, "evidence": evidence, "gameLogHash": digest(log)}
        pending = report_file.with_suffix(".tmp")
        pending.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
        pending.replace(report_file)
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=30)
        for path in installed:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("sdk", "stage", "receipt", "link", "host-project", "host-manifest", "registry", "test-mod", "other-mod", "report"):
        parser.add_argument("--" + name, required=True, type=Path)
    verify(parser.parse_args())
