<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Mod Development SDK Editor

Enabling `ModDevelopmentSDKEditor` adds a **Mod** dropdown to the Level Editor toolbar beside the Platforms menu. Choose **打包 Mod…** (Package Mod), select either a content plugin or the current project, fill in your mod version, target game identity, and base-release settings, and click **开始打包** (Start Packaging).

To build the plugin standalone from embedded sources, run `ModSDKTool build-editor` (which compiles it independently via UE 5.6 UAT `BuildPlugin`), then copy the output into your authoring project's `Plugins/` directory or the matching engine's `Engine/Plugins/` directory and enable it. `DevelopmentEditor` has zero build-time dependency on game-side `ModRuntime` modules. When your mod calls game APIs or subclasses `UModInstance`, also install and enable the author SDK plugin (`Plugins/<game-sdk>`) from the game's published SDK bundle—while the game publisher installs the matching `Host/Plugins/<game-sdk>` in the host project before building its base release.

## Packaging inputs

- **Source target:** Either an enabled content-only plugin (`CanContainContent: true`, no C++ modules, at least one `.uasset` or `.umap`, and its own `/<PluginName>/` package root) or **打包当前工程** (Package Current Project) for C++ mod projects.
- **Mod version:** A three-part numeric semantic version such as `1.0.0`.
- **Target game and base release:** The `GameId` and `BaseReleaseVersion` published with the game's SDK. The base release directory must contain `<ReleaseVersion>/<CookPlatform>/AssetRegistry.bin` (defaulting to `<AuthorProject>/Releases` when left blank; note that `Win64` uses the Cook platform folder name `Windows`).
- **Platform and container backend:** Target platform plus `Pak` or `IoStore` matching the host release. Keep engine content packaging disabled and use an isolated package root for `ModLoader` compatibility. Your authoring project must have the matching UE Cooker, target platform SDK, and any public types or assets referenced by the mod.
- **Output directory:** Defaults to `Saved/PackagedMods`, writing `<PluginName>-<ModVersion>-<TargetPlatform>.mod` without ever overwriting an existing archive.

All of these settings are also editable under **Project Settings → Plugins → Mod 打包** and persist per project in `EditorPerProjectUserSettings`.

## Package the current C++ project

1. **Build the modular host and Pak base release:** Enable `Host/Plugins/<game-sdk>` in the host project, compile the modular Win64 x64 Game target, and cook/stage a Pak base release. For the repository sample, use a UE 5.6 source build that supports modular Game DLLs and import libraries, build `UnrealModFrameworkDevModular`, and keep the resulting UBT `.target` receipt.
2. **Configure authoring settings:** Open the mod project, check **打包当前工程**, choose `Win64`, uncheck **使用 IoStore**, and point **宿主 Target 回执** at that `.target` file (if the host was built against a different UE installation, also fill in **宿主 Engine 目录** and ensure both engines share the exact same UE version and changelist). Both the `.target` receipt and the host's `.modules` manifests must be readable on disk.
3. **Run packaging:** The packager inspects Runtime modules and enabled plugin dependencies in your `.uproject`, compiles a temporary plugin against the host target receipt, and remaps project assets into the mod's isolated `/<ProjectName>/` package root for DLC Cook—retaining `<game-sdk>` as a shared plugin dependency while keeping the mod's own `/Script/` module paths intact so Blueprint parent classes resolve cleanly.
4. **Load in the host:** Set `bAllowNativeModules=True` under `[ModLoader]` in the host's `Config/DefaultGame.ini` alongside matching base-release settings and `Backend=Pak`, then drop the `.mod` file into `Mods/` beside the game executable. `ModLoader` loads and registers the mod's native DLLs before instantiating its lifecycle Blueprints.

Supplying an Editor `.target` receipt produces a package for Windows PIE, whereas supplying a Game `.target` receipt produces a package for the standalone packaged game. In `TestMod`, `/TestMod/BP_CallbackProbe.BP_CallbackProbe_C` subclasses the project's C++ `UModInstance` and logs from C++ to `LogTestModCallback`. Avoid hard-coded `/Game/...` asset strings in C++—either reference cooked `/<ModName>/...` package paths or use soft/hard asset properties that the migration step remaps automatically.

### Testing Pak mods in Windows PIE

1. Enable **Allow Cooked Content in the Editor** under **Project Settings → Engine → Cooker** in the host project, and place the Editor-targeted `.mod` into `<HostProject>/Binaries/Win64/Mods`.
2. Pass `-UsePaks` when launching the editor if Pak mounting is not already active, and set `s.AllowUnversionedContentInEditor=1` under `[ConsoleVariables]` in `Config/DefaultEngine.ini` when loading unversioned cooked assets.
3. If the `.mod` carries a shared shader archive, rendered PIE requires the base release's `ShaderArchive-*.ushaderbytecode` files inside `<HostProject>/Content` on a matching shader platform (or pass `-NullRHI` when verifying non-rendering lifecycle logic only).

## Build behavior

Before starting, the editor prompts to save dirty assets and then spawns an out-of-process `UnrealEditor-Cmd` commandlet. That commandlet invokes the engine's bundled `dotnet` binary and `AutomationTool.dll BuildCookRun` directly—never concatenating shell strings—and executes a clean DLC Cook/Stage inside a fresh per-task directory without reusing stale staging outputs.

Once staging finishes, the packager collects only the selected mod's staged containers (preserving UE container names, paths, and IoStore partitions), writes `Metadata/CookReceipt.json`, checks base-release and plugin invariants before and after cooking, streams SHA-256 digests over every payload, and reads back the temporary archive for full byte-level verification before committing the final `.mod` file.

Throughout the build, the Unreal Editor stays responsive with progress notifications, live Output Log streaming, and **Mod** menu shortcuts to cancel packaging or open the output folder. Cancelling, timing out, or shutting down the editor terminates the entire child process tree. Per-task requests, logs, and result JSON files are saved under `Saved/ModPackaging/<TaskId>/` (an interrupted run may leave a `.partial` file, which is never treated as a valid package).

### Headless CLI packaging

You can run the exact same packaging pipeline headlessly via the `ModPackage` commandlet:

```text
UnrealEditor-Cmd <AuthorProject.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

- **Content-plugin requests (`schemaVersion: 1`):** `UMF.ModPackageBuildRequest` specifies `pluginName`, `modVersion`, `gameId`, `baseReleaseVersion`, `baseReleaseDirectory`, `targetPlatform`, `outputFile`, `useIoStore`, `includeEngineContent`, and `timeoutMinutes` (you can inspect any editor-generated `Request.json` under `Saved/ModPackaging/<TaskId>/`). Relative paths resolve from the `.uproject` directory, and the directory containing `Request.json` serves as the task workspace—it must not already contain a `Stage/` subdirectory.
- **Current-project C++ requests (`schemaVersion: 2`):** Replace `pluginName` with `hostTargetReceipt` (plus `hostEngineDirectory` when needed), and use a brand-new task directory for every run.

## Archive

Every `.mod` file uses explicit unsigned little-endian integers—never raw C++ struct dumps—in an uncompressed, unsigned single-file container capped at **64 GiB** total size, **1 MiB** manifest, **64 MiB** index, and **65,536** payload entries. On disk, the file consists of a 64-byte Header, UTF-8 Manifest JSON, contiguous binary payloads, and UTF-8 Index JSON:

| Header byte offset | Size (bytes) | Field |
| --- | --- | --- |
| `0` | `8` | Magic bytes `UMFMOD\0\0` |
| `8` | `2` | Major version (`1` = content only, `2` = content + native UE modules, `3` = package with extension manifest) |
| `10` | `2` | Minor version (`0`) |
| `12` | `4` | Header byte length (`64`) |
| `16` | `8` | Manifest byte offset (`64`) |
| `24` | `8` | Manifest byte length |
| `32` | `8` | Index byte offset |
| `40` | `8` | Index byte length |
| `48` | `8` | Total file byte length |
| `56` | `8` | Flags (must be `0`) |

### Extension manifests and World registration

Selecting **Mod 扩展清单** in packaging settings (or setting `extensionsFile` in `Request.json`) produces an archive **v3** package. Extension manifests support `schemaVersion` `1` or `2`; when using version `2`, also supply the game's published `Contract/SdkContract.json` (`sdkContractFile` in `Request.json`) so the packager can auto-fill omitted `signature` hashes and reject mismatched contracts. Optional manifest arrays include:

- `contracts`: Hook/Event entries pairing an extension `id` with the SDK's `signatureHash` (`signature`).
- `dependencies`: Mod dependency entries with `modId`, optional inclusive `minimum` and exclusive `before` versions, and `required` (defaults to `true`).
- `services`: Required cross-mod service entries specifying `id`, `provider` (`modId`), `typeId`, calling-table `signature`, and optional version bounds / `required` flag.
- `content`: Logical content entries specifying `slot`, `id`, `action` (`provide`, `replace`, or `disable`), optional `priority`, and—for `provide`/`replace`—a package-local soft `asset` path.
- `patches`: World Patch entries specifying `id`, `anchor`, and one effect: `actorClass` (spawn Actor), or in v2 `componentClass` (attach scene component at an approved socket/point) or `dataId` + `dataAsset` (register cooked data asset). Optional fields include `conflictKey`, `required`, same-anchor ordering `after`, and in v2 `requiresServices` plus `waitSeconds` (`(0, 3600]` seconds for an optional Patch's first anchor).
- `provides` *(v2)*: Cross-mod service offers with `id`, `typeId`, `version`, and optional `signature`, `permission`, and `patch` (binding the service automatically to that Patch's spawned Actor; callback-registered services must match their declared offer).
- `incompatible` & `requiredGrants` *(v2)*: Conflicting `modId` values and mandatory host capability IDs checked prior to mounting.

Inside Blueprints or C++, wire World extensions during `UModInstance` lifecycle events:

- **Hook & Event subscriptions (**`On Mod Register`**):** Call **Register Mod Extension** with the SDK-generated ID. Observers take the payload scalar, modifiers take and return that scalar, and cancellation callbacks take the scalar and return `bool` (pick an exact-representable `float` on the node's cancellation value pin). `Immediate` fires synchronously; `NextTick` enqueues into a bounded per-World queue. Keep the returned subscription token to unsubscribe (when immediate unsubscription is enabled on the channel, remaining callbacks to that listener are skipped even mid-broadcast).
- **Cross-mod services:** Providers offer implementations during **On Mod Register** via **Provide Mod Service** (or C++ `Provide`) with the SDK's `id`, `serviceSignature`, and version; consumers call the generated **Acquire Service** node during **On Mod Bind**.
- **Struct and `UObject` payloads:** Reflected value structs copy declared public scalar and nested-struct fields into SDK-generated Blueprint structs. `UObject` callbacks receive `FModPayloadObject` and call `ProjectPayload_<View>` to inspect a read-only view valid until the callback returns.

### Independent native callbacks

For standalone C/C++ extension DLLs (without UE module coupling), `#include` the SDK's `ModExtensionABI.h` and `<GameId>.modextensionsabi.h`, export `UMF_RegisterExtensionsV1` using `UMF_EXTENSION_EXPORT` and `UMF_EXTENSION_CALL`, verify the host table's `Version` and `StructSize`, register callbacks with the generated header's exact ID and signature constants, and list those contracts in your v2 extension manifest. Pass the compiled x64 DLL via packaging settings or `nativeExtensionFile`, and enable `bAllowNativeExtensions=True` under `[ModLoader]` on the Win64 Pak host:

- `UMF_RegisterExtensionsV1` runs once per World on the Game Thread, receiving a session handle and an owned registration token. If supplied, `Close` runs after all in-flight callbacks drain and before the DLL is released—quiesce any background work started by the DLL before returning from `Close`.
- Input buffers, output buffers, object handles, and `Wrap` continuation tokens expire as soon as the current callback returns. `Wrap` handlers call `Next` once with the exact generated byte size; both `Replace` and `Wrap` return `UMF_ExtensionWrite` with a complete output payload, while authorized `Pre` handlers may return `UMF_ExtensionStop` with a cancellation value.
- Wire structs use compact, unpadded little-endian layouts (`1`-byte booleans and `64`-bit opaque object handles). Callbacks can look up generated host API tables via `FindHostQuery` and invoke read-only methods with `ObjectContext` and the object handle.

### Package manifest and payloads

`UMF.ModPackage` shares the header's major version and records mod identity, version, plugin/package root, cook settings, and base-release digests. Index `entries` are sorted ascending by case-sensitive normalized path, recording `path`, `offset`, `size`, `storage: "None"`, and payload `sha256` (rejecting absolute paths, `..` traversal, and case-insensitive collisions):

- `lifecycleClasses`: Automatically populated with concrete Blueprint subclasses of `ModInstance` and `ModInstanceSubSystem` found in the packaged content plugin.
- `Content/<StageRelativePath>` & `Metadata/CookReceipt.json`: Preserve cooked containers alongside provenance metadata. Build result `UMF.ModPackageBuildResult` v1 reports `status: "Packaged"` and the final archive's `sha256` once read-back verification succeeds.
- `nativeCode` *(archive v2, and v3 with UE modules)*: Records the host target, configuration, architecture, `BuildId`, and module list, storing `.modules` and compiled DLLs under `Native/`. The host supplies the shared SDK Runtime DLL while granting and revoking each mod's caller context independently (loaded Win64 UE module DLLs stay resident until process exit, so updating native UE code requires a game restart).
- Validation markers: Both manifest and build result record `shippingValidated: false`, while `exportRequirements` is `"SdkVerified"` when validated against `SdkContract.json` at package time and `"NotVerified"` otherwise (the host still enforces live contract and grant checks at mount time).
