<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModSDKTool

ModSDKTool (MDT) builds an independently installable authoring SDK from
resolved exports. It binds player contexts and object views to generated
methods that accept typed scalar and public UE object inputs. API IDs, public
types, names, and native symbols are all defined by the producer contract.

MHT owns C++ projection and ExportCodeGenerator owns Blueprint exports. MDT validates the v4 target manifests, generates the game and author variants of the shared Runtime, builds the authoring plugins through AutomationTool, and publishes a hashed bundle.

## Build and run

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

```text
ModSDKTool plan --request SdkBuildRequest.json --output Plan.json
ModSDKTool build --request SdkBuildRequest.json --output Result.json
ModSDKTool inspect --bundle <published-directory>
```

Request paths are relative to the request file. `plan` checks the manifests, host and Client connection metadata, and input hashes. `build` invokes UAT `BuildPlugin` and publishes the bundle. Diagnostics go to stderr. Write result files outside the bundle.

Published bundles are immutable. Identical inputs reuse a fully verified bundle
and return `UpToDate`; changed inputs require a new versioned output directory.
Publication prepares and verifies an owned sibling staging directory, then
renames it into the absent destination so failures never overwrite existing SDKs.

Use `ModSDKTool build-editor` to build the independent authoring plugin for an
installed Editor.

```text
ModSDKTool build-editor --package <new-directory>
                       --dotnet <host> --automation-tool <AutomationTool.dll>
                       --editor-modules <UnrealEditor.modules>
                       --architecture <x64|arm64> --mbt <ModBuildTool>
```

CMake embeds the files listed in `Resources/DevelopmentEditor/Files.cmake` into
MDT. Both `build-editor` and `build` write these files, the plugin descriptor,
and the packaging configuration into a temporary workspace. MDT then invokes UAT
and verifies the Editor BuildId and binaries before publishing the plugin
alongside the native tools. See the [desktop CI invocation](../../../../../Scripts/run_desktop_ci.py) for a reference script.

Before opening `ExampleMods/TestMod`, install both the generated
`ModDevelopmentSDKEditor` and the game's `UMFDevSDK` under
`ExampleMods/TestMod/Plugins`. A complete SDK bundle for the sample game
includes both plugins.

## Input contracts

`UMF.SdkContract` v2 is defined in [sdk-contract.schema.json](Protocol/sdk-contract.schema.json);
MDT also supports reading v1 contracts that use the original ABI field names.
The contract must have status `Complete` with zero unresolved types, and its
contract, projection, and native ABI SHA-256 identities must agree across all
target manifests. A contract with Hook/Event declarations and lifecycle
projection v2 may have an empty API list. The contract JSON itself must appear
as a hash-checked `Contract` artifact in a generated manifest, and public
includes must be MHT-declared `SdkHeader` artifacts. Host implementations,
registration and verification code, and private headers are never copied into
the public SDK.

`ContextHandlesV1` is the supported native client ABI:

```cpp
// Actual function names come from api.native.symbol.
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

A status of zero indicates success, while nonzero values indicate host errors.
Context, scope, and object tokens are opaque, process-local, and nonzero.
`ObjectReference` types carry a stable `id` and `cppName`. `ObjectGetter`
specifies `provider: LocalPlayerSubsystem`, `resultTypeId`, and
`nullResult: Error`. `FloatMethod` specifies `targetTypeId`, `parameterName`,
and a stable `parameterId`. `TypedMethod` describes parameters and the return
value, targeting either a player provider or an object view. An `ObjectInput`
parameter names the public UE class, include path, and module, while an object
result names the projected view TypeId. The `blueprint` flag controls whether a
Blueprint node is generated in addition to the native UE declaration. Getters
use Player scope, whereas methods support Player or World scope. APIs declare
`thread: GameThread` and `failureRule: ReturnError`, with per-API permission
lists. World-scoped host Blueprint APIs include `hostBlueprint.callingTableHash`;
the generated C++ SDK uses this hash to acquire the registered provider through
the active `UModInstance` and connects the native tables before the first call.

Each API also supplies `id`, a stable public `name`, `displayName`,
`signatureHash`, `projectionHash`, and `native: {header, symbol}`. Client
profiles must provide the required symbol records matching those endpoints.
Header paths remain relative to the generated batch root, and unsupported shapes
fail instead of generating placeholders.

The Runtime SDK contains opaque `USTRUCT` references and a `MinimalAPI`
Blueprint function library. Standard typed nodes expose success/failure
execution pins via `ExpandEnumAsExecs`, without requiring an empty Editor
module. Native function-pointer `static_assert` checks verify ABI compatibility
during UE compilation. Tokens are not Blueprint-editable; the host supplies
contexts via `FromNative`. Adapter errors are reported as negative numbers,
while nonnegative Blueprint `int64` error codes preserve native status values.

MHT exports global lifecycle declarations to `Contract/ModLifecycle.json` and
records the digest in `modLifecycle`. Once MDT verifies the lifecycle artifact, it generates `ModInstance.h` and `ModInstanceSubSystem.h`, then
invokes UAT to build the authoring SDK and the development editor plugin. The
generated reflection module preserves the game's `/Script/ModLoader` type paths,
while the game session owns the instances and delivers lifecycle events.

The v3 `UMF.SdkBuildRequest` includes `project`, `gameId`, `sdkVersion`, `pluginName`, `contract`, `tools`, `editor` and `targets`. Each target supplies `target`, `platform`, `configuration`, `architecture` and `generatedManifest`. MDT checks file hashes, compilation ownership, API signatures, connection symbols and ABI identity. All profiles must agree on the public headers and Client implementation. [Request schema](Protocol/sdk-build-request.schema.json)

### World extension contracts

The SDK's `extensions` contract defines and validates extension IDs and
signatures, driving code generation for Blueprint subscriptions, value structs,
object projections, and cross-mod service acquisition. The lifecycle projection
exposes **On Mod Register** and **On Mod Bind**, and the SDK bundles the ABI
headers required for independent native callbacks. When packaging with extension
manifest v2, pass the same `Contract/SdkContract.json` to the packager. See
[Author packaging and registration](Resources/DevelopmentEditor/README.md) for
details.

## Editor entry and output

Enable `ModDevelopmentSDKEditor`, open **Tools → Mod SDK**, choose **Plan Mod
SDK...** or **Build Mod SDK...**, and select a request JSON file. The editor can
run either the packaged MDT or a local executable under the ModRuntime plugin's
`Tools/build/ModSDKTool`, or you can browse for an executable and save its path
in editor settings. Output streams asynchronously to Output Log without blocking
the editor, and result JSON files are saved under
`Intermediate/ModTools/ModSDKTool/Results`. **Cancel Mod SDK task** terminates
the process tree.

The development plugin remains independent of game Runtime modules. Published
bundles carry MDT, MBT, and their license notices under the plugin's
`Binaries/ThirdParty/ModSDKTool/<platform>` directory.

```text
Manifest.json
Build/Plan.json
Contract/
Native/Include/
Host/Plugins/<game-sdk>/
Plugins/<game-sdk>/
Plugins/ModDevelopmentSDKEditor/
```

The publisher installs `Host/Plugins/<game-sdk>` in the game providing the shared Runtime before building and cooking it. This variant uses the real `ModRuntime` dependency. Independent author projects install both plugins under `Plugins/`. The author SDK supplies the `/Script/ModLoader` reflection mirror. Both SDK variants have the same Runtime module name, reflected types, Client source and ABI.

Mod packaging collects its private modules and retains the shared plugin dependency. The game issues separate caller tokens for the Mods and revokes each caller independently.

`inspect` verifies the inventory, shared Client implementation and Runtime module declarations in both plugins. Each game build provides a separate `host-link.json`. The publisher tests the live ABI and the two independent Mods in the staged game and keeps that evidence alongside the SDK. [Game verification](../../../../../Test/SharedRuntime/README.md)
