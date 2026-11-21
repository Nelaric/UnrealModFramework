<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework

[![Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

UnrealModFramework provides a game-side mod runtime and an independently built mod authoring editor for Unreal Engine 5.6. The development host integrates generated C++ and Blueprint contracts, `.mod` packaging and loading, and World-scoped extensions.

| Location | What to find |
| --- | --- |
| [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) | Development host and `ExportTest` integration example |
| [ModRuntime](UnrealModFramework/Plugins/ModRuntime/) | Game runtime, editor-side export tools, UHT adapter, and standalone tools |
| [TestMod](ExampleMods/TestMod/TestMod.uproject) | Independent author project for the character preview, native module, and Blueprint lifecycle callbacks |
| [Technical design](Docs/UnrealModFramework_设计文档.md) | Architecture and target-specific release criteria (Chinese) |
| [Performance benchmarks](Test/Performance/ue-call-benchmark.md) | Standalone UE call measurements and the [reproduction procedure](Docs/DevelopmentWorkflow.md#run-the-call-benchmark) |

`Binaries`, `Intermediate`, `Saved`, and `Dist` contain generated output.

## Quick start

1. Install the C++20 toolchain for your platform.
2. Clone the repository and enter its root directory.

   ```sh
   git clone https://github.com/Nelaric/UnrealModFramework.git
   cd UnrealModFramework
   ```

3. Run Setup for your platform.

   | Platform | C++ toolchain | Command |
   | --- | --- | --- |
   | Windows | MSVC and the Windows SDK | `.\Setup.bat` |
   | Linux | GNU `g++` | `sh Setup.sh` |
   | macOS | GNU `g++`, selected through `CXX` | `sh Setup.sh` |

Setup can use CMake 3.20 or later from `PATH`. On a fresh checkout without a suitable CMake, Setup downloads the pinned Kitware archive and verifies its SHA-256 hash. The initial download requires network access. [CMake package manifest](Setup/CMakePackages.txt)

Setup builds Release versions of the standalone tools `ModHeaderTool`, `ModBuildTool`, `ModSourceTool`, and `ModSDKTool`. Find each executable under `UnrealModFramework/Plugins/ModRuntime/Tools/build/<tool>/` and run it with `--help` to check that it starts. With Ninja or Unix Makefiles, pass `--jobs 8` to limit the build to eight parallel jobs.

### Build the development host on Win64

Build the `UnrealModFrameworkDevEditor` and `UnrealModFrameworkDev` Development targets with UE 5.6 and MSVC x64. If the configured libclang path is unavailable, set `UMF_MHT_LIBCLANG` to the full path to `libclang.dll` in an LLVM installation with matching Clang resource headers.

```powershell
$env:UMF_MHT_LIBCLANG = 'C:\LLVM\bin\libclang.dll'
$project = (Resolve-Path .\UnrealModFramework\UnrealModFrameworkDev.uproject).Path
$build = 'C:\UE_5.6\Engine\Build\BatchFiles\Build.bat'
& $build UnrealModFrameworkDevEditor Win64 Development "-Project=$project"
& $build UnrealModFrameworkDev Win64 Development "-Project=$project"
```

UBT prepares the UHT tools, obtains Editor metadata when needed, and links the generated host archives. Automatic contract generation and host linking currently run on Win64 x64 MSVC. [UHT build and bootstrap guide](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md)

### Export macro fields

Field names and allowed values are case-sensitive and defined in [ModExportMetadataSchema.h](UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public/ModExportMetadataSchema.h).

| Macro | Required fields | Optional fields |
| --- | --- | --- |
| All three | — | `ExportVersion` defaults to `"1.0.0"`, plus `DisplayName` and `Description` |
| `MOD_EXPORT_API` | `Scope`, `Thread`, `FailureRule` | `Permissions` defaults to an empty list, plus `FullView` and `CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`, `PublishRule` | `PayloadType`, `ExportId`, `Permission`, `PublishPermission`, `UnsubscribeRule`, `Authority` |
| `MOD_EXPORT_HOOK` | — | `ContextType`, `ExportId`, `Permission`, `Authority`, `AllowedStages` (defaults to `(Post)`), `AllowedOperations` (defaults to `()`) |

Complete generation checks active declarations against the selected target. Current Hook and Event generation uses World scope and runs on the game thread. The host connects generated C++ helpers or Blueprint extension nodes at the gameplay call sites. [Host authoring and supported signatures](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md)

## Build and load a mod

1. Build the host exports, then use MDT to publish the matching game SDK and generated `ModDevelopmentSDKEditor` plugin. For the sample, install `UMFDevSDK` and `ModDevelopmentSDKEditor` under `ExampleMods/TestMod/Plugins`. [SDK request and output](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md)
2. In the author's Level Editor toolbar, open **Mod → 打包 Mod…** to package an enabled content plugin or the current C++ project with the target game's base release and matching cook settings. The C++ project path requires a matching Win64 x64 modular host receipt and Pak backend. [Packaging guide](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.md)
3. Place the package in `Mods` beside the actual game executable, or in the host project's `Binaries/Win64/Mods` for Windows PIE with an Editor-compatible package. Configure the release identity, backend, and any native code permissions in the host's `[ModLoader]` section. [Loader configuration](Docs/CodingStandards/Modules.md#mod-loader)

The host opts into World extensions before gameplay. The runtime supports Hook/Event subscriptions, content selection, World Patch anchors, and versioned cross-mod services. The development host's `ModExtensions` game mode demonstrates World preparation, multiplayer plan matching, and single-player save migration. Shipping and additional toolchain combinations require target-specific validation. [Runtime implementation and milestones](Docs/UnrealModFramework_游戏端详细设计.md)

## Standalone tool guides

| Tool | Role |
| --- | --- |
| [ModHeaderTool (MHT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md) | Scans declarations and generates host code, public contracts, and native ABI artifacts |
| [ModBuildTool (MBT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModBuildTool/README.md) | Compiles and verifies generated archives and checks final-link records |
| [ModSourceTool (MST)](UnrealModFramework/Plugins/ModRuntime/Tools/ModSourceTool/src/Cli/CommandLine.cpp) | Traces error returns and exceptions, maintains the error catalog, and generates the owning module's host-link header |
| [ModSDKTool (MDT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md) | Plans, builds, and inspects SDK bundles and separately builds the authoring plugin |

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
