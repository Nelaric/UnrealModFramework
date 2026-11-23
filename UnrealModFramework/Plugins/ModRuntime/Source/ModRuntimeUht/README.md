<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModRuntime UHT adapter

`ModRuntime.ubtplugin.csproj` is a small .NET 8 UHT exporter. UE 5.6 discovers
the project inside the enabled ModRuntime plugin, builds the assembly, and loads
the `[UnrealHeaderTool]` class and `[UhtExporter]` entry. It uses assembly
references from the active engine, with no project references or engine changes.
The exporter is named `ModRuntime`. The reflected runtime module `ModLoader`
enables it in both Game and Editor manifests.

The adapter starts independent native MHT and MBT processes. C++ parsing,
validation, projection, and source generation remain in MHT; compilation and
archive validation remain in MBT. Blueprint export belongs to the
ExportCodeGenerator editor module. UBT performs the final game/module link.
See Epic's [UE 5.6 UHT extension documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6).

## Native tool and default invocation

Targets configured with
`ExportMetadata.ConfigureTarget(this)` build ModHeaderTool, ModBuildTool, and
ModSourceTool before UHT. For a direct UHT invocation, prepare the tools with
the native build script first. UHT looks for MHT in the following order.

1. `UMF_MHT_EXECUTABLE`.
2. `executable` in the project's optional `Config/ModHeaderTool.json`.
3. `<plugin>/Binaries/ThirdParty/ModHeaderTool/<host>/MHT[.exe]` or
   `ModHeaderTool[.exe]`, where host is Win64, Linux, or Mac.
4. The documented local build at
   `<plugin>/Tools/build/ModHeaderTool/ModHeaderTool[.exe]`.

Without configuration the default mode is `scan`. The development project's
`Config/ModHeaderTool.json` selects `generate`. In scan mode a normal UHT
invocation runs the following command.

```text
ModHeaderTool scan <actual.uproject> <metadata-options.json>
```

The authoritative metadata catalog defaults to
`<project>/Intermediate/ModTools/ModHeaderTool/MetadataOptions.json`. Before
generation, the adapter checks this snapshot with MHT. A missing, stale, or
older snapshot is exported with the current engine's `ModExportOptions`
commandlet and published after validation. Changes to tracked INI files such
as `DefaultEngine.ini` trigger this check on the next incremental build.
Scan mode refreshes an existing default snapshot and can still scan a project
with no snapshot when none of its declarations need it. An `options` override
that points elsewhere is managed by the caller.
On the build host, `metadataEditorTarget` names the Development Editor target that UBT
builds in bootstrap mode when the commandlet needs project binaries. Projects
that leave this setting out can prepare the Editor with `-MhtBootstrap`.

UHT reads the JSON report from stdout. Its output factory commits successful reports to
`Intermediate/ModTools/ModHeaderTool/Scan/<Target>/<Platform>/<Configuration>/Scan.json`
under the project. Scan validates marker parameters.

## Complete generation

Set `mode` to `generate` and list the export modules in `generation`. UBT records its actual platform, configuration and architecture in the UHT manifest. The adapter expands the target compiler's response files before the regular action graph is written. Each variant stores its `BuildInput.json` and pinned `Ubt/compile_commands.json` in `HostTargets`. MHT checks the target, module, configuration and CPU of the parsed translation unit.

UHT runs `ModHeaderTool generate` with the target database and host libclang. Win64 x64 MSVC also supplies independent MBT producer contexts. Other targets compile the generated host units directly in their owning UBT module through UHT's `CompileOutput`. This route publishes a `UMF.UbtHostArtifacts` manifest. MBT produces its existing `UMF.GeneratedArtifacts` manifest and independent SDK libraries on Win64 x64 MSVC.
The project generation root is
`Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>`.
Each run replaces that variant's
`SDK`, `Host`, `Client`, `Contract`, `Diagnostics` and
`<gameId>.generated-artifacts.json` at fixed paths. The adapter checks completeness, target identity, and every
generated file hash. Removed outputs are cleaned; identical files retain their
timestamps. The complete manifest is committed after all file replacements.
`generationId` remains a content identity.
For independent MBT modules owned by an exporting plugin, the adapter also publishes their
verified generated files at the same relative paths under the plugin's
`Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>`.
The project batch contains the shared contract, client ABI and complete manifest.

The example uses the development host's module and type identities. Set the
native tool and LLVM paths for your installation.

```json
{
  "schemaVersion": 1,
  "executable": "Plugins/ModRuntime/Binaries/ThirdParty/ModHeaderTool/Win64/MHT.exe",
  "mbt": "Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe",
  "mode": "generate",
  "timeoutSeconds": 600,
  "options": "Intermediate/ModTools/ModHeaderTool/MetadataOptions.json",
  "metadataEditorTarget": "UnrealModFrameworkDevEditor",
  "libclang": "<LLVM>/bin/libclang.dll",
  "blueprintExports": [{
    "asset": "/Game/Blueprints/BP_ModBlueprintAPIExportTest.BP_ModBlueprintAPIExportTest",
    "typeId": "game.blueprint_test",
    "module": "ExportTest"
  }],
  "generation": {
    "gameId": "UnrealModFrameworkDev",
    "typeIdentities": "Config/ModAPITypes.json",
    "modules": [{
      "name": "ExportTest",
      "sourceDirectory": "Source/ExportTest",
      "translationUnits": ["Source/ExportTest/GameCharacter.cpp", "Source/ExportTest/GameLocalPlayerAPI.cpp"]
    }],
    "analysisArguments": ["-Wno-invalid-constexpr"]
  }
}
```

Paths in this adapter configuration are relative to the project directory.
Paths inside a build input retain MHT's own resolution rules. `timeoutSeconds`
is 1–86400, default 600. Automatic preparation requires a configured `libclang`
with the resource headers in the same LLVM installation. `options` is optional.
MBT discovery uses `UMF_MBT_EXECUTABLE`, the root `mbt` setting, or the local
`Tools/build/ModBuildTool/ModBuildTool[.exe]` output under the ModRuntime plugin, in that order.
ModSourceTool (MST) discovery uses `UMF_MOD_SOURCE_TOOL_EXECUTABLE`, the root
`sourceTool` setting, or `Tools/build/ModSourceTool/ModSourceTool[.exe]` under the ModRuntime plugin,
in that order.
`UMF_MHT_BUILD_INPUT` selects generate mode and overrides target configuration.
`UMF_MHT_LIBCLANG` overrides the configured library path. Changing environment settings
requires a new UHT invocation, for example `-ForceHeaderGeneration` through UBT.

A target or variant `buildInput` selects a caller-managed input. Its compilation
database and response files must match the active target. Clang also requires
the headers produced by standard UHT CodeGen.

### Host Blueprint API exports

Configure `blueprintExports` in the project's `Config/ModHeaderTool.json`.
Each entry supplies the saved Blueprint object path as `asset`, a stable
`typeId`, and a participating Runtime `module`. The development project
includes an entry for `BP_ModBlueprintAPIExportTest`. Set `PublicName` on
the Blueprint API declaration before publishing. Existing declarations
with empty names and versions use their current declaration name and
`1.0.0`.

The `ModExportBlueprints` commandlet compiles the saved asset and passes
the compiled `UFunction` signature to MHT. The supported instance API
profile is `World`, `GameThread`, and `ReturnError`, with value scalar
inputs and one scalar output or `void`. The configured packages are added
to the host cook. Use `-MhtBootstrap` to prepare the Editor when needed.

The game calls `UModAPIHostSubsystem::RegisterWorldProvider` once its object
is ready and retains the registration. It uses `SetWorldCallerPolicy` to
select authorized mods, types, and permissions. The generated
`<SDK>API.h` provides `Acquire_<object reference type>`. A native mod calls
it with its active `UModInstance` to connect the generated tables and
acquire a reference for the typed SDK methods. The host invokes the actual
Blueprint instance with `ProcessEvent`. Mod unload, world closure, or
policy replacement revokes the corresponding callers.

### Host Blueprint Hook and Event exports

Add the saved asset to `blueprintExports`. Declare the Hook or Event in **蓝图Mod导出**, then connect the execution input on the corresponding **Hook Point** or **Publish Event** node. Unconnected declarations are skipped. A Hook's original-behavior graph takes and returns the same payload type, and the node exposes the result, status, cancellation, and commit state. Events support `Immediate` and `NextTick` with optional immediate unsubscription. Payloads may use supported scalars, reflected value structs, or UObjects with a generated World view.

For C++, associate the macro with a member signature on a type with a World identity. An empty `MOD_EXPORT_HOOK()` wraps the original out-of-line definition when MBT builds the target. C++ Events can call the generated publisher or use `HostEndpoint` to generate a publishing member definition. [Generated contracts and payload limits](../../Tools/ModHeaderTool/README.md#world-extension-artifacts)

### Automatic Hook builds

Run MBT on the host that owns the UBT compile actions, with the UE SDK for the selected target. Each invocation builds one architecture. This example builds the Linux Development Editor from the repository root.

```sh
mbt=UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool
"$mbt" build-ue-target --engine /opt/UE_5.6 --project UnrealModFramework/UnrealModFrameworkDev.uproject \
  --target UnrealModFrameworkDevEditor --platform Linux --configuration Development --architecture x64 --mode normal
```

Windows architectures are `x64`, `arm64` and `arm64ec`. Apple devices use `arm64`. UBT uses `iossimulator` for the iOS and visionOS simulators and `tvossimulator` for the tvOS simulator. `UMF_MHT_LIBCLANG` selects the build host's libclang, and `UMF_MHT_CLANG_RESOURCE_DIR` selects its matching resource directory when set. A project can set a host-specific library in `hostLibclang`. The compile database supplies the target SDK, include paths and architecture.

MHT and MST identify the annotated member in Clang's AST and write its source copy. MBT substitutes the input in the matching UBT action, including nested response files, and preserves its object and link outputs. The generated registration references a versioned symbol defined by the copy. COFF and ELF links reject an original object. On Apple targets the post-build step also checks the Mach-O imports with Xcode nm, including links that use dynamic lookup. C++ and the Blueprint thunk enter the same public function. When a bootstrap Editor is needed, the tools invoke UBT with `-MhtBootstrap`. That Development Editor calls the original function. The `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` test exercises both entry points.

The build rules use independent MBT archives for Win64 x64 MSVC and compile the generated host with UBT for other toolchains. Console SDKs, remote Mac action execution, universal binary packaging and installing third-party native Mod packages are separate platform integrations.

## Independent host compilation and linking

The owner rules call `ExportMetadata.ConfigureHost(this, Target)`. A companion
External rules class, such as `ExportTestHost`, calls
`ExportMetadata.ConfigureHostLibrary(this, Target, "ExportTest")`; the owner
privately depends on this External module. Setup is per owning module, while
individual API authors continue to write export macros and ordinary functions.

The rules write a stable target descriptor in the project below
`Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>`.
`MbtHostBuild` reads the actual MSVC action and expands its response files from
UBT's compilation database. The adapter pins the expanded commands and their
source database by hash. It preserves module producer definitions and ABI
settings, replaces source/object/dependency outputs, and adds generated include
paths. The current automatic adapter requires Win64 x64 MSVC, no PCH, no unity,
and no LTO. It does not guess missing compiler settings.

MHT emits declarations in public headers and all implementations in explicit
`.cpp` compilation units. MBT independently compiles those units and publishes
`Intermediate/ModTools/ModBuildTool/<Target>/<Platform>/<Configuration>/<Architecture>/<owner>.lib`
and its `.mbt.json` receipt under the owning project or plugin. A separate `<game>ModClient` producer context removes UE/game includes and forced definitions while retaining the real selected toolchain flags. MBT independently compiles the client ABI library into the project's `Intermediate/ModTools/ModBuildTool` variant directory. The v3 manifest lists all five host/client units for the one-owner sample and binds both endpoint sets to ExportIds. ModSDKTool (MDT) publishes the C ABI header and, for structured error catalogs, the separate `<game>.moderrors.h` header. See the [MHT connection protocol](../../Tools/ModHeaderTool/README.md). The generated manifest's `hostBuildContexts` map pins
each actual producer context. An unchanged MBT fingerprint reuses the library.

The private External module uses UE 5.6 `PublicPreBuildLibraries` to declare a
library that can be absent before the first UHT invocation. No empty placeholder
is created. UHT must finish MBT successfully before UBT reaches final linking.
The library does not propagate through an owning DLL's private dependency.
After native publication the adapter refreshes UE's cached `FileItem` information
for the library, receipt, and generated files. Internal UHT and UBT share that
cache; refreshing it lets the current build detect the new archive and relink.
After MBT builds the archive, the adapter runs `ModSourceTool generate-host-link`
to write `<owner>.modlink.h` under `Intermediate/ModTools/ModSourceTool/<Target>/<Platform>/<Configuration>/<Architecture>`
in the owning project or plugin. UBT force-includes this header in the owner module so the linker retains `UMF_ModHost_<owner>`
and its generated registrar. At game instance startup, `UModAPIHostSubsystem`
runs the generated registrations. MHT marks exported `ULocalPlayerSubsystem`
types in the Player scope so the runtime can resolve the existing service from
an explicitly chosen player. MBT also compiles SDK view methods, constructors,
and TypeId helpers.

The Target rules call `ExportMetadata.ConfigureTarget(this)` once to install
the UE 5.6 build hooks. UBT runs `Tools/Build.bat --target ModRuntimeUhtTools`
on Windows and `sh Tools/Build.sh --target ModRuntimeUhtTools` on Linux and macOS
when the script is present. The scripts reuse `Tools/build/.cmake` for incremental
Release builds. If the tools fail to build, UBT stops before UHT. Linux and
macOS use GNU `g++`. On macOS, set `CXX` to the installed GNU C++ compiler.
A staged Unix project without the workspace CMake finder can continue the UE
build when CMake is neither configured nor available on PATH.

On Win64 x64, the next pre-build step asks MBT to update the UHT dependency
stamp when a watched directory changes or a recorded optional file appears.
In the post-build step, MBT verifies the exact archive's PE export in the owning
DLL or EXE and checks the UBT target receipt and modular ownership. It writes
`<owner>.lib.link.json` and `<owner>.lib.linked.json` beside the archive.
The client library has its own build receipt.

## Build behavior and limits

Native diagnostics are forwarded to UHT. Nonzero exit codes, invalid results,
and timeouts fail UHT. UE's managed process group owns the child process lifetime.
The adapter does not enable `CompileOutput` or register C++ output filters;
MHT sources remain outside UBT's compilation list.

UHT no-output/reference, bootstrap, and `-Mode=GenerateClangDatabase` invocations
skip native generation. The normal build runs MHT after preparation. Complete generation
is rejected with `-FailIfGeneratedCodeChanges` because MHT has no matching dry-run
contract. Scan outputs retain UHT's normal change-checking behavior.

Existing tool, configuration, metadata, reflected headers, build inputs, and
generated-manifest inputs, metadata configuration inputs, compiler recipes, libraries, MBT receipts, and their
compiler dependencies are registered using `AddExternalDependency`. UHT
regenerates the affected variant when a registered file or the watch stamp
changes. With MSVC, the Windows SDK, and LLVM installed, a normal Win64 build
prepares the default project's inputs from a clean checkout.
The automatic link verification covers standard Win64 x64 project DLLs and
monolithic EXEs. Live Coding and Hot Reload require separate integration.

UBT generates the adjacent `.ubtplugin.csproj.props` file containing `EngineDir`;
it is ignored by Git. For a direct managed build:

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
