<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Mod Development SDK Editor

Enabling `ModDevelopmentSDKEditor` adds a **Mod** dropdown to the Level Editor toolbar, next to the Platforms area. Choose **打包 Mod…** (Package Mod), select a content plugin or the current project, enter the version, target game, and release options, and then click **开始打包** (Start Packaging).

Run `ModSDKTool build-editor` to build the authoring plugin through a dedicated UE 5.6 project Editor target. Install it in the authoring project's `Plugins` directory and enable it for that project. The development module is independent of the game's `ModRuntime` modules.

## Inputs

- An enabled project content plugin with `CanContainContent: true`, no C++ modules, at least one `.uasset` or `.umap`, and its own `/<PluginName>/` package root. For a C++ project, use **打包当前工程** (Package Current Project).
- A three-part numeric Mod version, such as `1.0.0`.
- A game ID and base release version supplied by the target game's SDK. The base release directory must contain `<ReleaseVersion>/<CookPlatform>/AssetRegistry.bin`; if left empty, it defaults to the project's `Releases` directory. The Cook platform name for `Win64` is `Windows`.
- A target platform and Pak or IoStore backend matching the target release. For archives intended for ModLoader, keep engine content disabled and use an independent package root. The authoring project also requires the corresponding UE Cooker, target SDK, and any public types or assets actually referenced by the Mod.
- An output directory, defaulting to `Saved/PackagedMods`. The output file is named `<PluginName>-<ModVersion>-<TargetPlatform>.mod`, and existing packages are never overwritten.

The same options are also available in **Project Settings → Plugins → Mod 打包** and are saved to the local project's EditorPerProjectUserSettings.

## Package the current C++ project

1. Build the modular Win64 x64 host and create a Pak base release. In this repository, use a UE source build capable of producing Game DLLs and import libraries, select the `UnrealModFrameworkDevModular` Game target, and obtain the actual UBT `.target` receipt from the host build.
2. Open the authoring project and enable **打包当前工程**. Select Win64, clear **使用 IoStore**, provide the host receipt, and specify the host Engine directory if it differs from the authoring Editor's Engine. Cook with the same UE version and changelist as the host. The receipt must resolve to the host project and its module manifests on disk.
3. The packager reads the current `.uproject` for Runtime modules and enabled plugin dependencies, builds a temporary plugin against the host target, and migrates project content into its own package root before DLC Cook. Blueprints continue to resolve parent classes under `/Script/` using their original module names.
4. Set `bAllowNativeModules=True` in the host's `[ModLoader]` configuration, use the same base-release identity and Pak backend, and place the resulting `.mod` under `Mods` next to the actual game executable. The loader registers the DLLs before creating lifecycle Blueprint instances.

An Editor receipt produces an Editor package for Windows PIE, whereas a Game receipt produces a package for a packaged Game host. In TestMod, `/TestMod/BP_CallbackProbe.BP_CallbackProbe_C` inherits from the project's native Mod instance, and its C++ load callback writes to `LogTestModCallback`. Code with hard-coded asset paths should resolve authored assets at their cooked package paths or use asset references that the migration step can remap.

To test in PIE, launch the host Editor with `-UsePaks` and place the Editor `.mod` in the host project's `Binaries/Win64/Mods`. If the package contains a shared shader library, rendered PIE requires the base cook's `ShaderArchive-*.ushaderbytecode` files in the host project's `Content` directory along with the matching shader platform. Use `-NullRHI` to verify lifecycle callbacks without rendering.

## Build behavior

The editor prompts you to save modified assets, then starts a separate `UnrealEditor-Cmd` commandlet. That process invokes the engine's bundled native dotnet executable and `AutomationTool.dll BuildCookRun` directly, without a shell, running a full DLC Cook/Stage in unique per-task directories without reusing previous payloads.

Only the selected Mod's staged content is collected, preserving UE container filenames, paths, and IoStore partitions. The staged content and a generated CookReceipt are written to a temporary archive. Base release and project/plugin configurations are checked before and after cooking; payloads are hashed with streaming SHA-256, and the temporary archive is read back and verified before the final file is committed.

The editor remains responsive during packaging, showing progress along with completion or failure notifications while streaming logs to Output Log. The **Mod** dropdown also provides cancel and open-output-directory actions. Cancellation, timeouts, and module shutdown all terminate the process tree. Requests, logs, and results are retained under `Saved/ModPackaging/<TaskId>/`. An interrupted process may leave behind a `.partial` file, which is not a completed package.

Headless packaging accepts the same versioned request:

```text
UnrealEditor-Cmd <AuthorProject.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

`UMF.ModPackageBuildRequest` v1 contains `pluginName`, `modVersion`, `gameId`, `baseReleaseVersion`, `baseReleaseDirectory`, `targetPlatform`, `outputFile`, `useIoStore`, `includeEngineContent`, and `timeoutMinutes`. The editor generates this request automatically, and you can inspect its structure in a completed task directory. Paths are resolved relative to the authoring project. The request's directory serves as the task directory and must not already contain a `Stage` subdirectory.

For a current-project build, set `schemaVersion` to `2` and supply `hostTargetReceipt`—plus `hostEngineDirectory` when needed—in place of `pluginName`. Start each run in a fresh task directory.

## Archive

All integers are explicitly unsigned little-endian; C++ struct memory is never serialized directly. The outer archive is uncompressed and unsigned. Size limits are 64 GiB per file, 1 MiB per manifest, 64 MiB per index, and 65,536 entries. The file layout is Header, UTF-8 Manifest JSON, contiguous payloads, and UTF-8 Index JSON.

| Header offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | `UMFMOD` followed by two zero bytes |
| 8 | 2 | Major version (`1` for content, `2` for native code with content, or `3` for packages with an extension manifest) |
| 10 | 2 | Minor version (`0`) |
| 12 | 4 | Header size (`64`) |
| 16 | 8 | Manifest offset (`64`) |
| 24 | 8 | Manifest length |
| 32 | 8 | Index offset |
| 40 | 8 | Index length |
| 48 | 8 | Total file length |
| 56 | 8 | Flags (always `0`) |

Select **Mod 扩展清单** in the packaging settings or set `extensionsFile` in the build request. Packages with an extension manifest use archive version 3. The extension manifest accepts schema version 1 or 2; for version 2, select the game's published `SdkContract.json` in packaging settings or set `sdkContractFile` in the request. The packager fills omitted `signature` fields and rejects any signature that differs from the SDK. The manifest supports the following optional arrays:

- `contracts`: each entry carries a generated Hook/Event `id` and the SDK's `signatureHash` in the `signature` field.
- `dependencies`: each entry carries `modId`, optional inclusive `minimum` and exclusive `before` versions, and an optional `required` flag (`true` by default).
- `services`: each entry carries a service `id`, the provider Mod's ID as `provider`, the generated `typeId` and calling table `signature`, plus the same optional version range and requirement flag.
- `content`: each entry carries `slot`, `id`, `action` (`provide`, `replace`, or `disable`), and an optional `priority`. Both `provide` and `replace` require an `asset` soft object path within the mod package.
- `patches`: each entry carries `id`, `anchor`, and one effect. Use `actorClass` to spawn an Actor. Version 2 also accepts `componentClass` to attach a scene component at a host-approved attachment point, or `dataId` and `dataAsset` to register a cooked data asset. Optional fields include `conflictKey`, `required`, and an `after` list of Patch IDs on the same anchor. Version 2 additionally accepts `requiresServices` for declared service dependencies and `waitSeconds` (greater than 0 and at most 3600 seconds) for an optional Patch's first anchor.
- `provides` (version 2): each entry carries a service `id`, `typeId`, and `version`, with optional `signature`, `permission`, and `patch`. A Patch-backed service binds to its spawned Actor, while services registered via callbacks must match their declared offers.
- `incompatible` and `requiredGrants` (version 2): `incompatible` lists conflicting `modId` values, and `requiredGrants` lists capability `id` values required before mounting.

In a Mod instance's **On Mod Register** event, call **Register Mod Extension** with the generated ID. An observer takes the SDK's scalar type, a modifier takes and returns that scalar type, and a cancellation callback takes that scalar type and returns a Bool. Because the registration node accepts a Float cancellation value, choose a value that is exactly representable. `Immediate` delivers events synchronously, whereas `NextTick` copies the scalar into a bounded World queue. Use the returned token to unsubscribe; when the host enables immediate unsubscription, remaining calls to the removed listener are skipped during the current publish. A provider registers its World service during **On Mod Register** via **Provide Mod Service** or the C++ `Provide` method, supplying a service object that implements the host's generated World interface, the interface `id` and `serviceSignature` from the SDK type contract, and the service version. Consumers can acquire a typed reference during **On Mod Bind** using the SDK's generated **Acquire Service** node.

Reflected value payloads copy public scalar fields and supported nested structs. Blueprint callbacks in the independent authoring project accept the SDK value struct named after the extension ID. Subscribe Blueprint handlers during **On Mod Register**. A struct modifier returns the same struct type, and a struct cancellation handler returns the complete cancellation result. UObject callbacks receive `FModPayloadObject` and read a read-only view via the generated `ProjectPayload_<View>` node; the view expires when the callback returns.

### Independent native callbacks

Include the SDK's `ModExtensionABI.h` and `<GameId>.modextensionsabi.h`, and export `UMF_RegisterExtensionsV1` using `UMF_EXTENSION_EXPORT` and `UMF_EXTENSION_CALL`. Verify the host API version and struct size before registering the exact ID and signature from the generated header, and declare those same contracts in the package's schema 2 extension manifest. Select the compiled x64 DLL in packaging settings or set `nativeExtensionFile` in the build request; the packager embeds the DLL as an indexed payload. On the target host, enable `bAllowNativeExtensions=True` under `[ModLoader]` for Win64 Pak releases.

- The entry point runs on the game thread once per World, and the host assigns a session and an owned registration token. An optional `Close` callback runs after in-flight calls drain and before the DLL reference is released; the DLL must quiesce any work it started before `Close` returns.
- Input, output, object, and continuation tokens are valid only for the duration of the current callback. `Wrap` invokes `Next` once with the exact generated size. Both `Replace` and `Wrap` return `UMF_ExtensionWrite` along with the complete result. When permitted, `Pre` may return `UMF_ExtensionStop` with a cancellation result.
- Struct wire fields use the compact little-endian layout defined in the generated C header, with Booleans occupying one byte and object values represented as opaque 64-bit handles. Callbacks can connect to the generated client API via `FindHostQuery` and invoke read-only methods using `ObjectContext` and the handle.

### Package manifest and payloads

`UMF.ModPackage` matches the header's major version and records the Mod identity, version, plugin/package root, build options, and base release fingerprints. Index `entries` are sorted in ascending order by case-sensitive path and record `path`, `offset`, `size`, `storage: "None"`, and the exact payload `sha256`. Absolute paths, directory traversal, and case-insensitive duplicates are rejected.

When the authoring SDK supplies `ModInstance` and `ModInstanceSubSystem`, the packager records concrete Blueprint subclasses from the selected content plugin in the optional `lifecycleClasses` field. The game uses these class paths to create session entries after mounting.

`Content/<StageRelativePath>` preserves the UE cook output, and `Metadata/CookReceipt.json` records package roots, staged paths, build configuration, and release provenance. In `UMF.ModPackageBuildResult` v1, status `Packaged` indicates that packaging and byte-level verification succeeded and includes the final archive's SHA-256.

Archive v2 and any v3 archive containing UE modules include a `nativeCode` manifest listing the host target, configuration, architecture, BuildId, and module names. The packager builds individual modules with UBT and writes a matching `.modules` file from the host receipt. The module manifest and DLLs are stored under `Native/` and covered by the archive's hashed index. Loaded native modules remain resident until process exit, so updating one requires restarting the host.

Both the manifest and build result record `shippingValidated: false`. The manifest's `exportRequirements` is set to `SdkVerified` when packaging validates an extension manifest against the supplied SDK contract, and `NotVerified` otherwise. The loader still verifies the actual host contracts and grants at admission time.
