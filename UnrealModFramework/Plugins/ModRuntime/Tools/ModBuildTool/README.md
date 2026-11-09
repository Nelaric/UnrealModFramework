<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModBuildTool

MBT independently compiles generated host binding sources into one native static archive per owning module. The game build links that archive into the owning binary without adding MHT sources to UBT's C++ compilation list. The intended authoring interface remains an export marker followed by the original function declaration.

This implementation provides archive builds, required-symbol verification, explicit public artifacts for MST, and association with trusted successful final-link records. The [UHT adapter](../../Source/ModRuntimeUht/README.md) now invokes MHT generation and MBT independent compilation for the sample UE 5.6 Win64 MSVC targets, and private External module rules attach each host archive to its owner. A general UBT action extension and trusted final-link record production remain future work. An MHT `scan` report is not a generated-artifact manifest. `Built` proves archive construction; `Linked` proves the checked association with a trusted record and actual image bytes. Neither proves gameplay behavior.

## Build and commands

From the repository root:

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModBuildTool
```

An existing CMake build directory may also build the `ModBuildTool` target directly. Invoke the resulting executable:

```text
ModBuildTool plan --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --expected-input <plan.inputFingerprint> --output BuildResult.json
ModBuildTool verify-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool finalize-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --link-receipt Target.link.json --output LinkedArtifacts.json
ModBuildTool hash-context --file HostCompileContext.json
ModBuildTool hash-file --file <path>
ModBuildTool inspect-archive --file <archive> --platform Win64 --architecture x64 --anchor <symbol> --symbol <api-symbol-1> --symbol <api-symbol-2>
ModBuildTool inspect-binary --file <image> --platform Win64 --architecture x64 --kind Executable
```

`plan` validates inputs without compiling or creating directories. `build-host` locks the archive, revalidates an optional planned fingerprint, and returns `Built` or `UpToDate`. `verify-host` fails for stale inputs or artifacts. Exit code `0` means success; `1` means invalid input, build/I/O failure, or stale verification. Results are JSON on stdout or `--output`; child diagnostics go to stderr.

## Compilation context

See [host-context.schema.json](Protocol/host-context.schema.json). The UHT adapter transports the real owner MSVC recipe from an actual UBT compilation database and verifies its producer definitions. Other integrations must likewise export the real owning-module context. MBT does not infer it from a default CMake compiler or a hand-written approximation.

The context requires `format: "UMF.ModHostContext"`, `schemaVersion: 1`, `ownerModule`, `moduleApiMode: "Producer"`, an undecorated C `registrationAnchor`, and `target` fields `name`, `type`, `configuration`, `linkType`, `platform`, and `architecture`. Explicit `lto`, `pch`, and `unity` values must be `false` in this version.

Optional `owningBinary` specifies the real UBT binary's stable `id`, context-relative `path`, and `kind` (`Executable` or `DynamicLibrary`). It is mandatory for `finalize-host`.

`workingDirectory` is the real process working directory. `outputLibrary` is outside the managed generation root and ends in `.lib` or `.a`. Relative context paths resolve against the context directory. `dependencies` declares forced includes, recipe inputs, and other toolchain files. `environmentMode: Explicit` runs the tools with the declared `environment`. The default `Inherit` mode applies those values to the caller's environment. Plans and receipts expose the effective `environmentHash`.

Both `compiler` and `archiver` specify a tool `path`, an `arguments` array, and `responseFormat` (`msvc` or `gnu`). Compiler arguments must contain `{source}`, `{object}`, and `{dependencies}`. Archiver arguments require `{output}` and a standalone `{objects}` array entry, expanded to all current object paths. MBT invokes the executable directly with a response file, without a shell.

`msvc` response files use UTF-16LE with BOM and Windows quoting and require a Windows host. An option and its separate value stay on the same line. `gnu` uses UTF-8 and GNU response quoting. Compiler `dependencyFormat` is `msvc-json` for `/sourceDependencies`, or `make` for complete Make-style dependency output such as `-MD -MF {dependencies}`. The adapter must ensure real dependencies, including system headers, are covered. Batch files and shell recipes are unsupported.

## Generated-artifact contract

New producers use [generated-artifacts.schema.json](Protocol/generated-artifacts.schema.json): `UMF.GeneratedArtifacts` version `2`, status `Complete`, and zero `unresolvedTypeCount`. MBT retains [legacy v1](Protocol/generated-artifacts-v1.schema.json) build compatibility; final-link association requires v2. Bootstrap and scan reports are rejected.

In v2, `hostBuildContexts` maps every compilation owner to its parsed context hash. Use `hash-context` for the compact sorted JSON digest. The v1 single-context fallback is `buildContextHash`. MBT checks every v2 `inputFiles` hash and re-enumerates `watchedDirectories` before accepting the batch. `GeneratedInputsStale` identifies the path that requires a new MHT run. `generationId`, `inputHash`, `contractHash`, `projectionHash`, and `nativeAbiHash` are lowercase SHA-256 strings.

`generatedFiles` lists relative paths, `contentHash`, and `ownerModule` (nullable for shared artifacts). Paths are unique, contain no `..`, and must remain within the manifest directory after resolving symlinks. Every file must match its hash. `compilationUnits` explicitly lists `ownerModule` and `source`; MBT only compiles listed `.cpp`, `.cc`, or `.cxx` files belonging to this owner. Empty export sets still require a generated unit defining the stable registration anchor.

## Cache and publication

Each v2 generated file requires a role: `HostBinding`, `HostRegistration`, `CompileVerification`, `HostHeader`, `SdkHeader`, `Contract`, `SdkResource`, or private `Diagnostic`. Plans, build results, and linked results expose only this owner's and shared `SdkHeader`/`Contract`/`SdkResource` files through `publicArtifacts`, with absolute `sourcePath`, normalized `relativePath`, hash, role, and owner. SDK headers require `.h`/`.hpp`; contracts require `.json`. Compilation units cannot also be public artifacts. Unspecified or unknown legacy roles remain private.

`requiredSymbols` is an array of `{ "ownerModule": "...", "symbol": "..." }` records with optional `exportId`, `signatureHash`, and `projectionHash`. Symbols are undecorated C identifiers. Duplicate owner/symbol pairs and duplicate export IDs within one owner fail; a Host and Client owner may describe the same logical ExportId. MBT adds the stable context registration anchor and validates the complete owner-specific set in one archive pass. Mach-O inspection adds the platform C prefix. Empty export sets may use an empty array, but still require the anchor. Legacy v1 may omit the array and retain anchor-only verification.

The fingerprint covers the MBT executable/version, input documents, generated files, compiler and archiver, declared dependencies, previously reported transitive compiler dependencies, and effective child environment. A changed source or existing include triggers compilation and discovery of new include dependencies.

`<library>.mbt.json` is the successful commit record, containing target, owner, context and generation identities, input fingerprint, archive hash, anchor, and compiler dependencies. Cache reuse requires matching inputs, archive hash, and archive validation. A malformed or missing receipt is never a successful cache entry.

Builds hold an OS lock and use a newly owned temporary directory. A dirty module recompiles its generated units and creates an entirely new archive, preventing deleted archive members from surviving. Publication replaces the archive and atomically commits its receipt last. Interrupted publication cannot pass subsequent hash verification.

Archive validation checks native objects, target architecture, and every required symbol in the index, reporting the complete missing set. Formats are Win64 COFF, Linux ELF, and macOS Mach-O, with `x64` and `arm64` targets. Import-library members, thin archives, unknown objects, and mismatched architectures are rejected. LTO, PCH, unity, and C++ modules/header units are unsupported. Platform support requires actual platform validation; a Windows build does not establish Linux or macOS acceptance.

## Two-API producer example

MHT emits a v2 manifest with four host units (bridge, C ABI endpoints, verification, and UE views) plus one independent client unit owned by `<game>ModClient`. The context anchors are `UMF_ModHost_<owner>` and `UMF_ModClient_<game>`. The [MHT ABI protocol](../ModHeaderTool/README.md) supplies the concrete public header, Host/Client symbol records, and MST contract.

The local-character and add-health APIs are ordinary manifest data, not special MBT behavior. A simplified v2 symbol fragment for `ExportTest` can declare; the actual MHT names are read from its generated descriptor:

```json
{
  "requiredSymbols": [
    { "ownerModule": "ExportTest", "symbol": "UMF_ExportTest_GetLocalCharacter", "exportId": "Game.Player.GetLocalCharacter" },
    { "ownerModule": "ExportTest", "symbol": "UMF_ExportTest_AddHealth", "exportId": "Game.Character.AddHealth" }
  ]
}
```

MBT also requires `UMF_ModHost_ExportTest` from the context. MHT marks `SDK/Public/<game>.modabi.h` as `SdkHeader`, generated JSON contracts as `Contract`, and implementations as private compilation units. Host and Client records associate each ExportId with matching signature/projection hashes; internal UE-view headers have the private `HostHeader` role. This is a fragment; a complete input still needs all hashes and compilation units. MST receives the public file list and verifies hashes again when copying. MBT does not publish private host sources or build the final authoring plugin.

## Final-link association

`finalize-host` consumes [host-link-receipt.schema.json](Protocol/host-link-receipt.schema.json) and produces [linked-artifacts.schema.json](Protocol/linked-artifacts.schema.json). For the UE 5.6 Win64 x64 adapter, the UBT post-build step runs `finalize-ue-target`. It checks the UBT target receipt and the archive's unique PE export before publishing the link record and linked artifacts beside the archive. Other link adapters can supply their own successful link record to `finalize-host`.

The record requires `UMF.ModHostLinkReceipt` v1, `status: "Succeeded"`, an exact `target` match, and `binary` fields `id`, `path`, `kind`, `contentHash`, and unique `ownerModules`. `hostLibraries` lists the real final-link archives, with one record per owner: `ownerModule`, `path`, `contentHash`, `hostReceiptHash`, `contextHash`, `generationId`, `registrationAnchor`, and `anchorRetained: true`. Record paths resolve against its directory.

MBT requires a current host build, exact owning binary identity, correct module membership, matching library/receipt hashes and batch/context identities, anchor retention declaration, actual image hash, and native image kind/architecture. Supported images are PE, ELF, and thin Mach-O; ELF distinguishes PIE executables from shared libraries. Fat Mach-O is unsupported. Input changes during verification fail.

The `UMF.ModHostLinkedArtifacts` / `Linked` output includes public files, contract hashes, required symbols, binary/archive identities, and hashes/paths of both receipts for MST aggregation. Link success and retention are assertions of the trusted adapter: MBT verifies associations and bytes, not JSON authorship or actual runtime registration. It does not infer successful linkage from arbitrary strings in a binary.

## UE 5.6 integration

The current adapter runs MHT and MBT synchronously inside UHT after standard CodeGen. Generated headers contain declarations; all generated implementations are `.cpp` files listed for MBT only. `ExportMetadata.ConfigureHost` writes actual Target descriptors and adds an owner-private External dependency. That External module uses `PublicPreBuildLibraries` to link the stable MBT archive even when it did not exist at the start of the first build. Normal cached builds use registered external dependencies; a forced repeat can run MBT and reuse a valid receipt. No engine patch is installed. Clean-checkout bootstrap and compilation-database preparation remain prerequisites.

The following broader action-graph integration is still a future contract:

1. Build an owning-module/binary map from the real Target. After standard UHT, export actual module compilation contexts and run complete MHT generation.
2. On cached Makefiles, also check source inventories and dependencies after the UHT check and before action invalidation. Non-reflected headers need independent discovery.
3. Call `plan` and incorporate `inputFingerprint` into the MBT action command/version. Explicitly dirty actions with `upToDate=false`, including corrupted outputs with unchanged inputs.
4. Execute `build-host --expected-input ...`; declare archive and receipt outputs, set `bProducesImportLibrary=false`, and initially disable remote/UBA execution.
5. Attach the archive and anchor retention to the real owning binary's private final link environment. Add the archive as a link prerequisite and do not propagate all host libraries through Runtime public dependencies.
6. Reserve stable per-module archive paths/actions, including generated empty batches. Isolate engine/toolchain, Target, configuration, platform, and architecture variants.
7. Implement platform-specific symbol retention only on the appropriate final link. Include host products in module outputs, receipts, Clean, and plugin output filtering.
8. After successful final linkage, export the trusted current-target link record and invoke `finalize-host`. MST aggregates all required module/target associations before publishing a complete SDK.

In this UE 5.6 source, `GenerateHeaderFuncs` runs after UHT during Makefile creation but not on Makefile reuse, so it cannot be the sole trigger. UHT exporter `.ubtplugin.csproj` support is not a general action-graph hook. Missing Editor-derived option snapshots require an explicit non-publishable top-level bootstrap before the formal build. Do not recursively build the current Target from a callback. Live Coding, Hot Reload, precompiled distribution, and remote Mac require separate integration.
