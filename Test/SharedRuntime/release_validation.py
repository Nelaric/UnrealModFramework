# Copyright (c) 2026 Nelaric Contributors
"""Build the released SDK and exercise the packaged Win64 shared Runtime."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

from .prepare_projects import GAME, ROOT, prepare, stage_host
from .verify_game import verify

EDITOR = GAME + "Editor"
MODULAR = GAME + "Modular"
CONFIG = "Development"
PLATFORM = "Win64"
ARCH = "x64"
LOCK = json.loads((ROOT / ".github/engine-lock.json").read_text(encoding="utf-8"))


def document(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def engine_dir(location):
    engine = location / "Engine" if (location / "Engine").is_dir() else location
    if not (engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll").is_file():
        raise ValueError(f"Expected a complete UE installation at {engine}")
    return engine


class Validation:
    def __init__(self, engine, work, commit, game_target):
        self.engine = engine_dir(engine)
        self.game_target = game_target
        self.work = work
        self.commit = commit
        ubt_base = "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
        self.editor = self.engine / "Binaries/Win64/UnrealEditor-Cmd.exe"
        self.uat = "Binaries/DotNET/AutomationTool/AutomationTool.dll"
        self.ubt = self.engine / ubt_base
        candidates = sorted(self.engine.glob("Binaries/ThirdParty/DotNet/*/win-x64/dotnet.exe"))
        if not candidates or not self.editor.is_file() or not (self.engine / self.uat).is_file():
            raise ValueError("Release validation requires a complete Win64 UE installation with Editor and UAT")
        if game_target == MODULAR and not (self.engine / "Binaries/Win64/UnrealGame.modules").is_file():
            raise ValueError("The selected Modular target needs a full engine with matching Modular Game build products")
        self.dotnet = candidates[-1]
        version = document(self.engine / "Build/Build.version")
        if tuple(version[k] for k in ("MajorVersion", "MinorVersion", "PatchVersion")) != tuple(map(int, LOCK["ueVersion"].split("."))):
            raise ValueError("The release engine version differs from the engine lock")
        self.env = dict(os.environ)
        self.env["UE-LocalDataCachePath"] = str(work / "DDC")
        self.env["UE-SharedDataCachePath"] = "None"
        self.env["uebp_LogFolder"] = str(work / "UATLogs")
        self.tools = ROOT / "UnrealModFramework/Plugins/ModRuntime/Tools"
        for key, tool in (("UMF_MHT_EXECUTABLE", "ModHeaderTool"), ("UMF_MOD_SOURCE_TOOL_EXECUTABLE", "ModSourceTool")):
            self.env[key] = str(self.tool(tool))

    def tool(self, name):
        return self.tools / "build" / name / (name + ".exe")

    def run(self, stage, *command, timeout=3600):
        started = time.monotonic()
        log = self.work / (stage + ".log")
        print(f"Release validation {stage}", flush=True)
        with log.open("w", encoding="utf-8") as output:
            result = subprocess.run([str(x) for x in command], stdout=output, stderr=subprocess.STDOUT, cwd=ROOT,
                                    env=self.env, timeout=timeout, check=False)
        if result.returncode:
            print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]))
            raise RuntimeError(f"{stage} failed with exit code {result.returncode}")
        print(f"UMF timing {stage} {time.monotonic() - started:.3f} s", flush=True)

    def prepare_tools(self):
        self.run("native-tools", "cmd.exe", "/d", "/c", "call", self.tools / "Build.bat", "--jobs", "4", "--target", "all")
        self.run("libclang", "cmake", "-P", self.tools / "SetupLibClang.cmake")
        receipt = self.tools / "build/libclang-Windows-x64.path"
        self.env["UMF_MHT_LIBCLANG"] = str((receipt.parent / receipt.read_text().strip()).resolve())

    def mbt(self, stage, operation, engine, project, target, *arguments):
        self.run(stage, self.tool("ModBuildTool"), operation, "--engine", engine, "--project", project, "--target", target,
                 "--platform", PLATFORM, "--architecture", ARCH, *arguments)

    def build_host(self, project, label):
        bundle = self.work / (label + "-metadata")
        identity = ["--commit", self.commit, "--engine-commit", LOCK["upstreamCommit"]]
        self.mbt(label + "-prepare", "prepare-metadata", self.engine, project, EDITOR, *identity, "--output", bundle)
        for target in (EDITOR, self.game_target):
            self.mbt(label + "-" + target, "build-ue-target", self.engine, project, target, *identity,
                     "--configuration", CONFIG, "--mode", "normal", "--metadata", bundle)

    @staticmethod
    def generated(project, target):
        root = project.parent / "Intermediate/ModTools/ModHeaderTool/Generated" / target / PLATFORM / CONFIG / ARCH
        return root / (GAME + ".generated-artifacts.json")

    def build_sdk(self, producer):
        modules = self.engine / "Binaries/Win64/UnrealEditor.modules"
        version = document(self.tools.parent / "ModRuntime.uplugin")["VersionName"]
        editor_manifest = self.generated(producer, EDITOR)
        output = self.work / "Sdk"
        request = self.work / "SdkRequest.json"
        save(request, {
            "format": "UMF.SdkBuildRequest", "schemaVersion": 3, "project": str(producer), "gameId": GAME,
            "sdkVersion": version, "pluginName": "UMFDevSDK",
            "contract": str(editor_manifest.parent / "Contract" / (GAME + ".sdkcontract.json")),
            "outputDirectory": str(output), "workDirectory": str(self.work / "Mdt"),
            "tools": {"mbt": str(self.tool("ModBuildTool")), "dotnet": str(self.dotnet),
                      "automationTool": str(self.engine / self.uat)},
            "editor": {"platform": PLATFORM, "architecture": ARCH, "configuration": CONFIG,
                       "modules": str(modules), "buildId": document(modules)["BuildId"]},
            "targets": [{"target": target, "platform": PLATFORM, "configuration": CONFIG, "architecture": ARCH,
                         "generatedManifest": str(self.generated(producer, target))} for target in (EDITOR, self.game_target)],
        })
        mdt = self.tool("ModSDKTool")
        self.run("sdk-plan", mdt, "plan", "--request", request, "--output", self.work / "SdkPlan.json")
        self.run("sdk-build", mdt, "build", "--request", request, "--output", self.work / "SdkResult.json")
        self.run("sdk-inspect", mdt, "inspect", "--bundle", output)
        return output

    def cook(self, host, release):
        version = "rc-" + self.commit
        stage = self.work / "Stage"
        self.run("cook-stage", self.dotnet, self.engine / self.uat, "BuildCookRun", f"-project={host}",
                 f"-target={self.game_target}", f"-unrealexe={self.editor}", "-platform=Win64", "-clientconfig=Development",
                 "-nocompile", "-nocompileeditor", "-nop4", "-unattended", "-cook", "-stage", "-pak", "-package",
                 "-skipiostore", "-nodebuginfo", "-map=/Game/Levels/Basic/L_Basic+/Engine/Maps/Entry",
                 f"-createreleaseversion={version}", f"-createreleaseversionroot={release}",
                 f"-stagingdirectory={stage}", "-AdditionalCookerOptions=-ddc=InstalledNoZenLocalFallback "
                 "-package=/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple")
        registry = release / version / "Windows/AssetRegistry.bin"
        with registry.open("rb") as data:
            digest = hashlib.file_digest(data, "sha256").hexdigest()
        save(self.work / "BaseRelease.json", {"commit": self.commit, "version": version, "assetRegistrySha256": digest})
        return version, registry, stage / "Windows"

    def build_extensions(self, sdk):
        build = self.work / "Native"
        self.run("native-extensions-configure", "cmake", "-S", ROOT / "Test/SharedRuntime/Native", "-B", build,
                 "-A", "x64", f"-DUMF_SDK_INCLUDE={sdk / 'Native/Include/SDK/Public'}")
        self.run("native-extensions-build", "cmake", "--build", build, "--config", "Release")
        return build / "Release"

    def package(self, author, name, sdk, extensions, release, version):
        self.run(name + "-editor", self.dotnet, self.ubt, name + "Editor", PLATFORM, CONFIG,
                 f"-Project={author}", "-Architecture=x64", "-NoEngineChanges", "-NoHotReloadFromIDE", "-WaitMutex", "-NoUBA")
        common = ["-unattended", "-nullrhi", "-nosound", "-nosplash", "-nop4", "-stdout",
                  "-ddc=InstalledNoZenLocalFallback"]
        if name == "SharedCaller":
            self.run(name + "-blueprint", self.editor, author, "-run=SharedCallerAssets", *common)
            if not (author.parent / "Content/BP_SharedCaller.uasset").is_file():
                raise RuntimeError("The SharedCaller commandlet did not write its lifecycle Blueprint")
        folder = self.work / (name + "Package")
        folder.mkdir()
        request = folder / "Request.json"
        package = self.work / (name + ".mod")
        description = folder / "Extensions.json"
        extension = {"schemaVersion": 2, "contracts": [{"id": "hook/game.character/change-health"}]}
        if name == "TestMod":
            extension["content"] = [{"slot": "character.mesh", "id": "character.mannequin", "action": "replace",
                                     "asset": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"}]
        save(description, extension)
        save(request, {"format": "UMF.ModPackageBuildRequest", "schemaVersion": 3, "modVersion": "1.0.0",
                       "gameId": GAME, "baseReleaseVersion": version, "baseReleaseDirectory": str(release),
                       "targetPlatform": PLATFORM, "cookConfiguration": CONFIG, "useIoStore": False,
                       "includeEngineContent": False, "extensionsFile": str(description),
                       "sdkContractFile": str(sdk / "Contract/SdkContract.json"),
                       "nativeExtensionFile": str(extensions / f"{name}Extension.dll"),
                       "outputFile": str(package), "timeoutMinutes": 55})
        self.run(name + "-package", self.editor, author, "-run=ModPackage", f"-Request={request}", *common)
        if not package.is_file():
            raise RuntimeError(f"The packaging commandlet did not write {package.name}")
        return package

    def execute(self):
        self.prepare_tools()
        producer = stage_host(self.work / "P")
        self.build_host(producer, "producer")
        sdk = self.build_sdk(producer)
        projects = self.work / "Projects"
        prepare(projects, sdk)
        host = projects / "H" / (GAME + ".uproject")
        self.build_host(host, "shared-runtime")
        release = self.work / "Releases"
        version, registry, stage = self.cook(host, release)
        receipt = host.parent / f"Binaries/Win64/{self.game_target}.target"
        link = (host.parent / "Intermediate/ModTools/ModHeaderTool/HostTargets" / self.game_target /
                PLATFORM / CONFIG / ARCH / "host-link.json")
        extensions = self.build_extensions(sdk)
        test_mod = self.package(projects / "A/TestMod.uproject", "TestMod", sdk, extensions, release, version)
        other_mod = self.package(projects / "B/SharedCaller.uproject", "SharedCaller", sdk, extensions, release, version)
        verify(SimpleNamespace(sdk=sdk, stage=stage, receipt=receipt, link=link, host_project=host,
                               host_manifest=self.generated(host, self.game_target), registry=registry,
                               test_mod=test_mod, other_mod=other_mod, report=self.work / "SharedRuntime"))
        self.run("benchmark", sys.executable, ROOT / "Test/Performance/run_benchmark.py", "--engine", self.engine,
                 "--report", self.work / "Performance.md", timeout=14400)
        save(self.work / "ReleaseValidation.json", {"commit": self.commit, "baseRelease": version,
                                                   "gameTarget": self.game_target,
                                                   "sharedRuntime": "Succeeded", "performance": "Succeeded"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--game-target", choices=(GAME, MODULAR), default=GAME)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if not re.fullmatch(r"[a-f0-9]{40}", args.commit) or head != args.commit:
        raise SystemExit("Release Candidate commit does not match the checkout")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT).strip():
        raise SystemExit("Release validation requires a clean checkout of the Release Candidate")
    try:
        validation = Validation(args.engine.resolve(), args.work.resolve(), args.commit, args.game_target)
        validation.work.mkdir(parents=True, exist_ok=False)
        validation.execute()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
