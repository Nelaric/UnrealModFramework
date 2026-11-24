<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Modules.zh-CN.md)

# Modules and Dependency Boundaries

The repository contains the development host and the game-side `ModRuntime` plugin; MDT generates and builds the independent authoring plugin from embedded source resources. Each module has a single responsibility, declares only the dependencies it uses, and keeps implementation details in `Private`. Public headers are contracts: they must compile when included on their own, and `.Build.cs` dependencies must be declared public only when those headers require them. Circular module dependencies are forbidden.

The UE project lives in `UnrealModFramework/`. All module and configuration paths in this document are relative to that project directory. The `ModRuntime` plugin contains `HAL`, `ExportMetadata`, `ModAPI`, `ModLoader`, `BlueprintIntegration`, and `ExportCodeGenerator`. The `ModDevelopmentSDKEditor` plugin contains `DevelopmentEditor`.

| Responsibility | Location | UE module type |
| --- | --- | --- |
| Native directory notifications | `Plugins/ModRuntime/Source/Runtime/Core/HAL/` | `Runtime` |
| Export metadata and configuration choices | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| Generated API bindings, World extension dispatch, content, Patch, services, and compatibility hooks | `Plugins/ModRuntime/Source/Runtime/ModAPI/` | `Runtime` |
| Mod package loader | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| Blueprint integration | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| Export code generation | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| Independent authoring tools | `Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/Source/DevelopmentEditor/` | `Editor` |
| Development host | `Source/ExportTest/` | `Runtime` |

The host project's `[CoreRedirects]` in `Config/DefaultEngine.ini` maps `/Script/ModBlueprintIntegration` to `/Script/BlueprintIntegration`, preserving references in existing Blueprint assets.

`ExportMetadata` must compile in both Game and Editor targets. It must not depend on editor modules, any particular host game's code, a content-distribution provider, or the authoring editor. Runtime modules are organized by responsibility: `ExportMetadata` owns the existing contract types and configuration options, while future services belong in modules named after their specific functions. The host game may depend on the `ModRuntime` plugin to expose explicit extension points.

`Runtime` and `Editor` are directory groupings inside the single `ModRuntime` plugin rather than UE modules themselves. Declare each module in `ModRuntime.uplugin`. `ExportMetadata` publicly depends on `Core` for UE strings and configuration arrays.

`BlueprintIntegration` owns Blueprint Mod export declarations in the Editor target. It depends directly on `Core`, `CoreUObject`, `Engine`, `BlueprintGraph`, `GraphEditor`, `Kismet`, `KismetCompiler`, `ExportMetadata`, `ModAPI`, `Slate`, `SlateCore`, `ToolWidgets`, `UMGEditor`, and `UnrealEd`. The export panel and implementation graphs remain in this module because they require Blueprint editor and compiler modules that runtime contracts do not use. `GraphEditor` and `ToolWidgets` supply the native My Blueprint action list and Add button, while `UMGEditor` keeps the tab registered across Widget Blueprint mode switches. Neither `ExportMetadata` nor the independent `DevelopmentEditor` depends on `BlueprintIntegration`. The module loads after engine initialization so `Kismet` can register its editor services.

The module registers a `蓝图Mod导出` tab in Blueprint editors, docked next to My Blueprint wherever that tab exists. A fallback tab spawner covers interface, macro library, Widget, and Animation Blueprint editor modes that use different layouts; creation is disabled for Blueprint types that cannot provide the required runtime implementation. The panel reuses the same `SGraphActionMenu`, section styling, Add button, search box, and view-options controls as My Blueprint. API, Event, and Hook appear as collapsible sections with circular Add buttons. Rows use native palette icons, selection styling, and inline renaming; selecting a row shows its properties in the Blueprint editor's existing Details panel, and double-clicking opens its implementation graph. Rename and Delete are available in the context menu as well as via F2 and Delete. The export tab does not include a separate property pane. Declarations are transactional objects stored inside a `UBlueprintExtension` on the Blueprint asset. API implementation graphs and Hook original-behavior graphs are owned by their respective declarations and exposed to Blueprint graph discovery; the compiler produces executable function graphs from them. Event declarations store a payload type and publication settings. The module does not register function, variable, or dispatcher detail customizations.

| Declaration | Stored fields | Editor graph |
| --- | --- | --- |
| API | Version, display and public names, description, scope, thread, failure rule, permissions | Implementation graph |
| Event | Version, display name, description, payload, source scope, publication and unsubscription policy, permission, authority | `Publish Event` node in the host's graph |
| Hook | Version, display name, description, payload, `AllowedStages`, `AllowedOperations`, permission, authority | Original-behavior graph and `Hook Point` node |

Configured choices live in the `[ModExportMetadata]` section of `Config/DefaultGameMod.ini`. API choices use `APIScopes`, `APIThreads`, `APIPermissions`, and `APIFailureRules`; Event choices use `EventSourceScopes` and `EventPublishRules`. Add entries with `+Key=Value`. API permissions are stored as an array of identifiers.

Blueprint Mod exports are defined solely by independent declarations created in this panel. API and Hook graphs belong exclusively to their declaration objects, and Event declarations store their own payload and publication settings. The module contains no import, migration, ownership-repair, or compatibility path for declarations backed by ordinary functions or Event Dispatchers; unsupported declarations must be recreated. The export panel manages implementation-graph lifetime. Graph discovery, opening, and compilation validate ownership so that a missing or detached graph produces a diagnostic instead of opening as an orphaned graph.

Declaration names follow the forms `ModAPI_<name>`, `ModEvent_<name>`, and `ModHook_<name>`. The model provides a single display name and tooltip shared across the export list, Details heading, native graph tabs, breadcrumbs, entry nodes, search results, and compiler diagnostics. Editing accepts either the base name or the full name with its matching type prefix and stores only the base name, preventing inline edits from duplicating the prefix. API and Hook graphs use a K2-derived schema for native graph displays, independent of their internal object names. Discovery and opening bind this schema only to independently owned implementation graphs, avoiding source-graph renaming or legacy declaration conversion; the separate entry-node drawing factory has been removed. Compiled functions use the declaration's name as both their function name and display-name metadata, keeping call-node titles and action-menu labels consistent. The compiler applies that name to intermediate graphs, entry and result nodes, and local-variable scopes without renaming source graphs during compilation. Renaming updates graph names, local scopes, and loaded function references—including references to the declaration's compiled name when it differs from the source graph's internal name—within a single editor transaction. Empty, duplicate, overlong, and invalid names are rejected without falling back to GUIDs or generated numbers. The declaration GUID remains an internal identity hidden from Details.

The Blueprint extension contributes API, Event, and Hook names and tooltips to Find in Blueprints. Creating, renaming, editing, or deleting a declaration, as well as panel undo/redo, refreshes the search index for saved assets. Panel search matches both canonical and base names. Refreshing a selected row also updates its Details heading, including after edits made directly in Details. Source graphs and compiler copies share the same name-binding routine for entry and result function references, function-display metadata, and local-variable scopes, so native Find References uses the declaration's function name as well.

`Mod::ExportMetadata::FOptions` reads the configured choices. The host explicitly places each declaration's **Publish Event** or **Hook Point** node in a Blueprint execution graph; the compiler resolves the declaration by GUID and expands the node into a runtime call. `ModExportBlueprints` compiles the configured saved assets and includes Hook and Event declarations that have a connected extension node. MHT consumes the resulting snapshot and generates the descriptors and SDK projection.

`ModAPI` is a Runtime module for Game and Editor targets. Its public dependencies are `Core`, `CoreUObject`, `Engine`, and `ModLoader`, and its private dependency is `Json`. `UModAPIHostSubsystem` manages providers, permissions, and generated API bindings per GameInstance. `UModExtensionSubsystem` prepares and stops extensions per World, coordinating Hook/Event snapshots, content slots, Patch anchors, service leases, and the gameplay digest. The host owns the integration in `ExportTest`, including gameplay admission and save handling.

MDT writes the embedded `DevelopmentEditor` source into a temporary workspace and builds the authoring plugin with UAT `BuildPlugin`. The authoring plugin builds independently of the game-side modules; versioned JSON contracts and generated public headers carry the SDK and package interfaces across that boundary. Cross-process preview remains a design item.

`ExportCodeGenerator` is an Editor module that publicly depends on `ExportMetadata` and privately depends on `Core`, `CoreUObject`, `Engine`, `Json`, `Projects`, `AssetRegistry`, `BlueprintIntegration`, `ModAPI`, and `UnrealEd`. `ModExportOptions` exports resolved GameMod options and UE redirects, while `ModExportBlueprints` exports the configured Blueprint declarations. MHT resolves active C++ declarations from a target's libclang AST, consumes the Blueprint snapshot, and generates host bridges, public contracts, ABI declarations, and verification code. Supported profiles include synchronous UObject APIs and World Hook/Event extensions with supported scalar, reflected value, or projected object payloads.

Keep native mod ABI types separate from UE-facing public APIs. UE-facing C++ contracts use UE types, whereas the external C ABI uses fixed-width fields, explicit lengths, opaque handles, and documented ownership because UE object and C++ ABI layouts are not portable across independently built binaries.

New modules must document their responsibility, direct dependencies, supported targets, and why an existing module cannot own the capability. Add optional integrations as separate modules or plugins. Do not add a dependency merely to make a missing include compile.

## Mod loader

`ModLoader` discovers, validates, extracts, and mounts `.mod` archives and forwards the results to game sessions. It supports archive v1 for cooked content, v2 for native UE modules, and v3 for an extension manifest with optional native code. Its public dependencies are `Core`, `CoreUObject`, and `Engine`, and its private dependencies are `Json`, `PakFile`, `AssetRegistry`, `HAL`, `RenderCore`, and the engine's OpenSSL library; Editor targets also depend privately on `UnrealEd` for PIE lifetime tracking and package release. The runtime module follows the UBT target so generated Hooks can register on both desktop and mobile hosts. Mounting requires a matching Pak or IoStore backend along with matching game, base release, engine, and platform identities. Win64 x64 Pak hosts explicitly opt in to UE module loading with `bAllowNativeModules=True` and to independent extension DLL loading with `bAllowNativeExtensions=True`. `ModLoader` owns process admission and package leases; `ModAPI` installs the generated extension contract validator and owns World callbacks and bindings.

After engine initialization, game-thread code can call `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` or `LoadDirectory(Directory, Policy)`. C++ consumers declare a `ModLoader` dependency in their module rules. These calls prepare resources synchronously and should run before gameplay begins. Successful results list the mounted long package names for asset loading.

Host C++ can subscribe via `OnLoadCompleted()`. Each startup or manual operation broadcasts its origin, input path, and `FLoadResult` entries. An empty scan yields no entries, while a scan-level failure returns a single entry containing `Error` and `Detail`. Callbacks run on the game thread; reentrant and wrong-thread completions are deferred.

Packaged games scan top-level `.mod` files in the `Mods` directory beside the running game executable by default; for a standard Win64 package, this is `<Project>/Binaries/Win64/Mods`. Configure the host release in `Config/DefaultGame.ini`, and set `bLoadAtStartup=False` in `[ModLoader]` to disable the startup scan.

The runtime `HAL` module provides native directory notifications on Win64, Linux, and Mac. When startup loading is enabled, the loader watches the same directory by default; set `bWatchForChanges=False` to use startup and manual loading only. The watcher waits for file metadata to settle, prepares new archives on a worker thread, and commits them on the game thread. An archive declaring an already-mounted ModId is skipped without emitting a load notification.

```ini
[ModLoader]
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<SHA-256 of the base release AssetRegistry.bin>
Backend=IoStore
PreparationTimeoutSeconds=120
```

Use the game and base release values supplied to the authoring tool, and select the container backend used by the game release. The loader verifies the archive format, target platform, UE build, cook receipt, and payload digests.

With no configured digests and no explicit override, the allowlist is disabled. To enable it, add one `+TrustedPackageSha256=<digest>` entry per approved `.mod` using the `sha256` field from its build result. An explicit `bEnforcePackageAllowlist=False` bypasses a populated list, whereas setting it to `True` requires a match even when the list is empty. Manual loads use the corresponding `FLoadPolicy` fields.

Process admission also accepts `+EnabledMods=<ModId>`, `+RequiredMods=<ModId>`, `+PinnedArchives=<ModId>=<archive SHA-256>`, and `+ExtensionGrants=<ModId>=<permission ID>` in the same section. Set `bEnableAllMods=False` with an empty enable list to select an empty mod set. The host can supply a complete World grant set for a mod in `FWorldPolicy::Grants`; otherwise, the World inherits the admission grants. The first prepared package selection and grants carry across Worlds within the same GameInstance.

Use `UModExtensionSubsystem` to declare content slots and Patch anchors before calling `Prepare`. Set `FPatchAnchorRules::Attachment` or `DataAssetType` to enable component attachments or data registrations. The host can query `ResolvePatchData` with an anchor and data ID, and `GetPatchOrigin` with an Actor or component. `SetFailureHandler` receives the reason a World closed. `GetGameplayDigest` reports the initial ready plan's fingerprint, while `ExpectedGameplayDigest` checks against an authoritative value before opening gameplay. `GetPackageRevisions` returns the exact admitted archive digests.

Multiplayer verifies the game, base release, generated contracts, and admitted Mod releases during `PreLogin`. The server sends its map and plan digest before UE Welcome; the client checks the map before traveling and confirms the local plan before Join, and the server requires that confirmation on the joining connection. If target base registries differ, set `NetworkBaseCompatibilitySha256` to the same approved base compatibility digest on every target. To publish equivalent Client and Server Mod artifacts under a shared release identity, add one `+GameplayReleases=<archive SHA-256>=<ReleaseId>=<gameplay compatibility SHA-256>` entry for each exact artifact; each target still verifies its own archive. The sample `ModExtensions` mode uses reconnecting travel.

`FModExtensionSave` validates save requirements during World preparation. The host supplies schema migrations, logical content, and persistent object resolution, while missing optional Mod blocks are carried forward. In standalone `ModExtensions`, `ModWriteSave <slot>` and `ModLoadSave <slot>` operate under `Saved/ModSaves`; writing publishes to a new slot, and migration preserves the original file.

Payloads are extracted to `Saved/ModCache/<archive SHA-256>` and revalidated before reuse. Each mount retains its cooked package root and admits only one version of a given ModId at a time. Moving or deleting the source archive from the watched directory starts an unload while the cached content remains available. Once packages are safe to release, the loader unmounts the containers, releases the shader library, and unregisters the package root; if references remain, it logs the retained package. This version accepts unencrypted content under its own package root and appends its cooked asset registry when present.

On Windows, PIE uses the host project's `Binaries/Win64/Mods` directory and the same `[ModLoader]` settings. Enable **Allow Cooked Content in the Editor** under **Project Settings → Engine → Cooker**. With `Backend=Pak`, `ModLoader` initializes the Pak platform file when the editor starts; with `Backend=IoStore`, launch the editor with `-UsePaks -UseIoStore`. For unversioned cooked assets, set `s.AllowUnversionedContentInEditor=1` in the `[ConsoleVariables]` section of `Config/DefaultEngine.ini`.

PIE worlds in the same process share mounted packages while owning separate Mod instances. Each instance receives the startup load results once its PIE map is ready. When PIE ends, the loader stops delivering session results, releases loaded packages after world teardown, and then unmounts the containers and unregisters their package roots. If an editor object still references a package, the loader reports it and retains the mount until cleanup can succeed.

`ModSDKTool` (MDT) generates `ModInstance` and `ModInstanceSubSystem` from the host's lifecycle export while preserving their `/Script/ModLoader` paths. Packaging automatically collects concrete Blueprint subclasses from the content plugin. After a load batch completes, the game session creates entries for successful mounts and delivers each result to active entries. C++ subclasses override `OnModLoadCompleted` and call `Super::OnModLoadCompleted` to invoke `ReceiveModLoadCompleted` (displayed as **On Mod Load Completed** in Blueprint). Both entry points receive the same result, including the error code and reason for failures.

**On Mod Before Unload** runs once for each instance of the departing mod on the game thread while its content remains mounted. C++ subclasses override `OnModBeforeUnload` and call `Super::OnModBeforeUnload` to dispatch the Blueprint event. Use this callback to restore gameplay state and release references to mod assets. Host C++ can subscribe to `IModLoaderModule::OnBeforeUnload()` to receive the mounted package record.

## Framework-only integration

Consumer-facing methods belong in the first `public:` section. Methods used solely for framework integration or Unreal lifecycle callbacks may occupy a second `public:` section, as specified in [Public API](API.md). Any internal access key, if introduced, is an integration convention rather than an authorization boundary. Do not expose a private implementation header through a public header, and do not use a non-reflected key as a `UFUNCTION` parameter. Cross-module access requires an explicit, documented contract.
