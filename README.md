<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework

[![Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

UnrealModFramework lets Unreal Engine games expose C++ and Blueprint APIs, Hooks, and Events to mods. Integrating `ModRuntime` lets a host game validate and mount `.mod` packages and run World-scoped extensions. Mod authors work in a standalone UE project using the target game's generated SDK and the independently built `ModDevelopmentSDKEditor` plugin. The development baseline is UE 5.6.

## Quick start

Clone the repository and run Setup from its root with the C++20 toolchain for your build host.

```sh
git clone https://github.com/Nelaric/UnrealModFramework.git
cd UnrealModFramework
```

| Build host | C++ toolchain | Setup |
| --- | --- | --- |
| Windows | MSVC and the Windows SDK | `.\Setup.bat` |
| Linux | GNU `g++` | `sh Setup.sh` |
| macOS | GNU `g++`, selected via `CXX` for Setup and the host build | `sh Setup.sh` |

Setup reuses CMake 3.20 or later from its cache or `PATH`; when none is available, it downloads a pinned archive and verifies its SHA-256 digest. It prefers Ninja and falls back to NMake on Windows or Make on Linux and macOS. [CMake package manifest](Setup/CMakePackages.txt)

On x64 and ARM64 Windows, Linux (glibc), and macOS hosts, Setup also downloads and verifies `libclang` and matching Clang resource headers from LLVM 21.1.8. It caches them under `.tools/libclang/` for offline reuse and writes a relative path file that UHT reads automatically. The initial download requires network access. [libclang package manifest](UnrealModFramework/Plugins/ModRuntime/Tools/LibClangPackages.txt)

Setup builds the four Release executables under `UnrealModFramework/Plugins/ModRuntime/Tools/build/<tool>/`.

### Build the development host

Install UE 5.6 and the target platform SDK. From the repository root, run ModBuildTool (MBT) to build the sample Win64 x64 Development Editor with automatic C++ Hook dispatch.

```powershell
$mbt = "./UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe"
& $mbt build-ue-target --engine "<UE installation>" --project "UnrealModFramework/UnrealModFrameworkDev.uproject" `
  --target UnrealModFrameworkDevEditor --platform Win64 --configuration Development --architecture x64 --mode normal
```

Change `--target` to `UnrealModFrameworkDev` for a Development Game build. When an export snapshot needs Editor binaries, the build bootstraps the configured Editor target automatically. To test in the Editor, open the development project once the build finishes. [Development host project](UnrealModFramework/UnrealModFrameworkDev.uproject) · [Linux command and target architectures](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#automatic-hook-builds)

UBT compiles generated host code in its owning module on every toolchain. The game's shared SDK Runtime module compiles the Client implementation. CircleCI builds the UE 5.6.1 Linux x64 Development Game and Editor targets and runs the `ChangeHealthDispatch` automation test in the Editor job. [Build and test coverage](Docs/DevelopmentWorkflow.md#linux-build-scope)

## Get ModRuntime from a development branch

Run Setup on the target branch, then run the export script from the repository root.

| Build host | Export command |
| --- | --- |
| Windows | `.\Scripts\GetDevelopmentPlugin.bat` |
| Linux and macOS | `sh Scripts/GetDevelopmentPlugin.sh` |

The script exports the plugin source, the four tool executables, `libclang`, and its resource headers to `Dist/ModRuntime`, atomically replacing any previous export on success. Run Setup and the export script on the OS and architecture where the bundled tools will execute. Copy `Dist/ModRuntime` to `Plugins/ModRuntime` in your UE C++ host project, enable the plugin, and build the project. Keep CMake 3.20 or later on `PATH` so `ExportMetadata.ConfigureTarget` can rebuild the native tools inside the copied plugin.

## Export APIs, Hooks, and Events

1. Call `ExportMetadata.ConfigureTarget(this)` once in each participating Game or Editor target. Exporting modules depend on `ModAPI` through their normal UE build rules. This dependency also exposes the export macros. [Host module and link setup](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#host-compilation-and-linking)
2. Set `mode` to `generate` in `Config/ModHeaderTool.json` and specify the game ID, type identities, and `metadataEditorTarget`. For Blueprint exports, list each saved asset, its stable `typeId`, and its owning module under `blueprintExports`. [Sample host configuration](UnrealModFramework/Config/ModHeaderTool.json)
3. MHT discovers project modules under `Source`. Use `Plugins` in the optional `<project>.modexport` beside the `.uproject` to select plugins. Set `ExcludeModules` at the top level for all selected modules or inside a plugin entry for that plugin alone. [Module selection](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#module-selection)
4. Declare exports in C++ or in the Blueprint editor's **蓝图Mod导出** panel, and build the target through MBT when using automatic C++ Hooks. [Host declarations and supported signatures](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md)

### Export a C++ Hook

Place `MOD_EXPORT_HOOK()` before a member declaration on a type with a World identity, and define the function out of line in its `.cpp` file. The sample also exposes the member to Blueprints with `BlueprintCallable`.

```cpp
MOD_EXPORT_HOOK()
UFUNCTION(BlueprintCallable, Category = "Character|Health")
EXPORTTEST_API float ChangeHealth(float Delta);
```

In `ExportTest`, this declaration derives `hook/game.character/change-health` from the configured World type ID and member name. Both C++ and Blueprint calls enter the same dispatcher. With the required grants, mods can modify inputs or cancel the call in `Pre`, replace the original implementation in `On`, and observe the result in `Post`.

Pass `NoPre`, `NoOn`, or `NoPost` in the macro to disable individual stages. For example, `MOD_EXPORT_HOOK(NoOn)` always runs the original function once `Pre` allows the call to proceed. Passing all three flags compiles the original function directly and omits the Hook from the SDK. When renaming a member, list its previous names in `CompatibleNames = ("ChangeHealth")`; former IDs retain their contracts and grants as long as the signature and stage policy remain compatible. [Hook contract and payloads](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#world-extension-artifacts)

### API and Event export fields

Field names and allowed values are case-sensitive. [ModExportMetadataSchema.h](UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public/ModExportMetadataSchema.h)

| Macro | Required fields | Optional fields |
| --- | --- | --- |
| Shared (API and Event) | — | `ExportVersion` (defaults to `"1.0.0"`), `DisplayName`, `Description` |
| `MOD_EXPORT_API` | `Scope`, `Thread`, `FailureRule` | `Permissions` (defaults to an empty list), `FullView`, `CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`, `PublishRule` | `PayloadType`, `ExportId`, `Permission`, `PublishPermission`, `UnsubscribeRule`, `Authority` (defaults to `Server`), `HostEndpoint`, `HostCommitPayload` |

MHT validates active declarations against the selected target. `HostCommitPayload` is reserved in the current generator and requires `HostEndpoint`. World-scoped Hooks and Events execute on the game thread. [Generated Event publishers](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#generated-c-event-publishers)

## Build and load a mod

1. List the v4 target manifests in the MDT v3 request and run `ModSDKTool build --request SdkBuildRequest.json`. Install the Host plugin in the game providing the shared Runtime and the authoring plugins in each Mod project. [SDK request and bundle](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md)
2. In the author project's Level Editor toolbar, choose **Mod → 打包 Mod…**, supply the target game's base release and matching Cook settings, and package either an enabled content plugin or the current C++ project. Packaging a C++ project requires the Pak backend and a matching Win64 x64 modular host receipt; to test the packaged sample game, build `UnrealModFrameworkDevModular` with a UE source build that supports modular Game targets. [Packaging and PIE setup](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.md)
3. Configure the release identity, container backend, and any native code permissions under `[ModLoader]` in the host's `Config/DefaultGame.ini`. Place the `.mod` archive in `Mods/` beside the running game executable—or, for Windows PIE, place an Editor-compatible archive in the host project's `Binaries/Win64/Mods/`. [Loader configuration](Docs/CodingStandards/Modules.md#mod-loader)

The host prepares World extensions before gameplay begins. The `ModExtensions` game mode demonstrates World preparation with a content slot and a World Patch anchor, multiplayer plan verification, and single-player save migration. Shipping builds and additional toolchain combinations require target-specific validation. [Runtime implementation and milestones](Docs/UnrealModFramework_游戏端详细设计.md#当前实现与限制)

## Repository layout

| Location | Contents |
| --- | --- |
| [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) | Development host with the `ExportTest` integration |
| [ModRuntime](UnrealModFramework/Plugins/ModRuntime/) | Game runtime, editor-side export modules, UHT adapter, and standalone tools |
| [TestMod](ExampleMods/TestMod/TestMod.uproject) | Independent authoring project with a character preview and native and Blueprint lifecycle callbacks |
| [Technical design](Docs/UnrealModFramework_设计文档.md) | Architecture and target-specific release criteria (Chinese) |
| [Performance benchmarks](Test/Performance/ue-call-benchmark.md) | Standalone UE call measurements with a [reproduction procedure](Docs/DevelopmentWorkflow.md#run-the-call-benchmark) |

`Binaries`, `Intermediate`, `Saved`, and `Dist` contain generated output.

## Standalone tool guides

| Tool | Role |
| --- | --- |
| [ModHeaderTool (MHT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md) | Scans declarations and generates host code, public contracts, and native ABI artifacts |
| [ModBuildTool (MBT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModBuildTool/README.md) | Drives UE builds with generated Hook sources, verifies generated archives, and checks final links |
| [ModSourceTool (MST)](UnrealModFramework/Plugins/ModRuntime/Tools/ModSourceTool/src/Cli/CommandLine.cpp) | Rewrites Hook source copies, analyzes failure returns and exceptions, and generates host-link headers |
| [ModSDKTool (MDT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md) | Plans, builds, and inspects SDK bundles and separately builds the authoring plugin |

## Contributing

See the [contribution guide](CONTRIBUTING.md), [Issue forms](https://github.com/Nelaric/UnrealModFramework/issues/new/choose), and [PR template](.github/pull_request_template.md) to prepare a focused change. The [development workflow](Docs/DevelopmentWorkflow.md) explains local checks, trusted PR builds, and documentation publishing.

## API documentation

The published site includes the API entry pages, public headers, and coding standards generated by Doxygen. [UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/) · [Repository API index](Docs/API/README.md)

The API Pages workflow publishes on pushes to `main` when `API_PAGES_ENABLED=true`. [Publishing configuration](Docs/Hosting/README.md)

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
