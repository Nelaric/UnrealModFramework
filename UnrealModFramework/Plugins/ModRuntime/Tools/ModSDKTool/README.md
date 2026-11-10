<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModSDKTool

MST builds an independently installable authoring SDK from resolved exports.
It connects player context and object views to generated methods with typed
scalar and public UE object inputs. API IDs, public types, names, and native
symbols come from the producer's contract.

MHT owns C++ analysis and projection. ExportCodeGenerator owns Blueprint export.
MST consumes completed results, asks MBT to compile generated libraries, creates
UE adapters, builds plugins through AutomationTool, and publishes a file-hashed
bundle. MHT scan reports are not complete contracts.

## Build and run

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

```text
ModSDKTool plan --request SdkBuildRequest.json --output Plan.json
ModSDKTool build --request SdkBuildRequest.json --output Result.json
ModSDKTool inspect --bundle <published-directory>
```

Request paths are relative to the request file. `plan` validates MBT inputs in an
owned temporary directory without compiling or publishing. `build` uses real MBT
recipes and UE `BuildPlugin`. Tool paths must be native executables, not shell
commands or batch files. Child diagnostics and JSON progress use stderr; final
JSON uses stdout or `--output`. Every child has a deadline and process-tree
cancellation. Result files must be outside the published bundle.

Published bundles are immutable. Identical inputs reuse a fully verified bundle;
changed inputs require a new versioned output directory. Publication verifies an
owned sibling staging directory, then renames it into the absent destination.
Failures do not replace existing SDKs.

Use `ModSDKTool build-editor` to package the independent authoring plugin from
its descriptor and an installed Editor. Pass a new directory with `--package`,
the Editor's .NET host, AutomationTool assembly, module manifest and
architecture, and the MBT executable. MST verifies the Editor BuildId and
binary before publishing the plugin with the native tools. [CircleCI invocation](../../../../../.circleci/build-editor-plugin.sh)

## Input contracts

[sdk-contract.schema.json](Protocol/sdk-contract.schema.json) defines
`UMF.SdkContract` v2. MST also reads v1 contracts with the original ABI field names.
The contract must be `Complete` with zero unresolved types.
Its contract, projection, and native ABI SHA-256 identities must agree with all
library batches. The JSON file itself must be a hash-checked `Contract` artifact
in a generated manifest. Public includes must be MBT-reported `SdkHeader`
artifacts. Host implementations, registration, verification, and private headers
are never copied to the public SDK.

`ContextHandlesV1` is the supported native client ABI:

```cpp
// Actual function names come from api.native.symbol.
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

Status zero means success. Tokens are opaque, process-local, and nonzero.
`ObjectReference` types have stable `id` and `cppName`. `ObjectGetter` specifies
`provider: LocalPlayerSubsystem`, `resultTypeId`, and `nullResult: Error`.
`FloatMethod` specifies `targetTypeId`, `parameterName`, and stable `parameterId`.
`TypedMethod` describes parameters and the result. A method uses a player
provider or a view as its target. An `ObjectInput` parameter names the public UE
class, include, and module. An object result names the projected view TypeId.
The `blueprint` flag selects whether to generate a node in addition to the
native UE declaration. Getters use Player scope. Methods support Player or World. APIs declare
`thread: GameThread` and `failureRule: ReturnError`, with per-API permissions.

Each API also supplies `id`, stable public `name`, `displayName`, `signatureHash`,
`projectionHash`, and `native: {header, symbol}`. Client profiles need the required
symbol records matching the endpoints. Header paths remain relative to the
generated batch root. Unsupported shapes fail instead of generating placeholders.

The Runtime SDK contains opaque `USTRUCT` references and a `MinimalAPI` Blueprint
function library. Standard typed nodes expose success/failure execution pins
using `ExpandEnumAsExecs`. Native function-pointer `static_assert` checks run
during UE compilation. Tokens are not Blueprint-editable; the host supplies
contexts using `FromNative`. Adapter errors are negative; nonnegative Blueprint
`int64` error codes preserve native status values.

MHT exports global lifecycle declarations as `Contract/ModLifecycle.json` and
records the digest in `modLifecycle`. Once MBT verifies the export and builds the
libraries, MST generates `ModInstance.h` and `ModInstanceSubSystem.h` and uses
UAT to build the author SDK and the development editor plugin. The generated
reflection module preserves the game's `/Script/ModLoader` type paths. The game
session owns the instances and delivers lifecycle events.

[sdk-build-request.schema.json](Protocol/sdk-build-request.schema.json) defines
`UMF.SdkBuildRequest` v1:

| Field | Purpose |
| --- | --- |
| `project`, `gameId`, `sdkVersion` | Real project and versioned game identity |
| `pluginName` | Stable SDK plugin/reflection identity |
| `contract` | Completed public producer contract |
| `outputDirectory` | Immutable bundle destination |
| `developmentPlugin` | Independent plugin containing `DevelopmentEditor` |
| `tools.mbt` | MBT with support for the input manifest version and binary inspection |
| `tools.dotnet`, `tools.automationTool` | Actual UE-compatible host and assembly |
| `editor` | Host platform, architecture, Development, real engine `.modules`, matching `buildId` |
| `libraries` | Host and Client entries with `context` and `generatedManifest` |
| `workDirectory` | Optional short temporary root, useful on Windows |
| `timeoutSeconds` | Per-child deadline, 1–86400; default 1800 |

Both library roles use MBT's owning-module archive builder. A Client context is
the producer context of the independent client library's own module, not an
import context for game-private code. At least one Client profile must match the
author Editor. Jobs must write distinct outputs.

Plugin compilation uses `BuildPlugin -NoTargetPlatforms` for the author Editor.
MST checks actual module binaries, architecture, matching BuildId, and the file
inventory. Generated Build.cs rejects missing client profiles.

## Editor entry and output

Enable `ModDevelopmentSDKEditor`. Under **Tools → Mod SDK**, choose **Plan Mod
SDK...** or **Build Mod SDK...**, then select a request JSON. If a packaged MST is
absent, locate the built executable once; the path is stored in project editor
settings. Output is streamed without blocking the editor. Results are saved to
`Saved/ModSDKTool`. **Cancel Mod SDK task** terminates the process tree.

The development plugin remains independent of game Runtime modules. Bundles
carry MST, MBT, and their license notices in its
`Binaries/ThirdParty/ModSDKTool/<platform>` directory.

```text
<bundle>/
  Manifest.json
  Contract/SdkContract.json
  Native/Include/...
  Native/Lib/<platform>/<architecture>/<configuration>/<target-type>/...
  Plugins/<game-sdk>/...
  Plugins/ModDevelopmentSDKEditor/...
  Build/Plan.json
  Build/NativeResults.json
```

`Built` means native archives and author-Editor plugins were built and verified.
Final game linkage, a game-target SDK Runtime module, and live API binding are
explicitly unverified in the result. A matching Runtime SDK module must exist in
the game before cooked Blueprint calls can execute. Blueprint export, trusted
host-link records, and player-context delivery remain upstream prerequisites.
Public module/type/function/parameter names form
reflection identity and must remain stable across compatible releases.

MHT now emits a matching `.sdkcontract.json`, pure C `.modabi.h`, independent
client endpoints, and a v3 manifest. See the [MHT connection protocol](../ModHeaderTool/README.md).
Use its Host and `<game>ModClient` contexts as separate library jobs. The native
connection descriptor names host queries and client Connect/Disconnect symbols;
MST requires it to be a hash-checked public Contract artifact, matches its identities
and API signatures, and copies it into both the bundle and SDK plugin at the
declared relative path. `inspect` verifies both copies and their contract identity.
The loader still needs to connect the module and deliver host-approved tokens.
MHT marks the SDK adapter contract `Unsupported` for API graphs outside this
MST adapter's shapes. The internal UE-view `.modcontract.json` remains a separate
contract and is rejected as an MST SDK contract.
