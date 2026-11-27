<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Modules.zh-CN.md)

# Modules and Dependency Boundaries

This repository contains the development host project and the game-side `ModRuntime` plugin, while ModSDKTool (MDT) generates and compiles the standalone authoring plugin (`ModDevelopmentSDKEditor`) from embedded source resources. Every module owns a single clear responsibility, declares only the dependencies it directly uses, and keeps implementation details under `Private/`. Public headers are contracts: each must compile cleanly on its own, and `.Build.cs` dependencies belong in `PublicDependencyModuleNames` only when `Public/` headers require them. Circular module dependencies are strictly forbidden.

The Unreal project resides in `UnrealModFramework/`, and all module and config paths below are relative to that directory. `ModRuntime` comprises six UE modules (`HAL`, `ExportMetadata`, `ModAPI`, `ModLoader`, `BlueprintIntegration`, and `ExportCodeGenerator`), while `ModDevelopmentSDKEditor` ships `DevelopmentEditor`:

| Responsibility | Path under `UnrealModFramework/` | UE module type |
| --- | --- | --- |
| Native file-system directory watcher | `Plugins/ModRuntime/Source/Runtime/Core/HAL/` | `Runtime` |
| Export metadata schemas and config options | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| Generated API bindings, World extension dispatch, content slots, Patches, services, and session/save helpers | `Plugins/ModRuntime/Source/Runtime/ModAPI/` | `Runtime` |
| `.mod` archive admission, extraction, and mounting | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| Blueprint Mod export panel, declaration model, and K2 compiler hooks | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| Commandlets that export GameMod options and Blueprint snapshots | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| Standalone mod authoring and packaging UI/commandlet | `Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/Source/DevelopmentEditor/` | `Editor` |
| Sample host game integration | `Source/ExportTest/` | `Runtime` |

Note that `Runtime/` and `Editor/` under `Plugins/ModRuntime/Source/` are directory groupings rather than UE modules themselves; every actual module is listed in `ModRuntime.uplugin`. In the host project, `[CoreRedirects]` in `Config/DefaultEngine.ini` redirects `/Script/ModBlueprintIntegration` to `/Script/BlueprintIntegration` so existing Blueprint assets keep their serialized references intact.

### Core runtime and SDK boundaries

- **`ExportMetadata`:** Compiles in both Game and Editor targets and publicly depends only on `Core` (for UE strings and config arrays). It must never depend on Editor modules, host-game gameplay modules, content-distribution services, or `DevelopmentEditor`. Future runtime capabilities should live in dedicated modules named after their responsibility rather than expanding `ExportMetadata`.
- **`ModAPI`:** Runs in Game and Editor targets, with public dependencies on `Core`, `CoreUObject`, `Engine`, and `ModLoader`, and a private dependency on `Json`. `UModAPIHostSubsystem` manages API providers, caller policies, and generated bindings per `GameInstance`; `UModExtensionSubsystem` prepares and tears down World extensions—coordinating Hook/Event snapshots, content slots, Patch anchors, cross-mod service leases, and gameplay digests. Concrete gameplay admission and save integration live in the host (`ExportTest`).
- **`DevelopmentEditor` and SDK variants:** MDT extracts the embedded `DevelopmentEditor` sources into a temporary workspace and compiles `ModDevelopmentSDKEditor` independently via UAT `BuildPlugin`, communicating across the game/authoring boundary solely through versioned JSON contracts and generated public headers (cross-process live preview remains a design item). Meanwhile, UHT registers generated host C++ wrappers for compilation directly inside their owning host modules, while game and author SDK plugins share the same `<pluginName>Runtime` module name, reflected types, and Client source—the game variant binds to the real `ModRuntime` plugin, whereas the author variant provides a reflection mirror of `/Script/ModLoader` (see [Host compilation and linking](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md#host-compilation-and-linking) and [SDK installation](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md#install-and-verify)).
- **`ExportCodeGenerator`:** An Editor module with a public dependency on `ExportMetadata` and private dependencies on `Core`, `CoreUObject`, `Engine`, `Json`, `Projects`, `AssetRegistry`, `BlueprintIntegration`, `ModAPI`, and `UnrealEd`. Its `ModExportOptions` commandlet dumps resolved `[ModExportMetadata]` choices and UE redirects, and `ModExportBlueprints` compiles configured Blueprint assets to snapshot their exports. MHT combines that snapshot with active C++ declarations from `libclang` ASTs to emit host bridges, public contracts, C ABI headers, and verification units.
- **UE APIs vs. C ABI:** Keep native C ABI definitions strictly separate from UE-facing C++ interfaces. Because `UObject` layouts and C++ standard-library ABIs are not portable across independently compiled binaries, external C ABI headers use fixed-width scalars, explicit byte lengths, opaque handles, and documented buffer ownership.

Whenever proposing a new module, document its single responsibility, direct dependencies, supported target types, and why no existing module should own it. Package optional integrations as separate modules or plugins, and never add a module dependency just to silence a missing `#include`.

### Blueprint export integration (`BlueprintIntegration`)

`BlueprintIntegration` owns Blueprint Mod export authoring in Editor targets. It depends on `Core`, `CoreUObject`, `Engine`, `BlueprintGraph`, `GraphEditor`, `Kismet`, `KismetCompiler`, `ExportMetadata`, `ModAPI`, `Slate`, `SlateCore`, `ToolWidgets`, `UMGEditor`, and `UnrealEd`, loading `PostEngineInit` so `Kismet` editor services are ready. Neither `ExportMetadata` nor `DevelopmentEditor` depends on it. `GraphEditor` and `ToolWidgets` power the native My Blueprint-style action list and Add button, while `UMGEditor` keeps the tab registered across Widget Blueprint mode switches.

In Blueprint editors, the module docks a **蓝图Mod导出** tab beside My Blueprint (falling back to a standalone tab spawner in interface, macro-library, Widget, and Animation Blueprint layouts, and disabling creation on Blueprint classes that cannot host runtime implementations). The panel mirrors My Blueprint's `SGraphActionMenu`, collapsible `API` / `Event` / `Hook` sections, circular Add buttons, search box, inline rename (`F2`), and `Delete` handling, routing property edits to the editor's existing Details panel and opening implementation graphs on double-click.

Each declaration is a transactional object stored inside a `UBlueprintExtension` on the Blueprint asset—never backed by ordinary Blueprint functions or Event Dispatchers (legacy function/dispatcher-backed declarations are unsupported and must be recreated):

| Kind | Stored properties | Associated Blueprint graph / node |
| --- | --- | --- |
| API | Version, display & public names, description, scope, thread, failure rule, permissions | Dedicated implementation graph |
| Event | Version, display name, description, payload type, source scope, publish & unsubscribe rules, permission, authority | **Publish Event** node placed in a host execution graph |
| Hook | Version, display name, description, payload type, `AllowedStages`, `AllowedOperations`, permission, authority | Dedicated original-behavior graph + **Hook Point** node in a host execution graph |

Candidate dropdown values are read by `Mod::ExportMetadata::FOptions` from `[ModExportMetadata]` in `Config/DefaultGameMod.ini` (`APIScopes`, `APIThreads`, `APIPermissions`, `APIFailureRules`, `EventSourceScopes`, and `EventPublishRules`, populated via `+Key=Value`).

Canonical names follow `ModAPI_<name>`, `ModEvent_<name>`, and `ModHook_<name>`. Inline editing accepts either the bare name or the prefixed form and stores only the base name so prefixes never double up; empty, duplicate, overlong, or invalid names are rejected outright rather than falling back to GUIDs or numeric suffixes. API and Hook implementation graphs use a K2-derived schema owned exclusively by their declaration (orphaned or detached graphs emit diagnostics instead of opening). During compilation, the Blueprint compiler binds the canonical declaration name onto intermediate graphs, entry/result nodes, local-variable scopes, and display metadata without mutating the source graph, while interactive renames update graph names, scopes, and loaded function references atomically in one undo transaction. Saved declarations also index into Find in Blueprints and native Find References. When exporting, `ModExportBlueprints` compiles the saved Blueprint and includes any Hook or Event whose **Hook Point** or **Publish Event** node has a connected execution pin.

## Mod loader

`ModLoader` discovers, verifies, extracts, and mounts `.mod` archives—supporting archive **v1** (cooked content), **v2** (content plus host-matched native UE modules), and **v3** (extension manifest with optional UE modules or standalone native callback DLLs)—and delivers load results to game sessions. It publicly depends on `Core`, `CoreUObject`, and `Engine`, and privately on `Json`, `PakFile`, `AssetRegistry`, `HAL`, `RenderCore`, and OpenSSL (plus `UnrealEd` in Editor builds for PIE tracking and cleanup). `ModLoader` owns process-level archive admission and package leases, while `ModAPI` registers the generated contract validator and drives World-level bindings and callbacks.

### Startup scanning, hot-watching, and manual loading

By default, packaged games scan top-level `.mod` files in `Mods/` next to the game executable (`<Project>/Binaries/Win64/Mods` on a standard Win64 build); set `bLoadAtStartup=False` under `[ModLoader]` in `Config/DefaultGame.ini` to disable automatic startup scanning. When startup loading is active, the `HAL` module also watches `Mods/` on Win64, Linux, and macOS (`bWatchForChanges=False` disables live watching): once a newly dropped file's metadata settles, a worker thread validates and extracts it before committing the mount on the Game Thread (archives whose `ModId` is already mounted are silently skipped).

Host C++ modules that depend on `ModLoader` can also trigger synchronous loads on the Game Thread before gameplay begins via `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` or `LoadDirectory(Directory, Policy)`, and subscribe to `OnLoadCompleted()` to receive Game Thread notifications carrying the load origin, path, and `FLoadResult` list (empty directory scans produce an empty list; scan-level failures produce a single error entry).

### Host configuration and admission policy

Configure `[ModLoader]` in `Config/DefaultGame.ini` using the exact release identity passed to the authoring packager and the container backend (`Pak` or `IoStore`) used by your base build:

```ini
[ModLoader]
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<SHA-256 of the base release AssetRegistry.bin>
Backend=IoStore
PreparationTimeoutSeconds=120
```

- **Native code opt-in (Win64 x64 Pak):** Set `bAllowNativeModules=True` to permit host-matched UE module DLLs, and `bAllowNativeExtensions=True` to permit standalone C ABI callback DLLs.
- **Package allowlist:** Off by default when no digests or overrides are configured. Add `+TrustedPackageSha256=<sha256>` entries (from each `.mod` build result) to restrict loading to approved archives. Explicitly setting `bEnforcePackageAllowlist=False` bypasses a populated list, whereas `bEnforcePackageAllowlist=True` enforces matching even if the list is empty (blocking all packages). Manual loads accept equivalent overrides via `FLoadPolicy`.
- **Selection, pinning, and grants:** Use `+EnabledMods=<ModId>`, `+RequiredMods=<ModId>`, `+PinnedArchives=<ModId>=<archive SHA-256>`, and `+ExtensionGrants=<ModId>=<permission ID>`. Setting `bEnableAllMods=False` with an empty `+EnabledMods` list admits zero mods. At the World level, `FWorldPolicy::Grants` can override a mod's grant set; otherwise, Worlds inherit process-admission grants. The package set and grants established by the first prepared World remain frozen across subsequent Worlds in the same `GameInstance`.

### World preparation, multiplayer, and saves

Before calling `UModExtensionSubsystem::Prepare`, register content slots and Patch anchors (configuring `FPatchAnchorRules::Attachment` or `DataAssetType` where component attachment or data-asset registration is allowed). Hosts can query resolved Patch assets with `ResolvePatchData`, trace spawned Actors/components back to a mod via `GetPatchOrigin`, observe World failure closures through `SetFailureHandler`, inspect exact mounted archive hashes via `GetPackageRevisions`, and read or enforce the deterministic plan fingerprint through `GetGameplayDigest` and `ExpectedGameplayDigest`.

In multiplayer (`PreLogin`), the server and client verify matching game identity, base release, generated contracts, and admitted mod releases: the server sends its map name and plan digest prior to UE `Welcome`, and the client checks the map before traveling and confirms its matching local plan digest before `Join`. When Client and Server builds legitimately produce different `AssetRegistry.bin` hashes, set `NetworkBaseCompatibilitySha256` to a shared compatibility digest on both targets; likewise, map platform-specific Client/Server `.mod` files onto a shared gameplay identity via `+GameplayReleases=<archive SHA-256>=<ReleaseId>=<gameplay compatibility SHA-256>`.

For single-player persistence, `FModExtensionSave` validates required mods during World preparation while carrying forward optional mod data blocks whose owning mod is currently absent. In the sample `ModExtensions` mode, console commands `ModWriteSave <slot>` and `ModLoadSave <slot>` read and write under `Saved/ModSaves`, publishing writes atomically to a new slot and keeping the original save untouched if migration fails.

### Caching, unloading, PIE, and ModInstance callbacks

Extracted payloads are cached under `Saved/ModCache/<archive SHA-256>` and re-verified before reuse. Each mounted mod owns an isolated `/<PluginName>/` package root (only one version of a `ModId` may be mounted at a time) and appends its cooked `AssetRegistry` when present. Deleting or moving a `.mod` out of a watched directory initiates graceful unload while cached files remain intact: once leases and asset references clear, the loader unmounts containers, releases shader libraries, and unregisters the package root (logging and retaining the mount if live references remain).

On Windows PIE, `ModLoader` reads `.mod` files from the host project's `Binaries/Win64/Mods` using the same `[ModLoader]` config:

- Enable **Allow Cooked Content in the Editor** under **Project Settings → Engine → Cooker**.
- For `Backend=Pak`, `ModLoader` initializes the Pak platform file automatically at editor startup; for `Backend=IoStore`, launch the editor with `-UsePaks -UseIoStore`.
- For unversioned cooked packages, add `s.AllowUnversionedContentInEditor=1` under `[ConsoleVariables]` in `Config/DefaultEngine.ini`.

Multiple PIE worlds in one editor process share mounted containers while receiving separate `UModInstance` objects. On PIE exit, the loader halts result delivery, releases packages after world teardown, and unmounts any container no longer referenced by editor objects.

MDT generates `ModInstance` and `ModInstanceSubSystem` under `/Script/ModLoader`, and the packager automatically records concrete Blueprint subclasses found in a mod's content plugin. After mounting, each active instance receives **On Mod Load Completed** (`OnModLoadCompleted` in C++, calling `Super::OnModLoadCompleted` to fire Blueprint `ReceiveModLoadCompleted`). Before unload—while assets are still mounted—each instance receives **On Mod Before Unload** (`OnModBeforeUnload` in C++) on the Game Thread so it can restore game state and drop asset references; host C++ can also listen via `IModLoaderModule::OnBeforeUnload()`.

## Framework-only integration

Keep consumer-facing operations in a class's first `public:` section and place internal framework hooks or Unreal lifecycle overrides in an adjacent second `public:` section, as detailed in [Public API, Errors, and Documentation](API.md). Internal passkey types (if used) are wiring conventions rather than security boundaries—never leak private headers through `Public/` headers or use non-reflected passkeys in `UFUNCTION` signatures. Every cross-module call must go through an explicit, documented contract.
