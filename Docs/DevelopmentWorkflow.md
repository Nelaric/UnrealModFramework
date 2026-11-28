<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This page explains contribution checks, Linux builds, and documentation publishing. Start with the [contribution guide](../CONTRIBUTING.md) when preparing a change.

## From a change to `main`

1. Make a focused change on a branch or fork, and run the relevant [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request to `main` using the [PR template](../.github/pull_request_template.md), and link any related issue when applicable.
3. Resolve any check failures and complete maintainer review before merging. Maintainers should configure branch protection to require the applicable checks.
4. Once [API Pages is enabled](Hosting/README.md), merging to `main` publishes the generated API documentation.

## Development CI

[Development CI](../.github/workflows/development.yml) starts automatically for PRs to `main`. Code Quality pins the test merge commit used by the later jobs. Native Tools depends on Code Quality. For same-repository PRs, Code Quality also gates the four Editor jobs and six Game Prepare jobs in the protected `development-engine` environment. Fork PRs run the engine-free jobs. Each Game waits for its own Game Prepare and assigned Editor. Windows and Linux ARM64 use their platform's X64 Editor metadata. Each MacOS Game uses metadata from the Editor of the same architecture. Development Result summarizes the 18 upstream jobs and checks the current PR and `main` once before publishing the status.

Editor exports, validates, and publishes metadata before building the normal target and running headless `Nelaric.*` Automation Tests. Game Prepare executes all metadata-independent actions. Game installs the same locked private SDK on a fresh native runner, restores the project's checkpoint, imports metadata, and finishes the monolithic Development target. Development Result reports automation counts, artifact sizes, phase durations, action reuse, and the reasons for nonportable actions.

### Configure Engine Jobs

1. Configure the `development-engine` GitHub environment with maintainer review of the candidate. Store `UMFCI_READ_TOKEN` there with read access to the private UMFCI installer and releases. Set `UMF_STATUS_APP_ID` and `UMF_STATUS_APP_PRIVATE_KEY` for the GitHub App that publishes `ci/development` on the public test merge commit.
2. Set `UMFCI_RELEASE_TAG` in [Development CI](../.github/workflows/development.yml) to the published immutable UMFCI release for all ten SDK targets. The runner uses the installer and .NET SDK configuration at that tag. The release index maps each target to its named SDK archive and installation manifest.
3. For a manual run of a reviewed PR, dispatch from the protected default branch with the PR number, PR head SHA, current `main` base SHA, and GitHub test merge SHA. PR updates cancel older runs for the same PR.

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

[The main ruleset](../.github/rulesets/protect-main.json) defines the Development CI merge gate. Bind `ci/development` to the authorized GitHub App and `Code Quality` to GitHub Actions when enabling the ruleset.

## Quality Workflow

The Quality workflow checks PR and commit naming, formatting, text conventions, and API documentation. It uses standalone Linux fixtures to check automatic Hook compilation and linking across COFF, ELF, and Mach-O targets, as well as MST cross-file exception analysis with GCC and Clang compilation databases. Tool versions are pinned in the workflow and `.config/dotnet-tools.json`. [Quality workflow](../.github/workflows/quality.yml)

## Read Unreal automation results

Each independent framework capability requires behavior-focused automated tests as specified in the [review standard](CodingStandards/BuildAndReview.md#automation-tests), without a numeric coverage threshold. For an existing Unreal automation run, `Scripts/check_automation_report.py` validates the exported report, log, and editor exit code:

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## Run the call benchmark

From the repository root, run the [benchmark driver](../Test/Performance/run_benchmark.py) with a UE 5.6 installation and an explicit report path so the checked-in measurement is preserved.

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

The standalone benchmark runs 200 cases with 1,000 samples per available path. Win64 x64 measures host C++, host Blueprint, and the C++ and Blueprint paths inside a mounted `BenchmarkMod`. Use an engine source build capable of producing a modular Development Game, or pass the matching modular runtime via `--modular-runtime`. Other supported platforms run only the host paths. Unavailable paths are marked as `未测试` in the report. The summary reports the mean of available case means for each column and reflects the tested machine and fixtures; see the [checked-in results](../Test/Performance/ue-call-benchmark.md).

## If a check fails

Open the failed Actions job to read its log and summary. When asking for review assistance, include the failed run link and the diagnosis you have already tried.

Workflow definitions live in [Development CI](../.github/workflows/development.yml), [Quality](../.github/workflows/quality.yml), and [API Pages](../.github/workflows/pages.yml).
