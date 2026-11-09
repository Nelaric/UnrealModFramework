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

`ModSDKTool` (MST) provides `plan`, `build`, and `inspect` for game-specific SDK bundles. Build it with `Tools/Build.bat --target ModSDKTool`. See the [MST guide](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md) for the request and artifact formats.

## MHT C++ export parameter validation

MHT is a standalone CMake C++20 tool for C++ `MOD_EXPORT_API`, `MOD_EXPORT_EVENT`, and `MOD_EXPORT_HOOK` declarations. It does not scan or validate Blueprints. Headers live in `include/ModHeaderTool`, implementations in `src`, and `main.cpp` only forwards the entry point. Build the tool:

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModHeaderTool
```

After compiling the UE Editor target, export choices from the shared `ExportMetadata::FOptions` provider. This uses UE-resolved GameMod configuration and the global GameplayTags dictionary, including native and data-table tags. MHT does not maintain an approximate INI/tag parser or depend on `BlueprintIntegration`:

```powershell
$project = (Resolve-Path UnrealModFramework/UnrealModFrameworkDev.uproject).Path
& "C:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=ModExportOptions -unattended -NullRHI -nosound
.\UnrealModFramework\Plugins\ModRuntime\Tools\build\Release\bin\Release\ModHeaderTool.exe scan --project $project --output UnrealModFramework/Intermediate/ModHeaderTool/Exports.json
```

The catalog defaults to `Saved/ModHeaderTool/MetadataOptions.json` under the project. Override it with the commandlet's `-Output=<file.json>` and MHT's `--options <file.json>`. Added, removed, or changed configuration inputs invalidate its fingerprints and require re-export. Re-export after rebuilding native-tag modules or updating tag data tables as well. A catalog is unnecessary when no export invocations are found. The optional `.modexpot` scan-directory configuration is never created automatically.

`ModExportMetadataSchema.h` defines case-sensitive field names and choice mappings:

| Declaration | Required fields | Optional fields |
| --- | --- | --- |
| All three | `ExportId`: string literal naming a registered GameplayTag | `ExportVersion` defaults to `"1.0.0"`; `DisplayName` and `Description` |
| `MOD_EXPORT_API` | `Scope`, `Thread`, `FailureRule` | `Permissions` defaults to an empty list |
| `MOD_EXPORT_EVENT` | `SourceScope`, `PublishRule` | `PayloadType` |
| `MOD_EXPORT_HOOK` | Only the common required field | `ContextType`; `AllowedStages` defaults to `(Observe)` |

Choices accept identifiers or strings. Permissions and stages use parenthesized lists, such as `Permissions = ("WorldRead", "WorldWrite")` or `AllowedStages = (Observe, Modify)`. Stages require at least one item; duplicate items are errors. `ExportVersion` is a two- or three-component numeric version string without redundant leading zeros. `PayloadType` and `ContextType` validate C++ type-name syntax; type existence and declaration binding remain AST-stage concerns.

```cpp
MOD_EXPORT_API(
    ExportId = "ModTest.APIExpot.Blueprint.TestFunction",
    Scope = World,
    Thread = GameThread,
    FailureRule = ReturnError,
    Permissions = ("WorldRead"))
```

Parsing supports comments, line splices, adjacent strings, ordinary/raw strings, standard escapes, Unicode escapes, and `TEXT("...")`. Arbitrary C++ expressions, named constants, user-defined string suffixes, and nested lists are rejected. Unknown/duplicate fields, missing required fields, wrong literal types, unregistered IDs, invalid choices, and duplicate list entries produce `file(line,column): error: macro: reason` diagnostics. Lines and columns are one-based; columns count UTF-8 bytes.

Recoverable diagnostics are collected before returning `1` without writing a successful report. Success returns `0`. Report schema version `2` preserves raw arguments and adds parsed `Metadata` and `ParametersValidated`. `PreprocessorEvaluated` remains `false`: conditional branches are not evaluated, and AST types, declaration binding, and runtime policy semantics remain outside this stage.

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
