<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Mod Development SDK Editor

Enable `ModDevelopmentSDKEditor` to add a **Mod** dropdown to the Level Editor toolbar, beside the Platforms area. Choose **打包 Mod…** (Package Mod) and select a content plugin or the current project, then enter the release options and start packaging.

Run `ModSDKTool build-editor` to generate the authoring plugin and compile it with UE 5.6 UAT `BuildPlugin`. Install the resulting plugin in the authoring project's `Plugins` or the matching engine's `Engine/Plugins`, then enable it in the authoring project. The development module is independent of the game's ModRuntime modules.

## Inputs

- An enabled project content plugin with `CanContainContent: true`, no C++ modules, and at least one `.uasset` or `.umap`. Keep its own `/<PluginName>/` package root. For a C++ project, use **打包当前工程** (Package Current Project).
- A three-part numeric Mod version, such as `1.0.0`.
- A game ID and base release supplied by the target game's SDK. The release root contains `<ReleaseVersion>/<CookPlatform>/AssetRegistry.bin`. An empty root uses the project's `Releases` directory. The Cook platform for `Win64` is `Windows`.
- A target platform, IoStore/Pak choice, and engine-content policy matching the target release. The author project also needs the corresponding UE Cooker, SDK, and public types/assets actually referenced by the Mod.
- An output directory, defaulting to `Saved/PackagedMods`. The file is named `<PluginName>-<ModVersion>-<TargetPlatform>.mod`; existing packages are never replaced.

The same options appear in **Project Settings → Plugins → Mod 打包** and persist in the local project's EditorPerProjectUserSettings.

## Package the current C++ project

1. Build the modular Win64 x64 host and create a Pak base release. In this repository, select the `UnrealModFrameworkDevModular` Game target with a UE source build that can produce the Game DLLs and import libraries. Supply the actual UBT `.target` receipt from the host build.
2. Open the author project and enable **打包当前工程**. Select Win64 with **使用 IoStore** cleared, provide the host receipt, and select the host Engine directory when it differs from the authoring Editor's Engine. Cook with the host's UE version and changelist. The receipt must resolve to the host project and its module manifests on disk.
3. The packager reads the current `.uproject` for Runtime modules and enabled plugin dependencies, builds a temporary plugin against the host target, and migrates project content to its own package root before DLC Cook. It keeps the module names that the Blueprint parents use under `/Script/`.
4. Set `bAllowNativeModules=True` in the host's `[ModLoader]` configuration and place the resulting `.mod` next to the actual game executable under `Mods`. Use the same base-release identity and Pak backend. The loader registers the DLLs before creating lifecycle Blueprint instances.

An Editor receipt produces an Editor package for Windows PIE. Use the Game receipt for a packaged Game host. For TestMod, `/TestMod/BP_CallbackProbe.BP_CallbackProbe_C` inherits the project's native Mod instance. Its C++ load callback writes to `LogTestModCallback`. Code that uses a hard-coded asset path should resolve the authored asset at its cooked package path or use an asset reference that the migration can remap.

For PIE, launch the host Editor with `-UsePaks` and place the Editor `.mod` in the host project's `Binaries/Win64/Mods`. If the package contains a shared shader library, rendered PIE requires the base cook's `ShaderArchive-*.ushaderbytecode` files in the host project's `Content` and the matching shader platform. Use `-NullRHI` to verify lifecycle callbacks without rendering.

## Build behavior

The editor prompts to save assets, then starts a separate UnrealEditor commandlet. That process invokes the engine's bundled native dotnet executable and `AutomationTool.dll BuildCookRun` directly, without a shell. It performs a full DLC Cook/Stage using unique task directories.

Only the selected Mod's staged content is collected. UE container filenames, paths, and IoStore partitions are preserved. A CookReceipt and the content are written to a temporary archive. The base release and project/plugin configuration are checked across cooking; payloads are hashed with streaming SHA-256, then the archive is read and verified before the final file is committed.

The editor remains responsive and shows progress and completion/failure notifications. Logs appear in Output Log. The Mod dropdown also offers cancel and open-output-directory actions. Cancellation, timeout, and module shutdown terminate the process tree. Requests, logs, and results remain under `Saved/ModPackaging/<TaskId>/`. An interrupted process may leave a `.partial` file; that file is not a completed package.

Headless packaging accepts the same request:

```text
UnrealEditor-Cmd <AuthorProject.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

`UMF.ModPackageBuildRequest` v1 contains `pluginName`, `modVersion`, `gameId`, `baseReleaseVersion`, `baseReleaseDirectory`, `targetPlatform`, `outputFile`, `useIoStore`, `includeEngineContent`, and `timeoutMinutes`. The editor produces this request automatically. Paths are resolved relative to the author project. The request's directory is the task directory and must not already contain a `Stage` directory.

For a current-project build, set `schemaVersion` to `2` and supply `hostTargetReceipt` and, when needed, `hostEngineDirectory` in place of `pluginName`. Start each request in a fresh task directory.

## Archive

Integers are explicitly unsigned little endian; C++ struct memory is never serialized. The outer archive is uncompressed and unsigned. Limits are 64 GiB per file, 1 MiB per manifest, 64 MiB per index, and 65,536 entries. Layout: Header, UTF-8 Manifest JSON, contiguous payloads, UTF-8 Index JSON.

| Header offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | `UMFMOD` followed by two zero bytes |
| 8 | 2 | Major version `1` for content, `2` for native code with content, or `3` for packages with an extension manifest |
| 10 | 2 | Minor version `0` |
| 12 | 4 | Header size `64` |
| 16 | 8 | Manifest offset `64` |
| 24 | 8 | Manifest length |
| 32 | 8 | Index offset |
| 40 | 8 | Index length |
| 48 | 8 | Total file length |
| 56 | 8 | Flags, always `0` |

Select **Mod 扩展清单** in the packaging settings or set `extensionsFile` in the build request. Packages with an extension manifest use archive version 3. The extension manifest uses `schemaVersion: 1` and accepts the following optional arrays.

- `contracts` entries carry a generated Hook/Event `id` and the SDK's `signatureHash` in the `signature` field.
- `dependencies` entries carry `modId`, optional inclusive `minimum` and exclusive `before` versions, and optional `required` (true by default).
- `services` entries carry `id`, the provider's Mod ID as `provider`, the generated `typeId` and calling table `signature`, and the same optional version range and requirement flag.
- `content` entries carry `slot`, `id`, `action` (`provide`, `replace` or `disable`) and optional `priority`. Provide and replace require an `asset` soft object path within the mod package.
- `patches` entries carry `id`, `anchor`, and `actorClass`. Optional fields are `conflictKey`, `required`, and an `after` list of Patch IDs on the same anchor.

In a Mod instance's **On Mod Register** event, call **Register Mod Extension** with the generated ID. An observer takes one Float. An input or result modifier takes and returns one Float. A cancellation callback takes a Float and returns a Bool, and the registration supplies the complete cancellation value. Use the returned token to unsubscribe. A provider registers its World service during **On Mod Register** with **Provide Mod Service** or the C++ `Provide` method. Supply a service object implementing the host's generated World interface, the interface `id` and `serviceSignature` from the SDK type contract, and the provided service version. The consumer's **On Mod Bind** event can acquire its typed reference with the SDK's generated **Acquire Service** node.

`UMF.ModPackage` matches the header's major version and records Mod identity, version, plugin/package root, build options, and base release fingerprints. Index `entries` are ordered by case-sensitive path and contain `path`, `offset`, `size`, `storage: "None"`, and exact payload `sha256`. Absolute paths, traversal, and case-insensitive duplicates are rejected.

When the author SDK supplies `ModInstance` and `ModInstanceSubSystem`, the packager records concrete Blueprint subclasses from the selected content plugin in the optional `lifecycleClasses` field. The game uses these class paths to create session entries after mounting.

`Content/<StageRelativePath>` preserves UE output. `Metadata/CookReceipt.json` records package roots, staged paths, build configuration, and release provenance. `UMF.ModPackageBuildResult` v1 status `Packaged` means packaging and byte verification succeeded and includes the final file's SHA-256.

Version 2 adds a `nativeCode` manifest with the host target, configuration, architecture, BuildId, and module names. The packager builds individual modules with UBT and writes a matching `.modules` file using the host receipt. The module manifest and DLLs live under `Native/` and share the archive's hashed index. Loaded native modules stay resident until process exit. Updating one of those modules takes effect after restarting the host.

The manifest and result report `shippingValidated: false`, with `exportRequirements: "NotVerified"` in the manifest.
