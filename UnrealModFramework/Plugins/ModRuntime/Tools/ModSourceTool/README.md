<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# ModSourceTool (MST)

ModSourceTool (MST) inspects a target's real C++ compilation database with `libclang` to trace API failure paths and rewrite out-of-line C++ Hook definitions. During normal builds, ModHeaderTool (MHT) invokes MST to produce rewritten Hook source copies, and ModBuildTool (MBT) feeds those copies into UnrealBuildTool (UBT) compile actions (see the [MHT guide](../ModHeaderTool/README.md) and [MBT guide](../ModBuildTool/README.md)).

## Build and commands

Build MST from the repository root with `.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSourceTool` on Windows, or `sh UnrealModFramework/Plugins/ModRuntime/Tools/Build.sh --target ModSourceTool` on Linux and macOS:

```text
ModSourceTool analyze-errors SourceAnalysisRequest.json
ModSourceTool update-catalog SourceAnalysisRequest.json
ModSourceTool rewrite-hooks HookSourceRequest.json
ModSourceTool generate-host-link <owner-module> <output-header>
```

## Analyze API error paths

Pass a `UMF.SourceAnalysisRequest` v1 JSON file specifying `compilationDatabase`, `target`, `catalog`, `member`, `libclang`, `resourceDirectory`, and `analysisArguments`:

- Supply the selected target's complete compiler flags **without** precompiled-header (`PCH`) flags, paired with matching `libclang` and Clang resource headers.
- Relative paths in the request resolve from the current working directory, while each catalog entry's `source` path resolves relative to the catalog file itself.

In the referenced `UMF.SourceErrorCatalog` v2 file, `member` selects an API ID whose record defines the qualified `function` name, `module`, `source` file, and persisted `reasons` map (plus `nextDetailByType` for stable code allocation):

- Add `signature` when needed to disambiguate overloaded C++ methods.
- Set `failureReturn` to the boolean, integer, or `nullptr` sentinel that signals an error return; omit `failureReturn` when tracing C++ exceptions only (see the Linux fixture in [quality.yml](../../../../../.github/workflows/quality.yml)).

Both `analyze-errors` and `update-catalog` write a `UMF.SourceErrorReport` v1 JSON document to stdout (and diagnostics to stderr) listing every analyzed translation unit, failure origin, and cross-file call chain visible in the compilation database:

- `analyze-errors` checks the AST against already-assigned error classifications and fails if it encounters any unassigned failure reason.
- `update-catalog` allocates stable detail codes for newly discovered reasons and updates the catalog file atomically under an exclusive lock. When refactoring guard expressions in C++, keep existing reason keys and codes intact to preserve error-code stability.

## Hook source copies

During `generate` runs, MHT prepares a `UMF.HookSourceRequest` v1 payload and invokes `rewrite-hooks`. MST locates each requested out-of-line member definition in Clang's AST and returns both the original and rewritten source text to MHT, which writes out the patched copies alongside `Diagnostics/HookSources.json` for MBT to substitute into UBT compile actions. If a target definition is missing or a source file changes mid-analysis, the rewrite fails immediately (see [Automatic Hook builds](../../Source/ModRuntimeUht/README.md#automatic-hook-builds)).

`generate-host-link` is retained for legacy Win64 x64 MSVC static-archive integrations, emitting a header that forces the linker to retain a generated host archive. Current UBT targets compile generated host units directly inside their owning modules via UHT (see [Legacy archive commands](../ModBuildTool/README.md#legacy-archive-commands)).
