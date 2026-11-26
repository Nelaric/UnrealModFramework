<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModRuntime UHT adapter

`ModRuntime.ubtplugin.csproj` is a UHT exporter that inherits the engine's target
framework (.NET 8 on UE 5.6 and .NET 10 on UE 5.8). UBT discovers the project
inside the enabled ModRuntime plugin, builds the assembly, and loads its
`[UnrealHeaderTool]` class and `[UhtExporter]` entry point. It references assemblies
from the active engine directly, requiring no project references or engine changes.
The exporter is named `ModRuntime`, and the reflected runtime module `ModLoader`
enables it in both Game and Editor manifests.

The adapter prepares the current UBT target's analysis inputs and runs MHT. MHT owns C++ projection and generation. ExportCodeGenerator handles Blueprint exports. UBT compiles and links the generated host units.

## Native tool and default invocation

Targets configured with `ExportMetadata.ConfigureTarget(this)` build ModHeaderTool,
ModBuildTool, and ModSourceTool before UHT runs. When invoking UHT directly, prepare
the tools first with the native build script. UHT searches for MHT in the following
order:

1. The `UMF_MHT_EXECUTABLE` environment variable.
2. `executable` in the project's optional `Config/ModHeaderTool.json`.
3. `<plugin>/Binaries/ThirdParty/ModHeaderTool/<host>/MHT[.exe]` or
   `ModHeaderTool[.exe]`, where `<host>` is `Win64`, `Linux`, or `Mac`.
4. The documented local build at
   `<plugin>/Tools/build/ModHeaderTool/ModHeaderTool[.exe]`.

Without explicit configuration, the default mode is `scan`. The development project's
`Config/ModHeaderTool.json` selects `generate`. In `scan` mode, a standard UHT
invocation runs the following command:

```text
ModHeaderTool scan <actual.uproject> <metadata-options.json>
```

The authoritative metadata catalog defaults to
`<project>/Intermediate/ModTools/ModHeaderTool/MetadataOptions.json`. Before
generation, the adapter checks this snapshot with MHT. If the snapshot is missing,
stale, or uses an older version, the adapter exports a fresh one with the active
engine's `ModExportOptions` commandlet and publishes it after validation. Changes to
tracked INI files such as `DefaultEngine.ini` trigger this check on the next
incremental build. `scan` mode refreshes an existing default snapshot and can still
scan a project with no snapshot when none of its declarations require one. Any
`options` override pointing elsewhere is managed by the caller.

On the build host, `metadataEditorTarget` names the Development Editor target that UBT
builds in bootstrap mode when the commandlet needs project binaries. Projects that
omit this setting can prepare the Editor with `-MhtBootstrap`.

UHT reads the JSON report from stdout, and its output factory commits successful
reports to
`Intermediate/ModTools/ModHeaderTool/Scan/<Target>/<Platform>/<Configuration>/Scan.json`
under the project. `scan` mode validates marker syntax and parameters.

## Complete generation

Set `mode` to `generate` and configure the game ID and type identities in `generation`. UBT records the
actual platform, configuration, and architecture in the UHT manifest. The adapter
expands the target compiler's response files before the regular action graph is
written, and each variant stores its `BuildInput.json` and pinned
`Ubt/compile_commands.json` in `HostTargets`. MHT validates the target, module,
configuration, and CPU architecture of each parsed translation unit.

Each target emits `UMF.GeneratedArtifacts` v4 with a role and owner for every compilation unit. UHT selects the host and verification units for UBT.

The project generation root is
`Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>`.
Each run replaces that variant's `SDK`, `Host`, `Client`, `Contract`, `Diagnostics`,
and `<gameId>.generated-artifacts.json` at fixed paths. The adapter verifies
completeness, target identity, and the hash of every generated file. Removed outputs
are cleaned up, while identical files retain their timestamps. The complete manifest
is committed after all file replacements succeed, and `generationId` remains a
content-based identity.

The development host uses this configuration with tools and libclang discovered
automatically from Setup. [Checked-in configuration](../../../../Config/ModHeaderTool.json)

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

Paths in this adapter configuration are resolved relative to the project directory,
while paths inside a build input retain MHT's own resolution rules. Both `options` and
`timeoutSeconds` (1 to 86400, default 600) are optional. Automatic preparation uses the
host `libclang` and matching resource headers. With the default configuration, the
adapter reads `Tools/build/libclang-<system>-<arch>.path` relative to `Tools/build/`
and falls back to a host LLVM installation. The same relative path file locates
`libclang` inside a `Dist/ModRuntime` export; an explicit path in the environment or
project configuration takes precedence.

MBT discovery checks `UMF_MBT_EXECUTABLE`, the root `mbt` setting, and the local
`Tools/build/ModBuildTool/ModBuildTool[.exe]` output under the ModRuntime plugin, in
that order. ModSourceTool (MST) discovery checks `UMF_MOD_SOURCE_TOOL_EXECUTABLE`, the
root `sourceTool` setting, and `Tools/build/ModSourceTool/ModSourceTool[.exe]` under
the ModRuntime plugin, in that order.

`UMF_MHT_BUILD_INPUT` selects `generate` mode and overrides the target configuration,
while `UMF_MHT_LIBCLANG` overrides the configured library path. Changing environment
settings requires a new UHT invocation (for example, by passing
`-ForceHeaderGeneration` to UBT).

A target- or variant-level `buildInput` selects a caller-managed input whose
compilation database and response files must match the active target. Clang also
requires the headers produced by standard UHT CodeGen.

### Module selection

Place an optional `<project>.modexport` next to the `.uproject`. With the file absent or empty (`{}`), MHT discovers modules recursively under the project's `Source` using their `*.Build.cs` rules. This example includes the Combat and Audio plugins, excludes GameEditor globally, and excludes CombatEditor within Combat.

```json
{
  "ExcludeModules": ["GameEditor"],
  "Plugins": [
    { "Name": "Combat", "ExcludeModules": ["CombatEditor"] },
    "Audio"
  ]
}
```

A string in `Plugins` can name a plugin under the project's `Plugins` tree or give a path to a `.uplugin` descriptor. To set exclusions for one plugin, use an object with `Name` for a plugin name or `Path` for its descriptor. Relative paths start at the project directory.

Top-level `ExcludeModules` applies to the project and every selected plugin. An `ExcludeModules` list inside a plugin entry applies only to that plugin. Both lists match UE module names case-insensitively. When multiple entries resolve to the same plugin, their local exclusions are combined. UE determines which plugins participate in the target build.

The standalone scan and automatic generation share this selection. For a module with export markers or configured Blueprint exports, UHT takes its C++ translation units from the active target's compilation database. Adding a module or a `.cpp` to the target makes it discoverable on the next build. `ModHeaderTool discover <project.uproject>` prints the selected modules and whether they contain markers. Caller-managed `buildInput` files continue to specify their exact inputs.

### Host Blueprint API exports

Configure `blueprintExports` in the project's `Config/ModHeaderTool.json`. Each entry
supplies the saved Blueprint object path as `asset`, a stable `typeId`, and a
Runtime `module` within the selected scope. UBT attaches the generated compilation units through UHT, so choose an owner with a UE reflection header. The development project includes an example entry for
`BP_ModBlueprintAPIExportTest`. Set `PublicName` on the Blueprint API declaration
before publishing; existing declarations with empty names or versions default to their
current declaration name and `1.0.0`.

During generation, the `ModExportBlueprints` commandlet compiles the saved asset and
passes the compiled `UFunction` signature to MHT. The supported instance API profile
is `World`, `GameThread`, and `ReturnError`, accepting value scalar inputs and
returning either a single scalar output or `void`. Configured packages are added to
the host cook. Use `-MhtBootstrap` to prepare the Editor when needed.

Once its object is ready, the game calls `UModAPIHostSubsystem::RegisterWorldProvider`
and retains the registration handle. It then uses `SetWorldCallerPolicy` to specify
authorized mods, types, and permissions. The generated `<SDK>API.h` provides
`Acquire_<object reference type>`. A native mod calls it with its active `UModInstance`
to connect the generated function tables and acquire a reference for typed SDK method
calls. The host invokes the underlying Blueprint instance via `ProcessEvent`. Mod
unload, world closure, or policy replacement revokes the corresponding callers.

### Host Blueprint Hook and Event exports

Add the saved asset to `blueprintExports`. Declare the Hook or Event in **蓝图Mod导出**,
then connect the execution input on the corresponding **Hook Point** or
**Publish Event** node; unconnected declarations are skipped. A Hook's
original-behavior graph takes and returns the same payload type, and the node exposes
the result, status, cancellation, and commit state. Events support `Immediate` and
`NextTick` delivery with optional immediate unsubscription. Payloads may use supported
scalars, reflected value structs, or UObjects with a generated World view.

In C++, associate the export macro with a member signature on a type that has a World
identity. An empty `MOD_EXPORT_HOOK()` wraps the original out-of-line definition when
MBT builds the target. C++ Events can call the generated publisher directly or use
`HostEndpoint` to emit a publishing member definition. See
[Generated contracts and payload limits](../../Tools/ModHeaderTool/README.md#world-extension-artifacts).

### Automatic Hook builds

Run MBT on the host that executes the UBT compile actions, with the UE SDK for the
selected target installed. Each invocation builds one architecture. Apple target
link checks use `python3` and Xcode's `nm`. This example builds the Linux Development
Editor from the repository root.

```sh
mbt=UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool
"$mbt" build-ue-target --engine /opt/UE_5.6 --project UnrealModFramework/UnrealModFrameworkDev.uproject \
  --target UnrealModFrameworkDevEditor --platform Linux --configuration Development --architecture x64 --mode normal
```

The standalone Hook fixtures exercise these UBT platform and architecture identifiers.
The Win64 `arm64ec` fixture also requires the matching Windows SDK runtime library.
[Hook target fixtures](../../../../../Scripts/test_hook_targets.py)

| UBT platform | Architecture |
| --- | --- |
| `Win64` | `x64`, `arm64`, `arm64ec` |
| `Linux` | `x64` |
| `LinuxArm64` | `arm64` |
| `Mac`, `Android` | `x64`, `arm64` |
| `IOS`, `VisionOS` | `arm64`, `iossimulator` |
| `TVOS` | `arm64`, `tvossimulator` |

`UMF_MHT_LIBCLANG` selects the build host's `libclang`, and
`UMF_MHT_CLANG_RESOURCE_DIR` selects its matching resource directory. A project can
also configure a host-specific library path via `hostLibclang`. The compilation
database supplies the target SDK, include paths, and architecture. [UE integration builds and automation coverage](../../../../../Docs/DevelopmentWorkflow.md#linux-build-scope)

MHT and MST identify the annotated member in Clang's AST and write its source copy.
MBT substitutes that input in the matching UBT action (including nested response
files) while preserving the original object and link outputs. The generated
registration references a versioned symbol defined by the copy, so COFF and ELF links
reject an unpatched original object. On Apple targets, the post-build step also checks
Mach-O imports with Xcode `nm`, including links that use dynamic lookup. Both C++ and
the Blueprint thunk enter the same public function. When a bootstrap Editor is needed,
the tools invoke UBT with `-MhtBootstrap`, producing a Development Editor that calls
the original function directly. The `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`
test exercises both entry points.

UBT compiles all generated host and verification units in their owning modules.

## Host compilation and linking

UHT registers host and verification units in each owner through `CompileOutput`. MDT places `ClientSource` in the shared SDK Runtime. MBT drives Hook source replacement, refreshes generation watches, and checks the generation-specific symbol in the final image identified by the UBT receipt and module manifest. Each target records its proof in `HostTargets/.../host-link.json`.

## Build behavior and limits

Native diagnostics are forwarded to UHT. Nonzero exit codes, invalid results, and
timeouts fail UHT, and UE's managed process group owns child process lifetimes. With
UBT host compilation, the adapter uses `CompileOutput` to register `*.umfhost.cpp`
wrapper units in the owning module. UBT compiles the host units and MHT emits the
standalone client source as a separate artifact.

UHT no-output/reference, bootstrap, and `-Mode=GenerateClangDatabase` invocations skip
native generation. Normal builds run MHT after preparation finishes. Complete
generation is rejected under `-FailIfGeneratedCodeChanges` because MHT has no matching
dry-run contract, whereas `scan` outputs retain UHT's normal change-checking behavior.

UHT registers tool, configuration, metadata and source inputs as external dependencies. MBT refreshes the watch stamp when the set of inputs changes.

UBT generates the adjacent `.ubtplugin.csproj.props` file containing `EngineDir`
(ignored by Git). For a direct managed build:

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
