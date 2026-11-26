# Copyright (c) 2026 Nelaric Contributors
"""Build a native Development target and run its Unreal automation suite."""

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

from check_automation_report import check_report

HOST = "UnrealModFrameworkDev"
TEST_PREFIX = "Nelaric.UnrealModFramework.Host."
HOST_TESTS = (TEST_PREFIX + "ChangeHealth", TEST_PREFIX + "ChangeHealthDispatch")


def run(*args: str | Path) -> None:
    print(subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    subprocess.run(args, check=True)


def stage_game(engine: Path, dotnet: Path, editor: Path, project: Path, ue_platform: str,
               arch: str, stage: Path, receipt: dict) -> Path:
    cooker_arch = "x64" if (ue_platform, arch) == ("Win64", "arm64") else arch
    run(dotnet, engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll", HOST + "Editor",
        ue_platform, "Development", f"-Project={project}", f"-architecture={cooker_arch}", "-NoHotReload")
    run(dotnet, engine / "Binaries/DotNET/AutomationTool/AutomationTool.dll", "BuildCookRun",
        f"-project={project}", f"-target={HOST}", f"-platform={ue_platform}", f"-clientarchitecture={arch}",
        "-clientconfig=Development", "-nocompile", "-nocompileeditor", "-nop4", "-unattended", "-utf8output",
        f"-unrealexe={editor}", "-cook", "-stage", "-pak", "-skipiostore", "-nodebuginfo",
        "-map=/Engine/Maps/Entry", "-AdditionalCookerOptions=-nullrhi -unattended",
        f"-stagingdirectory={stage}")
    # Windows also stages a bootstrap with the same basename. Run the binary from UBT's receipt.
    target_receipt = json.loads(Path(receipt["ueTargetReceipt"]["path"]).read_text(encoding="utf-8-sig"))
    name = Path(target_receipt["Launch"]).name
    candidates = [path for path in stage.rglob(name) if path.is_file()
                  and ("Binaries" in path.parts or path.parent.name == "MacOS")]
    if len(candidates) != 1:
        raise ValueError(f"Expected one staged {name} for {arch}, found {candidates}.")
    return candidates[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    parser.add_argument("ue_platform", choices=("Win64", "Mac", "Linux", "LinuxArm64"))
    parser.add_argument("architecture", choices=("x64", "arm64"))
    parser.add_argument("target", choices=("Game", "Editor"))
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    arch = args.architecture
    ue_platform = args.ue_platform
    machine = platform.machine().lower()
    host_arch = {"amd64": "x64", "x86_64": "x64", "aarch64": "arm64", "arm64": "arm64"}.get(machine)
    host_platform = {"Windows": "Win64", "Darwin": "Mac",
                     "Linux": "LinuxArm64" if host_arch == "arm64" else "Linux"}[platform.system()]
    if (host_platform, host_arch) != (ue_platform, arch):
        raise ValueError(f"Native host is {host_platform}/{machine}, expected {ue_platform}/{arch}.")

    engine = args.engine_root.absolute() / "Engine"
    version = json.loads((engine / "Build/Build.version").read_text(encoding="utf-8-sig"))
    if [version[key] for key in ("MajorVersion", "MinorVersion", "PatchVersion")] != [5, 6, 1]:
        raise ValueError("Desktop CI requires Unreal Engine 5.6.1.")
    project_root = Path.cwd() / "UnrealModFramework"
    project = project_root / (HOST + ".uproject")
    tools = project_root / "Plugins/ModRuntime/Tools/build"
    suffix = ".exe" if ue_platform == "Win64" else ""
    mbt = tools / "ModBuildTool" / ("ModBuildTool" + suffix)
    mdt = tools / "ModSDKTool" / ("ModSDKTool" + suffix)
    binaries = engine / "Binaries" / ue_platform
    editor = binaries / {"Win64": "UnrealEditor-Cmd.exe", "Mac": "UnrealEditor.app/Contents/MacOS/UnrealEditor"
                         }.get(ue_platform, "UnrealEditor")
    host = {"Win64": "win", "Mac": "mac"}.get(ue_platform, "linux")
    dotnet_dir = engine / "Binaries/ThirdParty/DotNet"
    runtimes = sorted(dotnet_dir.glob(f"*/{host}-{arch}/dotnet{suffix}"))
    if not runtimes and host == "mac":
        runtimes = sorted(dotnet_dir.glob(f"*/osx-{arch}/dotnet"))
    if not runtimes and (host, arch) == ("win", "arm64"):
        runtimes = sorted(dotnet_dir.glob("*/win-x64/dotnet.exe"))
    if not runtimes:
        raise ValueError(f"Missing bundled .NET host for {host}-{arch} in {dotnet_dir}.")
    dotnet = runtimes[-1]
    os.environ["DOTNET_ROOT"] = str(dotnet.parent)
    os.environ["PATH"] = str(dotnet.parent) + os.pathsep + os.environ["PATH"]
    target = HOST + ("Editor" if args.target == "Editor" else "")
    results = args.results.absolute()
    results.mkdir(parents=True, exist_ok=False)
    run(mbt, "build-ue-target", "--engine", engine.parent, "--project", project,
        "--target", target, "--platform", ue_platform, "--configuration", "Development",
        "--architecture", arch, "--mode", "normal")
    receipt_path = (project_root / "Intermediate/ModTools/ModHeaderTool/HostTargets" / target
                    / ue_platform / "Development" / arch / "host-link.json")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    expected = {"format": "UMF.UbtHostLinkReceipt", "target": target, "configuration": "Development",
                "platform": ue_platform, "architecture": arch}
    if any(receipt.get(key) != value for key, value in expected.items()) or not receipt.get("binaries"):
        raise ValueError(f"Invalid host-link receipt for {target}/{ue_platform}/{arch}.")
    shutil.copyfile(receipt_path, results / "host-link.json")

    if args.target == "Editor":
        executable = editor
        arguments = [str(project)]
    else:
        executable = stage_game(engine, dotnet, editor, project, ue_platform, arch,
                                results.parent / (results.name + "-stage"), receipt)
        arguments = ["/Engine/Maps/Entry"]
    if ue_platform == "Mac":
        # A stock macOS Editor can be universal. Require the intended slice.
        launch = ["/usr/bin/arch", "-arm64" if arch == "arm64" else "-x86_64", executable]
    else:
        run(mbt, "inspect-binary", "--file", executable, "--platform", ue_platform,
            "--architecture", arch, "--kind", "Executable")
        launch = [executable]
    log = results / "automation.log"
    report = results / "report"
    process = subprocess.run([*launch, *arguments, f"-ExecCmds=Automation RunTests {TEST_PREFIX};SoftQuit",
                              "-TestExit=Automation Test Queue Empty", "-unattended", "-nullrhi", "-nosound",
                              "-nosplash", "-NoCrashReports", f"-abslog={log}", f"-ReportExportPath={report}"],
                             timeout=1200)
    check_report(report / "index.json", log, process.returncode, required_tests=HOST_TESTS)
    if args.target == "Editor":
        run(mdt, "build-editor", "--package", results.parent / (results.name + "-plugin"), "--dotnet", dotnet,
            "--automation-tool", engine / "Binaries/DotNET/AutomationTool/AutomationTool.dll",
            "--editor-modules", binaries / "UnrealEditor.modules", "--architecture", arch, "--mbt", mbt)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
