# Copyright (c) 2026 Nelaric Contributors
"""Compare native MST cross-file error and rethrow analysis with GCC and Clang."""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

mst = Path(sys.argv[1]).resolve(strict=True)
resource_dir = subprocess.check_output(["clang++-19", "-print-resource-dir"], text=True).strip()
with tempfile.TemporaryDirectory(prefix="mst-compilers-") as directory:
    root = Path(directory)
    tools = Path("UnrealModFramework/Plugins/ModRuntime/Tools")
    common = root / "Common"
    shutil.copytree(tools / "Common", common)
    response = common / "Process/ResponseFile.cpp"
    filesystem = common / "Files/File.cpp"
    sources = (
        response, filesystem,
        common / "Process/ProcessArguments.cpp",
        common / "Files/Path.cpp",
        common / "Files/TemporaryName.cpp",
    )
    member = "api/ci/build-tool/write-response"
    function = "Mod::BuildTool::ResponseFile::WriteResponseFile"

    previous = None
    for compiler in ("g++", "clang++-19"):
        driver = shutil.which(compiler)
        if not driver:
            raise SystemExit(f"Missing native compiler {compiler}")
        directory = root / compiler
        directory.mkdir()
        database = directory / "compile_commands.json"
        database.write_text(json.dumps([{
            "directory": str(directory),
            "file": str(source),
            "arguments": [
                driver, "-std=c++20", "-O2", "-g", "-fexceptions", "-I" + str(root),
                "-DUE_TARGET_NAME=MSTNative", '-DUE_MODULE_NAME="ModBuildTool"',
                "-DWITH_EDITOR=0", "-c", str(source), "-o", source.with_suffix(".o").name,
            ],
        } for source in sources]), encoding="utf-8")
        request = directory / "SourceAnalysisRequest.json"
        request.write_text(json.dumps({
            "format": "UMF.SourceAnalysisRequest",
            "schemaVersion": 2,
            "compilationDatabase": str(database),
            "target": "MSTNative",
            "projectRoot": str(root),
            "members": [{
                "id": member,
                "function": function,
                "module": "ModBuildTool",
                "source": str(response),
                "translationUnit": str(response),
            }],
            "libclang": "/usr/lib/llvm-19/lib/libclang.so",
            "resourceDirectory": resource_dir,
            "analysisArguments": [],
        }), encoding="utf-8")
        command = [str(mst), "analyze-errors", str(request)]
        (result,) = json.loads(subprocess.check_output(command, text=True))["members"]
        failures = result["failures"]
        direct = any(
            item["originFunction"] == function
            and item["exitKind"] == "throw" and not item["callChain"]
            for item in failures
        )
        rethrow = any(
            item["originFunction"] == "Mod::BuildTool::File::WriteFileAtomically"
            and item["exitKind"] == "throw" and item["callChain"] and item.get("rethrowLocations")
            for item in failures
        )
        if not direct or not rethrow or str(filesystem) not in result["analyzedFiles"]:
            raise SystemExit(f"{compiler} did not resolve the direct and cross-file rethrow paths")
        codes = {item["key"]: (item["macro"], item["code"]) for item in failures}
        print(compiler, json.dumps(codes, sort_keys=True), flush=True)
        if previous is not None and codes != previous:
            raise SystemExit("GCC and Clang disagree on the generated source error codes")
        previous = codes
