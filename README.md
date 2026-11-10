<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework

[![Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

UnrealModFramework is a UE 5.6 development project for a portable game-side mod runtime and an independently built mod authoring editor. The repository currently contains the project and plugin module skeletons. Package loading, content resolution, native mod APIs, and editor workflows remain to be implemented and validated.

Paths below are relative to the repository root. The UE project lives in `UnrealModFramework/`; repository configuration, documentation, and quality scripts live alongside it.

| Path | Purpose |
| --- | --- |
| `UnrealModFramework/UnrealModFrameworkDev.uproject` | Development host project |
| `UnrealModFramework/Plugins/ModRuntime/` | Game-side source plugin; `Source/Runtime/` and `Source/Editor/` group its modules by target |
| `UnrealModFramework/SourcePlugins/ModDevelopmentSDKEditor/` | Independently built authoring plugin |
| `Docs/CodingStandards/` | Project coding and contribution standards |
| `.circleci/config.yml` | UE 5.6.1 Linux build configuration |
| `Scripts/` | Local quality checks |

`Binaries`, `Intermediate`, `Saved`, and `Dist` are generated output and are excluded from Git. The authoring plugin builds with UAT `BuildPlugin` from its descriptor in `UnrealModFramework/SourcePlugins`; use a short temporary `-Package` path on Windows and copy the resulting plugin into a release directory after the build.

See the [coding standards](Docs/CodingStandards/README.md), [contribution guide](CONTRIBUTING.md), and [CircleCI setup](Docs/CircleCI.md) before changing code or build files.

## SDK build tool

Each Release tool executable is built into its own `Plugins/ModRuntime/Tools/build/<tool>` directory in the Unreal project.

`ModSDKTool` (MST) provides `plan`, `build`, and `inspect` for game-specific SDK bundles. Build it with `Tools/Build.bat --target ModSDKTool`. See the [MST guide](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md) for the request and artifact formats.

## MHT C++ export parameter validation

MHT is a standalone CMake C++20 tool for C++ `MOD_EXPORT_API`, `MOD_EXPORT_EVENT`, and `MOD_EXPORT_HOOK` declarations. It does not scan or validate Blueprints. Headers live in `include/ModHeaderTool`, implementations in `src`, and `main.cpp` only forwards the entry point. Build the tool:

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModHeaderTool
```

For standalone MHT scans, use the compiled UE Editor's `ModExportOptions` commandlet to export the resolved GameMod choices and UE function redirects.

```powershell
$project = (Resolve-Path UnrealModFramework/UnrealModFrameworkDev.uproject).Path
& "C:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=ModExportOptions -unattended -NullRHI -nosound
.\UnrealModFramework\Plugins\ModRuntime\Tools\build\ModHeaderTool\ModHeaderTool.exe scan --project $project --output UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Exports.json
```

The catalog defaults to `Intermediate/ModTools/ModHeaderTool/MetadataOptions.json` under the project. Override it with the commandlet's `-Output=<file.json>` and MHT's `--options <file.json>`. Added, removed, or changed configuration inputs invalidate its fingerprints and require re-export. A catalog is unnecessary when no export invocations are found. The optional `.modexpot` scan-directory configuration is never created automatically.

`ModExportMetadataSchema.h` defines case-sensitive field names and choice mappings:

| Declaration | Required fields | Optional fields |
| --- | --- | --- |
| All three | — | `ExportVersion` defaults to `"1.0.0"`, plus `DisplayName` and `Description` |
| `MOD_EXPORT_API` | `Scope`, `Thread`, `FailureRule` | `Permissions` defaults to an empty list, plus `FullView` and `CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`, `PublishRule` | `PayloadType` |
| `MOD_EXPORT_HOOK` | — | `ContextType`, `AllowedStages` (defaults to `(Post)`), and `AllowedOperations` (defaults to `()`) |

Choices accept identifiers or strings. Use parenthesized lists for permissions, stages, and operations, such as `AllowedStages = (Pre, Post)` with `AllowedOperations = (ModifyInputs)`. Stages require at least one item, and list entries must be unique. `ExportVersion` is a two- or three-component numeric version string without redundant leading zeros. `PayloadType` and `ContextType` validate C++ type-name syntax. The AST stage checks type existence and declaration binding.

`ModifyInputs` and `Cancel` require `Pre`. `Wrap` and `Replace` each require `On` and an explicit entry in `AllowedOperations`. `ModifyResult` requires `Post`. The declaration defaults to read-only access after a successful operation. Regenerate older metadata option snapshots with `ModExportOptions` for the updated choices.

```cpp
MOD_EXPORT_API(
    Scope = World,
    Thread = GameThread,
    FailureRule = ReturnError,
    Permissions = ("WorldRead"))
```

Parsing supports comments, line splices, adjacent strings, ordinary/raw strings, standard escapes, Unicode escapes, and `TEXT("...")`. Arbitrary C++ expressions, named constants, user-defined string suffixes, and nested lists are rejected. Unknown or duplicate fields, missing required fields, wrong literal types, invalid choices, and duplicate list entries produce `"file"(line,column): error: macro: reason` diagnostics. Line and column numbers are one-based. Columns count UTF-8 bytes.

Recoverable diagnostics are collected before returning `1` without writing a successful report. Success returns `0`. Report schema version `2` preserves raw arguments and adds parsed `Metadata` and `ParametersValidated`. `PreprocessorEvaluated` remains `false`: conditional branches are not evaluated, and AST types, declaration binding, and runtime policy semantics remain outside this stage.

## MHT API source generation

The [thin UHT adapter](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md) provides an automatically discovered `ModRuntime.ubtplugin.csproj` entry. The development project's `Config/ModHeaderTool.json` selects `generate`; without configuration the adapter defaults to `scan`. The generated batch lives under `Intermediate/ModTools/ModHeaderTool/Generated` in the project. In generate mode the adapter runs MHT, then independent MBT host compilation before returning from UHT. UBT links the resulting archive through the owning module's private External dependency.

The synchronous UObject backend generates public non-static instance APIs with scalar values or loaded UObject pointer inputs, scalar/void or World-scoped UObject pointer results, GameThread, and ReturnError. MHT binds active markers to real libclang AST declarations and exact overloads using UBT's compilation database. The `ErrorIfNull` projection returns `FModRuntimeObject<T>`, with read-only views for const objects. MHT also generates a fixed-width ContextHandlesV1 C ABI and independent client endpoints. Generation reports missing projections for Event/Hook, structures, containers, and reference parameters.

MHT generates each API ID from its type ID and public function name. `CompatibleNames = ("OldName")` adds a callable entry under the old name. When the field is omitted, MHT uses applicable UE function redirects from the refreshed metadata catalog. See [API compatibility names](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#api-compatibility-names).

`FullView = ("USkeletalMesh")` selects a UObject pointer class from the signature. MHT checks the public instance members visible in the target build, including public bases. Generated members require the permissions specified by the declarations that select the class. An unresolved member projection fails the generation batch and names the class and member. See [FullView and object inputs](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#fullview-and-object-inputs).

Runtime annotations now live in `ExportMetadata/Public/ModExportAnnotations.h`; the Editor module's existing `ModExportMacros.h` forwards to that header. The `ModAPI` Runtime module owns results, cached typed tables, World/Player scopes, explicit providers, permissions, and revocation. Views do not own or expose their UObject. Gameplay lifetime, world cleanup, controller changes, provider removal, and caller revocation invalidate affected calls. A read grant does not implicitly grant mutation.

The development host exposes the current character through `UGameLocalPlayerAPI::GetLocalCharacter`. On that view, `AddHealth` returns updated health and `SetMesh` accepts a loaded skeletal mesh with a compatible skeleton.

Generation workflow:

1. Build MHT, MBT, and ModSourceTool through `Tools/Build.bat` and supply a matching libclang and resource headers. LLVM 21.1.8, UE 5.6, and MSVC 14.51 were verified here. Only LLVM 18.1.8's stable C API headers and license are vendored.
2. On a clean checkout, build Development Editor with `-MhtBootstrap` if the options exporter is unavailable. The normal build exports the default metadata snapshot when needed. Bootstrap omits generated registrations and is restricted to Development Editor.
3. Use UBT `-Mode=GenerateClangDatabase -NoPCH` with the actual Target and `-Compiler=VisualStudio2022`. For the Editor example below, create `<project>/Intermediate/ModTools/ModHeaderTool/Clang/Editor` and pass it as `-OutputDir`. Prepare the matching build input again after changing the Target or toolchain configuration.
4. Run the normal UE build. UHT passes the owner compiler recipe to MBT, runs MHT for the sources, and invokes ModSourceTool to generate the owner link header before UBT links. Editor and Game use separate output roots and host archives. Use the direct MHT command below for standalone source generation.

A build input identifies `schemaVersion: 1`, `project`, `gameId`, `target`, `platform`, `configuration`, `architecture`, `objectReturnPolicy: "ErrorIfNull"`, `compilationDatabase`, and `modules`. Each module supplies `name`, `sourceDirectory`, and explicit `translationUnits`. `project` resolves from the input file; other source paths resolve from the project. `typeIdentities` references the versioned `Config/ModAPITypes.json`; alternatively provide an inline `types` map. `analysisArguments` pins toolchain adapters/resource headers; `pathRoots` names machine-specific roots for normalized diagnostics and hashes. The database uses the [standard Clang format](https://clang.llvm.org/docs/JSONCompilationDatabase.html); every selected TU must have exactly one actual command, and UBT Target/platform/configuration/module macros are checked.

```json
{
  "schemaVersion": 1,
  "project": "../../../../UnrealModFrameworkDev.uproject",
  "gameId": "UnrealModFrameworkDev",
  "target": "UnrealModFrameworkDevEditor",
  "platform": "Win64",
  "configuration": "Development",
  "architecture": "x64",
  "objectReturnPolicy": "ErrorIfNull",
  "compilationDatabase": "Intermediate/ModTools/ModHeaderTool/Clang/Editor/compile_commands.json",
  "typeIdentities": "Config/ModAPITypes.json",
  "modules": [{
    "name": "ExportTest",
    "sourceDirectory": "Source/ExportTest",
    "translationUnits": [
      "Source/ExportTest/GameCharacter.cpp",
      "Source/ExportTest/GameLocalPlayerAPI.cpp"
    ]
  }],
  "analysisArguments": [
    "-resource-dir=<LLVM>/lib/clang/21",
    "-fms-compatibility-version=19.51",
    "-Wno-invalid-constexpr"
  ],
  "pathRoots": { "Engine": "<UE_5.6>", "LLVM": "<LLVM>" }
}
```

Standalone generation uses the complete producer context set from a UHT build of the same variant.

```powershell
$mht = ".\UnrealModFramework\Plugins\ModRuntime\Tools\build\ModHeaderTool\ModHeaderTool.exe"
$output = "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDevEditor/Win64/Development/x64"
$contexts = "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/HostTargets/UnrealModFrameworkDevEditor/Win64/Development/x64/contexts.json"
& $mht generate --build-input UnrealModFramework/Intermediate/ModTools/ModHeaderTool/BuildInputs/Editor.json --output-dir $output --host-contexts $contexts --libclang "<LLVM>/bin/libclang.dll"
```

Generation directly replaces fixed paths below MHT: `SDK`, `Host`, `Client`, `Contract`, `Diagnostics`, and `<gameId>.generated-artifacts.json`. There are no profile/batch directories or `current.json`. Removed outputs are cleaned and unchanged files keep their timestamps. The complete manifest is committed last; interrupted replacement cannot be consumed as a complete result. `generationId` remains a content fingerprint.

MBT compiles `Host/<owner>/<owner>.modhost.cpp`, `<owner>.modabi.cpp`, `<owner>.modverify.cpp`, `SDK/Private/<owner>.modviews.cpp`, and the independent `Client/<game>ModClient/<game>ModClient.modclient.cpp`. The generated `<owner>.modlink.h` retains the owner registration entry point from the host archive. `UModAPIHostSubsystem` registers the generated types for each game instance. `ExportTest` privately depends on the External `ExportTestHost` rules, which declare the stable library path through `PublicPreBuildLibraries`. Libraries and receipts live in `Intermediate/ModTools/ModBuildTool/<Target>/<Platform>/<Configuration>/<Architecture>` under the owning project or plugin. The client library belongs to the project.

MHT publishes `.modabi.h`, `.clientabi.json`, and `.sdkcontract.json` with an artifact manifest, explicit compilation units, and checked Host/Client API symbols. The [client ABI documentation](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md) defines query, Connect/Disconnect, token delivery, and lifecycle rules. MST consumes the resolved SDK contract; internal UE-view headers are private host artifacts.

The automatic adapter supports Win64 x64 MSVC and preserves the real UBT producer flags. The pre-build watch invalidates UHT on directory changes, and the post-build step verifies the archive's witness export in the actual owning binary before publishing its link receipt. See the [UHT integration](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md).

To grant a caller access, use the game instance's `UModAPIHostSubsystem::OpenLocalPlayer<UGameLocalPlayerAPI>(Player, Permissions)` with the chosen player and approved permissions. The generated provider metadata resolves that player's existing subsystem. Use the returned binding with `Mod::API::FViewFactory::Make<UGameLocalPlayerAPI>(Binding)` and pass the resulting view to the caller. Call `GetLocalCharacter()`, then `AddHealth(Amount)` on the successful character view, checking both results. Revoke the caller when its mod stops. Player removal, controller changes, and world cleanup close the player scope. An unavailable character produces `ObjectUnavailable`.

## Contributing

Use the [contribution guide](CONTRIBUTING.md), [Issue forms](https://github.com/Nelaric/UnrealModFramework/issues/new/choose), and [PR template](.github/pull_request_template.md) to prepare a focused change. The [development workflow](Docs/DevelopmentWorkflow.md) explains local checks, trusted PR builds, and documentation publishing.

## API documentation

The [API documentation entry](Docs/API/README.md) and coding standards are built with Doxygen and published at [UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/). Approved updates publish automatically from `main`. See the [organization hosting guide](Docs/Hosting/README.md) for publishing configuration.

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## License

Project-authored material is licensed under the [MIT License](LICENSE) unless stated otherwise. Unreal Engine and third-party material retain their own terms; see [NOTICE.md](NOTICE.md).

## Thanks

Thanks to everyone contributing to UnrealModFramework, to Epic Games for Unreal Engine, and to CircleCI for the Linux project build service.
