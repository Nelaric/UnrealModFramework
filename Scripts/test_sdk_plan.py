# Copyright (c) 2026 Nelaric Contributors

import argparse
import json
import platform
import subprocess
import tempfile
from pathlib import Path

from game_checkpoint import file_sha, read_json, require, write_json


def fixture(mdt, mbt, root):
    host = {"Windows": "Win64", "Linux": "Linux", "Darwin": "Mac"}[platform.system()]
    arch = "arm64" if platform.machine().lower() in {"aarch64", "arm64"} else "x64"
    target = {"target": "Example", "platform": host, "architecture": arch, "configuration": "Development"}
    native = {"contractHash": "a" * 64, "projectionHash": "b" * 64, "nativeABIHash": "c" * 64}
    scope = {"status": "Complete", "gameId": "Example"}
    source = root / "generated"
    lifecycle = "Contract/ModLifecycle.json"
    connection = "Contract/ClientABI.json"
    contract_path = "Contract/SdkContract.json"
    client = "Client/ExampleModClient/ExampleModClient.modclient.cpp"
    header = "SDK/Public/Example.h"
    write_json(source / lifecycle, {"schemaVersion": 2})
    contract = scope | native | {
        "format": "UMF.SdkContract", "schemaVersion": 2, "unresolvedTypeCount": 0,
        "adapterABI": "ContextHandlesV1", "connectionDescriptor": connection, "types": [], "apis": [],
        "modLifecycle": {"resource": lifecycle, "module": "ModLoader", "schemaVersion": 2,
                         "resourceHash": file_sha(source / lifecycle)},
        "extensions": [{"id": "event/example/changed", "kind": "Event", "scope": "World", "thread": "GameThread",
                        "inputType": "int32", "resultType": "void", "signatureHash": "d" * 64}],
    }
    write_json(source / contract_path, contract)
    write_json(source / connection, scope | native | {
        "format": "UMF.ClientABI", "schemaVersion": 2, "adapterABI": "ContextHandlesV1", "abiVersion": 1,
        "clientOwnerModule": "ExampleModClient", "connections": [], "apis": [],
    })
    for name, content in ((client, "// Client fixture\n"), (header, "#pragma once\n")):
        path = source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    public = [{"path": path, "role": role, "contentHash": file_sha(source / path)}
              for path, role in ((contract_path, "Contract"), (connection, "Contract"), (lifecycle, "SdkResource"),
                                 (header, "SdkHeader"), (client, "ClientSource"))]
    public[-1]["ownerModule"] = "ExampleModClient"
    generated = source / "Manifest.json"
    write_json(generated, scope | native | target | {
        "format": "UMF.GeneratedArtifacts", "schemaVersion": 4, "unresolvedTypeCount": 0, "outputLayout": "Flat",
        "generationId": "e" * 64, "generatedFiles": public, "requiredSymbols": [],
        "compilationUnits": [{"source": client, "role": "ClientSource", "ownerModule": "ExampleModClient"}],
    })
    write_json(root / "Example.uproject", {"FileVersion": 3})
    write_json(root / "UnrealEditor.modules", {"BuildId": "Example"})
    uat = root / "Tools/AutomationTool/AutomationTool.dll"
    ubt = root / "Tools/UnrealBuildTool/UnrealBuildTool.dll"
    for placeholder in (uat, ubt):
        placeholder.parent.mkdir(parents=True)
        placeholder.write_bytes(b"SDK plan checks file identity without executing this fixture.")
    request = root / "request.json"
    write_json(request, {
        "format": "UMF.SdkBuildRequest", "schemaVersion": 3,
        "project": "Example.uproject", "gameId": "Example", "pluginName": "ExampleSdk", "sdkVersion": "1.0.0",
        "tools": {"mbt": str(mbt), "dotnet": str(mdt), "automationTool": str(uat)},
        "contract": str(source / contract_path),
        "editor": {"configuration": "Development", "platform": host, "architecture": arch,
                   "modules": "UnrealEditor.modules", "buildId": "Example"},
        "targets": [target | {"generatedManifest": str(generated)}],
    })
    return request, source, header


def command(mdt, verb, argument, path, error=""):
    result = subprocess.run([str(mdt), verb, argument, str(path)], capture_output=True, text=True)
    if error:
        require(result.returncode != 0 and error in result.stderr, f"Expected {error}: {result.stderr}")
        return None
    require(result.returncode == 0, result.stderr)
    return json.loads(result.stdout)


def check_bundle(mdt, root):
    bundle = root / "bundle"
    contract = bundle / "Contract/SdkContract.json"
    write_json(contract, {"format": "UMF.SdkContract", "schemaVersion": 2})
    manifest = {
        "format": "UMF.ModSdkBundle", "schemaVersion": 2, "gameId": "Example", "sdkVersion": "1.0.0",
        "inputFingerprint": "f" * 64, "capabilities": {},
        "files": [{"path": contract.relative_to(bundle).as_posix(), "contentHash": file_sha(contract)}],
    }
    write_json(bundle / "Manifest.json", manifest)
    inspection = command(mdt, "inspect", "--bundle", bundle)
    require(inspection["valid"] and inspection["inputFingerprint"] == manifest["inputFingerprint"],
            "SDK inspection did not confirm the bundle inventory")
    write_json(contract, read_json(contract) | {"tampered": True})
    command(mdt, "inspect", "--bundle", bundle, "Bundle hash mismatch")


def main():
    parser = argparse.ArgumentParser(description="Test native SDK planning and manifest validation")
    parser.add_argument("mdt", type=Path)
    parser.add_argument("mbt", type=Path)
    arguments = parser.parse_args()
    mdt = arguments.mdt.resolve(strict=True)
    mbt = arguments.mbt.resolve(strict=True)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        request, source, header = fixture(mdt, mbt, root)
        first = command(mdt, "plan", "--request", request)
        require(first == command(mdt, "plan", "--request", request) and first["status"] == "Planned"
                and len(first["publicHeaders"]) == 1, "SDK plan is inconsistent with its generated inputs")
        (source / header).write_text("changed", encoding="utf-8")
        command(mdt, "plan", "--request", request, "Duplicate or changed generated input")
        check_bundle(mdt, root)
    print("SDK plan and manifest checks passed.")


if __name__ == "__main__":
    main()
