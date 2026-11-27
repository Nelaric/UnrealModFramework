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

`prepare-metadata` reuses a current bundle. For a missing or stale bundle it builds the native Development Editor with the bootstrap rules and invokes the export commandlet. `validate-metadata` checks an existing bundle for one target without invoking UBT. Normal MBT builds validate the bundle before UBT and generate native inputs independently for each target. CI gives MBT the same reviewed commit on every job. A local checkout with edited sources also fingerprints the current project files.

The four bundle files live under `Metadata/`. Paths use `Project/` and `Engine/` roots. Options and Blueprint declarations carry reflected names, roles and scalar-field shapes. Target-side configuration projections must match the producer's inputs. Incompatible redirects, architecture-specific config layers, changed Blueprint packages, source drift and payload corruption fail validation. The target derives C++ declarations, layout, compiler arguments, Hook objects and ABI from its own compile database. When a Blueprint uses a reflected struct, the target checks the field shape against its loaded reflected type. Bundle files are generated, and `Intermediate/` is ignored by Git.

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

Use [Release Validation](../.github/workflows/release-validation.yml) from a private manual caller with the exact reviewed Release Candidate SHA. Its Win64 x64 `umf-release` runner provides a full Editor and an already built matching modular Game engine via `UMF_FULL_UE` and `UMF_MODULAR_UE`. This can be an authorized workstation with no paid runner. The same entry point can run directly on that workstation.

```text
python -m Test.SharedRuntime.release_validation --engine <full-editor> --modular-engine <full-modular-game> --work <new-directory> --commit <full-sha>
```

The release flow stages the integration assets under their existing package names, builds the modular Game, runs MDT plan/build/inspect, builds the author projects, and cooks, stages and packages the host and Mods. It derives the release version from the candidate commit and hashes the actual cooked `AssetRegistry.bin`. The staged game runs the SharedRuntime/resource replacement and Mod unload verification, followed by the performance benchmark. Preserve the reports in the authorized workspace. [SharedRuntime procedure](../Test/SharedRuntime/README.md)

## Phase 1 verification

The current lock has independently verified Windows x86_64, Windows ARM64, Linux x86_64 and Linux ARM64 Game releases. The remaining six entries await qualified releases. The full Windows UE 5.6.1 Editor and the verified Windows x86_64 Game SDK provide local coverage while those entries are unavailable. The four-producer critical path, both ARM64 Game jobs and the full Release Validation run require their real runner and SDK environments.
