<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModRuntime UHT Adapter

When `ModRuntime` is enabled in a UE project, UnrealBuildTool (UBT) discovers `ModRuntime.ubtplugin.csproj`, builds the assembly against the active engine's target framework (falling back to .NET 8), and loads its `[UnrealHeaderTool]` plugin and `[UhtExporter]` entry point. Because the reflected `ModLoader` module participates in both Game and Editor targets, the exporter runs automatically across both target types.

In the overall build pipeline, this adapter bridges UBT/UHT and the native Mod tools:

- **Adapter (`ModRuntimeUht`):** Gathers the active target's compile commands and snapshots, invokes ModHeaderTool (MHT), and registers generated host C++ wrappers back into UBT.
- **ModHeaderTool (MHT):** Parses C++ declarations via `libclang` and emits host bindings, public contracts, and C ABI headers.
- **ExportCodeGenerator:** Compiles and snapshots configured Blueprint exports in Editor commandlet runs.
- **UBT & ModBuildTool (MBT):** Compile generated host bindings inside their owning modules, substitute rewritten Hook sources, and verify final desktop binaries.

## Native tool and default invocation

Calling `ExportMetadata.ConfigureTarget(this)` in a `.Target.cs` file registers a pre-build step that builds MHT, MBT, and ModSourceTool (MST). If your pipeline provisions prebuilt binaries separately, call `ExportMetadata.ConfigureTarget(this, buildNativeTools: false)` and ensure all three executables are in place before UBT starts (or run the native build script manually before invoking UHT directly).

UHT locates MHT in the following priority order:

1. The `UMF_MHT_EXECUTABLE` environment variable.
2. The `executable` field in the project's optional `Config/ModHeaderTool.json`.
3. `<plugin>/Binaries/ThirdParty/ModHeaderTool/<host>/MHT[.exe]` or `ModHeaderTool[.exe]`, where `<host>` is `Win64`, `Linux`, or `Mac`.
4. The local build output at `<plugin>/Tools/build/ModHeaderTool/ModHeaderTool[.exe]`.

MBT is resolved in a matching order (`UMF_MBT_EXECUTABLE` → root `mbt` config field → `Tools/build/ModBuildTool/ModBuildTool[.exe]`), as is MST (`UMF_MOD_SOURCE_TOOL_EXECUTABLE` → root `sourceTool` config field → `Tools/build/ModSourceTool/ModSourceTool[.exe]`; see the [ModSourceTool guide](../../Tools/ModSourceTool/README.md)).

### Default `scan` mode and metadata snapshots

If `Config/ModHeaderTool.json` is absent or omits `mode`, the adapter defaults to `scan` mode (the sample project's config selects `generate`). During a standard UHT run in `scan` mode, the adapter executes:

```text
ModHeaderTool scan <actual.uproject> <metadata-options.json>
```

MHT validates export macro syntax and field values, writes a JSON report to stdout, and lets UHT's output factory commit successful reports to `Intermediate/ModTools/ModHeaderTool/Scan/<Target>/<Platform>/<Configuration>/Scan.json`.

The authoritative metadata option catalog lives at `<project>/Intermediate/ModTools/ModHeaderTool/MetadataOptions.json` by default. Before running generation, the adapter asks MHT to validate this snapshot; if it is missing, out of date, or from an older schema, the adapter runs the engine's `ModExportOptions` commandlet to export and publish a fresh copy. Edits to tracked INI files such as `DefaultEngine.ini` automatically re-trigger this check on the next incremental build. (`scan` mode also refreshes an existing default snapshot, and can still scan a project with no snapshot if none of its declarations depend on configured options; custom paths set via `options` are managed by the caller.)

When a commandlet needs project Editor binaries that have not been built yet, `metadataEditorTarget` tells UBT which Development Editor target to bootstrap automatically. Projects that omit `metadataEditorTarget` can bootstrap their Editor manually with `-MhtBootstrap`.

## Complete generation

To generate full host bindings and SDK contracts, set `mode` to `generate` and populate `generation` with your game ID and type-identity map. The development host uses the configuration below, discovering tools and `libclang` automatically from Setup (see [Config/ModHeaderTool.json](../../../../Config/ModHeaderTool.json)):

```json
{
  "schemaVersion": 1,
  "mode": "generate",
  "metadataEditorTarget": "UnrealModFrameworkDevEditor",
  "blueprintExports": [{
    "asset": "/Game/Blueprints/BP_ModBlueprintAPIExportTest.BP_ModBlueprintAPIExportTest",
    "typeId": "game.blueprint_test",
    "module": "ExportTest"
  }],
  "generation": {
    "gameId": "UnrealModFrameworkDev",
    "typeIdentities": "Config/ModAPITypes.json",
    "analysisArguments": ["-Wno-invalid-constexpr"]
  }
}
```

Relative paths in `Config/ModHeaderTool.json` resolve from the `.uproject` directory, whereas paths inside a low-level build input follow MHT's own resolution rules. Both `options` and `timeoutSeconds` (`1`–`86400`, default `600`) are optional. By default, the adapter locates the host `libclang` and matching Clang resource headers through `Tools/build/libclang-<system>-<arch>.path` (which works identically inside a `Dist/ModRuntime` export) before falling back to a system LLVM installation; explicit environment or config paths always take precedence.

You can also override generation via environment variables: setting `UMF_MHT_BUILD_INPUT` forces `generate` mode with a custom build input, and `UMF_MHT_LIBCLANG` overrides the `libclang` binary path. After changing environment variables, force a fresh UHT run (for example, by passing `-ForceHeaderGeneration` to UBT).

### How target inputs and outputs are managed

UBT records the active platform, configuration, and CPU architecture in the UHT manifest. During normal builds, the adapter runs UBT's `GenerateClangDatabase` for the active target, expands compiler response files before the main action graph is written, and saves `BuildInput.json` plus `Ubt/compile_commands.json` under `Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>/`. MHT verifies the target, module, configuration, and architecture of every translation unit it parses (caller-supplied `buildInput` overrides must likewise match the active target and include headers emitted by standard UHT CodeGen).

Generated outputs are written to `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>/`, updating `SDK/`, `Host/`, `Client/`, `Contract/`, `Diagnostics/`, and the `UMF.GeneratedArtifacts` v4 manifest (`<gameId>.generated-artifacts.json`). Each run verifies completeness, target identity, and per-file hashes: obsolete files are removed, unchanged files keep their timestamps, and the v4 manifest is committed atomically at the end with a content-addressed `generationId`. The adapter registers Host and compile-verification C++ units with UBT and leaves `ClientSource` for ModSDKTool (MDT).

### Module selection

By default—when no `<project>.modexport` file exists beside the `.uproject` or when it contains `{}`—MHT recursively discovers all modules under the project's `Source/` tree via their `*.Build.cs` files. To include plugins or exclude specific modules, add `<project>.modexport`:

```json
{
  "ExcludeModules": ["GameEditor"],
  "Plugins": [
    { "Name": "Combat", "ExcludeModules": ["CombatEditor"] },
    "Audio"
  ]
}
```

- Entries in `Plugins` can be plain strings (either a plugin name under `Plugins/` or a relative/absolute `.uplugin` path) or objects specifying `Name` or `Path` alongside plugin-scoped `ExcludeModules` (relative paths resolve from the `.uproject` directory).
- Top-level `ExcludeModules` applies across the project and all selected plugins, whereas per-plugin `ExcludeModules` applies only to that plugin (if multiple entries resolve to the same plugin, their exclusions merge). Module names match case-insensitively, while UE still decides which plugins are enabled for the active target.

Both `scan` and `generate` share these rules. For every selected module that contains export markers or owns a configured Blueprint export, UHT pulls its active C++ translation units from the target's compilation database—adding a new module or `.cpp` file picks it up automatically on the next build. Run `ModHeaderTool discover <project.uproject>` at any time to inspect which modules are selected and whether they contain markers.

### Host Blueprint API exports

To export APIs authored in a Blueprint asset, add an entry to `blueprintExports` in `Config/ModHeaderTool.json` with:

- `asset`: the saved Blueprint object path
- `typeId`: a stable public identifier kept across releases
- `module`: an in-scope Runtime module that owns at least one UE reflected header (so UHT can attach generated C++ wrapper units to it)

See `BP_ModBlueprintAPIExportTest` in the sample project. Before publishing a new API, set an explicit `PublicName` in the **蓝图Mod导出** panel (existing declarations with blank names or versions fall back to their declaration name and `1.0.0`).

During generation, `ModExportBlueprints` compiles the saved asset, extracts the compiled Kismet `UFunction` signature, and hands the snapshot to MHT. Currently supported Blueprint instance APIs use `World`, `GameThread`, and `ReturnError`, taking pass-by-value boolean/numeric scalars and returning either one scalar or `void`. Configured Blueprint packages are automatically added to the host cook (`-MhtBootstrap` can prepare the Editor beforehand if needed).

At runtime, once the Blueprint instance exists in a World, the host calls `UModAPIHostSubsystem::RegisterWorldProvider` (holding onto the returned handle) and configures authorized mods, types, and permissions with `SetWorldCallerPolicy`. On the mod side, the generated `<SDK>API.h` exposes `Acquire_<object reference type>`, which a C++ mod calls with its active `UModInstance` to connect the function tables and obtain a typed provider handle (reusable for the lifetime of that World until the mod unloads, the World closes, or the host replaces its policy):

```cpp
FModObject_T_f4f57c0936062c84 Provider;
if (UUMFDevSDKLibrary::Acquire_FModObject_T_f4f57c0936062c84(*this, Provider))
{
	EUMFDevSDKCallOutcome Outcome;
	int64 ErrorCode;
	UUMFDevSDKLibrary::TestModAPIExport(Provider, 1.0, Outcome, ErrorCode);
}
```

Under the hood, the host dispatches the call to the live Blueprint object via `ProcessEvent`.

### Host Blueprint Hook and Event exports

After listing the saved Blueprint in `blueprintExports`, declare your Hook or Event in the **蓝图Mod导出** panel and wire the execution input pin on its **Hook Point** or **Publish Event** node inside a host graph—declarations without a connected execution pin are skipped during export.

- **Hooks:** The Hook's original-behavior graph accepts a payload and returns the same type, while the **Hook Point** node outputs the final value, status code, cancellation flag, and commit flag.
- **Events:** Support `Immediate` and `NextTick` delivery with optional immediate unsubscription.
- **Payloads:** May be supported scalars, reflected value structs, or `UObject` types that have a generated World view.

On the C++ side, place `MOD_EXPORT_HOOK()` or `MOD_EXPORT_EVENT(...)` on a member of a type with a configured World identity. An empty `MOD_EXPORT_HOOK()` automatically wraps the out-of-line `.cpp` definition when built through MBT, while C++ Events can either call generated helper functions directly or specify `HostEndpoint` to let MHT generate the member body (see [World extension artifacts](../../Tools/ModHeaderTool/README.md#world-extension-artifacts)).

### Automatic Hook builds

Run MBT on the machine executing UBT compile actions, with the target platform's UE SDK installed. Each `build-ue-target` run builds a single architecture (Apple target link verification also requires `python3` and Xcode's `nm`). For example, to build the Linux x64 Development Editor from the repository root:

```sh
mbt=UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool
"$mbt" build-ue-target --engine /opt/UE_5.6 --project UnrealModFramework/UnrealModFrameworkDev.uproject \
  --target UnrealModFrameworkDevEditor --platform Linux --configuration Development --architecture x64 --mode normal
```

Standalone CI fixtures ([Scripts/test_hook_targets.py](../../../../../Scripts/test_hook_targets.py)) exercise automatic Hook rewriting and linking across these UBT platform and architecture pairs (Win64 `arm64ec` additionally requires the matching Windows SDK runtime libraries):

| UBT platform | Tested architectures |
| --- | --- |
| `Win64` | `x64`, `arm64`, `arm64ec` |
| `Linux` | `x64` |
| `LinuxArm64` | `arm64` |
| `Mac`, `Android` | `x64`, `arm64` |
| `IOS`, `VisionOS` | `arm64`, `iossimulator` |
| `TVOS` | `arm64`, `tvossimulator` |

`UMF_MHT_LIBCLANG` and `UMF_MHT_CLANG_RESOURCE_DIR` select the build host's `libclang` and Clang resource headers (projects can also set a per-host library path via `hostLibclang`), while target SDK sysroots, include paths, and target triples come directly from UBT's compilation database (see [Linux build scope](../../../../../Docs/DevelopmentWorkflow.md#linux-build-scope)).

Under the hood, MHT and MST locate the out-of-line C++ definition in Clang's AST and emit a rewritten source copy. MBT swaps that copy into the matching UBT compile action (including nested response files) while keeping original object and binary output paths intact. Because generated registration code references a generation-stamped symbol defined only in the rewritten copy, COFF and ELF linkers automatically fail if an unpatched object file slips through; on Apple targets, MBT additionally inspects Mach-O symbol tables with `nm` to catch unresolved dynamic-lookup Hook imports. Both C++ callers and Blueprint thunks enter the same public function, as verified by `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` (whereas `-MhtBootstrap` Editor builds compile the original unhooked function directly).

## Host compilation and linking

For every reflected owner module listed in the v4 manifest, UHT registers generated `*.umfhost.cpp` wrapper units via `CompileOutput` so UBT compiles them with that module's exact build environment. Exporting modules simply declare a normal `ModAPI` dependency in `.Build.cs` (`ConfigureHost` and `ConfigureHostLibrary` remain as no-friction compatibility helpers for older build rules).

Meanwhile, MDT packages the separate `ClientSource` into the shared SDK Runtime module. On supported single-architecture desktop builds, MBT runs after pending link actions, verifies each owner binary's generation-stamped symbol against the UBT `.target` receipt and `.modules` manifests, and writes `HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>/host-link.json` (see [Final-link association](../../Tools/ModBuildTool/README.md#final-link-association) and [SDK installation](../../Tools/ModSDKTool/README.md#install-and-verify)).

## Build behavior and limits

- **Process management and diagnostics:** Native tool diagnostics forward straight into UHT logs. Nonzero exit codes, malformed outputs, or timeouts fail the UHT pass immediately, and UE's `ManagedProcessGroup` cleans up child processes on cancellation.
- **Skipped UHT modes:** Reference/no-output passes, `-MhtBootstrap` builds, and `-Mode=GenerateClangDatabase` invocations skip native MHT generation. Passing `-FailIfGeneratedCodeChanges` rejects `generate` mode (since MHT has no dry-run equivalent), while `scan` mode preserves UHT's standard change detection.
- **Incremental dependency tracking:** UHT registers tool binaries, config files, snapshots, and source inputs via `AddExternalDependency`, and each target variant writes `watch.json` pointing to its v4 manifest. Before compilation, MBT checks watched directories and tracked files, touching `.mht-watch.stamp` whenever UBT needs to invalidate its cached action graph.
- **Standalone C# build:** UBT automatically generates the gitignored `ModRuntime.ubtplugin.csproj.props` next to the project. To build the C# adapter manually, pass `EngineDir` explicitly:

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
