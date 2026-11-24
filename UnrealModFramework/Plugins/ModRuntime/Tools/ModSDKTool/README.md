<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModSDKTool

ModSDKTool (MDT) builds an independently installable authoring SDK from
resolved exports. It binds player contexts and object views to generated
methods that accept typed scalar and public UE object inputs. API IDs, public
types, names, and native symbols are all defined by the producer contract.

MHT owns C++ analysis and projection, while ExportCodeGenerator owns Blueprint
export. MDT consumes completed outputs, invokes MBT to compile generated
libraries, creates UE adapters, builds plugins through AutomationTool, and
publishes a file-hashed bundle. MHT scan reports are not complete contracts.

## Build and run

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

```text
ModSDKTool plan --request SdkBuildRequest.json --output Plan.json
ModSDKTool build --request SdkBuildRequest.json --output Result.json
ModSDKTool inspect --bundle <published-directory>
```

Paths in a request are resolved relative to the request file. `plan` validates
MBT inputs in a dedicated temporary directory without compiling or publishing,
while `build` runs real MBT recipes and UE `BuildPlugin`. Tool paths must point
to native executables rather than shell commands or batch files. Child
diagnostics and JSON progress are written to stderr, and the final JSON result
is written to stdout or `--output`. Every child process enforces a deadline and
supports process-tree cancellation. Result files must be written outside the
published bundle.

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
alongside the native tools. See the [CircleCI invocation](../../../../../.circleci/build-editor-plugin.sh) for a reference script.

Before opening `ExampleMods/TestMod`, install both the generated
`ModDevelopmentSDKEditor` and the game's `UMFDevSDK` under
`ExampleMods/TestMod/Plugins`. A complete SDK bundle for the sample game
includes both plugins.

## Input contracts

`UMF.SdkContract` v2 is defined in [sdk-contract.schema.json](Protocol/sdk-contract.schema.json);
MDT also supports reading v1 contracts that use the original ABI field names.
The contract must have status `Complete` with zero unresolved types, and its
contract, projection, and native ABI SHA-256 identities must agree across all
library batches. A contract with Hook/Event declarations and lifecycle
projection v2 may have an empty API list. The contract JSON itself must appear
as a hash-checked `Contract` artifact in a generated manifest, and public
includes must be MBT-reported `SdkHeader` artifacts. Host implementations,
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
records the digest in `modLifecycle`. Once MBT verifies the export and compiles
the libraries, MDT generates `ModInstance.h` and `ModInstanceSubSystem.h`, then
invokes UAT to build the authoring SDK and the development editor plugin. The
generated reflection module preserves the game's `/Script/ModLoader` type paths,
while the game session owns the instances and delivers lifecycle events.

`UMF.SdkBuildRequest` v2 is defined in [sdk-build-request.schema.json](Protocol/sdk-build-request.schema.json).
Set `schemaVersion` to `2` and omit `developmentPlugin`.

| Field | Purpose |
| --- | --- |
| `project`, `gameId`, `sdkVersion` | Target project and versioned game identity |
| `pluginName` | Stable SDK plugin and reflection module identity |
| `contract` | Completed public producer contract |
| `outputDirectory` | Optional bundle output directory (defaults to `<project>/Intermediate/ModTools/ModSDKTool/<pluginName>/<sdkVersion>`) |
| `tools.mbt` | MBT executable supporting the input manifest version and binary inspection |
| `tools.dotnet`, `tools.automationTool` | UE-compatible .NET host executable and UAT assembly |
| `editor` | Host platform, architecture, Development configuration, actual engine `.modules` file, and matching `buildId` |
| `libraries` | Host and Client entries, each specifying `context` and `generatedManifest` |
| `workDirectory` | Optional short temporary root directory (useful on Windows) |
| `timeoutSeconds` | Per-child process deadline in seconds (1–86400; default 1800) |

Both library roles use MBT's owning-module archive builder. A Client context is
the producer context for the independent client library's own module, not an
import context for game-private code. At least one Client profile must match the
authoring Editor, and each job must write to a distinct output.

Plugin compilation runs `BuildPlugin -NoTargetPlatforms` for the authoring
Editor. MDT verifies actual module binaries, target architecture, matching
BuildId, and the complete file inventory. The generated Build.cs rejects builds
when no matching client profile is present.

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
<bundle>/
  Manifest.json
  Contract/SdkContract.json
  Native/Include/...
  Native/Lib/<platform>/<architecture>/<configuration>/<target-type>/...
  Plugins/<game-sdk>/...
  Plugins/ModDevelopmentSDKEditor/...
  Host/Plugins/<game-sdk>/...  (when non-Editor Client profiles are supplied)
  Build/Plan.json
  Build/NativeResults.json
```

A `Built` status confirms that MDT compiled and verified the native archives and
authoring Editor plugins. Final game linkage, the Game-target SDK Runtime build,
and live API binding are performed and verified on the host side. The UE 5.6
Win64 host post-build step can produce MBT link records. Cooked Blueprint calls
require the matching Runtime SDK module in the game. Because public module,
type, function, and parameter names define reflection identity, keep them stable
across compatible releases.

The optional `Host/Plugins/<game-sdk>` variant shares the same runtime module
and Blueprint names as the authoring SDK. Supply matching Host and Client
library requests for every target that builds or cooks the game, and install and
enable the host variant in the game project before building or cooking. When
installing the plugin into a host whose export commandlets need the new module
before it is compiled, run the Development Editor bootstrap build with
`-MhtBootstrap` first.

MHT emits a matching `.sdkcontract.json`, pure C `.modabi.h`, independent client
endpoints, and a v3 manifest; see the [MHT connection protocol](../ModHeaderTool/README.md)
for details. Configure its Host and `<game>ModClient` contexts as separate
library jobs. The native connection descriptor lists host queries and client
Connect/Disconnect symbols; MDT requires it to be a hash-checked public Contract
artifact, verifies its identities and API signatures, and copies it into both
the bundle and the SDK plugin at the declared relative path. `inspect` verifies
both copies and their contract identity. The game supplies the authorization
policy and live world provider for host Blueprint APIs, while other context
delivery (such as local-player scope) belongs to the game integration. MHT marks
the SDK adapter contract `Unsupported` for API graphs outside this MDT adapter's
supported shapes. The internal UE-view `.modcontract.json` remains a separate
contract and is rejected as an MDT SDK contract.
