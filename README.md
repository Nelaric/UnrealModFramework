<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework

[![Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

UnrealModFramework enables Unreal Engine games to expose C++ and Blueprint APIs, Hooks, and Events to mods with compile-time generated contracts. By integrating `ModRuntime`, a host game can validate and mount `.mod` archives and run World-scoped extensions without exposing private game modules. Mod authors work in their own standalone UE projects using a generated game SDK alongside the independently built `ModDevelopmentSDKEditor` plugin.

The current development baseline is **Unreal Engine 5.6**.

## Quick start

Clone the repository and run the setup script from the root directory after installing the C++20 toolchain for your host OS:

```sh
git clone https://github.com/Nelaric/UnrealModFramework.git
cd UnrealModFramework
```

| Build host | C++20 toolchain | Setup command |
| --- | --- | --- |
| Windows | MSVC and the Windows SDK | `.\Setup.bat` |
| Linux | GNU `g++` | `sh Setup.sh` |
| macOS | GNU `g++` (selected via `CXX` for Setup and host builds) | `sh Setup.sh` |

Setup reuses CMake 3.20+ from its local cache or `PATH`, or downloads a pinned archive verified by SHA-256 (see [Setup/CMakePackages.txt](Setup/CMakePackages.txt)). It prefers Ninja as the CMake generator, falling back to NMake on Windows and Make on Linux or macOS.

On x64 and ARM64 Windows, Linux (glibc), and macOS hosts, Setup also downloads and verifies `libclang` and matching Clang resource headers from LLVM 21.1.8 (see [LibClangPackages.txt](UnrealModFramework/Plugins/ModRuntime/Tools/LibClangPackages.txt)). Network access is required on the first run; afterward, the files are cached under `.tools/libclang/` for offline reuse and linked via a relative path file that UHT reads automatically.

When finished, Setup outputs Release executables for all four standalone tools under `UnrealModFramework/Plugins/ModRuntime/Tools/build/<tool>/`.

### Build the development host

With UE 5.6 and the target platform SDK installed, run ModBuildTool (MBT) from the repository root to build the sample Win64 x64 Development Editor with automatic C++ Hook dispatch:

```powershell
$mbt = "./UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe"
& $mbt build-ue-target --engine "<UE installation>" --project "UnrealModFramework/UnrealModFrameworkDev.uproject" `
  --target UnrealModFrameworkDevEditor --platform Win64 --configuration Development --architecture x64 --mode normal
```

To build a Development Game instead, change `--target` to `UnrealModFrameworkDev`. Whenever an export snapshot requires Editor binaries that have not been built yet, the build bootstraps the configured Editor target automatically. Once compilation finishes, open [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) to test in the Editor. For Linux commands and supported target architectures, see [Automatic Hook builds](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#automatic-hook-builds).

During compilation, UBT builds the generated host bindings directly inside each owning module and writes a v4 manifest under `Intermediate/ModTools/ModHeaderTool/Generated/`. On supported desktop targets, MBT verifies the linked host binaries and writes `host-link.json` to `Intermediate/ModTools/ModHeaderTool/HostTargets/` for that target variant. CircleCI validates UE 5.6.1 Linux x64 Development Game and Editor builds, Editor Hook dispatch, and the standalone authoring plugin (see [Linux build scope](Docs/DevelopmentWorkflow.md#linux-build-scope)).

## Get ModRuntime from a development branch

To package `ModRuntime` for your own project from a branch, run Setup on that branch and then run the export script from the repository root:

| Build host | Export command |
| --- | --- |
| Windows | `.\Scripts\GetDevelopmentPlugin.bat` |
| Linux and macOS | `sh Scripts/GetDevelopmentPlugin.sh` |

The script bundles the plugin source, all four tool executables, `libclang`, and its Clang resource headers into `Dist/ModRuntime`, atomically replacing any previous export on success. Run both Setup and the export script on the same OS and CPU architecture that will execute the bundled tools.

Copy `Dist/ModRuntime` to `Plugins/ModRuntime` in your UE C++ host project, enable the plugin, and build. Keep CMake 3.20+ on `PATH` if your target rebuilds the native tools during UBT pre-build steps, or point the target at prebuilt executables as described in [Native tool and default invocation](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#native-tool-and-default-invocation).

## Export APIs, Hooks, and Events

1. **Configure target rules:** Call `ExportMetadata.ConfigureTarget(this)` once in each participating Game or Editor `.Target.cs`. Add `ModAPI` to the `.Build.cs` dependencies of any module that exports declarations; this exposes both the export macros and host runtime interfaces (see [Host compilation and linking](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#host-compilation-and-linking)).
2. **Configure generation:** Set `mode` to `generate` in `Config/ModHeaderTool.json` and fill in the game ID, type identities, and `metadataEditorTarget`. For Blueprint exports, list each saved asset path, its stable `typeId`, and its owning Runtime module under `blueprintExports` (see the [sample host configuration](UnrealModFramework/Config/ModHeaderTool.json)).
3. **Select modules:** MHT automatically scans project modules under `Source/`. To include plugins or exclude specific modules, place an optional `<project>.modexport` file next to your `.uproject`—use `Plugins` to opt plugins in, and `ExcludeModules` either at the top level or inside an individual plugin entry (see [Module selection](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#module-selection)).
4. **Declare and build:** Annotate C++ declarations with export macros or create Blueprint exports in the Blueprint editor's **蓝图Mod导出** panel. When using automatic C++ Hooks, build the target through MBT (see the [UHT adapter guide](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md)).

### Export a C++ Hook

Place `MOD_EXPORT_HOOK()` before a member declaration on a type that has a configured World identity, and define the function out of line in a `.cpp` file. You can combine it with standard UE specifiers such as `BlueprintCallable`:

```cpp
MOD_EXPORT_HOOK()
UFUNCTION(BlueprintCallable, Category = "Character|Health")
EXPORTTEST_API float ChangeHealth(float Delta);
```

In `ExportTest`, this declaration derives the Hook ID `hook/game.character/change-health` from the World type ID and member name, routing both C++ and Blueprint calls through the same dispatcher. When granted the corresponding permissions, mods can modify inputs or cancel execution in `Pre`, replace the implementation in `On`, and observe the return value in `Post`.

Pass `NoPre`, `NoOn`, or `NoPost` to disable individual stages. For instance, `MOD_EXPORT_HOOK(NoOn)` guarantees that the original function runs whenever `Pre` allows the call through, while passing all three flags compiles the original function directly and omits the Hook from the SDK. When renaming a hooked function, list its previous names via `CompatibleNames = ("ChangeHealth")`—old IDs keep their contracts and grants intact as long as the signature and stage policy stay compatible. See [World extension artifacts](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#world-extension-artifacts) for details.

### API and Event export fields

Macro field names and candidate values are case-sensitive (see [ModExportMetadataSchema.h](UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public/ModExportMetadataSchema.h)):

| Macro | Required fields | Optional fields |
| --- | --- | --- |
| Shared (`MOD_EXPORT_API` & `MOD_EXPORT_EVENT`) | — | `ExportVersion` (defaults to `"1.0.0"`), `DisplayName`, `Description` |
| `MOD_EXPORT_API` | `Scope`, `Thread`, `FailureRule` | `Permissions` (defaults to empty), `FullView`, `CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`, `PublishRule` | `PayloadType`, `ExportId`, `Permission`, `PublishPermission`, `UnsubscribeRule`, `Authority` (defaults to `Server`), `HostEndpoint`, `HostCommitPayload` |

MHT validates active declarations against the target being compiled. `HostCommitPayload` is currently reserved by the generator and requires `HostEndpoint`. All World-scoped Hooks and Events execute on the game thread (see [Generated C++ Event publishers](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#generated-c-event-publishers)).

## Build and load a mod

1. **Publish the game SDK:** Build the host Game and Editor targets, reference their v4 manifests in an MDT v3 build request, and run `ModSDKTool build --request SdkBuildRequest.json`. Enable the generated `Host/Plugins/<game-sdk>` in the host project before building and cooking its base release, and install both plugins under `Plugins/` into each mod author's project (see [Build a sample SDK](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md#build-a-sample-sdk) and [Install and verify](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md#install-and-verify)).
2. **Package the mod:** In the authoring project's Level Editor toolbar, open **Mod → 打包 Mod…**, supply the target game's base release and matching Cook settings, and package either an enabled content plugin or the current C++ project. Packaging a C++ project requires the Pak backend and a matching Win64 x64 modular host `.target` receipt; to test against the sample host, build `UnrealModFrameworkDevModular` using a UE source build that supports modular Game targets (see [Mod Development SDK Editor](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.md)).
3. **Mount and run:** Configure the release identity, container backend, and any native code opt-ins under `[ModLoader]` in the host's `Config/DefaultGame.ini` (see [Mod loader configuration](Docs/CodingStandards/Modules.md#mod-loader)). Drop the `.mod` file into `Mods/` next to the packaged game executable—or, for Windows PIE, place an Editor-compatible `.mod` in the host project's `Binaries/Win64/Mods/`.

The host prepares World extensions before gameplay starts. In the sample project, the `ModExtensions` game mode demonstrates World preparation with content slots and Patch anchors, multiplayer plan handshakes, and single-player save migration (see [Host startup and World preparation](Docs/UnrealModFramework_设计文档.md#6-启动编排与失败处理)). Shipping configurations and additional platform/toolchain combinations must be validated per target.

## Repository layout

| Location | Contents |
| --- | --- |
| [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) | Sample host project with the `ExportTest` integration |
| [ModRuntime](UnrealModFramework/Plugins/ModRuntime/) | Game runtime, Editor export modules, UHT adapter, and standalone C++ tools |
| [TestMod](ExampleMods/TestMod/TestMod.uproject) | Standalone mod authoring project featuring character preview and C++/Blueprint lifecycle callbacks |
| [Technical design](Docs/UnrealModFramework_设计文档.md) | System architecture, protocols, and per-target release criteria (Chinese) |
| [Shared Runtime regression](Test/SharedRuntime/README.md) | End-to-end test running two independently packaged mods against one shared SDK Runtime in a staged Win64 game |
| [Performance benchmarks](Test/Performance/ue-call-benchmark.md) | Standalone UE call latency measurements and [reproduction instructions](Docs/DevelopmentWorkflow.md#run-the-call-benchmark) |

`Binaries`, `Intermediate`, `Saved`, and `Dist` hold generated build outputs and are not tracked in Git.

## Standalone tool guides

| Tool | Role |
| --- | --- |
| [ModHeaderTool (MHT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md) | Parses active declarations and generates host bridges, public contracts, and C ABI headers |
| [ModBuildTool (MBT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModBuildTool/README.md) | Drives UE builds with rewritten Hook sources and verifies linked desktop binaries |
| [ModSourceTool (MST)](UnrealModFramework/Plugins/ModRuntime/Tools/ModSourceTool/README.md) | Rewrites out-of-line Hook definitions and analyzes API failure returns and exceptions |
| [ModSDKTool (MDT)](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md) | Plans, builds, and verifies SDK bundles, and builds the standalone authoring plugin |

## Contributing

Read the [contribution guide](CONTRIBUTING.md), choose an [issue form](https://github.com/Nelaric/UnrealModFramework/issues/new/choose), and follow the [PR template](.github/pull_request_template.md) to prepare focused changes. Local checks, trusted PR builds, and documentation publishing are covered in [Development and CI Workflow](Docs/DevelopmentWorkflow.md).

## API documentation

Our Doxygen site publishes the API entry pages, public headers, and project coding standards:

- **Live site:** [UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/)
- **Repository index:** [Docs/API/README.md](Docs/API/README.md)

When `API_PAGES_ENABLED=true`, pushes to `main` automatically rebuild and deploy the site via GitHub Pages (see [Organization documentation hosting](Docs/Hosting/README.md)).

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## License

Project-authored material is licensed under the [MIT License](LICENSE) unless stated otherwise. Unreal Engine and third-party components retain their own terms; see [NOTICE.md](NOTICE.md).

## Thanks

Thanks to everyone contributing to UnrealModFramework, to Epic Games for Unreal Engine, and to CircleCI for supporting our Linux build pipeline.
