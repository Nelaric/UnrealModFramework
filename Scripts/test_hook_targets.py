#!/usr/bin/env python3
# Copyright (c) 2026 Nelaric Contributors
"""Cross-compile a generated ChangeHealth Hook and check the link against the original object."""

import argparse
import json
import os
import platform as host_platform
import shutil
import subprocess
import sys
import tempfile
from itertools import combinations
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from UnrealModFramework.Plugins.ModRuntime.Tools.verify_macho_hooks import verify_target
from test_metadata_bundle import ENGINE_COMMIT, SOURCE_COMMIT, digest, encoded, variant

EXPORT_HEADERS = (
    Path(__file__).resolve().parents[1]
    / "UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public"
)


def write_json(path, document):
    path.write_text(json.dumps(document), encoding="utf-8")
    return str(path)


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


def write_hook_options(root, project):
    groups = (
        "APIScopes APIThreads APIPermissions APIFailureRules EventSourceScopes EventPublishRules "
        "HookStages HookOperations ExtensionAuthorities"
    ).split()
    choices = {group: [] for group in groups}
    choices.update(
        HookStages=["Pre", "On", "Post"],
        HookOperations=["ModifyInputs", "Cancel", "Replace"],
        ExtensionAuthorities=["Any", "Server"],
    )
    return write_json(
        root / "Options.json",
        {
            "SchemaVersion": 5,
            "Project": project,
            "Options": choices,
            "ConfigRoots": [],
            "InputFiles": [],
            "Inputs": {},
            "FunctionRedirects": [],
        },
    )


def verify_module_selection(root, mht, mbt):
    root.mkdir()
    project = write_json(root / "Game.uproject", {})
    options = write_hook_options(root, project)
    plugin = root / "Plugins/Features/Combat"
    plugin.mkdir(parents=True)
    write_json(plugin / "Combat.uplugin", {})
    audio = root / "Plugins/Audio"
    audio.mkdir()
    write_json(audio / "Audio.uplugin", {})
    marked = {"CombatAPI", "AudioAPI", "GameEditor"}
    for tree, name in (
        (root / "Source", "Game"),
        (root / "Source/Editor", "GameEditor"),
        (plugin / "Source/Runtime", "CombatAPI"),
        (plugin / "Source/Editor", "CombatEditor"),
        (audio / "Source/Runtime", "AudioAPI"),
        (audio / "Source/Editor", "AudioEditor"),
    ):
        directory = tree / name
        directory.mkdir(parents=True)
        (directory / f"{name}.Build.cs").touch()
        source = "MOD_EXPORT_HOOK()\n" if name in marked else f"// {name}\n"
        (directory / "Unit.cpp").write_text(source, encoding="utf-8")
    descriptor = root / "Game.modexport"

    def check(expected):
        modules = json.loads(run([mht, "discover", project], capture_output=True).stdout)
        assert [item["name"] for item in modules] == sorted(expected), modules
        assert {item["name"] for item in modules if item["hasMarkers"]} == marked.intersection(expected)
        report = json.loads(run([mht, "scan", project, options], capture_output=True).stdout)
        assert sorted(Path(path).name for path in report["Directories"]) == sorted(expected), report
        assert report["ScannedFileCount"] == len(expected)
        assert {Path(entry["File"]).parent.name for entry in report["Exports"]} == marked.intersection(expected)
        return report

    check(["Game", "GameEditor"])
    write_json(descriptor, {"Plugins": ["Combat"], "ExcludeModules": ["GameEditor", "combateditor"]})
    check(["Game", "CombatAPI"])
    write_json(descriptor, {"Plugins": ["Plugins/Features/Combat/Combat.uplugin"], "ExcludeModules": ["Game"]})
    check(["GameEditor", "CombatAPI", "CombatEditor"])
    selection = {
        "ExcludeModules": ["GameEditor", "AudioEditor"],
        "Plugins": [{"Name": "combat", "ExcludeModules": ["combateditor", "Game", "AudioAPI"]}, "Audio"],
    }
    write_json(descriptor, selection)
    report = check(["Game", "CombatAPI", "AudioAPI"])
    watches = write_json(root / "watches.json", {
        "status": "Complete", "watchedDirectories": report["WatchedDirectories"], "inputFiles": [],
    })
    refresh = [mbt, "refresh-ue-watches", "--contexts", watches]
    stamp = root / ".mht-watch.stamp"
    run(refresh, capture_output=True)
    assert not stamp.exists()
    selection["Plugins"][0]["ExcludeModules"].remove("combateditor")
    write_json(descriptor, selection)
    run(refresh, capture_output=True)
    assert stamp.is_file()
    check(["Game", "CombatAPI", "CombatEditor", "AudioAPI"])
    write_json(descriptor, {"Plugins": [
        {"Name": "Combat", "ExcludeModules": ["CombatEditor", "AudioEditor"]},
        {"Path": "Plugins/Audio/Audio.uplugin", "ExcludeModules": ["audioapi", "GameEditor", "CombatAPI"]},
    ]})
    check(["Game", "GameEditor", "CombatAPI", "AudioEditor"])
    write_json(descriptor, {"Plugins": [
        {"Path": "Plugins/Features/Combat/Combat.uplugin", "ExcludeModules": ["CombatAPI"]},
        "Combat",
        {"Name": "Combat", "ExcludeModules": ["CombatEditor"]},
    ]})
    check(["Game", "GameEditor"])
    write_json(descriptor, {"ExcludeModules": ["Game", "GameEditor"]})
    check([])
    write_json(descriptor, {"Plugins": ["MissingPlugin"]})
    invalid = subprocess.run([mht, "discover", project], capture_output=True, text=True)
    assert invalid.returncode and "MissingPlugin" in invalid.stderr
    for entry in (
        {"ExcludeModules": ["CombatEditor"]},
        {"Name": "Combat", "Path": "Plugins/Features/Combat/Combat.uplugin"},
        {"Name": "Combat", "ExcludeModules": "CombatEditor"},
        {"Path": "Plugins/Features/Combat"},
    ):
        write_json(descriptor, {"Plugins": [entry]})
        invalid = subprocess.run([mht, "discover", project], capture_output=True, text=True)
        assert invalid.returncode and "Plugins[0]" in invalid.stderr, invalid.stderr


def prepare_fake_ubt(root):
    engine = root / "Fake Engine"
    (engine / "Source").mkdir(parents=True)
    system = {"win32": "win", "linux": "linux", "darwin": "mac"}[sys.platform]
    architecture = "arm64" if host_platform.machine().lower() in ("arm64", "aarch64") else "x64"
    runtime = engine / f"Binaries/ThirdParty/DotNet/8.0/{system}-{architecture}"
    runtime.mkdir(parents=True)
    if os.name == "nt":
        shutil.copy2(sys.executable, runtime / "dotnet.exe")
        for library in Path(sys.base_prefix).glob("*.dll"):
            shutil.copy2(library, runtime / library.name)
    else:
        (runtime / "dotnet").symlink_to(Path(sys.executable).resolve())
    (engine / "Build").mkdir()
    write_json(engine / "Build/Build.version", {"MajorVersion": 5, "MinorVersion": 6, "PatchVersion": 1})
    ubt = engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
    ubt.parent.mkdir(parents=True)
    ubt.write_text(
        r'''import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

def file_path(value):
    path = os.path.normpath(value)
    if os.name == "nt" and not path.startswith("\\\\?\\"):
        path = "\\\\?\\" + path
    return Path(path)


options = dict(arg[1:].split("=", 1) for arg in sys.argv[1:] if arg.startswith("-") and "=" in arg)
if options.get("Mode") == "Execute":
    actions = json.loads(file_path(options["Actions"]).read_bytes())["Actions"]
    for action in actions:
        if action["Type"] == "Compile":
            assert not action["bCanExecuteRemotely"] and not action["bCanExecuteInUBA"]
            assert " HookSource=" in action["CommandVersion"]
            program, arguments = action["CommandPath"], action["CommandArguments"]
            if Path(program).stem == "cl-filter":
                _, delimiter, child = arguments.partition(" -- ")
                assert delimiter, arguments
                command = child
            else:
                command = f'"{program}" {arguments}' if os.name == "nt" else [program, *shlex.split(arguments)]
            subprocess.run(command, cwd=action["WorkingDirectory"], check=True)
        elif action["Type"] == "Link":
            assert action["PrerequisiteItems"] == actions[0]["ProducedItems"]
        elif action["Type"] == "PostBuildStep":
            for link in actions:
                if link["Type"] == "Link":
                    for output in link["ProducedItems"]:
                        assert action["PrerequisiteItems"].count(output) == 1, (output, action)
else:
    project = file_path(options["Project"]).parent
    shutil.copyfile(project / "CompileActions.json", file_path(options["WriteOutdatedActions"]))
''',
        encoding="utf-8",
    )
    return engine


def verify_empty_host_finalization(root, mbt):
    root.mkdir()
    project = write_json(root / "Game.uproject", {})
    manifest = root / "Game.generated-artifacts.json"
    batch = {
        "format": "UMF.UbtHostArtifacts", "status": "Complete", "gameId": "Game", "target": "Game",
        "platform": "Win64", "configuration": "Development", "architecture": "x64", "hostBuildContexts": {},
        "compilationUnits": [{"ownerModule": "GameModClient", "source": "Client/GameModClient.modclient.cpp"}],
    }
    write_json(manifest, batch)
    contexts = write_json(root / "contexts.json", {
        "format": "UMF.ModHostContextSet", "schemaVersion": 1, "project": project,
        "generatedManifest": str(manifest), "contexts": {},
    })
    receipt = write_json(root / "Game.target", {
        "Project": project, "TargetName": "Game", "Platform": "Win64", "Configuration": "Development",
        "Architecture": "x64",
    })
    args = [mbt, "finalize-ue-target", "--contexts", contexts, "--ue-target-receipt", receipt]
    assert json.loads(run(args, capture_output=True).stdout) == []
    batch["compilationUnits"].append({"ownerModule": "Game", "source": "Host/Game/Game.modhost.cpp"})
    write_json(manifest, batch)
    assert subprocess.run(args, capture_output=True).returncode != 0


def verify_direct_host_finalization(root, args):
    # Use actual cross-linked images. A stale generation must fail even when its text exists as data.
    for platform, arch, triple, suffix in (
        ("Win64", "x64", "x86_64-pc-windows-msvc", ".dll"),
        ("Win64", "arm64", "aarch64-pc-windows-msvc", ".dll"),
        ("Linux", "x64", "x86_64-unknown-linux-gnu", ".so"),
        ("LinuxArm64", "arm64", "aarch64-unknown-linux-gnu", ".so"),
        ("Mac", "x64", "x86_64-apple-macos12", ".dylib"),
        ("Mac", "arm64", "arm64-apple-macos12", ".dylib"),
    ):
        directory = root / platform / arch
        directory.mkdir(parents=True)
        project = write_json(directory / "Game.uproject", {})
        generation = "a" * 64
        stale = "b" * 64
        prefix = "UMF_HostBinding_Game_"
        source = directory / "owner.c"
        source.write_text(
            "#ifdef _WIN32\n#define API __declspec(dllexport)\n#else\n"
            '#define API __attribute__((visibility("default")))\n#endif\n'
            f"API int {prefix}{generation}(void) {{ return 1; }}\n"
            f'API const char *unrelated(void) {{ return "{prefix}{stale}"; }}\n')
        image = directory / ("Owner" + suffix)
        flags = {"Win64": ["-Wl,/noentry"], "Linux": ["-fPIC"], "LinuxArm64": ["-fPIC"], "Mac": ["-Wl,-platform_version,macos,12.0,12.0"]}[platform]
        run([args.clang, "-target", triple, "-fuse-ld=lld", "-shared", "-nostdlib", *flags, source, "-o", image])
        write_json(directory / "Game.modules", {"BuildId": "1", "Modules": {"Game": image.name}})
        batch = {"format": "UMF.GeneratedArtifacts", "schemaVersion": 4, "status": "Complete", "unresolvedTypeCount": 0,
                 "target": "Game", "platform": platform, "configuration": "Development", "architecture": arch,
                 "contractHash": generation, "projectionHash": generation, "nativeABIHash": generation, "generationId": generation,
                 "compilationUnits": [{"ownerModule": "Game", "role": "HostBinding", "source": "Host/Game/Game.modhost.cpp"}]}
        manifest = directory / "Generated.json"
        write_json(manifest, batch)
        contexts = write_json(directory / "watch.json", {"format": "UMF.ModHostContextSet", "schemaVersion": 1,
                              "project": project, "generatedManifest": str(manifest), "contexts": {}})
        receipt = write_json(directory / "Game.target", {"Project": project, "TargetName": "Game", "Platform": platform,
                             "Configuration": "Development", "Architecture": arch, "Version": {"BuildId": "1"},
                             "BuildProducts": [{"Path": "$(ProjectDir)/Game.modules", "Type": "RequiredResource"},
                                               {"Path": "$(ProjectDir)/" + image.name, "Type": "DynamicLibrary"}]})
        command = [args.mbt, "finalize-ue-target", "--contexts", contexts, "--ue-target-receipt", receipt]
        result = json.loads(run(command, capture_output=True).stdout)
        assert result["binaries"][0]["linkWitness"] == prefix + generation
        batch["generationId"] = stale
        write_json(manifest, batch)
        assert subprocess.run(command, capture_output=True).returncode != 0
        print(f'{platform}/{arch}: current host binding verified, stale symbol rejected')


def write_build_bundle(project, platform, arch):
    root = Path(project).parent
    if platform in {"LinuxArm64", "Android"}:
        producer_platform = "Linux"
    elif platform in {"IOS", "TVOS", "VisionOS"}:
        producer_platform = "Mac"
    else:
        producer_platform = platform
    producer_arch = (arch if platform == "Mac" else "arm64") if producer_platform == "Mac" else "x64"
    producer = {**variant("GameEditor", producer_arch), "platform": producer_platform}
    consumers = [producer, {**variant("Game", producer_arch), "platform": producer_platform}]
    if producer_platform == "Win64":
        pairs = [("Win64", "arm64"), ("Win64", "arm64ec")]
    elif producer_platform == "Linux":
        pairs = [("LinuxArm64", "arm64"), ("Android", "x64"), ("Android", "arm64")]
    else:
        pairs = [(target, cpu) for target, simulator in
                 (("IOS", "iossimulator"), ("TVOS", "tvossimulator"), ("VisionOS", "iossimulator"))
                 for cpu in ("arm64", simulator)]
    consumers.extend({**variant("Game", cpu), "platform": target} for target, cpu in pairs)
    sources = {"Project/Game.uproject": digest(Path(project).read_bytes())}
    for folder in ("Config", "Source", "Plugins", "Platforms"):
        for path in (root / folder).rglob("*"):
            if path.suffix.lower() in {".ini", ".json", ".uplugin", ".cs", ".csproj", ".h", ".hpp", ".inl",
                                      ".c", ".cpp", ".cc", ".mm"} and path.is_file():
                content = path.read_bytes().removeprefix(b"\xef\xbb\xbf").replace(b"\r\n", b"\n")
                sources["Project/" + path.relative_to(root).as_posix()] = digest(content)
    scope = root / "Game.modexport"
    if scope.is_file():
        sources["Project/Game.modexport"] = digest(scope.read_bytes())
    bundle = root / "Metadata"
    bundle.mkdir()
    payloads = {
        "options.json": {"Project": "Project/Game.uproject", "SchemaVersion": 5, "Options": {}, "FunctionRedirects": []},
        "blueprints.json": {"format": "UMF.BlueprintExports", "schemaVersion": 1, "inputs": [], "types": []},
        "inputs.json": {"sources": sources, "assets": {}, "assetInventory": {},
                        "targetTypes": {"Game": "Game", "GameEditor": "Editor"}, "configRoots": [], "config": {}},
    }
    hashes = {}
    for name, payload in payloads.items():
        bytes_ = encoded(payload)
        (bundle / name).write_bytes(bytes_)
        hashes[name] = digest(bytes_)
    write_json(bundle / "manifest.json", {
        "format": "UMF.MetadataBundle", "schemaVersion": 1, "generatorVersion": "ModBuildTool.Metadata/2",
        "project": "Project/Game.uproject", "sourceCommit": SOURCE_COMMIT,
        "engine": {"major": 5, "minor": 6, "patch": 1, "upstreamCommit": ENGINE_COMMIT},
        "producer": producer, "consumers": consumers, "payloads": hashes,
    })
    return ["--commit", SOURCE_COMMIT, "--engine-commit", ENGINE_COMMIT, "--metadata", bundle]


def write_build_receipt(project, generated, platform, arch, clang, triple, short_root):
    if platform not in {"Win64", "Linux", "LinuxArm64", "Mac"} or arch not in {"x64", "arm64"}:
        return
    root = Path(project).parent
    batch_file = generated / "Game.generated-artifacts.json"
    generation = json.loads(batch_file.read_bytes())["generationId"]
    source = short_root / f"binding-{platform}-{arch}.c"
    image = source.with_suffix({"Win64": ".dll", "Mac": ".dylib"}.get(platform, ".so"))
    source.write_text(
        "#ifdef _WIN32\n#define API __declspec(dllexport)\n#else\n#define API __attribute__((visibility(\"default\")))\n#endif\n"
        f"API int UMF_HostBinding_Game_{generation}(void) {{ return 1; }}\n"
    )
    flags = {"Win64": ["-Wl,/noentry"], "Mac": ["-Wl,-platform_version,macos,13.0,13.0"]}.get(platform, ["-fPIC"])
    run([clang, "-target", triple, "-fuse-ld=lld", "-shared", "-nostdlib", *flags, source, "-o", image])
    binaries = root / "Binaries" / platform
    binaries.mkdir(parents=True)
    shutil.copy2(image, binaries / image.name)
    write_json(binaries / "Game.modules", {"BuildId": "1", "Modules": {"Game": image.name}})
    write_json(binaries / "Game.target", {
        "Project": project, "TargetName": "Game", "Platform": platform, "Configuration": "Development",
        "Architecture": arch, "Version": {"BuildId": "1"},
        "BuildProducts": [{"Path": f"$(ProjectDir)/Binaries/{platform}/Game.modules", "Type": "RequiredResource"},
                          {"Path": f"$(ProjectDir)/Binaries/{platform}/{image.name}", "Type": "DynamicLibrary"}],
    })
    variants = root / "Intermediate/ModTools/ModHeaderTool/HostTargets/Game" / platform / "Development" / arch
    variants.mkdir(parents=True)
    write_json(variants / "watch.json", {
        "format": "UMF.ModHostContextSet", "schemaVersion": 1, "project": str(project), "contexts": {},
        "generatedManifest": str(batch_file),
    })


def verify_long_project_path(root, args):
    prefix = "\\\\?\\" if os.name == "nt" else ""
    with tempfile.TemporaryDirectory(dir=prefix + str(root)) as directory:
        component = "long-project-directory-" * 5
        project_root = Path(directory) / component / component / component
        project = project_root / "Game.uproject"
        project_arg = str(project).removeprefix(prefix)
        assert len(project_arg) > 260
        config = project_root / "Config"
        config.mkdir(parents=True)
        write_json(project, {"FileVersion": 3})
        write_json(project_root / "CompileActions.json", {"Actions": []})

        variant = Path("Game/Win64/Development/x64")
        mod_tools = project_root / "Intermediate/ModTools"
        generated = mod_tools / "ModHeaderTool/Generated" / variant
        generated.mkdir(parents=True)
        write_json(
            generated / "Game.generated-artifacts.json",
            {
                "format": "UMF.GeneratedArtifacts",
                "status": "Complete",
                "target": "Game",
                "platform": "Win64",
                "configuration": "Development",
                "architecture": "x64",
                "generatedFiles": [], "schemaVersion": 4, "unresolvedTypeCount": 0,
                "generationId": "a" * 64, "contractHash": "a" * 64, "projectionHash": "a" * 64,
                "nativeABIHash": "a" * 64, "compilationUnits": [{"ownerModule": "Game", "role": "HostBinding"}],
            },
        )
        write_build_receipt(str(project), generated, "Win64", "x64", args.clang, "x86_64-pc-windows-msvc", root)
        bundle = write_build_bundle(project, "Win64", "x64")
        run(
            [
                args.mbt, "build-ue-target", "--engine", args.engine, "--project", project_arg,
                "--target", "Game", "--platform", "Win64", "--configuration", "Development",
                "--architecture", "x64", "--mode", "normal", *bundle,
            ],
            env={**os.environ, "PYTHONHOME": sys.base_prefix},
        )
        work = mod_tools / "HookBuild" / variant
        assert json.loads((work / "objects.json").read_text(encoding="utf-8")) == {}
        assert not list(work.glob(".mbt-stage-*"))


def verify_msvc_source_analysis(root, args):
    root.mkdir()
    source = root / "probe.cpp"
    source.write_text(
        '#include "ModExportMacros.h"\n'
        'bool FailingCall() { return false; }\n'
        'bool StableBusiness() { return MOD_SOURCE_FAILURE(MOD_ERROR_BUSINESS_TEST, 0x30010011, false); }\n'
        'bool ConflictingBusiness() { return MOD_SOURCE_FAILURE(MOD_ERROR_BUSINESS_OTHER, 0x30010011, false); }\n',
        encoding="utf-8",
    )
    database = write_json(
        root / "compile_commands.json",
        [{
            "directory": str(root),
            "file": str(source),
            "arguments": [
                "cl.exe", "/TP", "/std:c++20", "/dximplicitlyinstantiatevirtualfunction-",
                "/I" + str(EXPORT_HEADERS),
                "/DUE_TARGET_NAME=Game", '/DUE_MODULE_NAME="Probe"', "/DWITH_EDITOR=0", "/c", str(source),
            ],
        }],
    )
    member = "api/ci/probe/failing-call"
    request = write_json(
        root / "SourceAnalysisRequest.json",
        {
            "format": "UMF.SourceAnalysisRequest",
            "schemaVersion": 2,
            "compilationDatabase": database,
            "target": "Game",
            "projectRoot": str(root),
            "members": [{"id": member, "function": "FailingCall", "module": "Probe",
                         "source": str(source), "translationUnit": str(source), "signature": "bool ()",
                         "failureReturn": False}],
            "libclang": str(args.libclang),
            "resourceDirectory": str(args.resource_dir),
            "analysisArguments": ["--target=x86_64-pc-windows-msvc"],
        },
    )
    document = json.loads(Path(request).read_bytes())
    explicit = {**document["members"][0], "id": member + "-explicit", "function": "StableBusiness"}
    del explicit["failureReturn"]
    document["members"].append(explicit)
    write_json(Path(request), document)
    report = json.loads(run([args.mst, "analyze-errors", request], capture_output=True).stdout)
    members = {entry["memberId"]: entry for entry in report["members"]}
    assert [entry["key"] for entry in members[member]["failures"]] == ["DeclaredFailure"]
    (business,) = members[explicit["id"]]["failures"]
    assert (business["macro"], business["code"]) == ("MOD_ERROR_BUSINESS_TEST", 0x30010011)
    document["members"].append({**explicit, "id": member + "-conflict", "function": "ConflictingBusiness"})
    write_json(Path(request), document)
    collision = subprocess.run([str(args.mst), "analyze-errors", request], capture_output=True, text=True)
    assert collision.returncode != 0 and "Conflicting source error" in collision.stderr, collision.stderr



def verify_cross_file_exceptions(root, args):
    root.mkdir()
    source = root / "caller.cpp"
    helper = root / "helper.cpp"
    source.write_text(
        "void AcrossFile();\n"
        "void Run(bool Direct) { if (Direct) throw 3; try { AcrossFile(); } catch (int) { throw; } }\n",
        encoding="utf-8",
    )
    helper.write_text("void AcrossFile() { throw 7; }\n", encoding="utf-8")
    database = write_json(root / "compile_commands.json", [{
        "directory": str(root),
        "file": str(unit),
        "arguments": [str(args.clang), "--target=x86_64-pc-windows-msvc", "-std=c++20", "-fexceptions",
                      "-fcxx-exceptions", "-DUE_TARGET_NAME=Game", '-DUE_MODULE_NAME="Probe"', "-DWITH_EDITOR=0",
                      "-c", str(unit), "-o", str(unit.with_suffix(".obj"))],
    } for unit in (source, helper)])
    request = write_json(root / "SourceAnalysisRequest.json", {
        "format": "UMF.SourceAnalysisRequest", "schemaVersion": 2, "compilationDatabase": database,
        "target": "Game", "projectRoot": str(root), "libclang": str(args.libclang),
        "resourceDirectory": str(args.resource_dir), "analysisArguments": [],
        "members": [{"id": "api/ci/probe/exceptions", "module": "Probe", "function": "Run",
                     "translationUnit": str(source), "source": str(source)}],
    })
    first = run([args.mst, "analyze-errors", request], capture_output=True).stdout
    assert first == run([args.mst, "analyze-errors", request], capture_output=True).stdout
    (member,) = json.loads(first)["members"]
    failures = member["failures"]
    assert all(item["code"] == 0x28020001 for item in failures), member
    assert any(item["originFunction"] == "Run" and not item["callChain"] for item in failures), member
    assert any(item["originFunction"] == "AcrossFile" and item["callChain"] and item.get("rethrowLocations")
               for item in failures), member
    assert {Path(file).name for file in member["analyzedFiles"]} >= {"caller.cpp", "helper.cpp"}, member


def verify_hook_marker(header, source, flags, args, project, options, request):
    declaration = header.read_text(encoding="utf-8")
    scan = [args.mht, "scan", project, options]
    try:
        header.write_text(
            declaration.replace("MOD_EXPORT_HOOK()", "MOD_EXPORT_HOOK( /* marker */ )"), encoding="utf-8"
        )
        report = json.loads(run(scan, capture_output=True).stdout)
        metadata = report["Exports"][0]["Metadata"]
        assert metadata["AllowedStages"] == ["Pre", "On", "Post"]
        assert metadata["AllowedOperations"] == ["ModifyInputs", "Cancel", "Replace"]
        compile_cpp = [args.clang, *flags, "-DMOD_API_BOOTSTRAP=1", "-fsyntax-only", source]
        for field, error in (
            (",", "expected a field name"),
            ("HostEndpoint=Hook", "unknown field 'HostEndpoint'"),
            ('ExportVersion="1.0"', "unknown field 'ExportVersion'"),
            ('CompatibleNames=("OldHealth"), Permission="Other"', "unknown field 'Permission'"),
            ("AllowedStages=(Post)", "unknown field 'AllowedStages'"),
            ("NoPre, NoPre", "duplicate field 'NoPre'"),
            ("NoOn=false", "'NoOn' is a bare flag"),
            ("NoPost=true", "'NoPost' is a bare flag"),
            ("NoPrefix", "unknown field 'NoPrefix'"),
            ("NoOn,", "expected a field name"),
            ("NoPost,, NoPre", "expected a field name"),
        ):
            invalid = declaration.replace("MOD_EXPORT_HOOK()", f"MOD_EXPORT_HOOK({field})")
            header.write_text(invalid, encoding="utf-8")
            rejected = subprocess.run(scan, capture_output=True, text=True)
            assert rejected.returncode and error in rejected.stderr, rejected.stderr
            rejected = subprocess.run(compile_cpp, capture_output=True, text=True)
            assert rejected.returncode and "MOD_EXPORT_HOOK" in rejected.stderr, rejected.stderr
        # Even a third-party header that still swallows arguments must fail MHT generation.
        header.write_text(
            declaration.replace('#include "ModExportMacros.h"', "#define MOD_EXPORT_HOOK(...)").replace(
                "MOD_EXPORT_HOOK()", "MOD_EXPORT_HOOK(AllowedStages=(Post))"
            ),
            encoding="utf-8",
        )
        rejected = subprocess.run([args.mht, "generate", request], capture_output=True, text=True)
        assert rejected.returncode and "unknown field 'AllowedStages'" in rejected.stderr, rejected.stderr
        for names, error in (
            ('"OldHealth", "OldHealth"', "duplicate 'CompatibleNames' entry"),
            ("OldHealth", "expects a quoted function identifier"),
            ('"Bad-Name"', "expects a quoted function identifier"),
            ('"changeHealth"', "duplicate or inconsistent extension ID"),
            ('"Old_Health", "OldHealth"', "duplicate or inconsistent extension ID"),
        ):
            header.write_text(
                declaration.replace("MOD_EXPORT_HOOK()", f"MOD_EXPORT_HOOK(CompatibleNames = ({names}))"),
                encoding="utf-8",
            )
            rejected = subprocess.run([args.mht, "generate", request], capture_output=True, text=True)
            assert rejected.returncode and error in rejected.stderr, rejected.stderr
        header.write_text(
            declaration.replace("MOD_EXPORT_HOOK()", 'MOD_EXPORT_HOOK(CompatibleNames = ("AddHealth"))').replace(
                "float Health;", "MOD_EXPORT_HOOK()\nfloat AddHealth(float Delta);\nfloat Health;"
            ),
            encoding="utf-8",
        )
        rejected = subprocess.run([args.mht, "generate", request], capture_output=True, text=True)
        assert rejected.returncode and "duplicate or inconsistent extension ID" in rejected.stderr, rejected.stderr
        header.write_text(
            declaration.replace("MOD_EXPORT_HOOK()", "MOD_EXPORT_HOOK(CompatibleNames = ())"), encoding="utf-8"
        )
        assert json.loads(run(scan, capture_output=True).stdout)["Exports"][0]["Metadata"]["CompatibleNames"] == []
        run(compile_cpp)
    finally:
        header.write_text(declaration, encoding="utf-8")


def verify_hook_stages(header, source, flags, args, project, options, request, generated):
    declaration = header.read_text(encoding="utf-8")
    implementation = source.read_text(encoding="utf-8")
    stages = ("Pre", "On", "Post")
    contract = generated / "Contract/Game.sdkcontract.json"
    host = generated / "Host/Game/Game.modhost.cpp"
    copy = generated / "Diagnostics/HookSources/Game/GameCharacter.cpp"
    generate = [args.mht, "generate", request]
    signatures = set()
    try:
        for count in range(len(stages) + 1):
            for omitted in combinations(stages, count):
                markers = ["No" + stage for stage in omitted]
                markers.insert(1, 'CompatibleNames = ("OldHealth")')
                marker = "MOD_EXPORT_HOOK(" + ", /* stage */ ".join(markers) + ")"
                header.write_text(declaration.replace("MOD_EXPORT_HOOK()", marker), encoding="utf-8")
                report = json.loads(run([args.mht, "scan", project, options], capture_output=True).stdout)
                metadata = report["Exports"][0]["Metadata"]
                allowed = [stage for stage in stages if stage not in omitted]
                operations = (["ModifyInputs", "Cancel"] if "Pre" in allowed else []) + (
                    ["Replace"] if "On" in allowed else []
                )
                assert metadata["AllowedStages"] == allowed and metadata["AllowedOperations"] == operations
                assert all(metadata["No" + stage] is True for stage in omitted)
                run([args.clang, *flags, "-DMOD_API_BOOTSTRAP=1", "-fsyntax-only", source])
                run(generate, capture_output=True)
                entries = json.loads(contract.read_bytes()).get("extensions", [])
                if not allowed:
                    assert not entries and not copy.exists()
                    assert not (generated / "Diagnostics/HookSources.json").exists()
                    assert not (generated / "SDK/Public/Game.modextensionsabi.h").exists()
                    continue
                mask = (3 if "Pre" in allowed else 0) | (16 if "On" in allowed else 0)
                assert {e["id"] for e in entries} == {
                    "hook/game.character/change-health",
                    "hook/game.character/old-health",
                }
                assert all(entry["operations"] == mask for entry in entries)
                definitions = host.read_text(encoding="utf-8-sig")
                for stage in stages:
                    enabled = stage in allowed
                    assert all(entry.get(stage.lower(), False) is enabled for entry in entries)
                    assert definitions.count(f"Entry.b{stage} = {str(enabled).lower()};") == len(entries)
                signatures.add(next(e["signatureHash"] for e in entries if e["id"].endswith("/change-health")))
                assert copy.read_text(encoding="utf-8-sig").count("AGameCharacter_ChangeHealth::Dispatch") == 1
        assert len(signatures) == 7
        # A second Hook in the same translation unit still needs a copy when ChangeHealth is fully disabled.
        header.write_text(
            header.read_text(encoding="utf-8").replace(
                "float Health;", "MOD_EXPORT_HOOK(NoPre)\nfloat Heal(float Delta);\nfloat Health;"
            ),
            encoding="utf-8",
        )
        source.write_text(
            implementation + "\nfloat AGameCharacter::Heal(float Delta) { return Health += Delta; }\n",
            encoding="utf-8",
        )
        run(generate, capture_output=True)
        assert [e["id"] for e in json.loads(contract.read_bytes())["extensions"]] == ["hook/game.character/heal"]
        rewritten = copy.read_text(encoding="utf-8-sig")
        assert "AGameCharacter_Heal::Dispatch" in rewritten
        assert "AGameCharacter_ChangeHealth::Dispatch" not in rewritten
    finally:
        header.write_text(declaration, encoding="utf-8")
        source.write_text(implementation, encoding="utf-8")


def verify_discovery_conflicts(header, source, build_input, options, request, args):
    files = [header, source, Path(build_input), Path(options)]
    saved = {file: file.read_bytes() for file in files}
    try:
        header.write_bytes(saved[header] + b"\nclass OtherOwner { public: MOD_EXPORT_HOOK() float Change(float Value); };\n")
        source.write_bytes(saved[source] + b"\nfloat OtherOwner::Change(float Value) { return Value; }\n")
        build = json.loads(saved[Path(build_input)])
        build["reflection"].append({"cpp": "OtherOwner", "module": "Game", "path": "/Script/Game.OtherOwner",
                                    "file": str(header), "legacyId": "game.character"})
        write_json(Path(build_input), build)
        collision = subprocess.run([str(args.mht), "generate", request], capture_output=True, text=True)
        assert collision.returncode != 0 and "type identity 'game.character' is already used" in collision.stderr

        header.write_text(
            '#pragma once\n#include "ModExportMacros.h"\nclass UObject {};\n'
            'class UScoped : public UObject { public: static void StaticClass();\n'
            'MOD_EXPORT_API(Scope=World, Thread=GameThread, FailureRule=ReturnError) bool A();\n'
            'MOD_EXPORT_API(Scope=Player, Thread=GameThread, FailureRule=ReturnError) bool B(); };\n',
            encoding="utf-8",
        )
        source.write_text('#include "GameCharacter.h"\nbool UScoped::A() { return true; }\n'
                          'bool UScoped::B() { return true; }\n', encoding="utf-8")
        metadata = json.loads(saved[Path(options)])
        metadata["Options"].update(APIScopes=["World", "Player"], APIThreads=["GameThread"],
                                   APIFailureRules=["ReturnError"])
        write_json(Path(options), metadata)
        Path(build_input).write_bytes(saved[Path(build_input)])
        scope = subprocess.run([str(args.mht), "generate", request], capture_output=True, text=True)
        assert scope.returncode != 0 and "conflicting export scopes" in scope.stderr, scope.stderr
    finally:
        for file, data in saved.items():
            file.write_bytes(data)


def verify(root, args, platform, arch, triple, apple_platform=None):
    module = root / "Source/Game"
    module.mkdir(parents=True)
    (module / "Game.Build.cs").touch()
    project = write_json(root / "Game.uproject", {})
    source = module / "GameCharacter.cpp"
    header = module / "GameCharacter.h"
    header.write_text(
        '#pragma once\n#include "ModExportMacros.h"\nclass AGameCharacter {\npublic:\n'
        "MOD_EXPORT_HOOK()\nfloat ChangeHealth(float Delta);\nfloat Health;\n};\n",
        encoding="utf-8",
    )
    source.write_text(
        '#include "GameCharacter.h"\nfloat AGameCharacter::ChangeHealth(float Delta) '
        "{ return Health += Delta; }\n",
        encoding="utf-8",
    )
    options = write_hook_options(root, project)
    flags = [
        f"--target={triple}",
        "-I" + EXPORT_HEADERS.as_posix(),
        "-std=c++20",
        "-ffreestanding",
        "-fno-exceptions",
        "-fno-rtti",
        "-DUE_TARGET_NAME=Game",
        '-DUE_MODULE_NAME="Game"',
        "-DUE_BUILD_DEVELOPMENT=1",
        f"-DUBT_COMPILED_PLATFORM={platform}",
    ]
    obj = root / ("character.obj" if platform == "Win64" else "character.o")
    analysis_flags = []
    if platform == "Linux":
        analysis_flags = [
            "-MD", f"-MF{obj.with_suffix('.d')}", "-gz=zlib", "-Werror=unused-command-line-argument",
        ]
    database = write_json(
        root / "compile_commands.json",
        [
            {
                "directory": str(root),
                "file": str(source),
                "arguments": [str(args.clang), *flags, *analysis_flags, "-c", str(source), "-o", str(obj)],
            }
        ],
    )
    build_input = write_json(
        root / "BuildInput.json",
        {
            "schemaVersion": 1,
            "objectReturnPolicy": "ErrorIfNull",
            "project": project,
            "gameId": "Game",
            "target": "Game",
            "platform": platform,
            "configuration": "Development",
            "architecture": arch,
            "compilationDatabase": database,
            "analysisArguments": ["-resource-dir=" + str(args.resource_dir)],
            "reflection": [{"cpp": "AGameCharacter", "module": "Game", "path": "/Script/Game.GameCharacter",
                            "file": str(header), "legacyId": "game.character"}],
            "modules": [{"name": "Game", "sourceDirectory": str(module), "translationUnits": [str(source)]}],
        },
    )
    generated = root / "Intermediate/ModTools/ModHeaderTool/Generated/Game" / platform / "Development" / arch
    (root / "Config").mkdir()
    request = write_json(
        root / "Generation.json",
        {
            "format": "UMF.ModHeaderGenerationRequest",
            "schemaVersion": 1,
            "hostCompiler": "UBT",
            "buildInput": build_input,
            "outputDirectory": str(generated),
            "sourceTool": str(args.mst),
            "libclang": str(args.libclang),
            "metadataOptions": options,
            "lifecycleSource": str(EXPORT_HEADERS.parents[1] / "ModLoader/Public"),
        },
    )
    contract_file = generated / "Contract/Game.sdkcontract.json"
    declaration = header.read_text(encoding="utf-8")
    original_contract = None
    if (platform, arch) in {("Win64", "x64"), ("LinuxArm64", "arm64"), ("Mac", "arm64")}:
        reflected = json.loads(Path(build_input).read_bytes())
        write_json(Path(build_input), {**reflected, "reflection": []})
        run([args.mht, "generate", request])
        (native,) = json.loads(contract_file.read_bytes())["extensions"]
        assert native["id"] == "hook/cpp.game.agamecharacter/change-health", native
        without_override = [{**reflected["reflection"][0], "legacyId": ""}]
        write_json(Path(build_input), {**reflected, "reflection": without_override})
        run([args.mht, "generate", request])
        (automatic,) = json.loads(contract_file.read_bytes())["extensions"]
        assert automatic["id"] == "hook/ue.game.gamecharacter/change-health", automatic
        write_json(Path(build_input), reflected)
    if (platform, arch) == ("Win64", "x64"):
        verify_discovery_conflicts(header, source, build_input, options, request, args)
        verify_hook_marker(header, source, flags, args, project, options, request)
        verify_hook_stages(header, source, flags, args, project, options, request, generated)
        header.write_text(declaration.replace("MOD_EXPORT_HOOK()", "MOD_EXPORT_HOOK(NoOn)"), encoding="utf-8")
        run([args.mht, "generate", request])
        (original_contract,) = json.loads(contract_file.read_bytes())["extensions"]
    header.write_text(
        declaration.replace("ChangeHealth", "AdjustHealth").replace(
            "MOD_EXPORT_HOOK()", 'MOD_EXPORT_HOOK(NoOn, CompatibleNames = ("ChangeHealth", "UpdateHealth"))'
        ),
        encoding="utf-8",
    )
    source.write_text(source.read_text().replace("ChangeHealth", "AdjustHealth"), encoding="utf-8")
    run([args.mht, "generate", request])
    contracts = {entry["id"]: entry for entry in json.loads(contract_file.read_bytes())["extensions"]}
    prefix = "hook/game.character/"
    assert set(contracts) == {prefix + name for name in ("change-health", "adjust-health", "update-health")}
    assert all(not entry.get("on", False) and entry["operations"] == 3 for entry in contracts.values())
    if original_contract is not None:
        assert contracts[original_contract["id"]] == original_contract
    native = (generated / "SDK/Public/Game.modextensionsabi.h").read_text(encoding="utf-8-sig")
    for hook_id, contract in contracts.items():
        symbol = "UMF_Ext_" + hook_id.replace("/", "_").replace(".", "_").replace("-", "_")
        assert f'#define {symbol}_ID "{hook_id}"' in native
        assert f'#define {symbol}_SIGNATURE "{contract["signatureHash"]}"' in native
    if (platform, arch) in (("Linux", "x64"), ("IOS", "iossimulator")):
        input_path = Path(build_input)
        mismatch = json.loads(input_path.read_text())
        mismatch["architecture"] = "arm64"
        write_json(input_path, mismatch)
        rejected = subprocess.run([args.mht, "generate", request], capture_output=True, text=True)
        assert rejected.returncode and "does not match compiler triple" in rejected.stderr
        mismatch["architecture"] = arch
        write_json(input_path, mismatch)
    source_map = generated / "Diagnostics/HookSources.json"
    (unit,) = json.loads(source_map.read_bytes())["units"]
    assert Path(unit["source"]) == source
    witness = unit["witness"]
    host = (generated / "Host/Game/Game.modhost.cpp").read_text(encoding="utf-8-sig")
    assert f'extern "C" UMF_HOOK_LOCAL void {witness}();' in host and f"    {witness}();" in host
    assert host.count('Entry.DispatchId = FName(TEXT("hook/game.character/adjust-health"));') == 2
    helpers = (generated / "Host/Game/Game.modextensions.h").read_text(encoding="utf-8-sig")
    assert helpers.count("static FValue Dispatch(") == 1 and "struct AGameCharacter_AdjustHealth" in helpers

    # Stand-ins for engine types let the actual MHT-generated header compile without an SDK/sysroot.
    for name in ("CoreGlobals.h", "Logging/LogMacros.h", "Math/UnrealMathUtility.h"):
        path = root / name
        path.parent.mkdir(exist_ok=True)
        path.write_text("", encoding="utf-8")
    (root / "ModExtensionSubsystem.h").write_text(
        r"""#pragma once
#define TEXT(x) x
#define UE_LOG(...) ((void)0)
using TCHAR = char;
struct FName { FName(const char*) {} };
struct FMath { static bool IsFinite(float x) { return __builtin_isfinite(x); } };
template<class> struct TFunctionRef;
template<class R, class A> struct TFunctionRef<R(A)> {
    template<class F> TFunctionRef(const F& f) : ctx(&f), call([](const void* c, A a) { return (*static_cast<const F*>(c))(a); }) {}
    R operator()(A a) const { return call(ctx, a); }
    const void* ctx; R (*call)(const void*, A);
};
namespace Mod::API { enum class EError { None }; }
namespace Mod::Extension {
struct FEndpoint {};
template<class T> struct THandlerCalls {};
template<class T> struct TCoreResult { T Value; Mod::API::EError Error; bool bCommitted; };
template<class T> using THookResult = TCoreResult<T>;
}
struct UModExtensionSubsystem {
    template<class T, class S, class C, class V, class R>
    static auto InvokeScalar(S&, FName, Mod::Extension::FEndpoint&, T v, C core, V, R) { return core(v); }
};
""",
        encoding="utf-8",
    )
    args_to_compile = [
        *flags,
        "-O1",
        "-I" + module.as_posix(),
        "-I" + root.as_posix(),
        "-I" + (generated / "Host/Game").as_posix(),
        "-c",
        source.as_posix(),
        "-o",
        obj.as_posix(),
    ]
    rsp = root / "compile options.rsp"
    rsp.write_text(
        "\n".join('"' + token.replace('"', '\\"') + '"' for token in args_to_compile), encoding="utf-8"
    )
    nested = root / "nested.rsp"
    nested.write_text('@"' + rsp.as_posix() + '"', encoding="utf-8")
    action = {
        "Id": 0,
        "Type": "Compile",
        "CommandPath": str(args.clang),
        "WorkingDirectory": str(root),
        "CommandArguments": '@"' + nested.as_posix() + '"',
        "ProducedItems": [str(obj)],
        "PrerequisiteActions": [],
        "CommandVersion": "test",
    }
    if os.name == "nt" and platform == "Win64" and arch == "x64":
        action["CommandPath"] = str(root / "cl-filter.exe")
        action["CommandArguments"] = f'-dependencies=fixture.txt -- "{args.clang}" ' + action["CommandArguments"]
    graph = root / "CompileActions.json"
    # An up-to-date target receipt can leave no WriteMetadata action between the links and the post-build script.
    write_json(graph, {"Actions": [
        action,
        {"Id": 1, "Type": "PostBuildStep", "PrerequisiteActions": [], "ProducedItems": [str(root / "post.ran")]},
        {"Id": 2, "Type": "Link", "PrerequisiteActions": [0], "ProducedItems": [str(root / "game.linked")]},
        {"Id": 3, "Type": "Link", "PrerequisiteActions": [0], "ProducedItems": [str(root / "module.linked")]},
    ]})
    write_build_receipt(project, generated, platform, arch, args.clang, triple, root)
    bundle = write_build_bundle(project, platform, arch)
    build = [
        args.mbt,
        "build-ue-target",
        "--engine", args.engine,
        "--project", project,
        "--target", "Game",
        "--platform", platform,
        "--configuration", "Development",
        "--architecture", arch,
        "--mode", "normal", *bundle,
    ]
    env = {**os.environ, "PYTHONHOME": sys.base_prefix}
    run(build, env=env)
    if platform == "Linux":
        write_json(graph, {"Actions": []})
        run(build, env=env)
        saved = source_map.read_bytes()
        source_map.write_bytes(saved + b" ")
        try:
            rejected = subprocess.run(build, env=env, capture_output=True, text=True)
            assert rejected.returncode and "Hook source map changed after generation" in rejected.stderr
        finally:
            source_map.write_bytes(saved)
    probe = root / "check.cpp"
    probe.write_text(
        '#if defined(_WIN32)\nextern "C" int _fltused = 0;\n#define LOCAL\n#else\n'
        '#define LOCAL __attribute__((visibility("hidden")))\n#endif\n'
        f'extern "C" LOCAL void {witness}();\n'
        "#if defined(_M_ARM64EC) || defined(__arm64ec__)\n"
        # A data relocation checks the same witness without requiring an x64 interop call in this SDK-only probe.
        f'extern "C" void (*check)() = &{witness};\n'
        "#else\n"
        f'extern "C" void check() {{ {witness}(); }}\n#endif\n',
        encoding="utf-8",
    )
    probe_obj = root / "check.o"
    run([args.clang, *flags, "-c", probe, "-o", probe_obj])

    def tool(name):
        return args.clang.with_name(name + args.clang.suffix)

    if platform == "Win64":
        link = [
            tool("lld-link"),
            "/dll",
            "/noentry",
            "/nodefaultlib",
            "/machine:" + arch,
            "/export:check",
            "/out:" + str(root / "game.dll"),
        ]
        if arch == "arm64ec":
            link.append(args.arm64ec_runtime)
    elif apple_platform:
        os_name, version = apple_platform
        link = [
            tool("ld64.lld"),
            "-dylib",
            "-arch",
            "x86_64" if arch == "x64" else "arm64",
            "-platform_version",
            os_name,
            version,
            version,
            "-o",
            root / "game.dylib",
        ]
    else:
        link = [tool("ld.lld"), "-shared", "-o", root / "game.so"]
    run([*link, obj, probe_obj])
    original_obj = obj.with_stem("original-character")
    run([args.clang, *flags, "-c", source, "-o", original_obj])
    failed = subprocess.run([*link, original_obj, probe_obj], capture_output=True, text=True)
    assert failed.returncode and witness in failed.stderr + failed.stdout, failed.stderr + failed.stdout
    if apple_platform:
        # UE allows dynamic lookup in cross-referenced Mac modules. Run the same post-link check as UBT.
        relaxed = [*link, "-undefined", "dynamic_lookup"]
        receipt = root / "Game.target"
        write_json(
            receipt, {"BuildProducts": [{"Type": "DynamicLibrary", "Path": "$(ProjectDir)/game.dylib"}]}
        )
        nm = [str(tool("llvm-nm"))]
        original = subprocess.run([*relaxed, original_obj, probe_obj], capture_output=True, text=True)
        if original.returncode:
            assert witness in original.stderr
        else:
            try:
                verify_target(root, receipt, generated, nm)
            except ValueError as error:
                assert witness in str(error)
            else:
                raise AssertionError("An original object passed the Mach-O post-link check")
        run([*relaxed, obj, probe_obj])
        verify_target(root, receipt, generated, nm)
    print(f"{platform}/{arch}: generated object linked; original rejected", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("clang", "resource-dir", "mbt", "mht", "mst", "libclang"):
        parser.add_argument("--" + field, type=lambda p: Path(p).resolve(strict=True), required=True)
    parser.add_argument(
        "--arm64ec-runtime",
        type=lambda p: Path(p).resolve(strict=True),
        help="also check Windows ARM64EC using the Windows SDK um/arm64/arm64rt.lib",
    )
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="umf hook targets ") as directory:
        verify_module_selection(Path(directory) / "discovery", args.mht, args.mbt)
        verify_empty_host_finalization(Path(directory) / "empty", args.mbt)
        verify_direct_host_finalization(Path(directory) / "host", args)
        args.engine = prepare_fake_ubt(Path(directory))
        verify_long_project_path(Path(directory), args)
        verify_msvc_source_analysis(Path(directory) / "msvc-source-analysis", args)
        verify_cross_file_exceptions(Path(directory) / "clang-source-analysis", args)
        for target in (
            ("Win64", "x64", "x86_64-pc-windows-msvc"),
            ("Win64", "arm64", "aarch64-pc-windows-msvc"),
            ("Linux", "x64", "x86_64-unknown-linux-gnu"),
            ("LinuxArm64", "arm64", "aarch64-unknown-linux-gnu"),
            ("Mac", "x64", "x86_64-apple-macos13", ("macos", "13.0")),
            ("Mac", "arm64", "arm64-apple-macos13", ("macos", "13.0")),
            ("IOS", "arm64", "arm64-apple-ios16", ("ios", "16.0")),
            ("IOS", "iossimulator", "arm64-apple-ios16-simulator", ("ios-simulator", "16.0")),
            ("TVOS", "arm64", "arm64-apple-tvos16", ("tvos", "16.0")),
            ("TVOS", "tvossimulator", "arm64-apple-tvos16-simulator", ("tvos-simulator", "16.0")),
            ("VisionOS", "arm64", "arm64-apple-xros1", ("xros", "1.0")),
            ("VisionOS", "iossimulator", "arm64-apple-xros1-simulator", ("12", "1.0")),
            ("Android", "arm64", "aarch64-linux-android26"),
            ("Android", "x64", "x86_64-linux-android26"),
        ):
            verify(Path(directory) / target[0] / target[1], args, *target)
        if args.arm64ec_runtime:
            verify(Path(directory) / "Win64/arm64ec", args, "Win64", "arm64ec", "arm64ec-pc-windows-msvc")
