<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModSDKTool (MDT)

ModSDKTool (MDT) turns resolved host exports into an immutable, standalone SDK bundle for mod authors and host games. It generates Unreal reflection and Blueprint wrappers that connect player contexts and object views to typed methods accepting scalar and public `UObject*` inputs—deriving every API ID, public type, function name, and native C symbol strictly from the producer contract.

In the broader pipeline, MHT owns C++ AST projection and `ExportCodeGenerator` snapshots Blueprint exports. MDT then verifies the v4 target manifests across Game and Editor builds, emits both the host-game and mod-author variants of the shared SDK Runtime, compiles the authoring plugins through Unreal AutomationTool (`BuildPlugin`), and publishes a hash-verified bundle.

## Build and run

Build MDT from the repository root:

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

Bundle workflow commands:

```text
ModSDKTool plan --request SdkBuildRequest.json
ModSDKTool build --request SdkBuildRequest.json
ModSDKTool inspect --bundle <published-directory>
```

Relative paths inside `SdkBuildRequest.json` resolve from the request file's directory:

- `plan`: Verifies every referenced v4 generation manifest, public contract, and shared Client source without building plugins.
- `build`: Compiles the authoring plugins via UAT `BuildPlugin` and publishes the complete bundle.
- `inspect`: Re-verifies all published files, manifests, and Runtime module declarations on disk.

JSON results are written to stdout and diagnostics to stderr (`0` = success, `1` = error, `130` = cancelled).

Published SDK directories are immutable: re-running `build` with identical inputs re-verifies the existing directory and returns `UpToDate`, whereas any input change requires bumping `sdkVersion` (or choosing a new output path). During publication, MDT stages and verifies everything inside an owned sibling directory before atomically renaming it into the destination, ensuring failed builds never corrupt an existing SDK.

### Building `ModDevelopmentSDKEditor` standalone

To build only the standalone authoring plugin against an installed Unreal Editor, run `build-editor`:

```text
ModSDKTool build-editor --package <new-directory>
                       --dotnet <host> --automation-tool <AutomationTool.dll>
                       --editor-modules <UnrealEditor.modules>
                       --architecture <x64|arm64> --mbt <ModBuildTool>
```

At CMake build time, the source files listed in `Resources/DevelopmentEditor/Files.cmake` are embedded directly into the MDT executable. Both `build` and `build-editor` extract those files—plus generated plugin descriptors and packaging rules—into a temporary workspace, run UAT `BuildPlugin`, verify the Editor `BuildId` and compiled binaries, and publish the plugin alongside bundled native tools (see [.circleci/build-editor-plugin.sh](../../../../../.circleci/build-editor-plugin.sh) for a working example).

Before opening the sample mod project (`ExampleMods/TestMod`), install both `ModDevelopmentSDKEditor` and the game's `UMFDevSDK` into `ExampleMods/TestMod/Plugins/`—a full `ModSDKTool build` bundle includes both plugins ready to copy.

## Input contracts

MDT consumes `UMF.SdkContract` **v2** ([sdk-contract.schema.json](Protocol/sdk-contract.schema.json)) while remaining backward-compatible with **v1** ABI field names:

- The contract must report `status: "Complete"` with zero unresolved types, and `contractHash`, `projectionHash`, and `nativeABIHash` must match across every target manifest listed in the build request.
- A contract that defines World Hooks/Events and v2 lifecycle projections may carry an empty `apis` array.
- The contract file itself must appear as a hash-verified `Contract` artifact in the target manifest, and every public header must be an MHT-declared `SdkHeader`—host C++ implementations, registration/verification units, and private headers are never copied into the published SDK.

### Native ABI and generated UE wrappers

Generated client calls target the `ContextHandlesV1` C ABI:

```cpp
// Concrete symbol names come from api.native.symbol.
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

Status `0` indicates success and nonzero values report host errors, while context, scope, and object handles are nonzero process-local opaque tokens:

- **`ObjectReference`:** Defines a stable type `id` and `cppName`.
- **`ObjectGetter`:** Uses `provider: LocalPlayerSubsystem`, `resultTypeId`, and `nullResult: Error` in `Player` scope.
- **`FloatMethod` & `TypedMethod`:** Operate in `Player` or `World` scope with `thread: GameThread`, `failureRule: ReturnError`, and per-API permission lists. `TypedMethod` supports scalar parameters, `ObjectInput` parameters (naming the public UE class, header, and module), and scalar/`void`/projected-view returns; its `blueprint` flag controls whether a Blueprint node is generated alongside the C++ wrapper.
- **Host Blueprint World APIs:** Carry `hostBlueprint.callingTableHash` so the generated C++ SDK can lazily connect native tables and acquire the live World provider through the caller's active `UModInstance`.

Every API entry specifies `id`, `name`, `displayName`, `signatureHash`, `projectionHash`, and `native: {header, symbol}`, all cross-checked against `requiredSymbols` (unsupported API shapes fail cleanly instead of emitting stub code).

In the generated `<pluginName>Runtime` UE module, opaque `USTRUCT` handles pair with a `MinimalAPI` Blueprint function library whose nodes expose success/failure execution pins via `ExpandEnumAsExecs`—no empty Editor module needed. Compile-time `static_assert` checks verify C ABI function-pointer types during UBT compilation. Tokens cannot be forged or edited in Blueprints (the host injects contexts via `FromNative`), adapter-layer errors return negative `int64` codes, and native host status codes pass through as nonnegative `int64` values.

Finally, MDT verifies `Contract/ModLifecycle.json` (matched against `modLifecycle` in the SDK contract) to generate `ModInstance.h` and `ModInstanceSubSystem.h` while preserving `/Script/ModLoader` reflection paths so Blueprint subclasses load seamlessly in the host game.

### Build a sample SDK

First, build both the host Game and Development Editor targets with MBT so their completed v4 manifests exist under `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>/` (see [Build the development host](../../../../../README.md#build-the-development-host)). Every listed target must agree on contract, projection, and ABI hashes as well as public headers, connection descriptors, and Client source.

Save the `UMF.SdkBuildRequest` v3 template below ([request schema](Protocol/sdk-build-request.schema.json)) as `SdkBuildRequest.json` in the repository root, filling in your UE 5.6 paths and the `BuildId` from `UnrealEditor.modules` (if you are packaging native C++ mods against the modular host sample, replace `UnrealModFrameworkDev` with `UnrealModFrameworkDevModular` in the first target entry and contract/manifest paths):

```json
{
  "format": "UMF.SdkBuildRequest",
  "schemaVersion": 3,
  "project": "UnrealModFramework/UnrealModFrameworkDev.uproject",
  "gameId": "UnrealModFrameworkDev",
  "sdkVersion": "1.0.0",
  "pluginName": "UMFDevSDK",
  "contract": "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDev/Win64/Development/x64/Contract/UnrealModFrameworkDev.sdkcontract.json",
  "tools": {
    "mbt": "UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe",
    "dotnet": "<engine bundled dotnet.exe>",
    "automationTool": "C:/UE_5.6/Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll"
  },
  "editor": {
    "platform": "Win64",
    "architecture": "x64",
    "configuration": "Development",
    "modules": "C:/UE_5.6/Engine/Binaries/Win64/UnrealEditor.modules",
    "buildId": "<BuildId from UnrealEditor.modules>"
  },
  "targets": [
    {
      "target": "UnrealModFrameworkDev",
      "platform": "Win64",
      "configuration": "Development",
      "architecture": "x64",
      "generatedManifest": "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDev/Win64/Development/x64/UnrealModFrameworkDev.generated-artifacts.json"
    },
    {
      "target": "UnrealModFrameworkDevEditor",
      "platform": "Win64",
      "configuration": "Development",
      "architecture": "x64",
      "generatedManifest": "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDevEditor/Win64/Development/x64/UnrealModFrameworkDev.generated-artifacts.json"
    }
  ]
}
```

By default, MDT publishes this bundle to `UnrealModFramework/Intermediate/ModTools/ModSDKTool/UMFDevSDK/1.0.0/`.

### World extension contracts

The `extensions` section of `.sdkcontract.json` drives code generation for Blueprint Hook/Event subscriptions, reflected value payload structs, read-only `UObject` payload projections, and cross-mod service acquisition, while exposing **On Mod Register** and **On Mod Bind** on `UModInstance` and bundling the C headers needed by standalone native callback DLLs. When packaging a mod with a v2 extension manifest, point the packager at the bundle's `Contract/SdkContract.json` (see [Mod Development SDK Editor](Resources/DevelopmentEditor/README.md)).

## Editor menu and bundle layout

With `ModDevelopmentSDKEditor` enabled, you can also run **Tools → Mod SDK → Plan Mod SDK...** or **Build Mod SDK...** directly from the Unreal Editor and pick a request JSON file. The editor uses either the MDT executable bundled inside the plugin, a local build under `ModRuntime/Tools/build/ModSDKTool`, or a custom path saved in Editor Settings. Logs stream asynchronously to the Output Log without freezing the UI, JSON results are written under `Intermediate/ModTools/ModSDKTool/Results`, and **Cancel Mod SDK task** terminates the entire child process tree.

A published SDK bundle has the following layout (carrying MDT, MBT, and third-party license notices under `Plugins/ModDevelopmentSDKEditor/Binaries/ThirdParty/ModSDKTool/<platform>`):

```text
Manifest.json
Build/Plan.json
Contract/
Native/Include/
Host/Plugins/<game-sdk>/
Plugins/<game-sdk>/
Plugins/ModDevelopmentSDKEditor/
```

## Install and verify

1. **Install the host SDK plugin:** Copy `Host/Plugins/<game-sdk>` into the host game's `Plugins/` directory, enable it, and then build and cook the game's base release. This host variant depends on `ModRuntime` and uses the game's real `/Script/ModLoader` classes.
2. **Install the author plugins:** Copy both `Plugins/<game-sdk>` and `Plugins/ModDevelopmentSDKEditor` into each standalone mod project's `Plugins/` directory. The author SDK supplies a `/Script/ModLoader` reflection mirror while sharing the exact same `<pluginName>Runtime` module name, reflected SDK types, Client source, and C ABI as the host variant.
3. **Package and test mods:** Package each mod against the matching host base release (for current-project C++ mods, supply the matching Win64 x64 modular `.target` receipt so the packager stages the mod's own native modules while keeping `<game-sdk>` as a shared plugin dependency; see [Package the current C++ project](Resources/DevelopmentEditor/README.md#package-the-current-c-project)).

Running `ModSDKTool inspect --bundle <path>` verifies file hashes, shared Client source, and Runtime module declarations across both SDK variants. Because bundle capability flags describe authoring artifacts only, archive the game's `host-link.json` and end-to-end staged-game verification report alongside the published SDK (see [Packaged Game Regression](../../../../../Test/SharedRuntime/README.md)).
