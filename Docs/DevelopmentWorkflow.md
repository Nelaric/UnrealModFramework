<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and Release Validation

Development uses UE 5.6.1 Development SDKs from immutable, independently verified private UMFCI releases. The [engine lock](../.github/engine-lock.json) records the installer commit, UE revision, target profiles, recipes, manifest SHA-256 values, .NET SDK and platform toolchains. An empty release entry blocks that target. The installer verifies the release and streams the archive, checking every file and the 5 GB installation limit.

| Editor producer | Game consumers |
| --- | --- |
| Windows x86_64 | Windows x86_64 and ARM64 |
| Linux x86_64 | Linux x86_64 and ARM64 |
| macOS x86_64 | macOS x86_64 |
| macOS ARM64 | macOS ARM64 |

The four Editor jobs run independently. Each Game group can start when its Editor job succeeds. Development Game jobs compile and link the monolithic target through UBT and verify its host-link receipt. Editor jobs run the normal target and verify the discovered and completed `Nelaric.*` tests in the headless automation report. The job logs record SDK installation, native tool preparation, metadata, reflection and generation, compile and link, and automation timings. The 5–10 minute cold critical path is a target for the full matrix.

## Build locally

Run [Setup](../README.md#build-the-development-host) once to build the native tools and prepare libclang. Run the same MBT stages that CI uses. This example uses a legal full Windows UE 5.6.1 installation for the Editor. Supply the independently verified Game SDK to the Game command when it is available.

```powershell
$mbt = "./UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe"
$project = "UnrealModFramework/UnrealModFrameworkDev.uproject"
$engine = "<full UE 5.6.1 or Editor SDK>"
$gameSdk = "<Win64 Game SDK or full UE 5.6.1>"
$bundle = "UnrealModFramework/Intermediate/ModTools/Metadata"
$commit = git rev-parse HEAD
$revision = (Get-Content .github/engine-lock.json -Raw | ConvertFrom-Json).upstreamCommit
$platform = @("--project", $project, "--platform", "Win64", "--architecture", "x64")
$identity = @("--commit", $commit, "--engine-commit", $revision)
$inputs = @("--configuration", "Development", "--metadata", $bundle)

& $mbt prepare-metadata --engine $engine --target UnrealModFrameworkDevEditor @platform @identity --output $bundle
& $mbt build-ue-target --engine $engine --target UnrealModFrameworkDevEditor @platform @identity @inputs --mode normal
& $mbt test-ue-target --engine $engine --target UnrealModFrameworkDevEditor @platform --report ".tools/automation"
& $mbt build-ue-target --engine $gameSdk --target UnrealModFrameworkDev @platform @identity @inputs --mode normal
```

Use a new report directory for each automation run. For UMFCI SDKs, set `UE_USE_SYSTEM_DOTNET=1` and `DOTNET_ROOT` to the locked native .NET SDK. The Windows SDK toolchain can be selected with `UMF_MSVC_VERSION` and `UMF_WINDOWS_SDK_VERSION`. The GitHub installer resolves and checks these versions before invoking MBT. Linux ARM64 uses UBT platform `LinuxArm64` and architecture `arm64`. Windows ARM64 uses `Win64` and `arm64`. Both use Game SDKs.

`prepare-metadata` reuses a current bundle. For a missing or stale bundle it builds the native Development Editor with the bootstrap rules and invokes the export commandlet. `validate-metadata` checks an existing bundle for one target without invoking UBT. Normal MBT builds validate the bundle before UBT and generate native inputs independently for each target. CI gives MBT the same reviewed commit on every job. A local checkout with edited sources also fingerprints the current project files and Blueprint discovery inputs. MBT queries UBT for the producer and consumer target types. The Editor discovers exported Blueprints by their saved declaration tags and dependencies. Each target derives native identities, layouts, and source error paths from its own compile commands.

The four bundle files live under `Metadata/`. Paths use `Project/` and `Engine/` roots. Options and Blueprint declarations carry reflected names, roles and scalar-field shapes. Target-side configuration projections must match the producer's inputs. Incompatible redirects, architecture-specific config layers, changed Blueprint packages, source drift and payload corruption fail validation. The target derives C++ declarations, layout, compiler arguments, Hook objects and ABI from its own compile database. When a Blueprint uses a reflected struct, the target checks the field shape against its loaded reflected type. Bundle files are generated, and `Intermediate/` is ignored by Git.

## Call individual Development phases

The [command file](../.github/development-commands.json) binds schema version 1 to 14 argument arrays. Execute each array from the candidate checkout. The same [phase driver](../Scripts/development_commands.py) can run locally with the CI environment. `build-editor` builds the normal target. Run `ModBuildTool test-ue-target` afterwards to execute and validate headless Automation.

| Job | Commands in order |
| --- | --- |
| Editor | `build-native-tools` → `prepare-libclang` → `prepare-editor-bootstrap` → `export-metadata` → `validate-metadata` → `build-editor` |
| Game Prepare | `build-native-tools` → `prepare-libclang` → `prepare-game` → `save-checkpoint` → `validate-checkpoint` |
| Game | `restore-checkpoint` → `validate-checkpoint` → `validate-metadata` → `import-metadata` → `resume-game` → `verify-game` |
| Native tools | `test-native-development <native-binary-directory>` |

Set `UMF_SOURCE`, `UMF_WORK`, `UMF_SDK`, `UMF_EVIDENCE`, `UMF_METADATA_PAYLOAD`, `UMF_CHECKPOINT_PAYLOAD` and `UMF_TOOLCHAIN` to the checkout, work and artifact directories, and toolchain JSON respectively. Set `UMF_TARGET`, `UMF_METADATA_TARGET`, `UMF_CONFIGURATION`, `UMF_ARCHITECTURE`, `UMF_UBT_PLATFORM` and `UMF_UBT_ARCHITECTURE` to the consumer and producer target identities. The SDK identity and checked-out commit come from `UMF_ENGINE_VERSION`, `UMF_UPSTREAM_COMMIT`, `UMF_LOCK_SHA256` and `UMF_COMMIT`. Run Game Prepare in a clean target workspace and restore in a fresh workspace with the same locked SDK and toolchain. UBT actions flagged `AbsolutePath` require matching absolute project, SDK and compiler roots on the two runners.

Game Prepare runs UBT and standard UHT, then executes every eligible action in the prerequisite graph. The linker and export-owning modules wait for metadata. The checkpoint contains the prepared native tools, libclang, eligible project object files and an inventory of their inputs. PCH files, resource compilation without dependency manifests and runtime file copies are replayed against the consumer's SDK. The consumer exports a fresh UBT graph after MHT runs, validates the original compiler recipes and their recorded dependencies, and restores the matching objects before executing the remaining actions. Split Win64 builds request deterministic UBT output and check the regenerated MSVC PCH debug objects before linking. Store both handoffs in authorized private storage.

The driver writes `metadata.json`, `checkpoint.json` and `game.json` to the evidence directory. Producer and consumer compare the Metadata and Checkpoint reports byte for byte. The Game report contains the linked binary and host-link receipt paths, reuse and replay counts, and measured remaining build and link durations.

## Required checks

Fork PRs run [Development](../.github/workflows/development.yml) with read-only permissions. The public aggregate `Development public` requires naming, text and formatting, API documentation, four native-tool checks, and synthetic COFF, ELF and Mach-O Hook links. Pages publishes independently. [API Pages](../.github/workflows/pages.yml)

The maintainer reviews the exact PR head and manually dispatches the engine workflow from an authorized **private** repository. Approve only a commit whose build scripts, dependencies and UE code are suitable for that environment. Configure a zero-spend Actions budget and set `UMF_ZERO_SPEND=true` in the private caller. Give `UMFCI_READ_TOKEN` read access to the private SDK releases and installer repository. Give the separate `UMF_STATUS_TOKEN` Commit statuses write access to UnrealModFramework. Only the final aggregation step receives the status token.

The private caller pins a reviewed revision of the reusable workflow.

```yaml
name: Reviewed Development
on:
  workflow_dispatch:
    inputs:
      pull_request: { type: number, required: true }
      commit: { type: string, required: true }
jobs:
  development:
    uses: Nelaric/UnrealModFramework/.github/workflows/development.yml@<reviewed-workflow-commit>
    with:
      pull_request: ${{ inputs.pull_request }}
      commit: ${{ inputs.commit }}
    secrets:
      UMFCI_READ_TOKEN: ${{ secrets.UMFCI_READ_TOKEN }}
      UMF_STATUS_TOKEN: ${{ secrets.UMF_STATUS_TOKEN }}
```

The reusable workflow matches the supplied SHA to the open PR's head and runs standard GitHub-hosted runners. A complete run reports `ci/development` on that SHA. The bundle passes between jobs only as a short-lived artifact of the private caller. Keep engine files, build products, logs containing restricted UE data and caches within authorized storage. A missing SDK, unavailable free runners or incomplete jobs cannot produce a success status.

Use the [versioned ruleset](../.github/rulesets/protect-main.json) after an authorized run has passed the entire matrix and the public aggregate is available. Keep the live required checks until then. Retire the old CircleCI trigger and webhook in the account once the new checks are enforcing the gate.

## Release Validation

Use [Release Validation](../.github/workflows/release-validation.yml) from a private manual caller with the exact reviewed Release Candidate SHA. Its Win64 x64 `umf-release` runner uses a complete UE installation set by `UMF_FULL_UE`. This can be an authorized workstation with no paid runner. The same entry point can run directly on that workstation.

```text
python -m Test.SharedRuntime.release_validation --engine <full-ue> --work <new-directory> --commit <full-sha>
```

The release flow stages the integration assets under their existing package names, builds the Monolithic Game, runs MDT plan/build/inspect, builds the author projects, and cooks, stages and packages the host and Mods. Its projects enable only UMF-owned plugins. MDT builds them in a dedicated UBT Editor target with default engine plugins disabled. The release version comes from the candidate commit and the digest comes from the actual cooked `AssetRegistry.bin`. The staged game loads two independent C ABI extensions and verifies Hook callbacks, API permissions, resource replacement, and unload. It then runs the 200-case performance benchmark. An existing full engine with Modular Game build products can run the same sample-game verification with `--game-target UnrealModFrameworkDevModular`. Preserve the reports in the authorized workspace. [SharedRuntime procedure](../Test/SharedRuntime/README.md)

## Run the call benchmark

Run `python Test/Performance/run_benchmark.py --engine <full-ue> --report <result.md>` to build, cook and measure the standalone benchmark. The benchmark builds the native extension for the selected desktop Game platform and architecture and includes the 200 host and packaged-Mod API cases. The Mod C++ samples time the C ABI Hook round trip and the host's engine call. The Mod Blueprint samples use graphs loaded from the same archive.

## Phase 1 verification

The current lock pins the Windows x86_64 Editor and five Game releases for Windows x86_64, Windows ARM64, Linux x86_64, Linux ARM64 and macOS ARM64. The remaining four entries await qualified releases. The full Windows UE 5.6.1 Editor and the verified Windows x86_64 Game SDK provide local coverage while those entries are unavailable. The four-producer critical path, both ARM64 Game jobs and the full Release Validation run require their real runner and SDK environments.
