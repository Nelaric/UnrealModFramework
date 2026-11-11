<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Modules.zh-CN.md)

# Modules and Dependency Boundaries

The repository contains a development host project and two framework plugins. Each module has one responsibility, declares only the dependencies it uses, and keeps implementation details in `Private`. Public headers are contracts: they must compile when included on their own, and their `.Build.cs` dependencies must be public only when those headers require them. Circular module dependencies are forbidden.

The UE project lives in `UnrealModFramework/`. Module and configuration paths in this document are relative to that project directory. The `ModRuntime` plugin contains `HAL`, `ExportMetadata`, `ModAPI`, `ModLoader`, `BlueprintIntegration`, and `ExportCodeGenerator`. The `ModDevelopmentSDKEditor` plugin contains `DevelopmentEditor`.

| Responsibility | Location | UE module type |
| --- | --- | --- |
| Native directory notifications | `Plugins/ModRuntime/Source/Runtime/Core/HAL/` | `Runtime` |
| Export metadata and configuration choices | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| Generated API results, scopes, bindings, permissions, and revocation | `Plugins/ModRuntime/Source/Runtime/ModAPI/` | `Runtime` |
| Mod package loader | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| Blueprint integration | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| Export code generation | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| Independent authoring tools | `SourcePlugins/ModDevelopmentSDKEditor/Source/DevelopmentEditor/` | `Editor` |
| Development host | `Source/ExportTest/` | `Runtime` |

The host project's `[CoreRedirects]` in `Config/DefaultEngine.ini` maps `/Script/ModBlueprintIntegration` to `/Script/BlueprintIntegration`, preserving references in existing Blueprint assets.

`ExportMetadata` must compile in Game and Editor targets. It cannot depend on editor modules, a particular host game's code, a content-distribution provider, or the authoring editor. Runtime modules are organized by responsibility: `ExportMetadata` owns the existing contract types and choices, while future services belong in modules named for their specific functions. The host game may depend on the ModRuntime plugin to provide explicit extension points.

`Runtime` and `Editor` group modules inside the single `ModRuntime` plugin; they are not UE modules. Declare each module in `ModRuntime.uplugin`. `ExportMetadata` publicly depends on `Core` for UE strings and configuration arrays.

`BlueprintIntegration` owns Blueprint Mod export declarations in the Editor target. It depends directly on `Core`, `CoreUObject`, `Engine`, `BlueprintGraph`, `GraphEditor`, `Kismet`, `KismetCompiler`, `ExportMetadata`, `Slate`, `SlateCore`, `ToolWidgets`, `UMGEditor`, and `UnrealEd`. The export panel and implementation graphs stay in this module because they need Blueprint editor and compiler modules that runtime contracts do not use. `GraphEditor` and `ToolWidgets` supply the native My Blueprint action list and Add button. The `UMGEditor` dependency keeps the tab registered across Widget Blueprint mode changes. Neither `ExportMetadata` nor the independent `DevelopmentEditor` depends on it. It loads after engine initialization so `Kismet` can register its editor services.

The module registers a `蓝图Mod导出` tab in Blueprint editors, next to My Blueprint where that tab exists. A fallback tab spawner covers the interface, macro library, Widget, and Animation Blueprint editor modes that use different layouts; creation is disabled for Blueprint types that cannot provide the required runtime implementation. The panel uses the same `SGraphActionMenu`, section styling, Add button, search box, and view-options controls as My Blueprint. API, Event, and Hook are collapsible sections with circular Add buttons. Rows use native palette icons, selection, and inline renaming; selecting a row shows its properties in the Blueprint editor's existing Details panel, and double-clicking opens its implementation graph. Rename and Delete are available in the context menu and through F2 and Delete. The export tab contains no separate property pane. Declarations are transactional objects inside a `UBlueprintExtension` on the Blueprint asset. API implementation graphs and Hook original-behavior graphs are owned by their respective declarations and are exposed to Blueprint graph discovery; the compiler creates executable function graphs from them. Event declarations contain a payload type and publication settings. The module does not register function, variable, or dispatcher detail customizations.

| Declaration | Stored fields | Editor graph |
| --- | --- | --- |
| API | Version, display name, description, scope, thread, failure rule, permissions | Implementation graph |
| Event | Version, display name, description, payload struct, source scope, publish rule | None |
| Hook | Version, display name, description, context struct, `AllowedStages`, `AllowedOperations` | Original-behavior graph |

Configured choices live in `[ModExportMetadata]` in `Config/DefaultGameMod.ini`. API choices use `APIScopes`, `APIThreads`, `APIPermissions`, and `APIFailureRules`. Event choices use `EventSourceScopes` and `EventPublishRules`. Add choices with `+Key=Value`. API permissions are stored as an array of identifiers.

Blueprint Mod exports are defined only by independent declarations created in this panel. API and Hook graphs belong exclusively to their declaration objects; Event declarations store their own payload and publication settings. The module contains no import, migration, ownership repair, or compatibility path for declarations backed by ordinary functions or Event Dispatchers. Unsupported declarations must be recreated. The export panel controls implementation-graph lifetime. Graph discovery, opening, and compilation validate ownership, so a missing or detached graph produces a diagnostic instead of being opened as an orphaned graph.

Declaration names are `ModAPI_<name>`, `ModEvent_<name>`, and `ModHook_<name>`. The model supplies one display name and tooltip for the export list, Details heading, native graph tabs, breadcrumbs, entry nodes, search results, and compiler diagnostics. Editing accepts the base name or the full name with its matching type prefix and stores the base name, so inline editing does not repeat the prefix. API and Hook graphs use a K2-derived schema for native graph displays, independent of their internal object names. Discovery and opening bind the schema only to independently owned implementation graphs; no source-graph renaming or legacy declaration conversion is needed. The separate entry-node drawing factory is removed. Compiled functions use the declaration's name as both their function name and display-name metadata, keeping call-node titles and action-menu labels consistent. The compiler applies that name to intermediate graphs, entry and result nodes, and local-variable scopes without renaming source graphs during compilation. Renaming updates graph names, local scopes, and loaded function references, including references to the declaration's compiled name when it differs from the source graph's internal name. The operations share the editor transaction. Empty, duplicate, overlong, and invalid names are rejected without GUIDs or generated numbers. The declaration GUID remains an internal identity hidden from Details.

The Blueprint extension contributes API, Event, and Hook names and tooltips to Find in Blueprints. Declaration creation, renaming, property edits, deletion, and panel undo/redo refresh the search index for saved assets. The panel search matches canonical and base names. Refreshing a selected row also refreshes its Details heading, including when the name was edited in Details. Source graphs and compiler copies share the same name-binding routine for entry and result function references, function-display metadata, and local-variable scopes. Native Find References therefore uses the declaration's function name as well.

`Mod::ExportMetadata::FOptions` reads the configured choices. MHT derives C++ API IDs from the type and public name. The export panel stores declarations and compiles API and Hook graphs, but it does not yet create Publish Event or Hook Point nodes or enforce runtime contracts. Before export, the generator must validate IDs, complete selections, and host policy semantics. Runtime code consumes generated descriptors.

`ModAPI` is a Runtime module for Game and Editor targets, publicly depending on `Core`, `CoreUObject`, and `Engine`. It owns generated API results, bindings, World/Player scopes, explicit providers, permissions, and revocation. `ExportMetadata` owns declaration metadata and configuration. `ExportTest` declares its character and LocalPlayer exports. MHT generates their registration and provider metadata, and `UModAPIHostSubsystem` manages the registry and player bindings per game instance.

`DevelopmentEditor` is built independently with UAT `BuildPlugin`. It must not statically depend on the project-side `ExportMetadata` module or a host game's modules. Shared package schemas and SDK contracts belong in versioned `Shared/ModProtocol` definitions when introduced; both plugins compile against those definitions separately. Runtime preview across processes uses a versioned bridge or IPC rather than a UBT dependency on the game project.

`ExportCodeGenerator` is an Editor module with a public `ExportMetadata` dependency and private `Core`, `CoreUObject`, `Engine`, `Json`, and `Projects` dependencies. Its `ModExportOptions` commandlet exports resolved GameMod choices and UE function redirects. The shared annotation macros are defined in `ExportMetadata/Public/ModExportMacros.h`. Standalone MHT retains lexical scan/metadata validation and adds generation from active API declarations through a real UBT compilation database and libclang AST. The first synchronous UObject backend generates closed SDK views, concrete host bridges, compile verification, contracts, and manifests for GameThread/ReturnError APIs with scalar value arguments and scalar/void/World-object results. Other forms and Event/Hook generation fail explicitly. MHT has no `BlueprintIntegration` dependency.

Keep native mod ABI types separate from UE-facing public APIs. UE-facing C++ contracts use UE types. The external C ABI uses fixed-width fields, explicit lengths, opaque handles, and documented ownership because UE object and C++ ABI layouts are not portable across independently built binaries.

New modules must document their responsibility, direct dependencies, supported targets, and why an existing module cannot own the capability. Add optional integrations as separate modules or plugins. Do not introduce a dependency merely to make a missing include compile.

## Mod loader

`ModLoader` discovers, validates, extracts, and mounts v1 `.mod` content and forwards results to game sessions. Its public dependencies are `Core`, `CoreUObject`, and `Engine`. Its private dependencies are `Json`, `PakFile`, `AssetRegistry`, `RenderCore`, and the engine's OpenSSL library. Editor targets also depend privately on `UnrealEd` for PIE lifetime and package release. The plugin descriptor enables the module for Win64, Linux, and Mac Game and Editor targets. Mounting requires a matching Pak or IoStore backend. The loader is independent of API binding, export metadata, and the authoring editor.

After engine initialization, game-thread code can call `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` or `LoadDirectory(Directory, Policy)`. C++ consumers declare a `ModLoader` dependency in their module rules. These calls prepare resources synchronously and belong before gameplay. Successful results list the mounted long package names for asset loading.

Host C++ can subscribe through `OnLoadCompleted()`. Each startup or manual operation broadcasts its origin, input path, and `FLoadResult` entries. An empty scan has no entries. A scan-level failure returns one entry with `Error` and `Detail`. Callbacks run on the game thread. Reentrant and wrong-thread completions are deferred.

Packaged games scan top-level `.mod` files in the `Mods` directory beside the running game executable by default. For a standard Win64 package, use `<Project>/Binaries/Win64/Mods`. Configure the host release in `Config/DefaultGame.ini`. Set `bLoadAtStartup=False` in `[ModLoader]` to disable the scan.

The runtime `HAL` module provides native directory notifications on Win64, Linux, and Mac. With startup loading enabled, the loader watches the same directory by default. Set `bWatchForChanges=False` to use startup and manual loading only. The watch waits for file metadata to settle, prepares new archives on a worker, and commits them on the game thread. An archive declaring an already mounted ModId is skipped without a load notification.

```ini
[ModLoader]
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<SHA-256 of the base release AssetRegistry.bin>
Backend=IoStore
PreparationTimeoutSeconds=120
```

Use the game and base release values supplied to the authoring tool, and select the backend used by the game release. The loader verifies the archive format, target platform, UE build, cook receipt, and payload digests.

The package allowlist is disabled by default. To enable it, add one `+TrustedPackageSha256=<digest>` per approved `.mod` using the `sha256` field in its build result. An explicit `bEnforcePackageAllowlist=False` bypasses a populated list. Setting it to `True` requires a match even when the list is empty. Manual loads use the corresponding `FLoadPolicy` fields.

Payloads live in `Saved/ModCache/<archive SHA-256>` and are rechecked before reuse. A mount retains its cooked package root and admits one version of a mod ID at a time. Moving or deleting its source archive from the watched directory starts an unload while the cached content is still available. The loader releases the containers, shader library, and package root once the packages are safe to release. If references remain, it logs the retained package. This version accepts unencrypted content in its own package root and can append its cooked asset registry when present.

On Windows, PIE uses the host project's `Binaries/Win64/Mods` directory and the same `[ModLoader]` settings. Enable **Allow Cooked Content in the Editor** under **Project Settings → Engine → Cooker**. With `Backend=Pak`, ModLoader initializes the Pak platform file when the editor starts. For `Backend=IoStore`, launch with `-UsePaks -UseIoStore`. For unversioned cooked assets, set `s.AllowUnversionedContentInEditor=1` in the `[ConsoleVariables]` section of `Config/DefaultEngine.ini`.

PIE worlds in the same process share the mounted packages and own separate Mod instances. Each instance receives the startup results when its PIE map is ready. At the end of PIE, the loader stops delivering session results, releases loaded packages after world teardown, then unmounts the containers and unregisters their package roots. If an editor object still references a package, the loader reports it and retains the mount until cleanup can succeed.

`ModSDKTool` (MDT) generates `ModInstance` and `ModInstanceSubSystem` from the host's lifecycle export and preserves their `/Script/ModLoader` paths. Packaging automatically collects concrete Blueprint subclasses from the content plugin. After the load batch finishes, the game session creates entries for successful mounts and delivers each result to active entries. C++ subclasses override `OnModLoadCompleted` and call `Super::OnModLoadCompleted` to invoke `ReceiveModLoadCompleted`, displayed as **On Mod Load Completed** in Blueprint. Both entry points receive the same result, including the error code and reason for failures.

**On Mod Before Unload** runs once for each instance of the departing mod, on the game thread while its content remains mounted. C++ subclasses override `OnModBeforeUnload` and call `Super::OnModBeforeUnload` to dispatch the Blueprint event. Use this callback to restore gameplay state and release references to mod assets. Host C++ can subscribe to `IModLoaderModule::OnBeforeUnload()` for the mounted package record.

## Framework-only integration

Consumer-facing methods belong in the first `public:` section. Methods used only for framework integration or Unreal lifecycle may occupy a second `public:` section, as specified in [Public API](API.md). An internal access key, if introduced, is an integration convention rather than an authorization boundary. Do not expose a private implementation header through a public header or use a non-reflected key as a `UFUNCTION` parameter. Cross-module access needs an explicit documented contract.
