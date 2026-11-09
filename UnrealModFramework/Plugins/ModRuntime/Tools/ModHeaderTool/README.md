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
| `Contract/<game>.clientabi.json` | Versioned connection descriptor: owner modules, table/query/connection symbols, slots, signatures, hashes, and lifecycle rules |
| `Contract/<game>.sdkcontract.json` | Resolved `UMF.SdkContract` for MST's supported adapter graph |
| `<game>.generated-artifacts.json` | v2 inventory with explicit host/client compilation units and required symbol bindings |

The descriptor follows [client-abi.schema.json](Protocol/client-abi.schema.json).
The client library owner is `<game>ModClient`; it has its own MBT producer
context, output library, registration anchor, and receipt. It does not link
game-private or UE code. The Win64 adapter derives its recipe from the actual
selected toolchain and removes game include paths and forced UE definitions.

The existing `.modviews.h`/`.modcalls.h` headers are internal UE host headers,
classified as `HostHeader`. Only `.modabi.h` is published as `SdkHeader`, so MST
does not copy a header that requires the game's C++ types or ModApi module.

## Connection and lifetime

1. Resolve each descriptor's `querySymbol` in the binary owning `ownerModule`.
   Query the table using the generated ABI version, `sizeof(table)`, and ABI hash.
2. Call the matching client `connectSymbol` with that table. Connect checks the
   version, structure size, hash, and all required callbacks before binding them.
3. Game code approves a caller, scope, and permissions. The host-only
   `Mod::Api::FNativeAccess::Open` issues a nonzero context and scope token.
   `UGameModApiHost::OpenLocalPlayer` now includes these in its returned `Native`.
4. Deliver those tokens to the consumer. A getter receives context/scope and
   returns an object token; a method receives context/object and scalar inputs.
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
Cross-context tokens fail lookup. The client performs typed callback calls,
with no string lookup or generic argument interpreter on the invocation path.

The C boundary uses explicit `cdecl`, 64-bit process-local tokens, fixed-width
integer/scalar arguments, and a `uint32_t` status result. Boolean arguments are
`uint8_t` restricted to 0 or 1. Output pointers are caller-owned; outputs are zero
on failure and committed on success. Status 0 succeeds; 65536 means unconnected,
65537 means ABI mismatch, and 65538 means an unexpected native exception.

`nativeAbiHash` is SHA-256 of the descriptor before adding `nativeAbiHash`,
`contractHash`, and `projectionHash`, using sorted nlohmann JSON keys and compact
`dump()`. It covers API shapes, connection names, status codes, and lifetime rules.
The public header embeds the same version/hash for compiled consumers.

## MBT and MST handoff

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

MST consumes `.sdkcontract.json`, the Host and Client contexts, and the same v2
generated manifest. Its existing adapter supports a LocalPlayerSubsystem object
getter connected to float instance methods, including GetLocalCharacter and
AddHealth. Other currently supported MHT scalar/void/object signatures still
receive a native ABI, while the limited MST adapter contract is explicitly
`Unsupported` when it cannot represent the whole API set.

Automatic toolchain transport is currently Win64 MSVC. Other platforms require
real platform contexts and validation. This generation does not implement mod
loading, token delivery into a running mod, trusted final-link receipts, or
automatic runtime module connection; those consumers use the explicit artifacts.
