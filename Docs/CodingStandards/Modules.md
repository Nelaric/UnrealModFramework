<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Modules.zh-CN.md)

# Modules and Dependency Boundaries

The repository contains the development host and the game-side ModRuntime plugin. MDT generates the independent authoring plugin from embedded source resources. Each module has one responsibility, declares only the dependencies it uses, and keeps implementation details in `Private`. Public headers are contracts: they must compile when included on their own, and their `.Build.cs` dependencies must be public only when those headers require them. Circular module dependencies are forbidden.

The UE project lives in `UnrealModFramework/`. Module and configuration paths in this document are relative to that project directory. The `ModRuntime` plugin contains `HAL`, `ExportMetadata`, `ModAPI`, `ModLoader`, `BlueprintIntegration`, and `ExportCodeGenerator`. The `ModDevelopmentSDKEditor` plugin contains `DevelopmentEditor`.

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

`ExportMetadata` must compile in Game and Editor targets. It cannot depend on editor modules, a particular host game's code, a content-distribution provider, or the authoring editor. Runtime modules are organized by responsibility: `ExportMetadata` owns the existing contract types and choices, while future services belong in modules named for their specific functions. The host game may depend on the ModRuntime plugin to provide explicit extension points.

`Runtime` and `Editor` group modules inside the single `ModRuntime` plugin; they are not UE modules. Declare each module in `ModRuntime.uplugin`. `ExportMetadata` publicly depends on `Core` for UE strings and configuration arrays.

`BlueprintIntegration` owns Blueprint Mod export declarations in the Editor target. It depends directly on `Core`, `CoreUObject`, `Engine`, `BlueprintGraph`, `GraphEditor`, `Kismet`, `KismetCompiler`, `ExportMetadata`, `ModAPI`, `Slate`, `SlateCore`, `ToolWidgets`, `UMGEditor`, and `UnrealEd`. The export panel and implementation graphs stay in this module because they need Blueprint editor and compiler modules that runtime contracts do not use. `GraphEditor` and `ToolWidgets` supply the native My Blueprint action list and Add button. The `UMGEditor` dependency keeps the tab registered across Widget Blueprint mode changes. Neither `ExportMetadata` nor the independent `DevelopmentEditor` depends on it. It loads after engine initialization so `Kismet` can register its editor services.

The module registers a `蓝图Mod导出` tab in Blueprint editors, next to My Blueprint where that tab exists. A fallback tab spawner covers the interface, macro library, Widget, and Animation Blueprint editor modes that use different layouts; creation is disabled for Blueprint types that cannot provide the required runtime implementation. The panel uses the same `SGraphActionMenu`, section styling, Add button, search box, and view-options controls as My Blueprint. API, Event, and Hook are collapsible sections with circular Add buttons. Rows use native palette icons, selection, and inline renaming; selecting a row shows its properties in the Blueprint editor's existing Details panel, and double-clicking opens its implementation graph. Rename and Delete are available in the context menu and through F2 and Delete. The export tab contains no separate property pane. Declarations are transactional objects inside a `UBlueprintExtension` on the Blueprint asset. API implementation graphs and Hook original-behavior graphs are owned by their respective declarations and are exposed to Blueprint graph discovery; the compiler creates executable function graphs from them. Event declarations contain a payload type and publication settings. The module does not register function, variable, or dispatcher detail customizations.

| Declaration | Stored fields | Editor graph |
| --- | --- | --- |
| API | Version, display and public names, description, scope, thread, failure rule, permissions | Implementation graph |
| Event | Version, display name, description, payload, source scope, publication and unsubscription policy, permission, authority | `Publish Event` node in the host's graph |
| Hook | Version, display name, description, payload, `AllowedStages`, `AllowedOperations`, permission, authority | Original-behavior graph and `Hook Point` node |

Configured choices live in `[ModExportMetadata]` in `Config/DefaultGameMod.ini`. API choices use `APIScopes`, `APIThreads`, `APIPermissions`, and `APIFailureRules`. Event choices use `EventSourceScopes` and `EventPublishRules`. Add choices with `+Key=Value`. API permissions are stored as an array of identifiers.

Blueprint Mod exports are defined only by independent declarations created in this panel. API and Hook graphs belong exclusively to their declaration objects; Event declarations store their own payload and publication settings. The module contains no import, migration, ownership repair, or compatibility path for declarations backed by ordinary functions or Event Dispatchers. Unsupported declarations must be recreated. The export panel controls implementation-graph lifetime. Graph discovery, opening, and compilation validate ownership, so a missing or detached graph produces a diagnostic instead of being opened as an orphaned graph.

Declaration names are `ModAPI_<name>`, `ModEvent_<name>`, and `ModHook_<name>`. The model supplies one display name and tooltip for the export list, Details heading, native graph tabs, breadcrumbs, entry nodes, search results, and compiler diagnostics. Editing accepts the base name or the full name with its matching type prefix and stores the base name, so inline editing does not repeat the prefix. API and Hook graphs use a K2-derived schema for native graph displays, independent of their internal object names. Discovery and opening bind the schema only to independently owned implementation graphs; no source-graph renaming or legacy declaration conversion is needed. The separate entry-node drawing factory is removed. Compiled functions use the declaration's name as both their function name and display-name metadata, keeping call-node titles and action-menu labels consistent. The compiler applies that name to intermediate graphs, entry and result nodes, and local-variable scopes without renaming source graphs during compilation. Renaming updates graph names, local scopes, and loaded function references, including references to the declaration's compiled name when it differs from the source graph's internal name. The operations share the editor transaction. Empty, duplicate, overlong, and invalid names are rejected without GUIDs or generated numbers. The declaration GUID remains an internal identity hidden from Details.

The Blueprint extension contributes API, Event, and Hook names and tooltips to Find in Blueprints. Declaration creation, renaming, property edits, deletion, and panel undo/redo refresh the search index for saved assets. The panel search matches canonical and base names. Refreshing a selected row also refreshes its Details heading, including when the name was edited in Details. Source graphs and compiler copies share the same name-binding routine for entry and result function references, function-display metadata, and local-variable scopes. Native Find References therefore uses the declaration's function name as well.

`Mod::ExportMetadata::FOptions` reads the configured choices. The host explicitly places the declaration's **Publish Event** or **Hook Point** node in a Blueprint execution graph. The compiler resolves the declaration by GUID and expands the node into a runtime call. `ModExportBlueprints` compiles configured saved assets and includes Hook/Event declarations with a connected extension node. MHT consumes the snapshot and generates the descriptors and SDK projection.

`ModAPI` is a Runtime module for Game and Editor targets. Its public dependencies are `Core`, `CoreUObject`, `Engine`, and `ModLoader`. It privately depends on `Json`. `UModAPIHostSubsystem` manages providers, permissions, and generated API bindings per GameInstance. `UModExtensionSubsystem` prepares and stops extensions per World. It coordinates Hook/Event snapshots, content slots, Patch anchors, service leases, and the gameplay digest. The host owns the integration in `ExportTest`, including gameplay admission and save handling.

MDT writes the embedded `DevelopmentEditor` source into a temporary workspace and builds the authoring plugin with UAT `BuildPlugin`. The authoring plugin builds independently of the game-side modules. Versioned JSON contracts and generated public headers carry the SDK and package interfaces across that boundary. Cross-process preview remains a design item.

`ExportCodeGenerator` is an Editor module. It publicly depends on `ExportMetadata` and privately on `Core`, `CoreUObject`, `Engine`, `Json`, `Projects`, `AssetRegistry`, `BlueprintIntegration`, `ModAPI`, and `UnrealEd`. `ModExportOptions` exports resolved GameMod options and UE redirects, and `ModExportBlueprints` exports the configured Blueprint declarations. MHT resolves active C++ declarations from a target's libclang AST and consumes the Blueprint snapshot. It generates host bridges, public contracts, ABI declarations, and verification code. The supported profiles include synchronous UObject APIs and World Hook/Event extensions with supported scalar, reflected value, or projected object payloads.

Keep native mod ABI types separate from UE-facing public APIs. UE-facing C++ contracts use UE types. The external C ABI uses fixed-width fields, explicit lengths, opaque handles, and documented ownership because UE object and C++ ABI layouts are not portable across independently built binaries.

New modules must document their responsibility, direct dependencies, supported targets, and why an existing module cannot own the capability. Add optional integrations as separate modules or plugins. Do not introduce a dependency merely to make a missing include compile.

## Mod loader

`ModLoader` discovers, validates, extracts, and mounts `.mod` archives and forwards results to game sessions. It reads archive v1 for cooked content, v2 for native UE modules, and v3 for an extension manifest with optional native code. Its public dependencies are `Core`, `CoreUObject`, and `Engine`. Its private dependencies are `Json`, `PakFile`, `AssetRegistry`, `HAL`, `RenderCore`, and the engine's OpenSSL library. Editor targets also depend privately on `UnrealEd` for PIE lifetime and package release. The plugin descriptor enables the module for Win64, Linux, and Mac Game and Editor targets. Mounting requires the matching Pak or IoStore backend and game, base release, engine, and platform identities. Win64 x64 Pak hosts explicitly opt in to UE module loading with `bAllowNativeModules=True` and to independent extension DLL loading with `bAllowNativeExtensions=True`. `ModLoader` owns process admission and package leases. `ModAPI` installs the generated extension contract validator and owns World callbacks and bindings.

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

With no configured digests and no explicit override, the allowlist is off. To enable it, add one `+TrustedPackageSha256=<digest>` per approved `.mod` using the `sha256` field in its build result. An explicit `bEnforcePackageAllowlist=False` bypasses a populated list. Setting it to `True` requires a match even when the list is empty. Manual loads use the corresponding `FLoadPolicy` fields.

Process admission also accepts `+EnabledMods=<ModId>`, `+RequiredMods=<ModId>`, `+PinnedArchives=<ModId>=<archive SHA-256>` and `+ExtensionGrants=<ModId>=<permission ID>` in the same section. Set `bEnableAllMods=False` with an empty enable list to select an empty set. The host can supply a complete World grant set for a mod in `FWorldPolicy::Grants`. Otherwise the World uses the admission grants. The first prepared package selection and grants carry across Worlds in the same GameInstance.

Use `UModExtensionSubsystem` to declare content slots and Patch anchors before calling `Prepare`. Set `FPatchAnchorRules::Attachment` or `DataAssetType` to enable component attachments or data registrations. The host can call `ResolvePatchData` with the anchor and data ID and `GetPatchOrigin` with the Actor or component. `SetFailureHandler` receives the reason the World closed. `GetGameplayDigest` reports the initial ready plan's fingerprint and `ExpectedGameplayDigest` compares an authoritative value before opening gameplay. `GetPackageRevisions` provides the exact admitted archive digests.

Multiplayer checks the game, base release, generated contracts and admitted Mod releases in `PreLogin`. The server sends its map and plan digest before UE Welcome. The client checks the map before travel and confirms the local plan before Join. The server requires that confirmation on the joining connection. If target base registries differ, set `NetworkBaseCompatibilitySha256` to the same approved base compatibility digest on every target. To publish equivalent Client and Server Mod artifacts, add one `+GameplayReleases=<archive SHA-256>=<ReleaseId>=<gameplay compatibility SHA-256>` entry for each exact artifact. Each target still verifies its own archive. The sample `ModExtensions` mode uses reconnecting travel.

`FModExtensionSave` checks save requirements during World preparation. The host supplies schema migrations, logical content and persistent object resolution. Missing optional Mod blocks carry forward. In standalone `ModExtensions`, `ModWriteSave <slot>` and `ModLoadSave <slot>` use `Saved/ModSaves`. Writing publishes a new slot. Migration preserves the original file.

Payloads live in `Saved/ModCache/<archive SHA-256>` and are rechecked before reuse. A mount retains its cooked package root and admits one version of a mod ID at a time. Moving or deleting its source archive from the watched directory starts an unload while the cached content is still available. The loader releases the containers, shader library, and package root once the packages are safe to release. If references remain, it logs the retained package. This version accepts unencrypted content in its own package root and can append its cooked asset registry when present.

On Windows, PIE uses the host project's `Binaries/Win64/Mods` directory and the same `[ModLoader]` settings. Enable **Allow Cooked Content in the Editor** under **Project Settings → Engine → Cooker**. With `Backend=Pak`, ModLoader initializes the Pak platform file when the editor starts. For `Backend=IoStore`, launch with `-UsePaks -UseIoStore`. For unversioned cooked assets, set `s.AllowUnversionedContentInEditor=1` in the `[ConsoleVariables]` section of `Config/DefaultEngine.ini`.

PIE worlds in the same process share the mounted packages and own separate Mod instances. Each instance receives the startup results when its PIE map is ready. At the end of PIE, the loader stops delivering session results, releases loaded packages after world teardown, then unmounts the containers and unregisters their package roots. If an editor object still references a package, the loader reports it and retains the mount until cleanup can succeed.

`ModSDKTool` (MDT) generates `ModInstance` and `ModInstanceSubSystem` from the host's lifecycle export and preserves their `/Script/ModLoader` paths. Packaging automatically collects concrete Blueprint subclasses from the content plugin. After the load batch finishes, the game session creates entries for successful mounts and delivers each result to active entries. C++ subclasses override `OnModLoadCompleted` and call `Super::OnModLoadCompleted` to invoke `ReceiveModLoadCompleted`, displayed as **On Mod Load Completed** in Blueprint. Both entry points receive the same result, including the error code and reason for failures.

**On Mod Before Unload** runs once for each instance of the departing mod, on the game thread while its content remains mounted. C++ subclasses override `OnModBeforeUnload` and call `Super::OnModBeforeUnload` to dispatch the Blueprint event. Use this callback to restore gameplay state and release references to mod assets. Host C++ can subscribe to `IModLoaderModule::OnBeforeUnload()` for the mounted package record.

## Framework-only integration

Consumer-facing methods belong in the first `public:` section. Methods used only for framework integration or Unreal lifecycle may occupy a second `public:` section, as specified in [Public API](API.md). An internal access key, if introduced, is an integration convention rather than an authorization boundary. Do not expose a private implementation header through a public header or use a non-reflected key as a `UFUNCTION` parameter. Cross-module access needs an explicit documented contract.
