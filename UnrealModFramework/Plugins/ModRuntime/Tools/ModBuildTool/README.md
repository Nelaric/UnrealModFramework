<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModBuildTool (MBT)

ModBuildTool (MBT) orchestrates UnrealBuildTool (UBT) builds that use automatic C++ Hooks, swapping rewritten source copies into the relevant compile actions and verifying the linked binaries of supported desktop targets. Alongside it, the [ModRuntime UHT adapter](../../Source/ModRuntimeUht/README.md) registers generated host and verification C++ units inside their owning UE modules, while ModSDKTool (MDT) compiles the generated Client implementation into the shared SDK Runtime (see [generated-artifacts.schema.json](Protocol/generated-artifacts.schema.json)).

## Build and commands

Build MBT from the repository root (or build the `ModBuildTool` target directly inside an existing CMake build directory):

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModBuildTool
```

Primary commands for UBT target builds:

```text
ModBuildTool build-ue-target --engine <path> --project <project.uproject> --target <target>
                             --platform <platform> --configuration <configuration>
                             --architecture <architecture> --mode <normal|rebuild|bootstrap>
ModBuildTool finalize-ue-target --contexts watch.json --ue-target-receipt Target.target
ModBuildTool refresh-ue-watches --contexts watch.json
ModBuildTool hash-file --file <path>
ModBuildTool inspect-binary --file <image> --platform Win64 --architecture x64 --kind Executable
```

`build-ue-target` drives UBT for a single target architecture using the engine's bundled .NET runtime:

- `normal`: Exports outdated UBT actions, verifies MHT's Hook source map and file digests, substitutes rewritten source copies into the compile actions, and executes the patched plan through UBT while recording object-file hashes for incremental builds. If a metadata or Blueprint snapshot needs Editor binaries that do not exist yet, a `normal` run bootstraps the configured Editor target automatically.
- `rebuild`: Forwards a clean rebuild request to UBT before exporting and patching actions.
- `bootstrap`: Invokes UBT with `-MhtBootstrap` so the Development Editor compiles the original unhooked C++ functions directly.

MBT schedules UBT post-build steps *after* pending link actions so binary checks always inspect completed outputs, and project target rules resolve the exact same `Config/ModHeaderTool.json` and per-target output directories as a direct UBT run. On Windows, the pre-build step reuses a running local `ModBuildTool.exe` while rebuilding MHT and MST; if you switch branches to newer MBT C++ sources, re-run Setup first.

## Final-link association

After a complete generation on single-architecture `Win64`, `Linux`, `LinuxArm64`, or `Mac` (`x64` or `arm64`) builds, target rules invoke `finalize-ue-target`. MBT walks the UBT `.target` receipt and `.modules` manifests to locate each host owner's modular shared library or monolithic executable, confirms that the binary exports its unique `UMF_HostBinding_<owner>_<generationId>` symbol, and writes a `UMF.UbtHostLinkReceipt` v1 record to:

```text
Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>/host-link.json
```

Each receipt records the target tuple, generation and contract digests, UBT `.target` receipt hash, and every owner image's path, binary kind, SHA-256 digest, and generation symbol. Desktop verification covers PE, ELF, and thin Mach-O images (Apple builds additionally check linked Hook symbols with Xcode's `nm`). Before shipping a release, always run end-to-end call verification against a staged game build (see [SDK installation and verification](../ModSDKTool/README.md#install-and-verify) and the [packaged game regression](../../../../../Test/SharedRuntime/README.md)).

On incremental builds, `refresh-ue-watches` compares current files on disk against the input inventory referenced by the variant's `watch.json` (which points to the v4 generated manifest). Whenever a watched directory or tracked input changes, MBT updates `.mht-watch.stamp` so UBT invalidates its cached action graph (see [ExportMetadata.Build.cs](../../Source/Runtime/ExportMetadata/ExportMetadata.Build.cs)).

Supported mobile/Apple Hook fixture pairs and UBT caveats are listed in [Automatic Hook builds](../../Source/ModRuntimeUht/README.md#automatic-hook-builds). Validate Live Coding, Hot Reload, precompiled binary distribution, and remote Mac builds separately for your project.

## Legacy archive commands

For custom non-UHT producers that supply their own per-module compilation recipes, MBT retains the standalone static-archive commands below (`Built` indicates a newly compiled archive, `UpToDate` indicates a verified cache hit, and `Linked` indicates final target-image verification):

```text
ModBuildTool plan --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool build-planned-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --input-fingerprint <plan.inputFingerprint>
ModBuildTool verify-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json
ModBuildTool finalize-host --context HostCompileContext.json --generated-manifest MyGame.generated-artifacts.json --link-receipt Target.link.json
ModBuildTool hash-context --file HostCompileContext.json
ModBuildTool inspect-archive --file <archive> --platform Win64 --architecture x64 --anchor <symbol>
```

`plan` validates all inputs without compiling or creating output directories. `build-host` acquires an exclusive archive lock and builds; `build-planned-host` additionally verifies `--input-fingerprint` while holding that lock; `verify-host` fails if any input or artifact is stale. JSON results go to stdout, child compiler/archiver diagnostics go to stderr, exit code `0` indicates success, and exit code `1` indicates invalid inputs, build/I/O errors, or stale artifacts.

## Compilation context

Supply the real owning module's compile recipe—including its producer `*_API` definitions—formatted per [host-context.schema.json](Protocol/host-context.schema.json):

| Field | Contract |
| --- | --- |
| `format` / `schemaVersion` | `"UMF.ModHostContext"` / `2` |
| `ownerModule` | Owning UE module name; every compiled unit must belong to this module |
| `moduleAPIMode` | `"Producer"`, preserving the module's export macro definitions |
| `registrationAnchor` | Undecorated C identifier that must appear in the static archive's symbol index |
| `owningBinary` | Optional final binary descriptor (`id`, `path`, `kind`: `"Executable"` or `"DynamicLibrary"`); required for `finalize-host` |
| `target` | `name`, `type`, `configuration`, `linkType`, `platform`, and `architecture` |
| `workingDirectory` | Working directory used when spawning compiler and archiver processes |
| `outputLibrary` | Output `.lib` or `.a` path outside the managed generation root (relative paths resolve from the context file's directory) |
| `compiler` / `archiver` | Executable `path`, `arguments` template array, and `responseFormat` (`"msvc"` or `"gnu"`) |
| `environment` / `environmentMode` | String map applied in `"Inherit"` (default overlay) or `"Explicit"` (clean environment) mode; `environmentHash` is recorded without leaking values |
| `dependencies` | Forced includes, toolchain files, and recipe inputs tracked for cache invalidation |
| `lto` / `pch` / `unity` | Must all be explicitly set to `false` |

Compiler `arguments` must include `{source}`, `{object}`, and `{dependencies}`; archiver `arguments` must include `{output}` plus a standalone `{objects}` element that expands to all current object files. MBT invokes tools directly via response files without a shell (`.bat` and shell-script wrappers are unsupported):

- `"msvc"` response files require a Windows host, use UTF-16LE with BOM and Windows quoting (keeping options and their values on the same line), and pair with `dependencyFormat: "msvc-json"` (`/sourceDependencies`).
- `"gnu"` response files use UTF-8 with GNU quoting and pair with `dependencyFormat: "make"` (for example, `-MD -MF {dependencies}` covering system headers as well).

## Generated-artifact contract

Legacy archive commands accept `UMF.GeneratedArtifacts` **v1–v3** manifests with `status: "Complete"` and `unresolvedTypeCount: 0` (archive `finalize-host` requires **v2** or **v3**; see the [v1 schema](Protocol/generated-artifacts-v1.schema.json) and [v3 schema](Protocol/generated-artifacts-v3.schema.json)).

In v2 and v3, `hostBuildContexts` maps each `ownerModule` to its compact sorted context digest (computed via `hash-context`), whereas v1 uses the single-context `buildContextHash`. Before accepting a manifest, MBT re-hashes `inputFiles` and re-scans `watchedDirectories`, reporting `GeneratedInputsStale` with the offending path whenever a fresh MHT pass is needed. All summary digests (`generationId`, `inputHash`, `contractHash`, `projectionHash`, `nativeABIHash`) are lowercase SHA-256 hex strings.

`generatedFiles` lists unique relative paths (no `..`, staying inside the manifest root after symlink resolution), `contentHash`, and nullable `ownerModule`. `compilationUnits` explicitly names each `.cpp`/`.cc`/`.cxx` unit and its `ownerModule`—even a module with zero exports must emit a unit defining `registrationAnchor` so stale archives from earlier builds are never reused accidentally.

## Cache and publication

Every file in a v2/v3 manifest carries a `role` (`HostBinding`, `HostRegistration`, `CompileVerification`, `HostHeader`, `SdkHeader`, `Contract`, `SdkResource`, or private `Diagnostic`). Plans and build/link results expose only `SdkHeader` (`.h`/`.hpp`), `Contract` (`.json`), and `SdkResource` entries in `publicArtifacts`; compilation units and untagged v1 files stay private.

`requiredSymbols` lists `{ "ownerModule", "symbol" }` entries (undecorated C names, with optional `exportId`, `signatureHash`, and `projectionHash`). Duplicate module/symbol pairs or duplicate `exportId` entries within the same owner fail validation, though Host and Client owners may share a logical `exportId`. MBT merges in `registrationAnchor` and verifies every required symbol in a single archive scan (prepending `_` on Mach-O).

Each archive's input fingerprint covers the MBT binary and version, manifest and context files, generated sources, compiler/archiver binaries, declared dependencies, transitive header dependencies from the previous compile, and `environmentHash`. Successful builds run under an exclusive OS lock in a fresh temp directory, recompile all units for a dirty module into a brand-new archive (preventing orphaned members), replace the `.lib`/`.a` first, and atomically commit `<library>.mbt.json` last. Archive inspection supports `x64` and `arm64` Win64 COFF, Linux ELF, and macOS Mach-O static libraries, rejecting import libraries, thin archives, LTO/PCH/unity outputs, and C++20 module units.

### Legacy final-link association

`finalize-host` consumes a `UMF.ModHostLinkReceipt` v1 JSON file produced by a build adapter ([link-receipt schema](Protocol/host-link-receipt.schema.json)), verifies every static archive against the linked binary on disk, and emits `UMF.ModHostLinkedArtifacts` ([linked-artifacts schema](Protocol/linked-artifacts.schema.json)):

| Link receipt field | Requirement |
| --- | --- |
| `status` | `"Succeeded"` |
| `target` | Must match the compilation context's `target` object exactly |
| `binary` | Owning binary `id`, relative `path`, `kind`, `contentHash`, and deduplicated `ownerModules` |
| `hostLibraries` | One entry per `ownerModule` with `path`, `contentHash`, `hostReceiptHash`, `contextHash`, `generationId`, `registrationAnchor`, and `anchorRetained: true` |

MBT verifies that the host build is up to date, hashes and identities match across all receipts, and the final binary is a valid PE, ELF (distinguishing PIE executables from shared libraries), or thin Mach-O image of the expected architecture (fat Mach-O binaries and mid-check file modifications are rejected).
