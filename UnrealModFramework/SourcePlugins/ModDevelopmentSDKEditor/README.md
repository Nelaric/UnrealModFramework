<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# Mod Development SDK Editor

Enable `ModDevelopmentSDKEditor` to add a **Mod** dropdown to the Level Editor toolbar, beside the Platforms area. Choose **打包 Mod…** (Package Mod), select the content plugin and release options in the native details panel, and start packaging.

The development host discovers `SourcePlugins` through `AdditionalPluginDirectories` and enables this plugin for Editor targets only. Independent distribution still uses UE 5.6 UAT `BuildPlugin`. Install the built plugin in the author's project `Plugins` or matching engine `Engine/Plugins`, then enable it. The development module does not depend on the game's ModRuntime modules.

## Inputs

- An enabled project content plugin with `CanContainContent: true`, no C++ modules, and at least one `.uasset` or `.umap`. Its `/<PluginName>/` package root is retained; remapping plugin content to `/Game/` is rejected.
- A three-part numeric Mod version, such as `1.0.0`.
- A game ID and base release supplied by the target game's SDK. The release root contains `<ReleaseVersion>/<CookPlatform>/AssetRegistry.bin`. An empty root uses the project's `Releases` directory. The Cook platform for `Win64` is `Windows`.
- A target platform, IoStore/Pak choice, and engine-content policy matching the target release. The author project also needs the corresponding UE Cooker, SDK, and public types/assets actually referenced by the Mod.
- An output directory, defaulting to `Saved/PackagedMods`. The file is named `<PluginName>-<ModVersion>-<TargetPlatform>.mod`; existing packages are never replaced.

The same options appear in **Project Settings → Plugins → Mod 打包** and persist in the local project's EditorPerProjectUserSettings.

## Build behavior

The editor prompts to save assets, then starts a separate UnrealEditor commandlet. That process invokes the engine's bundled native dotnet executable and `AutomationTool.dll BuildCookRun` directly, without a shell. It performs a full DLC Cook/Stage using unique task directories.

Only the selected Mod's staged content is collected. UE container filenames, paths, and IoStore partitions are preserved. A CookReceipt and the content are written to a temporary archive. The base release and project/plugin configuration are checked across cooking; payloads are hashed with streaming SHA-256, then the archive is read and verified before the final file is committed.

The editor remains responsive and shows progress and completion/failure notifications. Logs appear in Output Log. The Mod dropdown also offers cancel and open-output-directory actions. Cancellation, timeout, and module shutdown terminate the process tree. Requests, logs, and results remain under `Saved/ModPackaging/<TaskId>/`. An interrupted process may leave a `.partial` file; that file is not a completed package.

Headless packaging accepts the same request:

```text
UnrealEditor-Cmd <AuthorProject.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

`UMF.ModPackageBuildRequest` v1 contains `pluginName`, `modVersion`, `gameId`, `baseReleaseVersion`, `baseReleaseDirectory`, `targetPlatform`, `outputFile`, `useIoStore`, `includeEngineContent`, and `timeoutMinutes`. The editor produces this request automatically. Paths are resolved relative to the author project. The request's directory is the task directory and must not already contain a `Stage` directory.

## Archive v1

Integers are explicitly unsigned little endian; C++ struct memory is never serialized. The outer archive is uncompressed and unsigned. Limits are 64 GiB per file, 1 MiB per manifest, 64 MiB per index, and 65,536 entries. Layout: Header, UTF-8 Manifest JSON, contiguous payloads, UTF-8 Index JSON.

| Header offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | `UMFMOD` followed by two zero bytes |
| 8 | 2 | Major version `1` |
| 10 | 2 | Minor version `0` |
| 12 | 4 | Header size `64` |
| 16 | 8 | Manifest offset `64` |
| 24 | 8 | Manifest length |
| 32 | 8 | Index offset |
| 40 | 8 | Index length |
| 48 | 8 | Total file length |
| 56 | 8 | Flags, always `0` |

`UMF.ModPackage` v1 records Mod identity, version, plugin/package root, build options, and base release fingerprints. Index `entries` are ordered by case-sensitive path and contain `path`, `offset`, `size`, `storage: "None"`, and exact payload `sha256`. Absolute paths, traversal, and case-insensitive duplicates are rejected.

When the author SDK supplies `ModInstance` and `ModInstanceSubSystem`, the packager records concrete Blueprint subclasses from the selected content plugin in the optional `lifecycleClasses` field. The game uses these class paths to create session entries after mounting.

`Content/<StageRelativePath>` preserves UE output. `Metadata/CookReceipt.json` records package roots, staged paths, build configuration, and release provenance. `UMF.ModPackageBuildResult` v1 status `Packaged` means packaging and byte verification succeeded and includes the final file's SHA-256.

This version packages content Mods. Runtime mounting, native DLLs, signatures, framework content IDs, and export-usage analysis remain separate work. Manifest and result report `shippingValidated: false`, with `exportRequirements: "NotVerified"` in the manifest. A packaged artifact is not evidence of compatibility with a Shipping game; Pak and IoStore loading require independent validation.
