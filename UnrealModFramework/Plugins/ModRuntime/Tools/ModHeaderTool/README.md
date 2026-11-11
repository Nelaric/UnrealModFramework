<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# MHT client ABI generation

MHT generates a C-compatible `ContextHandlesV1` boundary together with its UE
view backend. MBT compiles all generated implementations independently. UBT
only consumes the host declarations and links the owning module's archive.

## Generated connection artifacts

Paths are relative to the fixed MHT output root:

| Artifact | Purpose |
| --- | --- |
| `SDK/Public/<game>.modabi.h` | C declarations, fixed-width types, statuses, ABI version/hash constants, and typed host tables |
| `Host/<owner>/<owner>.modabi.cpp` | Host C endpoints, exact member calls, checks, and exported table query |
| `Client/<game>ModClient/<game>ModClient.modclient.cpp` | Independent client endpoints, checked Connect, Disconnect, and ReleaseObject |
| `Contract/<game>.clientabi.json` | `UMF.ClientABI` v2 connection descriptor with owner modules, table/query/connection symbols, slots, signatures, hashes, and lifecycle rules |
| `Contract/<game>.sdkcontract.json` | Resolved `UMF.SdkContract` v2 for the adapter graph supported by ModSDKTool (MDT) |
| `<game>.generated-artifacts.json` | v3 inventory with explicit host/client compilation units and required symbol bindings |

The descriptor follows [client-abi.schema.json](Protocol/client-abi.schema.json).
The client library owner is `<game>ModClient`; it has its own MBT producer
context, output library, registration anchor, and receipt. It does not link
game-private or UE code. The Win64 adapter derives its recipe from the actual
selected toolchain and removes game include paths and forced UE definitions.

The existing `.modviews.h`/`.modcalls.h` headers are internal UE host headers,
classified as `HostHeader`. Only `.modabi.h` is published as `SdkHeader`, so MDT
does not copy a header that requires the game's C++ types or ModAPI module.

## Connection and lifetime

1. Resolve each descriptor's `querySymbol` in the binary owning `ownerModule`.
   Query the table using the generated ABI version, `sizeof(table)`, and ABI hash.
2. Call the matching client `connectSymbol` with that table. Connect checks the
   version, structure size, hash, and all required callbacks before binding them.
3. Game code approves a caller, scope, and permissions. The host-only
   `Mod::API::FNativeAccess::Open` issues a nonzero context and scope token.
   `UModAPIHostSubsystem::OpenLocalPlayer` binds the generated LocalPlayer provider and returns these tokens in `Native`.
4. Deliver those tokens to the consumer. A getter receives context/scope and
   returns an object token; a method receives context/object and its inputs.
   Every host call rechecks thread, caller lifetime, type, scope, and permissions.
5. Revoke that caller when its mod stops. Scope and registry teardown also revoke
   tokens. ReleaseObject explicitly invalidates all copies of that token without
   owning or destroying its UObject. Repeated projections reuse live bindings.
6. Disconnect the SDK client and quiesce callers before unloading a host or SDK
   module. Disconnect clears callbacks; it does not wait for in-flight calls or
   revoke other callers sharing that client module.

Connection tables are module-wide; identity, scope, permissions, and object
tokens remain per caller. No global current player/world is inferred. Tokens
are never addresses and are not reused during the Runtime module lifetime.
Cross-context tokens fail lookup. The client dispatches through typed callbacks.

The C boundary uses explicit `cdecl`, 64-bit process-local tokens, fixed-width
integer/scalar arguments, and a `uint32_t` status result. Boolean arguments are
`uint8_t` restricted to 0 or 1. Output pointers are caller-owned; outputs are zero
on failure and committed on success. Status 0 succeeds; 65536 means unconnected,
65537 means ABI mismatch, and 65538 means an unexpected native exception.

`nativeABIHash` is SHA-256 of the descriptor before adding `nativeABIHash`,
`contractHash`, and `projectionHash`, using sorted nlohmann JSON keys and compact
`dump()`. It covers API shapes, connection names, status codes, and lifetime rules.
The public header embeds the same version/hash for compiled consumers.

## API compatibility names

An explicit `MOD_EXPORT_API` generates an ID from the owner's type ID and the
method name, for example `api/game.character/increasehealth`. Add
`CompatibleNames = ("AddHealth")` to publish a separate
`api/game.character/addhealth` entry for the same implementation. The SDK
publishes both function names with the same signature and policy.

With `CompatibleNames` omitted, MHT uses the `FunctionRedirects` resolved by
Unreal's Engine and enabled plugin configuration. An exact same-class
redirect to an exported host `UFUNCTION` contributes its old name. Qualify
the redirect with the reflected class and module when the function name is
shared. A rename chain contributes each previous name. An explicit
`CompatibleNames = ()` selects an empty compatibility list.

MHT diagnoses invalid names, collisions, and invalid applicable redirects at
the declaration or INI entry. UBT builds refresh the default configuration
snapshot automatically. For standalone MHT invocations, run `ModExportOptions`
after editing redirects. Build the host and SDK from the same generated contract.

## FullView and object inputs

`FullView` is an optional `MOD_EXPORT_API` field containing quoted, qualified
C++ class names, for example `FullView = ("USkeletalMesh")`. Each name must
match a UObject pointer parameter in the annotated signature. Generation
groups requests by class, collects public concrete instance methods from the
class and its public bases in the target compilation, and keeps explicitly
exported methods under their existing contracts. Synthesized members require
a writable binding, the selecting declarations' permissions, and a host grant
named `FullView.<TypeId>`.
A missing projection fails generation with the name of each affected member.
Classes reached through those members use their ordinary projection rules.

UE callers pass typed UObject pointers. ContextHandlesV1 uses `const char*`
for borrowed UTF-8 object path bytes and `uint32_t` for their length, bounded
to 4,096 bytes. Null uses `nullptr` and zero length. The host
resolves an already loaded object, checks its class and world, and borrows it
for the synchronous call. Object results use host-owned view tokens. Public
engine classes use headers under the `Engine` path root in the build input.

## MBT and MDT handoff

MHT emits four units per host owner: bridge, ABI endpoints, verification, and UE
view implementations; the independent client has its own compilation unit.
Every source is explicitly listed in `compilationUnits`. `requiredSymbols`
records both host and client endpoints with the same ExportId and signature/
projection hashes. ExportId uniqueness is checked per library owner. Host table
queries, client connection functions, and release functions are also required.
The host registration anchor retains the ABI object for final linking.

The UHT adapter supplies both actual contexts through `--host-contexts` and runs
MBT for both libraries. Standalone `generate` requires a matching context set for every host module
and the standalone client before it can publish a Complete manifest.

MDT consumes `.sdkcontract.json`, the Host and Client contexts, and the same v3
generated manifest. It keeps the existing object getter and float method
adapters and also emits typed methods with scalar and public UE object inputs.
The SDK contract records `Unsupported` when it cannot represent the whole API set.

Automatic toolchain transport is currently Win64 MSVC. Other platforms require
real platform contexts and validation. This generation does not implement mod
loading, token delivery into a running mod, trusted final-link receipts, or
automatic runtime module connection; those consumers use the explicit artifacts.
