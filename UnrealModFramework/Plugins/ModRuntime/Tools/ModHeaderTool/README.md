<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# MHT client ABI generation

MHT generates UE views and the C-compatible `ContextHandlesV1` boundary. UBT compiles each host implementation in its owner and the Client implementation in the shared SDK Runtime.

## Generated connection artifacts

Paths below are relative to the fixed MHT output root:

| Artifact | Purpose |
| --- | --- |
| `SDK/Public/<game>.modabi.h` | C declarations, fixed-width types, status codes, ABI version/hash constants, and typed host function tables |
| `Host/<owner>/<owner>.modabi.cpp` | Host C endpoints, exact member calls, runtime checks, and exported table query entry |
| `Client/<game>ModClient/<game>ModClient.modclient.cpp` | Independent client endpoints, compatibility-checked Connect, Disconnect, and ReleaseObject |
| `Contract/<game>.clientabi.json` | `UMF.ClientABI` v2 connection descriptor with owner modules, table/query/connection symbols, slots, signatures, hashes, and lifecycle rules |
| `Contract/<game>.sdkcontract.json` | Resolved `UMF.SdkContract` v2 descriptor for the adapter graph supported by ModSDKTool (MDT) |
| `<game>.generated-artifacts.json` | v3 inventory with explicit host/client compilation units and required symbol bindings |

MHT identifies the C implementation as `ClientSource` and declares its connection symbols under `<game>ModClient`. MDT places that implementation in the shared UE Runtime module. [Client ABI schema](Protocol/client-abi.schema.json)

The `.modviews.h` and `.modcalls.h` headers are private UE `HostHeader` artifacts.
Public `SdkHeader` artifacts include `.modabi.h` and, when applicable, the error
catalog and independent extension ABI headers. MDT copies only verified artifacts
with public roles.

## Connection and lifetime

1. Resolve each descriptor's `querySymbol` in the binary owning `ownerModule`, then
   query the table using the generated ABI version, `sizeof(table)`, and ABI hash.
2. Call the matching client `connectSymbol` with that table. Connect verifies the
   version, structure size, hash, and all required callbacks before binding them.
3. Game code approves a caller, scope, and permissions. The host-only
   `Mod::API::FNativeAccess::Open` issues nonzero context and scope tokens;
   `UModAPIHostSubsystem::OpenLocalPlayer` binds the generated LocalPlayer provider
   and returns these tokens in `Native`.
4. Deliver those tokens to the consumer. A getter receives context and scope tokens
   and returns an object token; a method receives context and object tokens along
   with its inputs. Every host call re-verifies the thread, caller lifetime, type,
   scope, and permissions.
5. Revoke that caller when its mod stops; scope and registry teardown also revoke
   tokens. ReleaseObject explicitly invalidates all copies of that token without
   owning or destroying its UObject. Repeated projections reuse live bindings.
6. Disconnect the SDK client and quiesce callers before unloading a host or SDK
   module. Disconnect clears bound callbacks; it does not wait for in-flight calls
   or revoke other callers sharing that client module.

Connection tables are module-wide, whereas identity, scope, permissions, and object
tokens remain isolated per caller. No global current player or world is inferred.
Tokens are never raw addresses and are not reused during the Runtime module's
lifetime; cross-context tokens fail lookup. The client dispatches calls through typed
callbacks.

The C boundary uses explicit `cdecl`, 64-bit process-local tokens, fixed-width
integer/scalar arguments, and a `uint32_t` status code. Boolean arguments are passed
as `uint8_t` restricted to 0 or 1. Output pointers are caller-owned; outputs are
zeroed on failure and committed on success. Status 0 indicates success, 65536
indicates unconnected, 65537 indicates an ABI mismatch, and 65538 indicates an
unexpected native exception.

`nativeABIHash` is the SHA-256 digest of the descriptor before adding `nativeABIHash`,
`contractHash`, and `projectionHash`, serialized with sorted nlohmann JSON keys and
compact `dump()`. It covers API shapes, connection names, status codes, and lifetime
rules. The public header embeds the same version and hash constants for compiled
consumers.

## World extension artifacts

- `Host/<owner>/<owner>.modextensions.h` provides typed `Invoke` and `Publish` helpers for C++ call sites. The generated host bridge registers both C++ and Blueprint extension definitions and implements C++ members that declare `HostEndpoint`.
- The `extensions` section of `Contract/<game>.sdkcontract.json` records IDs, signatures, authorization, stage or delivery rules, and payload projections. The packager uses it to validate extension manifest v2.
- When lifecycle inputs and extension definitions are present, `SDK/Public/ModExtensionABI.h` and `SDK/Public/<game>.modextensionsabi.h` expose the independent DLL callback contract, IDs, signatures, and wire layouts. They work alongside the separate `ContextHandlesV1` tables used for generated API calls.

A Hook takes a single payload and returns the same type; an Event takes a single payload and returns `void`. Supported payloads include `float`, `double`, `int32`, `int64`, `bool`, `uint8`, supported reflected value structs, and UObjects with a generated World view. Struct projection copies declared public fields, while object projection is read-only for the callback's lifetime. Blueprint scalar APIs and connected Hook/Event declarations come from the Editor snapshot. See the [Host export procedure](../../Source/ModRuntimeUht/README.md).

`MOD_EXPORT_HOOK()` enables automatic dispatch for an out-of-line scalar member. The generated ID combines the World type ID and the dashed member name (for example, `hook/game.character/change-health`). A Hook defines `Pre` (with `ModifyInputs` and `Cancel`), `On` (with `Replace`), and `Post` (observation). The base grant uses the Hook ID, and operation grants append the operation suffix to that ID. MHT and MST emit the substitution map under `Diagnostics/HookSources.json`, and the UBT driver compiles the source copies for the selected target and architecture. See [Automatic Hook builds](../../Source/ModRuntimeUht/README.md#automatic-hook-builds).

Pass the bare flags `NoPre`, `NoOn`, and `NoPost` in any order to omit those stages. `NoPre` also removes `ModifyInputs` and `Cancel`. `NoOn` removes `Replace`, so calls that pass `Pre` run the original game function. When all three flags are set, MHT omits the Hook and its former IDs from the SDK, and MST leaves that function body intact.

When renaming a member, list its former names in the same marker—for example, `MOD_EXPORT_HOOK(NoOn, CompatibleNames = ("ChangeHealth", "UpdateHealth"))`. Every name uses the same stage policy and shares the same ordered handler list. Renaming preserves each former ID, signature, and grant name as long as the function signature and stage policy stay compatible; changing the stage policy changes the contract signature for every name. MHT rejects duplicate IDs, including names that map to the same dashed spelling.

### Generated C++ Event publishers

Set `HostEndpoint` to the name of a `Mod::Extension::FEndpoint` member to emit the annotated member's definition in `<owner>.modhost.cpp`. The generated Event body forwards its argument to `Publish`. C++ declarations can also call the helpers directly from their own definitions. `HostCommitPayload` is reserved in the current generator and requires `HostEndpoint`.

## API compatibility names

An explicit `MOD_EXPORT_API` generates an ID from the owner's type ID and method name
(for example, `api/game.character/increasehealth`). Adding
`CompatibleNames = ("AddHealth")` publishes a separate `api/game.character/addhealth`
entry for the same implementation, and the SDK exposes both function names with the
same signature and policy.

When `CompatibleNames` is omitted, MHT uses the `FunctionRedirects` resolved from
Unreal's Engine and enabled plugin configurations. An exact same-class redirect to an
exported host `UFUNCTION` contributes its old name; qualify the redirect with the
reflected class and module when the function name is shared. A rename chain
contributes every previous name. An explicit `CompatibleNames = ()` selects an empty
compatibility list.

MHT reports invalid names, collisions, and invalid applicable redirects at the
declaration or INI entry. UBT builds refresh the default configuration snapshot
automatically; for standalone MHT invocations, run `ModExportOptions` after editing
redirects. Always build the host and SDK from the same generated contract.

## FullView and object inputs

`FullView` is an optional `MOD_EXPORT_API` field accepting quoted, qualified C++ class
names (for example, `FullView = ("USkeletalMesh")`). Each name must match a UObject
pointer parameter in the annotated signature. Generation groups requests by class,
collects public concrete instance methods from that class and its public bases in the
target compilation, and preserves explicitly exported methods under their existing
contracts. Synthesized members require a writable binding, the selecting declarations'
permissions, and a host grant named `FullView.<TypeId>`. If any member lacks a
projection, generation fails and reports each affected member. Classes reached through
those members follow their ordinary projection rules.

UE callers pass typed UObject pointers. `ContextHandlesV1` uses `const char*` for
borrowed UTF-8 object path bytes and `uint32_t` for their length, bounded to 4,096
bytes; null uses `nullptr` and zero length. The host resolves an already-loaded
object, checks its class and world, and borrows it for the duration of the synchronous
call. Object results are returned as host-owned view tokens. Public engine classes
resolve headers under the `Engine` path root in the build input.

## MBT and MDT handoff

MHT emits four units per host owner—bridge, ABI endpoints, verification, and UE view
implementations—while the independent client has its own compilation unit. Every
source is listed explicitly in `compilationUnits`. `requiredSymbols` records both host
and client endpoints with matching ExportId and signature/projection hashes, and
ExportId uniqueness is checked per library owner. Host table queries, client
connection functions, and release functions are also required symbols. The host
registration anchor retains the ABI object through final linking.

UHT supplies `buildInput`, `outputDirectory`, `hostCompiler` set to `UBT`, and `sourceTool`. MHT publishes `UMF.GeneratedArtifacts` v4 with the owner, role and hash of the host, verification and Client sources.

MDT consumes `.sdkcontract.json` and each target's v4 `UMF.GeneratedArtifacts` manifest. It retains the existing object getter and float
method adapters while also emitting typed methods with scalar and public UE object
inputs. When the SDK contract cannot represent the full API set, it records `Unsupported`.

On Win64 x64 MSVC, the UBT adapter prepares the inputs and MBT verifies the final
linked image. A generated SDK can acquire an authorized Blueprint World provider
through its active `UModInstance`, while game integration supplies other contexts such
as LocalPlayer. Independent native callbacks connect through the runtime during World
preparation. See [UHT integration](../../Source/ModRuntimeUht/README.md) and
[Native extension packaging](../ModSDKTool/Resources/DevelopmentEditor/README.md#independent-native-callbacks).
