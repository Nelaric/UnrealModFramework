<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModRuntime UHT adapter

`ModRuntime.ubtplugin.csproj` is a small .NET 8 UHT exporter. UE 5.6 discovers
the project inside the enabled ModRuntime plugin, builds the assembly, and loads
the `[UnrealHeaderTool]` class and `[UhtExporter]` entry. It uses assembly
references from the active engine, with no project references or engine changes.
The exporter is named `ModRuntime` and is gated by the Runtime `ExportMetadata`
module, so it can run for Game and Editor manifests.

The adapter starts independent native MHT and MBT processes. C++ parsing,
validation, projection, and source generation remain in MHT; compilation and
archive validation remain in MBT. Blueprint export belongs to the
ExportCodeGenerator editor module. UBT performs the final game/module link.
See Epic's [UE 5.6 UHT extension documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6).

## Native tool and default invocation

The native tool must already be built. Discovery uses, in order:

1. `UMF_MHT_EXECUTABLE`.
2. `executable` in the project's optional `Config/ModHeaderTool.json`.
3. `<plugin>/Binaries/ThirdParty/ModHeaderTool/<host>/MHT[.exe]` or
   `ModHeaderTool[.exe]`, where host is Win64, Linux, or Mac.
4. The documented local build at
   `<plugin>/Tools/build/Release/bin/Release/ModHeaderTool[.exe]`.

Without configuration the default mode is `scan`. The development project's
`Config/ModHeaderTool.json` selects `generate`. In scan mode a normal UHT
invocation runs:

```text
ModHeaderTool scan --project <actual.uproject> --output <owned-temporary.json>
```

The authoritative metadata catalog defaults to
`<project>/Saved/ModHeaderTool/MetadataOptions.json`. When present it is passed
with `--options`. Export it using the existing `ModExportOptions` commandlet.
A missing catalog is diagnosed by MHT when required by the source exports.
Bootstrap the first Development Editor with `-MhtBootstrap` if needed; the
adapter skips this explicit bootstrap invocation.

Only a successful scan is committed through the UHT output factory to
`Inc/ExportMetadata/MHT/Scan.json`. This is the sibling of
`Inc/ExportMetadata/UHT`. Scan validates marker parameters; it does not perform
Clang declaration analysis or produce host bridges.

## Complete generation

Set `mode` to `generate` and supply an actual prepared MHT build input for each
Target variant. The adapter checks its project, Target, platform and configuration
against the UHT session and uses the x64 architecture enforced by the rules,
exports owner-specific MBT contexts, and starts
`generate --build-input ... --output-dir <MHT> --host-contexts <contexts.json>`.
It then runs `ModBuildTool build-host` for every owner before returning from UHT.
The generation root is `MHT/<Target>/<Configuration>/<Architecture>` under the
platform-specific `Inc/ExportMetadata` directory. Each run replaces that variant's
`SDK`, `Host`, `Client`, `Contract`, `Diagnostics` and
`<gameId>.generated-artifacts.json` at fixed paths. The adapter checks completeness, target identity, and every
generated file hash. Removed outputs are cleaned; identical files retain their
timestamps. The complete manifest is committed after all file replacements.
`generationId` remains a content identity, not a directory or retained batch.

Example project configuration; replace the native tool and LLVM paths:

```json
{
  "schemaVersion": 1,
  "executable": "Plugins/ModRuntime/Binaries/ThirdParty/ModHeaderTool/Win64/MHT.exe",
  "mbt": "Plugins/ModRuntime/Tools/build/Release/bin/Release/ModBuildTool.exe",
  "mode": "generate",
  "timeoutSeconds": 600,
  "options": "Saved/ModHeaderTool/MetadataOptions.json",
  "targets": {
    "UnrealModFrameworkDevEditor": {
      "variants": {
        "Win64/Development/x64": {
          "buildInput": "Intermediate/ModHeaderTool/BuildInputs/Editor.json"
        }
      },
      "libclang": "<LLVM>/bin/libclang.dll"
    }
  }
}
```

Paths in this adapter configuration are relative to the project directory.
Paths inside a build input retain MHT's own resolution rules. `timeoutSeconds`
is 1–86400, default 600. `libclang` and `options` are optional overrides.
MBT discovery uses `UMF_MBT_EXECUTABLE`, the root `mbt` setting, or the local
`Tools/build/Release/bin/Release/ModBuildTool[.exe]` output, in that order.
`UMF_MHT_BUILD_INPUT` selects generate mode and overrides target configuration;
`UMF_MHT_LIBCLANG` overrides its libclang path. Changing environment settings
requires a new UHT invocation, for example `-ForceHeaderGeneration` through UBT.

No compile environment is inferred from a UHT manifest. Prepare the real UBT
compilation database and matching MHT build input first. Choose the correct
platform, configuration, and architecture for that build input; the UHT manifest
does not supply that complete tuple. Avoid stale response files. The UHT exporter
API does not expose a cross-exporter dependency barrier; normal CodeGen must
already have committed the headers needed by Clang. This was exercised with the
installed UE 5.6 exporter order, not assumed for other engine versions.

## Independent host compilation and linking

The owner rules call `ExportMetadata.ConfigureHost(this, Target)`. A companion
External rules class, such as `ExportTestHost`, calls
`ExportMetadata.ConfigureHostLibrary(this, Target, "ExportTest")`; the owner
privately depends on this External module. Setup is per owning module, while
individual API authors continue to write export macros and ordinary functions.

The rules write a stable target descriptor below
`Intermediate/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>`.
`MbtHostBuild` reads the actual MSVC action and expands its response files from
UBT's compilation database. The adapter pins the expanded commands and their
source database by hash. It preserves module producer definitions and ABI
settings, replaces source/object/dependency outputs, and adds generated include
paths. The current automatic adapter requires Win64 x64 MSVC, no PCH, no unity,
and no LTO. It does not guess missing compiler settings.

MHT emits declarations in public headers and all implementations in explicit
`.cpp` compilation units. MBT independently compiles those units and publishes
`Intermediate/MBT/<Target>/<Platform>/<Configuration>/<Architecture>/<owner>.lib`
and its `.mbt.json` receipt. A separate `<game>ModClient` producer context removes UE/game includes and forced definitions while retaining the real selected toolchain flags. MBT independently compiles the client ABI library as well. The v2 manifest lists all five host/client units for the one-owner sample and binds both endpoint sets to ExportIds. Only the pure C ABI header is public to MST; see the [MHT connection protocol](../../Tools/ModHeaderTool/README.md). The generated manifest's `hostBuildContexts` map pins
each actual producer context. An unchanged MBT fingerprint reuses the library.

The private External module uses UE 5.6 `PublicPreBuildLibraries` to declare a
library that can be absent before the first UHT invocation. No empty placeholder
is created. UHT must finish MBT successfully before UBT reaches final linking.
The library does not propagate through an owning DLL's private dependency.
After native publication the adapter refreshes UE's cached `FileItem` information
for the library, receipt, and generated files. Internal UHT and UBT share that
cache; refreshing it lets the current build detect the new archive and relink.
Game source only calls the declared `UMF_ModHost_<owner>` anchor; no generated
`.cpp` or `.inl` is included by UBT-compiled source. SDK view methods, constructors,
and TypeId helpers also have out-of-line MBT implementations.

The Target rules call `ExportMetadata.ConfigureTarget(this)` once to install
UE 5.6 build hooks. The pre-build step updates a UHT dependency stamp when a
watched directory changes or a recorded optional file appears. MBT verifies the exact archive's PE export
in the owning DLL or EXE and checks the UBT target receipt and modular ownership.
It writes `<owner>.lib.link.json` and `<owner>.lib.linked.json` beside the archive.
The client library has its own build receipt.

## Build behavior and limits

Native stdout/stderr are forwarded to UHT. Nonzero exit codes, invalid results,
and timeouts fail UHT. UE's managed process group owns the child process lifetime.
The adapter does not enable `CompileOutput` or register C++ output filters;
MHT sources remain outside UBT's compilation list.

UHT no-output/reference and `-Mode=GenerateClangDatabase` preparation invocations
skip native side effects. Complete generation
is rejected with `-FailIfGeneratedCodeChanges` because MHT has no matching dry-run
contract. Scan outputs retain UHT's normal change-checking behavior.

Existing tool, configuration, metadata, reflected headers, build inputs, and
generated-manifest inputs, compiler recipes, libraries, MBT receipts, and their
compiler dependencies are registered using `AddExternalDependency`. UHT
regenerates the affected variant when a registered file or the watch stamp
changes. A clean checkout requires the documented bootstrap, metadata snapshot,
and actual compilation database for that variant.
The automatic link verification covers standard Win64 x64 project DLLs and
monolithic EXEs. Live Coding and Hot Reload require separate integration.

UBT generates the adjacent `.ubtplugin.csproj.props` file containing `EngineDir`;
it is ignored by Git. For a direct managed build:

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
