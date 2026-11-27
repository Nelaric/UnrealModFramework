<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModHeaderTool (MHT)

ModHeaderTool (MHT) parses active C++ and Blueprint export declarations for a specific UE build target, generating typed Unreal views, host C++ bridges, public SDK contracts, and the C-compatible `ContextHandlesV1` ABI boundary. UBT compiles each generated host implementation directly inside its owning game module, while ModSDKTool (MDT) packages the generated Client source into the shared SDK Runtime module built for both the host game and the mod authoring Editor.

## Build and commands

Build MHT from the repository root with `.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModHeaderTool` on Windows, or `sh UnrealModFramework/Plugins/ModRuntime/Tools/Build.sh --target ModHeaderTool` on Linux and macOS:

```text
ModHeaderTool discover <project.uproject>
ModHeaderTool check-options <project.uproject> <MetadataOptions.json>
ModHeaderTool scan <project.uproject> <MetadataOptions.json>
ModHeaderTool generate <GenerationRequest.json>
```

- `discover`: Prints the selected UE modules for the project and indicates whether each contains export markers.
- `check-options`: Validates the Editor metadata snapshot, returning exit code `2` when it needs to be regenerated via `ModExportOptions`.
- `scan`: Checks export macro syntax and field values lexically and writes a JSON report to stdout.
- `generate`: Runs full semantic analysis and code generation using `libclang`, the target's real UBT compilation database, and standard UHT generated headers. During normal UE builds, the UHT adapter writes `GenerationRequest.json` and invokes `generate` automatically (see [ModRuntime UHT Adapter](../../Source/ModRuntimeUht/README.md)).

## Generated connection artifacts

All paths below are relative to the target variant's MHT output root (`Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>/`):

| Artifact | Purpose |
| --- | --- |
| `SDK/Public/<game>.modabi.h` | Pure C declarations, fixed-width types, status codes, ABI version/hash constants, and typed host function tables |
| `Host/<owner>/<owner>.modabi.cpp` | Host C entry points, exact C++ member calls, runtime thread/scope/grant checks, and the exported table-query function |
| `Client/<game>ModClient/<game>ModClient.modclient.cpp` | Standalone client stubs plus compatibility-checked `Connect`, `Disconnect`, and `ReleaseObject` routines |
| `Contract/<game>.clientabi.json` | `UMF.ClientABI` v2 connection descriptor listing owner modules, query/connect symbols, table slots, signatures, hashes, and lifetime rules ([schema](Protocol/client-abi.schema.json)) |
| `Contract/<game>.sdkcontract.json` | Resolved `UMF.SdkContract` v2 descriptor consumed by MDT to generate UE wrappers |
| `<game>.generated-artifacts.json` | `UMF.GeneratedArtifacts` v4 manifest recording target identity, file SHA-256 digests, and compilation unit roles/owners ([schema](../ModBuildTool/Protocol/generated-artifacts.schema.json)) |

In `<game>.generated-artifacts.json`, MHT tags the client implementation with role `ClientSource` under logical owner `<game>ModClient`; MDT later places that source inside the SDK's concrete `<pluginName>Runtime` module. Private UE headers (`.modviews.h` and `.modcalls.h`) carry the internal `HostHeader` role, whereas public headers (`.modabi.h`, plus any error catalog and standalone extension ABI headers) carry `SdkHeader`—MDT copies only hash-verified artifacts marked with public roles.

## Connection and lifetime

At runtime, the host and shared SDK Runtime connect and manage caller lifetimes in six steps:

1. **Query the host table:** Locate the binary that owns `ownerModule`, resolve its exported `querySymbol`, and request the host function table by passing the generated ABI version, `sizeof(table)`, and `nativeABIHash`.
2. **Bind the client module:** Pass the returned table to the matching client `connectSymbol`, which verifies the version, struct size, ABI hash, and every required function pointer before storing them.
3. **Issue caller and scope tokens:** Host game code authorizes a mod caller, scope, and permission set—`Mod::API::FNativeAccess::Open` mints nonzero context and scope tokens on the host, while `UModAPIHostSubsystem::OpenLocalPlayer` binds the generated `LocalPlayer` provider and returns those tokens via `Native`.
4. **Invoke typed endpoints:** Hand the tokens to the mod consumer. Object getters take `(context, scope)` and return an object token; instance methods take `(context, object, ...)` and re-check the calling thread, caller liveness, object type, scope, and permissions on every call.
5. **Revoke or release tokens:** Stopping a mod revokes its caller context immediately, and tearing down a scope or registry invalidates associated tokens. Calling `ReleaseObject` invalidates all copies of a specific object token without owning or destroying the underlying `UObject` (repeated projections of a live object reuse its existing binding).
6. **Module shutdown vs. single-mod unload:** Unloading a single mod revokes only that mod's caller token while leaving the module-wide client table connected for other active mods. Unloading the host or SDK module itself requires quiescing all in-flight calls first and then disconnecting the module-wide table.

While function tables are shared per module, caller identities, scopes, permissions, and object tokens are strictly isolated per mod—no ambient "current world" or "current player" is ever assumed. Tokens are opaque 64-bit IDs (never raw pointers), are never recycled during the Runtime module's lifetime, and fail lookup if passed across caller contexts.

### Wire conventions and ABI hashing

Across the C boundary, functions use explicit `cdecl` calling conventions, 64-bit process-local tokens, fixed-width integer/floating-point scalars, `uint8_t` booleans restricted to `0` or `1`, and a `uint32_t` status return (`0` = success, `65536` = unconnected, `65537` = ABI mismatch, `65538` = unexpected native exception). Caller-supplied output buffers are zeroed on error and written only on success.

`nativeABIHash` is computed as the SHA-256 digest of the `UMF.ClientABI` JSON object *prior* to inserting `nativeABIHash`, `contractHash`, and `projectionHash`, serialized with sorted keys and compact `nlohmann::json::dump()`. Both the JSON contract and `<game>.modabi.h` embed the exact same version and hash constants.

## World extension artifacts

When a target declares World Hooks or Events, MHT emits three sets of extension artifacts:

- **`Host/<owner>/<owner>.modextensions.h` and host bridge sources:** Provide typed C++ `Invoke` and `Publish` helpers, register both C++ and Blueprint extension definitions at startup, and generate out-of-line bodies for C++ Events that specify `HostEndpoint`.
- **`extensions` in `Contract/<game>.sdkcontract.json`:** Records extension IDs, signature hashes, required grants, stage/delivery rules, and payload projections; the `.mod` packager validates v2 extension manifests against this section.
- **`SDK/Public/ModExtensionABI.h` and `SDK/Public/<game>.modextensionsabi.h`:** Emitted whenever lifecycle metadata and extension definitions are present, giving standalone native DLLs their callback registration contract, IDs, signature constants, and wire struct layouts alongside `ContextHandlesV1`.

A Hook accepts a single payload and returns the same type; an Event accepts a single payload and returns `void`. Supported payloads cover `float`, `double`, `int32`, `int64`, `bool`, `uint8`, reflected value structs (copying public fields), and `UObject` pointers that have a generated read-only World view valid for the duration of the callback. Blueprint scalar APIs and connected Blueprint Hooks/Events merge in from the Editor snapshot (see the [UHT adapter guide](../../Source/ModRuntimeUht/README.md)).

### Automatic C++ Hooks and stage flags

Annotating an out-of-line member function with `MOD_EXPORT_HOOK()` derives an extension ID from the owner's World type ID and kebab-cased member name (for example, `hook/game.character/change-health`). By default, a C++ Hook enables all three stages—`Pre` (`ModifyInputs` and `Cancel`), `On` (`Replace`), and `Post` (read-only observation)—using the Hook ID as the base grant and `<hook-id>/<operation>` for write grants. MHT and MST record rewritten source paths in `Diagnostics/HookSources.json` so MBT can compile them for the active target (see [Automatic Hook builds](../../Source/ModRuntimeUht/README.md#automatic-hook-builds)).

You can pass bare stage-omission flags in any order:

- `NoPre` disables `Pre` along with `ModifyInputs` and `Cancel`.
- `NoOn` disables `On` and `Replace`, guaranteeing that any call not cancelled in `Pre` executes the original C++ implementation.
- `NoPost` disables `Post`.
- Specifying `NoPre, NoOn, NoPost` together omits the Hook (and any former IDs) from the SDK entirely and leaves the original function unpatched.

When renaming a hooked C++ method, list its previous names via `CompatibleNames = ("ChangeHealth", "UpdateHealth")`. Current and former names share one stage policy and one ordered callback chain; as long as the function signature and stage flags stay compatible, every former ID keeps its contract and grant names intact (changing stage flags alters the contract signature across all names). Duplicate IDs—including distinct C++ spellings that normalize to the same dashed ID—fail generation.

### Generated C++ Event publishers

Setting `HostEndpoint` on `MOD_EXPORT_EVENT` to the name of a `Mod::Extension::FEndpoint` member causes MHT to emit the annotated C++ function's body in `<owner>.modhost.cpp`, forwarding its payload straight to `Publish`. Alternatively, omit `HostEndpoint` and call the generated `.modextensions.h` helper from your own `.cpp` definition. (`HostCommitPayload` is reserved for future use and requires `HostEndpoint`.)

## API compatibility names

An explicit `MOD_EXPORT_API` derives its default ID from the owner's type ID and method name (such as `api/game.character/increasehealth`). Adding `CompatibleNames = ("AddHealth")` publishes an additional `api/game.character/addhealth` entry backed by the exact same implementation, exposing both function names in the generated SDK with identical signatures and permissions.

If `CompatibleNames` is omitted, MHT automatically consults Unreal's `FunctionRedirects` across Engine and enabled plugin INI files: any exact same-class redirect pointing to an exported host `UFUNCTION` contributes its former name (including multi-step rename chains; qualify shared names with their reflected class and module). Pass an explicit empty list `CompatibleNames = ()` to opt out of redirect inference.

Invalid identifiers, ID collisions, or malformed applicable redirects are reported directly against the declaration or INI entry. Standard UBT builds refresh the options/redirect snapshot automatically; when invoking MHT standalone after editing INI redirects, re-run `ModExportOptions` first. Always build the host binaries and the published SDK from the same generated contract.

## FullView and object inputs

On `MOD_EXPORT_API`, the optional `FullView` field accepts quoted, qualified C++ class names matching `UObject*` parameters in that method's signature—for example, `FullView = ("USkeletalMesh")`. MHT merges `FullView` requests per class, gathers concrete public instance methods declared on that class and its public base classes in the active target AST, and synthesizes view bindings while leaving any explicitly exported methods under their own contracts. Calling a synthesized member at runtime requires a writable binding, the permissions of the declaration(s) that requested `FullView`, and the host grant `FullView.<TypeId>`. If any public method on the requested class cannot be projected, generation fails and lists every unsupported member rather than silently dropping methods.

Across the boundary, UE SDK callers pass typed `UObject*` pointers directly, while raw `ContextHandlesV1` callers pass a borrowed UTF-8 object path (`const char*` plus `uint32_t` byte length up to `4096`, or `nullptr` and `0` for null). On each synchronous call, the host resolves an already-loaded `UObject`, checks its class and World, and borrows it for the call's duration; object return values come back as host-managed view tokens. Headers for public engine classes resolve relative to the `Engine` root in the build input.

## MBT and MDT handoff

To tie the toolchain together:

1. **MHT** emits host bridges, C ABI stubs, view implementations, and compile-verification sources, recording every unit's `role` and `ownerModule` in `compilationUnits` and pairing Host/Client symbols in `requiredSymbols` by `ExportId`, `signatureHash`, and `projectionHash`.
2. **UHT & MBT** verify the v4 manifest, register `*.umfhost.cpp` wrappers in their owning UE modules, swap rewritten Hook copies into UBT compile actions, and write `host-link.json` after checking final desktop binaries (see [Final-link association](../ModBuildTool/README.md#final-link-association)).
3. **MDT** reads `.sdkcontract.json` alongside each target's v4 manifest to generate the host and author SDK plugins—supporting object getters, float methods, and general typed methods with scalar and public `UObject*` inputs (marking contracts `Unsupported` if any API shape falls outside its adapter set).

At runtime, mods acquire authorized World Blueprint providers via their active `UModInstance`, receive other scopes such as `LocalPlayer` from host gameplay code, and register standalone native C ABI callbacks during World preparation (see [ModRuntime UHT Adapter](../../Source/ModRuntimeUht/README.md) and [Independent native callbacks](../ModSDKTool/Resources/DevelopmentEditor/README.md#independent-native-callbacks)).
