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

[Development CI](../.github/workflows/development.yml) runs Code Quality on the PR test merge commit. Native Tools depends on Code Quality. Public PR and manual runs cover the engine-free jobs. In an authorized private run, four Editor jobs and six Game Prepare jobs also depend on Code Quality. Each Game job waits for its own Game Prepare job and the designated Editor. Windows and Linux ARM64 use their platform's X64 Editor metadata. Each MacOS Game uses metadata from the Editor of the same architecture. Development Result summarizes the 18 upstream jobs.

Editor exports, validates, and uploads metadata before building the normal target and running headless `Nelaric.*` Automation Tests. Game Prepare executes all metadata-independent actions. Game restores the installed SDK and the project's checkpoint on another native runner, validates the restored state, imports metadata, and finishes the monolithic Development target. Development Result reports automation counts, artifact sizes, phase durations, action reuse, and the reasons for nonportable actions.

### Configure And Dispatch A Private Run

1. Install this workflow, its local composite action, `Scripts/CI/`, `Scripts/check_git_naming.py`, and `Scripts/check_automation_report.py` on the protected default branch of an authorized private Nelaric repository. Limit workflow write and dispatch access to maintainers. Give `UMFCI_READ_TOKEN` read access to the private UMFCI installer and releases. Install a GitHub App with commit-status write access only for the public UMF repository and configure `UMF_STATUS_APP_ID` and `UMF_STATUS_APP_PRIVATE_KEY` in the private repository.
2. In the private copy of [the engine lock](../.github/engine-lock.json), set the immutable UMFCI release tag, recipe, and manifest digest for each of the ten targets. Set the installer revision, upstream engine commit, exact .NET SDK version, and toolchain settings.
3. Add the command file described below to the protected default branch.
4. Dispatch with the PR number, reviewed PR head SHA, current `main` base SHA, and GitHub test merge SHA. The workflow uses these revisions to identify the candidate and reports `ci/development` for the test merge commit. PR updates cancel older public runs. A new authorized dispatch cancels the previous private run for the same PR.

### Connect The Build Phases

The private `.github/development-commands.json` contains `schemaVersion` set to `1` and a `commands` object. Each entry is a nonempty argument array for a real tool or a reviewed adapter that directly calls the implemented interface. CI executes each array from the candidate checkout without a shell. Arguments can use `{source}`, `{control}`, `{work}`, `{sdk}`, `{evidence}`, `{target}`, `{configuration}`, `{architecture}`, `{ubt_platform}`, `{ubt_architecture}`, `{commit}`, `{lock_sha256}`, `{engine_version}`, `{upstream_commit}`, `{toolchain}`, `{metadata_target}`, `{metadata_payload}`, and `{checkpoint_payload}`. The corresponding upper-case `UMF_` variables are also set. Keep executable permissions and relocation data in the project's checkpoint format.

| Command Keys | Required Behavior |
| --- | --- |
| `build-native-tools`, `prepare-libclang` | Build the required tools once per producer job and prepare the host libclang used by its target. Store restorable tools and dependencies with the checkpoint. |
| `prepare-editor-bootstrap`, `export-metadata`, `validate-metadata`, `build-editor` | Bootstrap only when the exporter requires it. Write the native metadata bundle to `{metadata_payload}`, validate it through the project, and build the normal Editor. The workflow runs the Editor test suite directly. The Game consumer calls the same validator with its assigned `{metadata_target}`. |
| `prepare-game`, `save-checkpoint`, `validate-checkpoint` | Resolve the real target, module and action graph. Execute all work permitted without metadata. Save the native checkpoint to `{checkpoint_payload}` and use the project validator for action validity, input fingerprints, toolchain, native tools, ABI inputs, and path relocation. CI transports the installed SDK alongside this payload. |
| `restore-checkpoint`, `import-metadata`, `resume-game`, `verify-game` | Restore the project state against the recovered SDK, check the effective host toolchain, and resume only the remaining work. Verify the monolithic target's native link receipt and provide the linked executable path to MBT's binary inspector. |
| `test-native-development` | Run the implemented metadata, checkpoint, SDK plan and manifest tests in the Native Tools job. This key receives `{source}`, `{control}`, and `{native}` for the already built executable directory. |

Project validators supply the following small reports in `{evidence}`. Derive reports from actual tool output and build state. Checkpoint serialization and resume remain responsibilities of the project's native interfaces.

- Each report includes `target`, `source_commit`, `engine_lock_sha256`, `engine_version`, `configuration`, `architecture`, and `input_sha256`. For metadata, use the Editor producer's target and architecture.
- `metadata.json` contains `bundle_valid`. The validator must compute the same source inputs independently on the Editor and Game runners.
- `checkpoint.json` adds `toolchain_sha256`, `build_manifest_sha256`, `paths_valid`, `native_tools_runnable`, `inputs_valid`, `actions_valid`, `completed_actions`, `pending_actions`, `metadata_independent_pending_actions`, and `nonportable_actions` as an object mapping reasons to counts. The restore adapter must verify the runner's effective toolchain and recreate the same normalized `{toolchain}` file. CI compares its SHA-256 to the producer's and requires zero pending metadata-independent actions.
- `game.json` includes `link_type` set to `Monolithic`, `link_receipt_valid`, the checkout-relative `link_receipt` and `binary`, the work-directory-relative `mbt`, `metadata_input_sha256`, `reused_actions`, `reexecuted_actions`, `reexecuted_portable_actions`, `pending_actions`, `metadata_dependent_seconds`, and `link_seconds`. Replayed actions must fit the checkpoint's reported nonportable actions and the count of replayed portable actions must be zero.

The workflow uses each producer's immutable artifact ID, target, commit, and attempt. The reuse window for a failed-job rerun is one hour from upload within the same workflow run. To regenerate artifacts after that window or after cleanup by Development Result, rerun all jobs. GitHub's shortest native expiration is one day. [GitHub artifact retention](https://github.com/actions/upload-artifact#retention-period).

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
