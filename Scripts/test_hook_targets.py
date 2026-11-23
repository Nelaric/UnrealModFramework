#!/usr/bin/env python3
# Copyright (c) 2026 Nelaric Contributors
"""Cross-compile a generated ChangeHealth Hook and check the link against the original object."""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from build_hooks import load_sources, patch_actions, split_arguments

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from UnrealModFramework.Plugins.ModRuntime.Tools.verify_macho_hooks import verify_target

EXPORT_HEADERS = (
    Path(__file__).resolve().parents[1]
    / "UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public"
)


def write_json(path, document):
    path.write_text(json.dumps(document), encoding="utf-8")
    return str(path)


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


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
        for field in (
            ",",
            "HostEndpoint=Hook",
            'ExportVersion="1.0"',
            'ExportId="hook/game/other", Permission="Health.Observe", AllowedStages=(Post)',
        ):
            invalid = declaration.replace("MOD_EXPORT_HOOK()", f"MOD_EXPORT_HOOK({field})")
            header.write_text(invalid, encoding="utf-8")
            rejected = subprocess.run(scan, capture_output=True, text=True)
            assert rejected.returncode and "MOD_EXPORT_HOOK() takes no arguments" in rejected.stderr
        compile_cpp = [args.clang, *flags, "-DMOD_API_BOOTSTRAP=1", "-fsyntax-only", source]
        rejected = subprocess.run(compile_cpp, capture_output=True, text=True)
        assert rejected.returncode and "MOD_EXPORT_HOOK" in rejected.stderr
        # Even a third-party header that still swallows arguments must fail MHT generation.
        header.write_text(
            invalid.replace('#include "ModExportMacros.h"', "#define MOD_EXPORT_HOOK(...)"), encoding="utf-8"
        )
        rejected = subprocess.run([args.mht, "generate", request], capture_output=True, text=True)
        assert rejected.returncode and "MOD_EXPORT_HOOK() takes no arguments" in rejected.stderr
    finally:
        header.write_text(declaration, encoding="utf-8")


def verify(root, args, platform, arch, triple, apple_platform=None):
    module = root / "Source/Game"
    module.mkdir(parents=True)
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
    options = write_json(
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
    database = write_json(
        root / "compile_commands.json",
        [
            {
                "directory": str(root),
                "file": str(source),
                "arguments": [str(args.clang), *flags, "-c", str(source), "-o", str(obj)],
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
            "types": {"AGameCharacter": {"typeId": "game.character", "scope": "World"}},
            "modules": [{"name": "Game", "sourceDirectory": str(module), "translationUnits": [str(source)]}],
        },
    )
    generated = root / "Generated"
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
        },
    )
    if (platform, arch) == ("Win64", "x64"):
        verify_hook_marker(header, source, flags, args, project, options, request)
    run([args.mht, "generate", request])
    units = load_sources(generated, "Game", "Game", "Development", platform, arch)
    if (platform, arch) in (("Linux", "x64"), ("IOS", "iossimulator")):
        input_path = Path(build_input)
        mismatch = json.loads(input_path.read_text())
        mismatch["architecture"] = "arm64"
        write_json(input_path, mismatch)
        rejected = subprocess.run([args.mht, "generate", request], capture_output=True, text=True)
        assert rejected.returncode and "does not match compiler triple" in rejected.stderr
        mismatch["architecture"] = arch
        write_json(input_path, mismatch)
    witness = units[source]["witness"]
    host = (generated / "Host/Game/Game.modhost.cpp").read_text(encoding="utf-8-sig")
    assert f'extern "C" UMF_HOOK_LOCAL void {witness}();' in host and f"    {witness}();" in host

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
        "Type": "Compile",
        "CommandPath": str(args.clang),
        "WorkingDirectory": str(root),
        "CommandArguments": '@"' + nested.as_posix() + '"',
        "ProducedItems": [str(obj)],
        "CommandVersion": "test",
    }
    assert patch_actions({"Actions": [action]}, units, root) == {source: obj}
    run([args.clang, *split_arguments(action["CommandArguments"])])
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
    run([args.clang, *flags, "-c", source, "-o", obj])
    failed = subprocess.run([*link, obj, probe_obj], capture_output=True, text=True)
    assert failed.returncode and witness in failed.stderr + failed.stdout, failed.stderr + failed.stdout
    if apple_platform:
        # UE allows dynamic lookup in cross-referenced Mac modules. Run the same post-link check as UBT.
        relaxed = [*link, "-undefined", "dynamic_lookup"]
        receipt = root / "Game.target"
        write_json(
            receipt, {"BuildProducts": [{"Type": "DynamicLibrary", "Path": "$(ProjectDir)/game.dylib"}]}
        )
        nm = [str(tool("llvm-nm"))]
        original = subprocess.run([*relaxed, obj, probe_obj], capture_output=True, text=True)
        if original.returncode:
            assert witness in original.stderr
        else:
            try:
                verify_target(root, receipt, generated, nm)
            except ValueError as error:
                assert witness in str(error)
            else:
                raise AssertionError("An original object passed the Mach-O post-link check")
        run([args.clang, *split_arguments(action["CommandArguments"])])
        run([*relaxed, obj, probe_obj])
        verify_target(root, receipt, generated, nm)
    print(f"{platform}/{arch}: generated object linked; original rejected", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("clang", "resource-dir", "mht", "mst", "libclang"):
        parser.add_argument("--" + field, type=lambda p: Path(p).resolve(strict=True), required=True)
    parser.add_argument(
        "--arm64ec-runtime",
        type=lambda p: Path(p).resolve(strict=True),
        help="also check Windows ARM64EC using the Windows SDK um/arm64/arm64rt.lib",
    )
    args = parser.parse_args()
    arguments = ["", "dir with spaces", r"C:\SDK\bin", 'FOO="a b"']
    assert split_arguments(subprocess.list2cmdline(arguments)) == arguments
    assert split_arguments('"/DNAME=""a b"""') == ['/DNAME="a b"']
    with tempfile.TemporaryDirectory(prefix="umf hook targets ") as directory:
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
