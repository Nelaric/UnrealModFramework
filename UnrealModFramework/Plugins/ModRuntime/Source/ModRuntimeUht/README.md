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

## Host generation

Use MBT to prepare metadata with the project's Development Editor target and to build each normal target with that bundle. UBT supplies the target and compilation actions. UHT supplies reflected names, owning modules and generated output locations. MHT discovers exports and their signature dependencies in the active Clang AST. Game targets consume the prepared Blueprint snapshot and perform their native analysis without launching Editor.

The project identity is the `.uproject` filename stem. A reflected type normally takes an ID of the form `ue.<module>.<type>`, derived from its UHT `/Script/<Module>.<Type>` path with ASCII case folding. A native type identity includes the owning module and qualified C++ name. A previously published class identity can be retained using `UCLASS(meta = (ModTypeId = "..."))`. The development host uses this for `game.character` and `game.local_player_api`. Type layout and ABI are derived from the selected compilation commands.

Declare scope on an exported member when its scope cannot be inferred from its class. A local-player subsystem implies `Player`. The generator requires compatible scope among the exports on one class. An optional `FailureReturn = "false"` on an API declares a sentinel when that value represents failure. Use `MOD_SOURCE_FAILURE(MOD_ERROR_NAME, Code, Value)` at a business-specific failure to publish an explicit code. The owning source file, signature and target commands come from the AST and build graph.

Use the structured error layout `(Domain << 28) | (Type << 16) | Detail` for fixed business codes. The Business domain is `3`. Keep the code and its symbol stable once published. MST rejects reuse of the same code under different symbols or different codes under the same symbol across the analyzed exports. The source report carries the observed compiler paths for failures and exceptions independently of the public error code.

Use `UMF_MHT_EXECUTABLE`, `UMF_MHT_LIBCLANG` and `UMF_MOD_SOURCE_TOOL_EXECUTABLE` to select a local tool install. The build inputs, reflection inventory, Blueprint snapshot and generation receipts are generated under `Intermediate/ModTools`. [Development workflow](../../../../../../Docs/DevelopmentWorkflow.md)

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

The Editor discovers saved declarations using Asset Registry tags and declaration dependencies. A Blueprint's default identity is derived from its persistent package GUID. The runtime owner comes from its project or plugin modules. Set an override in the Blueprint Mod Export panel to preserve a published type ID or select an ambiguous owner. Preserve declaration GUIDs during renames and moves.

During metadata preparation, the `ModExportBlueprints` commandlet compiles the saved asset and
passes the compiled `UFunction` signature to MHT. The supported instance API profile
is `World`, `GameThread`, and `ReturnError`, accepting value scalar inputs and
returning either a single scalar output or `void`. Configured packages are added to
the host cook in a full-engine release build.

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

[MBT build and automation commands](../../../../../Docs/DevelopmentWorkflow.md#build-locally)

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
database supplies the target SDK, include paths, and architecture. [UE integration builds and automation coverage](../../../../../Docs/DevelopmentWorkflow.md)

MHT and MST identify the annotated member in Clang's AST and write its source copy.
MBT substitutes that input in the matching UBT action (including nested response
files) while preserving the original object and link outputs. The generated
registration references a versioned symbol defined by the copy, so COFF and ELF links
reject an unpatched original object. On Apple targets, the post-build step also checks
Mach-O imports with Xcode `nm`, including links that use dynamic lookup. Both C++ and
the Blueprint thunk enter the same public function. When a bootstrap Editor is needed,
`prepare-metadata` invokes UBT with `-MhtBootstrap`, producing a Development Editor that calls
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
