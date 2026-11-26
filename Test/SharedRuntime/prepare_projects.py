# Copyright (c) 2026 Nelaric Contributors
"""Prepare independent sample game and Mod projects for the packaged shared Runtime test."""
import argparse
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAME = "UnrealModFrameworkDev"


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def prepare(work, sdk):
    sdk = sdk.resolve()
    if not (sdk / "Host/Plugins/UMFDevSDK/UMFDevSDK.uplugin").is_file():
        raise ValueError("A published sample SDK with the game Runtime is required")
    work.mkdir(parents=True, exist_ok=True)
    if any((work / folder).exists() for folder in ("H", "A", "B")):
        raise ValueError("Choose a fresh work directory for the three project copies")
    game = ROOT / "UnrealModFramework"
    sample = ROOT / "ExampleMods/TestMod"
    for source, name, folder in ((game, GAME, "H"), (sample, "TestMod", "A")):
        destination = work / folder
        for directory in ("Content", "Config", "Source"):
            shutil.copytree(source / directory, destination / directory)
        shutil.copy2(source / (name + ".uproject"), destination)
    host = work / "H"
    for file in game.glob("*.modexport"):
        shutil.copy2(file, host)
    framework = game / "Plugins/ModRuntime"
    installed = host / "Plugins/ModRuntime"
    for item in ("Source", "Resources", "Config"):
        if (framework / item).is_dir():
            shutil.copytree(framework / item, installed / item)
    shutil.copy2(framework / "ModRuntime.uplugin", installed)
    shutil.copytree(sdk / "Host/Plugins/UMFDevSDK", host / "Plugins/UMFDevSDK")
    shutil.copytree(HERE / "Host/SharedRuntimeChecks", host / "Source/SharedRuntimeChecks")
    descriptor = host / (GAME + ".uproject")
    project = json.loads(descriptor.read_text(encoding="utf-8-sig"))
    project["Plugins"].append({"Name": "UMFDevSDK", "Enabled": True})
    project["Modules"].append({"Name": "SharedRuntimeChecks", "Type": "Runtime", "LoadingPhase": "Default", "TargetAllowList": ["Game"]})
    save(descriptor, project)
    target = host / "Source/UnrealModFrameworkDevModular.Target.cs"
    target.write_text(target.read_text().replace("bExplicitTargetForType = true;",
                      'ExtraModuleNames.Add("SharedRuntimeChecks");\n\t\tbExplicitTargetForType = true;'))
    second = work / "B"
    shutil.copytree(HERE / "SharedCaller", second)
    for suffix in (".Target.cs", "Editor.Target.cs"):
        template = (sample / "Source" / ("TestMod" + suffix)).read_text()
        (second / "Source" / ("SharedCaller" + suffix)).write_text(template.replace("TestMod", "SharedCaller"))
    save(second / "SharedCaller.uproject", {
        "FileVersion": 3, "EngineAssociation": "5.6", "DisableEnginePluginsByDefault": True,
        "Modules": [{"Name": "SharedCaller", "Type": "Runtime", "LoadingPhase": "Default"}],
        "Plugins": [{"Name": "UMFDevSDK", "Enabled": True},
                    {"Name": "ModDevelopmentSDKEditor", "Enabled": True, "TargetAllowList": ["Editor"]},
                    {"Name": "PythonScriptPlugin", "Enabled": True, "TargetAllowList": ["Editor"]}]})
    for project_dir in (work / "A", second):
        shutil.copytree(sdk / "Plugins", project_dir / "Plugins")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.work, args.sdk)
