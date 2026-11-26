<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModBuildTool

MBT drives UBT with generated Hook source replacements and verifies the final desktop images against their target receipts. UBT compiles the generated host and verification units in their owning modules.

The [UHT adapter](../../Source/ModRuntimeUht/README.md) invokes MHT generation and MBT independent compilation for the sample UE 5.6 Win64 MSVC targets, using private External module rules to attach each host archive to its owner. The Win64 UBT post-build step verifies the owning PE image and publishes link records: `Built` records archive construction, while `Linked` records verified association with a successful link and its image bytes.

All current host targets compile MHT's v4 host units with UBT. MBT prepares Hook compile actions and verifies the generation-specific binding symbol in each final desktop image using the UBT target receipt. The per-variant `host-link.json` records image and receipt hashes. MDT packages the generated Client implementation in a shared UE Runtime module. The independent archive commands below accept historical producer inputs. [Current generation format](Protocol/generated-artifacts.schema.json) · [Historical v3 format](Protocol/generated-artifacts-v3.schema.json)

## Build and commands

From the repository root:

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModBuildTool
```

You can also build the `ModBuildTool` target directly inside an existing CMake build directory. Invoke the resulting executable with one of the following commands:

```text
ModBuildTool build-ue-target --engine <path> --project <project.uproject> --target <target>
                             --platform <platform> --configuration <configuration>
                             --architecture <architecture> --mode <normal|rebuild|bootstrap>
ModBuildTool plan --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-planned-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --input-fingerprint <plan.inputFingerprint>
ModBuildTool verify-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool finalize-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --link-receipt Target.link.json
ModBuildTool finalize-ue-target --contexts TargetContexts.json --ue-target-receipt Target.target
ModBuildTool refresh-ue-watches --contexts TargetContexts.json
ModBuildTool hash-context --file HostCompileContext.json
ModBuildTool hash-file --file <path>
ModBuildTool inspect-archive --file <archive> --platform Win64 --architecture x64 --anchor <symbol>
ModBuildTool inspect-binary --file <image> --platform Win64 --architecture x64 --kind Executable
```

`build-ue-target` uses the engine's bundled .NET runtime to run UBT for a single target architecture. In `normal` mode, MBT exports outdated actions, verifies the MHT Hook source map and source hashes, substitutes generated inputs into the compile actions, and executes the updated plan through UBT while recording object hashes for incremental reuse. `rebuild` passes UBT's rebuild request before exporting actions. `bootstrap` runs UBT with `-MhtBootstrap` for the Development Editor to compile the original functions; a normal build prepares its bootstrap Editor automatically when needed.

`plan` validates inputs without compiling or creating directories. `build-host` acquires the archive lock and returns `Built` or `UpToDate`. `build-planned-host` additionally verifies the expected fingerprint from an earlier plan while holding the archive lock. `verify-host` fails when inputs or artifacts are stale. Exit code `0` indicates success, while exit code `1` indicates invalid input, a build or I/O failure, or stale verification. JSON results are written to stdout and child diagnostics to stderr.

## Compilation context

See [host-context.schema.json](Protocol/host-context.schema.json). The UHT adapter forwards the real owning-module MSVC recipe from an actual UBT compilation database and verifies its producer definitions. Other integrations must likewise export the real owning-module context; MBT does not infer it from a default CMake compiler or a hand-written approximation.

The context requires `format: "UMF.ModHostContext"`, `schemaVersion: 2`, `ownerModule`, `moduleAPIMode: "Producer"`, an undecorated C `registrationAnchor`, and `target` fields (`name`, `type`, `configuration`, `linkType`, `platform`, and `architecture`). Explicit `lto`, `pch`, and `unity` flags must all be `false` in this version.

The optional `owningBinary` field specifies the real UBT binary's stable `id`, context-relative `path`, and `kind` (`Executable` or `DynamicLibrary`); it is mandatory for `finalize-host`.

`workingDirectory` is the real process working directory, and `outputLibrary` must lie outside the managed generation root and end in `.lib` or `.a`. Relative context paths resolve against the context directory. `dependencies` declares forced includes, recipe inputs, and other toolchain files. `environmentMode: Explicit` runs tools with only the declared `environment`, whereas the default `Inherit` mode overlays those values onto the caller's environment. Plans and receipts expose the effective `environmentHash`.

Both `compiler` and `archiver` specify a tool `path`, an `arguments` array, and a `responseFormat` (`msvc` or `gnu`). Compiler arguments must include `{source}`, `{object}`, and `{dependencies}`. Archiver arguments require `{output}` and a standalone `{objects}` array entry that expands to all current object paths. MBT invokes each executable directly with a response file, without a shell.

`msvc` response files use UTF-16LE with BOM and Windows quoting, require a Windows host, and keep each option and its separate value on the same line. `gnu` response files use UTF-8 and GNU quoting. Compiler `dependencyFormat` is either `msvc-json` (for `/sourceDependencies`) or `make` (for complete Make-style dependency output such as `-MD -MF {dependencies}`). The adapter must ensure real dependencies, including system headers, are covered. Batch files and shell recipes are unsupported.

## Generated-artifact contract

New producers use the `UMF.GeneratedArtifacts` v3 [schema](Protocol/generated-artifacts.schema.json) with status `Complete` and zero `unresolvedTypeCount`. MBT continues to accept [legacy v1](Protocol/generated-artifacts-v1.schema.json) and v2 manifests using their original hash key. Final-link association requires v2 or v3, and bootstrap or scan reports are rejected.

In v2 and v3, `hostBuildContexts` maps every compilation owner to its parsed context hash (use `hash-context` to compute the compact sorted JSON digest); the v1 single-context fallback is `buildContextHash`. MBT verifies every v2 and v3 `inputFiles` hash and re-enumerates `watchedDirectories` before accepting the batch. When inputs are stale, `GeneratedInputsStale` identifies the path requiring a new MHT run. `generationId`, `inputHash`, `contractHash`, `projectionHash`, and `nativeABIHash` are lowercase SHA-256 strings.

`generatedFiles` lists relative paths, `contentHash`, and `ownerModule` (nullable for shared artifacts). Paths must be unique, contain no `..`, and remain within the manifest directory after symlink resolution; every file must match its recorded hash. `compilationUnits` explicitly lists `ownerModule` and `source`, and MBT compiles only listed `.cpp`, `.cc`, or `.cxx` files belonging to this owner. Empty export sets still require a generated unit defining the stable registration anchor.

## Cache and publication

Each generated file in v2 and v3 requires a `role`: `HostBinding`, `HostRegistration`, `CompileVerification`, `HostHeader`, `SdkHeader`, `Contract`, `SdkResource`, or private `Diagnostic`. Plans, build results, and linked results expose only this owner's and shared `SdkHeader`, `Contract`, and `SdkResource` files via `publicArtifacts`, including absolute `sourcePath`, normalized `relativePath`, hash, role, and owner. SDK headers must use `.h` or `.hpp`, and contracts must use `.json`. Compilation units cannot also be published as public artifacts, and unspecified or unknown legacy roles remain private.

`requiredSymbols` is an array of `{ "ownerModule": "...", "symbol": "..." }` records with optional `exportId`, `signatureHash`, and `projectionHash` fields. Symbols are undecorated C identifiers. Duplicate owner/symbol pairs and duplicate export IDs within a single owner fail validation, whereas a Host and Client owner may describe the same logical ExportId. MBT adds the context's stable registration anchor and validates the complete owner-specific set in one archive pass (Mach-O inspection adds the platform C prefix). Empty export sets may use an empty array, though the anchor remains required. Legacy v1 may omit the array and retain anchor-only verification.

The input fingerprint covers the MBT executable and version, input documents, generated files, compiler and archiver, declared dependencies, previously reported transitive compiler dependencies, and effective child environment. Changing a source file or an existing include triggers recompilation and discovery of new include dependencies.

`<library>.mbt.json` is the successful commit record, containing target, owner, context and generation identities, input fingerprint, archive hash, anchor, and compiler dependencies. Cache reuse requires matching inputs, a matching archive hash, and passing archive validation; a malformed or missing receipt never qualifies as a valid cache entry.

Builds acquire an exclusive OS lock and use a freshly created temporary directory. A dirty module recompiles its generated units and creates an entirely new archive, preventing removed archive members from surviving. Publication replaces the archive first and atomically commits its receipt last, so an interrupted publication cannot pass subsequent hash verification.

Archive validation checks native object formats, target architecture, and every required symbol in the index, reporting the complete missing set. Supported formats are Win64 COFF, Linux ELF, and macOS Mach-O for `x64` and `arm64` targets. Import-library members, thin archives, unknown objects, and mismatched architectures are rejected. LTO, PCH, unity builds, and C++ modules/header units are unsupported. Platform support requires actual validation on each platform; a Windows build does not establish Linux or macOS acceptance.

## Two-API producer example

MHT emits a v3 manifest with four host units (bridge, C ABI endpoints, verification, and UE views) plus one independent client unit owned by `<game>ModClient`. The context anchors are `UMF_ModHost_<owner>` and `UMF_ModClient_<game>`. See the [MHT ABI protocol](../ModHeaderTool/README.md) for the concrete public header, Host/Client symbol records, and MDT contract.

The local-character and add-health APIs are ordinary manifest entries rather than special-cased MBT behavior. Below is a simplified v3 symbol fragment for `ExportTest` (actual MHT symbol names are read from its generated descriptor):

```json
{
  "requiredSymbols": [
    { "ownerModule": "ExportTest", "symbol": "UMF_UnrealModFrameworkDev_ExportTest_A_b88dc4c71053a716_Host", "exportId": "api/game.local_player_api/getlocalcharacter" },
    { "ownerModule": "ExportTest", "symbol": "UMF_UnrealModFrameworkDev_ExportTest_A_b846156f4c92057a_Host", "exportId": "api/game.character/addhealth" }
  ]
}
```

MBT also requires `UMF_ModHost_ExportTest` from the context. MHT marks `SDK/Public/<game>.modabi.h` as `SdkHeader`, generated JSON contracts as `Contract`, and implementations as private compilation units. Host and Client records associate each ExportId with matching signature and projection hashes, while internal UE-view headers carry the private `HostHeader` role. This snippet is only a fragment; a complete input still needs all hashes and compilation units. MDT receives the public file list and re-verifies hashes when copying. MBT neither publishes private host sources nor builds the final authoring plugin.

## Final-link association

`finalize-host` consumes [host-link-receipt.schema.json](Protocol/host-link-receipt.schema.json) and produces [linked-artifacts.schema.json](Protocol/linked-artifacts.schema.json). In the UE 5.6 Win64 x64 adapter, the UBT post-build step runs `finalize-ue-target`, which checks the UBT target receipt and the archive's unique PE export before publishing the link record and linked artifacts beside the archive. Other link adapters can supply their own successful link records directly to `finalize-host`.

The link record requires `UMF.ModHostLinkReceipt` v1, `status: "Succeeded"`, an exact `target` match, and `binary` fields (`id`, `path`, `kind`, `contentHash`, and unique `ownerModules`). `hostLibraries` lists the real final-link archives with one record per owner: `ownerModule`, `path`, `contentHash`, `hostReceiptHash`, `contextHash`, `generationId`, `registrationAnchor`, and `anchorRetained: true`. Paths in the record resolve relative to its directory.

MBT requires a current host build, exact owning binary identity, valid module membership, matching library/receipt hashes and batch/context identities, an anchor retention declaration, a matching actual image hash, and a supported native image kind and architecture. Supported image formats are PE, ELF (distinguishing PIE executables from shared libraries), and thin Mach-O; fat Mach-O is unsupported. Input changes during verification fail.

The `UMF.ModHostLinkedArtifacts` / `Linked` output includes public files, contract hashes, required symbols, binary and archive identities, and the hashes and paths of both receipts for MDT aggregation. Link success and anchor retention are assertions of the trusted adapter: MBT verifies associations and bytes rather than JSON authorship or actual runtime registration, and it never infers successful linkage from arbitrary strings in a binary.

## UE 5.6 integration

MBT drives UBT with generated Hook source replacements and verifies the final desktop images against their target receipts. UBT compiles the generated host and verification units in their owning modules.

`build-ue-target --mode normal` uses the same `Config/ModHeaderTool.json` generation settings as UHT. When the local MBT executable drives a Windows build, the tool pre-build step reuses that executable and builds MHT and MST. Run Setup again after switching to a branch with newer MBT sources so the next invocation uses the updated driver.

The following broader action-graph integration remains a future contract:

1. Build an owning-module-to-binary map from the real Target. After standard UHT completes, export actual module compilation contexts and run complete MHT generation.
2. On cached Makefiles, also check source inventories and dependencies after the UHT check and before action invalidation, since non-reflected headers require independent discovery.
3. Call `plan` and incorporate `inputFingerprint` into the MBT action's command or version. Explicitly dirty actions when `upToDate=false`, including cases with corrupted outputs and unchanged inputs.
4. Execute `build-planned-host --input-fingerprint ...`. Declare archive and receipt outputs, set `bProducesImportLibrary=false`, and initially disable remote and UBA execution.
5. Attach the archive and anchor retention to the real owning binary's private final link environment. Add the archive as a link prerequisite rather than propagating host libraries through Runtime public dependencies.
6. Reserve stable per-module archive paths and actions, including generated empty batches. Isolate variants by engine/toolchain, Target, configuration, platform, and architecture.
7. Implement platform-specific symbol retention only on the appropriate final link, and include host products in module outputs, receipts, Clean, and plugin output filtering.
8. After final linkage succeeds, export the trusted current-target link record and invoke `finalize-host`. MDT aggregates all required module and target associations before publishing a complete SDK.

In this UE 5.6 codebase, `GenerateHeaderFuncs` runs after UHT during Makefile creation but not on Makefile reuse, so it cannot serve as the sole trigger. UHT exporter `.ubtplugin.csproj` support is not a general action-graph hook. The generation adapter can bootstrap the configured `metadataEditorTarget` when it needs project binaries for an export snapshot. When preparing the Editor manually, use a Development Editor bootstrap followed by the normal build. Live Coding, Hot Reload, precompiled distribution, and remote Mac require separate integration.
