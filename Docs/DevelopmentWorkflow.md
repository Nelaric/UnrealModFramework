<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This page describes Development CI, local builds, Release Validation, and documentation publishing. Start with the [contribution guide](../CONTRIBUTING.md) when preparing a change.

## From a change to `main`

1. Make a focused change on a branch or fork, and run the relevant [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request to `main` using the [PR template](../.github/pull_request_template.md), and link any related issue when applicable.
3. Resolve any check failures and complete maintainer review before merging. Maintainers should configure branch protection to require the applicable checks.
4. Once [API Pages is enabled](Hosting/README.md), merging to `main` publishes the generated API documentation.

## Development CI

[Development CI](../.github/workflows/development.yml) starts automatically for PRs to `main`. Code Quality pins the test merge commit used by the later jobs. Native Tools depends on Code Quality. For same-repository PRs, Code Quality also gates the four Editor jobs and six Game Prepare jobs in the protected `development-engine` environment. Fork PRs run the engine-free jobs. Each Game waits for its own Game Prepare and assigned Editor. Windows and Linux ARM64 use their platform's x86_64 Editor metadata. Each MacOS Game uses metadata from the Editor of the same architecture. The native Development Result check summarizes the 18 upstream jobs and confirms the current PR and `main` before reporting success.

Editor exports, validates, and publishes metadata before building the normal target and running headless `Nelaric.*` Automation Tests. Game Prepare executes all metadata-independent actions. Game installs the same locked private SDK on a fresh native runner, restores the project's checkpoint, imports metadata, and finishes the monolithic Development target. Development Result reports automation counts, artifact sizes, phase durations, action reuse, and the reasons for nonportable actions.

### Configure Engine Jobs

1. Configure the `development-engine` GitHub environment with maintainer review of the candidate. Store `UMFCI_READ_TOKEN` as an environment secret with `Contents: read` access limited to the private UMFCI repository.
2. Set `UMFCI_RELEASE_TAG` in [Development CI](../.github/workflows/development.yml) to the published immutable UMFCI release for all ten SDK targets. The runner uses the installer and .NET SDK configuration at that tag. The release index maps each target to its named SDK archive and installation manifest.
3. For a manual run of a reviewed PR, dispatch from the protected default branch with the PR number, PR head SHA, current `main` base SHA, and GitHub test merge SHA. Manual run checks are attached to the dispatch ref. The automatic PR run supplies the merge check. PR updates cancel older runs for the same PR.

### Connect The Build Phases

The [Development command configuration](../.github/development-commands.json) sets `schemaVersion` to `1` and maps each phase to an argument array for `Scripts/development_commands.py`. CI executes each array directly from the candidate checkout. Arguments can use `{source}`, `{control}`, `{work}`, `{sdk}`, `{evidence}`, `{target}`, `{configuration}`, `{architecture}`, `{ubt_platform}`, `{ubt_architecture}`, `{commit}`, `{lock_sha256}`, `{engine_version}`, `{upstream_commit}`, `{toolchain}`, `{metadata_target}`, `{metadata_payload}`, and `{checkpoint_payload}`. The engine phases also receive the corresponding upper-case `UMF_` variables. Keep executable permissions and relocation data in the project's checkpoint format.

| Command Keys | Required Behavior |
| --- | --- |
| `build-native-tools`, `prepare-libclang` | Build the required tools once per producer job and prepare the host libclang used by its target. Store restorable tools and dependencies with the checkpoint. |
| `prepare-editor-bootstrap`, `export-metadata`, `validate-metadata`, `build-editor` | Bootstrap only when the exporter requires it. Write the native metadata bundle to `{metadata_payload}`, validate it through the project, and build the normal Editor. The workflow runs the Editor test suite directly. The Game consumer calls the same validator with its assigned `{metadata_target}`. |
| `prepare-game`, `save-checkpoint`, `validate-checkpoint` | Resolve the real target, module and action graph. Execute all work permitted without metadata. Save the native project checkpoint to `{checkpoint_payload}` and use the project validator for action validity, input fingerprints, toolchain, native tools, ABI inputs, and path relocation. |
| `restore-checkpoint`, `import-metadata`, `resume-game`, `verify-game` | Restore project state against the freshly installed locked SDK and resume the remaining work. Verify the monolithic target's native link receipt and provide the linked executable path to MBT's binary inspector. |
| `test-native-development` | Run the implemented metadata, checkpoint, SDK plan and manifest tests in the Native Tools job. This key receives `{source}`, `{control}`, and `{native}` for the already built executable directory. |

Project validators supply the following small reports in `{evidence}`. Derive reports from actual tool output and build state. Checkpoint serialization and resume remain responsibilities of the project's native interfaces.

- Each report includes `target`, `source_commit`, `engine_lock_sha256`, `engine_version`, `configuration`, `architecture`, and `input_sha256`. Use the supplied `UMF_LOCK_SHA256` value for `engine_lock_sha256`. CI derives it from the UMFCI release ID and tag. For metadata, use the Editor producer's target and architecture.
- `metadata.json` contains `bundle_valid`. The validator must compute the same source inputs independently on the Editor and Game runners.
- `checkpoint.json` adds `toolchain_sha256`, `build_manifest_sha256`, `paths_valid`, `native_tools_runnable`, `inputs_valid`, `actions_valid`, `completed_actions`, `pending_actions`, `metadata_independent_pending_actions`, and `nonportable_actions` as an object mapping reasons to counts. CI independently configures the resume runner's toolchain and compares the normalized `{toolchain}` SHA-256 to the producer's before restoring the checkpoint.
- `game.json` includes `link_type` set to `Monolithic`, `link_receipt_valid`, the checkout-relative `link_receipt` and `binary`, the work-directory-relative `mbt`, `metadata_input_sha256`, `reused_actions`, `reexecuted_actions`, `reexecuted_portable_actions`, `pending_actions`, `metadata_dependent_seconds`, and `link_seconds`. Replayed actions must fit the checkpoint's reported nonportable actions and the count of replayed portable actions must be zero.

The workflow uploads each metadata bundle or project checkpoint and its build record as a GitHub Actions artifact. The project payload paths are reserved for outputs without Unreal Engine files. Each Game downloads the two upstream artifacts by ID within the same run. The checkpoint record identifies the locked UMFCI SDK, which the runner installs directly from its private release. Build logs are available in the Actions job. Each handoff can be used for one hour after it is sealed. GitHub's minimum retention is one day, and Development Result removes consumed artifacts after success. To regenerate artifacts after the handoff window or cleanup, rerun all jobs.

### Switch The Merge Gate

Quality supplies the current required checks. Enable the versioned Development CI ruleset when full engine validation is available.

[The main ruleset](../.github/rulesets/protect-main.json) defines `Code Quality` and `Development Result` as native GitHub Actions checks. Development Result requires all engine jobs to pass. Maintainers can open a same-repository PR for reviewed fork changes that need full engine validation.

## Quality Workflow

Quality provides the current required checks for PR and commit naming, format and text, and API documentation. [Quality workflow](../.github/workflows/quality.yml)

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

## Read Unreal automation results

Each independent framework capability requires behavior-focused automated tests as specified in the [review standard](CodingStandards/BuildAndReview.md#automation-tests), without a numeric coverage threshold. For an existing Unreal automation run, `Scripts/check_automation_report.py` validates the exported report, log, and editor exit code:

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## Release Validation

Release Validation uses the [engine lock](../.github/engine-lock.json) for its full engine revision.

Use [Release Validation](../.github/workflows/release-validation.yml) from a private manual caller with the exact reviewed Release Candidate SHA. Its Win64 x64 `umf-release` runner uses a complete UE installation set by `UMF_FULL_UE`. This can be an authorized workstation with no paid runner. The same entry point can run directly on that workstation.

```text
python -m Test.SharedRuntime.release_validation --engine <full-ue> --work <new-directory> --commit <full-sha>
```

The release flow stages the integration assets under their existing package names, builds the Monolithic Game, runs MDT plan/build/inspect, builds the author projects, and cooks, stages and packages the host and Mods. Its projects enable only UMF-owned plugins. MDT builds them in a dedicated UBT Editor target with default engine plugins disabled. The release version comes from the candidate commit and the digest comes from the actual cooked `AssetRegistry.bin`. The staged game loads two independent C ABI extensions and verifies Hook callbacks, API permissions, resource replacement, and unload. It then runs the 200-case performance benchmark. An existing full engine with Modular Game build products can run the same sample-game verification with `--game-target UnrealModFrameworkDevModular`. Preserve the reports in the authorized workspace. [SharedRuntime procedure](../Test/SharedRuntime/README.md)

## Run the call benchmark

Run `python Test/Performance/run_benchmark.py --engine <full-ue> --report <result.md>` to build, cook and measure the standalone benchmark. The benchmark builds the native extension for the selected desktop Game platform and architecture and includes the 200 host and packaged-Mod API cases. The Mod C++ samples time the C ABI Hook round trip and the host's engine call. The Mod Blueprint samples use graphs loaded from the same archive.

## If a check fails

Open the failed Actions job to read its log and summary. When asking for review assistance, include the failed run link and the diagnosis you have already tried.

Workflow definitions live in [Development CI](../.github/workflows/development.yml), [Quality](../.github/workflows/quality.yml), and [API Pages](../.github/workflows/pages.yml).
