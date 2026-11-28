# Copyright (c) 2026 Nelaric Contributors
"""Exercise MBT's portable metadata parser without starting UnrealBuildTool."""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE_COMMIT = "1" * 40
ENGINE_COMMIT = "2" * 40


def encoded(data):
    return (json.dumps(data, indent=2) + "\n").encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def variant(name, architecture, platform="Win64"):
    return {"name": name, "platform": platform, "configuration": "Development", "architecture": architecture}


def make_bundle(project, bundle, platform):
    project.mkdir()
    (project / "Config").mkdir()
    (project / "Content").mkdir()
    project_path = project / "Test.uproject"
    project_path.write_bytes(encoded({"FileVersion": 3}))
    asset = project / "Content/BP.uasset"
    asset.write_bytes(b"fixture bytes")
    source = "Project/Test.uproject"
    asset_path = "Project/Content/BP.uasset"
    payloads = {
        "options.json": {"Project": source, "SchemaVersion": 5, "Options": {}, "FunctionRedirects": []},
        "blueprints.json": {"format": "UMF.BlueprintExports", "schemaVersion": 1, "inputs": [asset_path],
                            "types": [{"asset": "/Game/BP.BP", "typeId": "probe", "module": "Fixture", "extensions": [], "apis": [],
                                       "class": "/Game/BP.BP_C", "file": asset_path}]},
        "inputs.json": {"sources": {source: digest(project_path.read_bytes())},
                        "assets": {asset_path: digest(asset.read_bytes())},
                        "assetInventory": {asset_path: digest(asset.read_bytes())},
                        "targetTypes": {"Test": "Game", "TestEditor": "Editor"},
                        "configRoots": [], "config": {}},
    }
    targets = [variant("TestEditor", "x64", platform), variant("Test", "x64", platform)]
    if platform == "Linux":
        targets.extend([variant("Test", "arm64", "LinuxArm64"), variant("Test", "x64", "Android"),
                        variant("Test", "arm64", "Android")])
    else:
        targets.extend([variant("Test", "arm64"), variant("Test", "arm64ec")])
    hashes = {}
    bundle.mkdir()
    for name, payload in payloads.items():
        data = encoded(payload)
        (bundle / name).write_bytes(data)
        hashes[name] = digest(data)
    (bundle / "manifest.json").write_bytes(encoded({
        "format": "UMF.MetadataBundle", "schemaVersion": 1, "generatorVersion": "ModBuildTool.Metadata/2",
        "project": source, "sourceCommit": SOURCE_COMMIT, "engine": {"major": 5, "minor": 6, "patch": 1,
        "upstreamCommit": ENGINE_COMMIT}, "producer": variant("TestEditor", "x64", platform),
        "consumers": targets,
        "payloads": hashes,
    }))


def exercise(mbt, root, platform):
    engine = root / "SDK/Engine"
    ubt = engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
    ubt.parent.mkdir(parents=True)
    ubt.write_bytes(b"not executable")
    (engine / "Build").mkdir()
    (engine / "Build/Build.version").write_bytes(encoded({"MajorVersion": 5, "MinorVersion": 6, "PatchVersion": 1}))
    producer = root / "EditorProject"
    bundle = root / "Metadata"
    make_bundle(producer, bundle, platform)
    consumer = root / "GameProject"
    shutil.copytree(producer, consumer)
    args = [mbt, "validate-metadata", "--engine", engine, "--project", consumer / "Test.uproject", "--target", "Test",
            "--platform", "LinuxArm64" if platform == "Linux" else platform, "--configuration", "Development", "--architecture", "arm64",
            "--commit", SOURCE_COMMIT, "--engine-commit", ENGINE_COMMIT, "--metadata", bundle]

    def verify(expected=None, **overrides):
        command = [str(arg) for arg in args]
        for key, value in overrides.items():
            command[command.index("--" + key.replace("_", "-")) + 1] = str(value)
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if expected is None:
            if result.returncode:
                raise AssertionError(result.stderr)
        elif result.returncode == 0 or expected not in result.stderr:
            raise AssertionError(f"Expected metadata rejection {expected!r}, got {result.returncode}: {result.stderr}")

    verify()
    user = consumer / "Config/UserGame.ini"
    user.write_text("[View]\nLayout=Local\n", encoding="utf-8")
    verify()
    user.write_text("[ModExportMetadata]\n+APIPermissions=Local\n", encoding="utf-8")
    verify("Target-specific configuration")
    user.unlink()
    verify("does not match", commit="0" * 40)
    verify("does not cover", platform="Mac")
    verify("manifest.json", metadata=root)
    options = bundle / "options.json"
    options_bytes = options.read_bytes()
    options.write_bytes(options_bytes + b" ")
    verify("payload integrity")
    options.write_bytes(options_bytes)
    asset = consumer / "Content/BP.uasset"
    asset.write_bytes(b"changed")
    verify("Blueprint discovery inputs have changed")
    asset.write_bytes((producer / "Content/BP.uasset").read_bytes())
    added = consumer / "Content/NewBlueprint.uasset"
    added.write_bytes(b"new export candidate")
    verify("Blueprint discovery inputs have changed")
    added.unlink()
    verify()
    (engine / "Config").mkdir()
    (engine / "Config/BaseEngine.ini").write_text("[CoreRedirects]\n+FunctionRedirects=(OldName=\"x\",NewName=\"y\")\n",
                                                encoding="utf-8")
    verify("Target configuration differs")
    (consumer / "Config/DefaultGameMod.ini").write_text("[ModExportMetadata]\n+APIPermissions=Changed\n",
                                                       encoding="utf-8")
    verify("Metadata is stale")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: test_metadata_bundle.py MOD_BUILD_TOOL")
    with tempfile.TemporaryDirectory(prefix="umf-metadata-") as directory:
        executable = Path(sys.argv[1]).resolve(strict=True)
        exercise(executable, Path(directory) / "Windows", "Win64")
        exercise(executable, Path(directory) / "Linux", "Linux")
    print("Portable metadata and rejection checks passed")
