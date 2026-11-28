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


FIXTURES = ROOT / "Test/ReleaseFixtures"


def stage_host(destination):
    game = ROOT / "UnrealModFramework"
    for directory in ("Content", "Config", "Source"):
        shutil.copytree(game / directory, destination / directory, ignore=shutil.ignore_patterns(
            "User*.ini", "DefaultEditorPerProjectUserSettings.ini"))
    descriptor = destination / (GAME + ".uproject")
    shutil.copy2(game / descriptor.name, descriptor)
    for file in game.glob("*.modexport"):
        shutil.copy2(file, destination)
    framework = game / "Plugins/ModRuntime"
    installed = destination / "Plugins/ModRuntime"
    for item in ("Source", "Resources", "Config"):
        if (framework / item).is_dir():
            shutil.copytree(framework / item, installed / item, ignore=shutil.ignore_patterns(
                "bin", "obj", "*.csproj.props", "*.props.user"))
    shutil.copy2(framework / "ModRuntime.uplugin", installed)
    level = Path("Content/Levels/Basic/L_Basic.umap")
    (destination / level).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(FIXTURES / level, destination / level)
    for name in ("DefaultEngine.ini", "DefaultGame.ini"):
        fixture = FIXTURES / "Config" / name
        ini = destination / "Config" / fixture.name
        contents = ini.read_text(encoding="utf-8") + "\n" if ini.is_file() else ""
        fixture_text = fixture.read_text(encoding="utf-8")
        if contents:
            fixture_text = fixture_text.split("\n", 1)[1].lstrip("\n")
        ini.write_text(contents + fixture_text, encoding="utf-8", newline="\n")
    return descriptor


def prepare(work, sdk):
    sdk = sdk.resolve()
    if not (sdk / "Host/Plugins/UMFDevSDK/UMFDevSDK.uplugin").is_file():
        raise ValueError("A published sample SDK with the game Runtime is required")
    work.mkdir(parents=True, exist_ok=True)
    if any((work / folder).exists() for folder in ("H", "A", "B")):
        raise ValueError("Choose a fresh work directory for the three project copies")
    sample = ROOT / "ExampleMods/TestMod"
    for directory in ("Content", "Config", "Source"):
        shutil.copytree(sample / directory, work / "A" / directory)
    shutil.copy2(sample / "TestMod.uproject", work / "A")
    host = work / "H"
    stage_host(host)
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
        "Modules": [{"Name": "SharedCaller", "Type": "Runtime", "LoadingPhase": "Default"},
                    {"Name": "SharedCallerEditor", "Type": "Editor", "LoadingPhase": "Default"}],
        "Plugins": [{"Name": "UMFDevSDK", "Enabled": True},
                    {"Name": "ModDevelopmentSDKEditor", "Enabled": True, "TargetAllowList": ["Editor"]}]})
    for project_dir in (work / "A", second):
        shutil.copytree(sdk / "Plugins", project_dir / "Plugins")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.work, args.sdk)
