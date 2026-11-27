<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This page explains contribution checks, desktop builds, and documentation publishing. Start with the [contribution guide](../CONTRIBUTING.md) when preparing a change.

## From a change to `main`

1. Make a focused change on a branch or fork, and run the relevant [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request to `main` using the [PR template](../.github/pull_request_template.md), and link any related issue when applicable.
3. Resolve any check failures and complete maintainer review before merging. Maintainers should configure branch protection to require the applicable checks.
4. Once [API Pages is enabled](Hosting/README.md), merging to `main` publishes the generated API documentation.

## Checks on a pull request

The [Quality workflow](../.github/workflows/quality.yml) checks naming, text, formatting and API documentation. Linux fixtures also exercise automatic Hook compilation and linking for COFF, ELF and Mach-O and cross-file MST analysis with GCC and Clang.

## Desktop build scope

[Desktop CI](../.github/workflows/desktop.yml) runs on pull requests to `main`, pushes to `main` and manual dispatch. The six GitHub-hosted runners cover 11 UE 5.6.1 Development jobs. All x64 runners and the Windows and macOS ARM64 runners build Game and Editor. Linux ARM64 builds Game.

| System | x64 runner | ARM64 runner |
| --- | --- | --- |
| Windows | `windows-2022` | `windows-11-arm` |
| macOS | `macos-15-intel` | `macos-15` |
| Linux | `ubuntu-24.04` | `ubuntu-24.04-arm` |

Each job builds the standalone C++ project PackageProbe from the pinned engine-package repository. Its runtime plugin derives a reflected class from GameplayAbilities. The verifier checks the generated reflection code, cooks the Engine Entry map and runs PackageProbe.Runtime.AbilityReflection. Editor jobs run it in the native editor. Game jobs stage and launch the Development game. The Unreal report records the result for the selected target and CPU.

The Windows and Linux ARM64 Game jobs use an x64 editor commandlet to cook the sample. Each launches the staged ARM64 game on its native runner.

Each job installs its own verified private package, pinned by the full producer commit in `UE_PACKAGE_RECIPE`. To promote individual targets independently, `UE_PACKAGE_RECIPES` accepts a JSON object mapping a target key such as `WINDOWS_X64_Editor` to its complete producer commit. The `Nelaric/UnrealEngine-CI` producer builds from a pinned Epic input, trims and inventories the files for one target, and verifies a fresh extraction on a second native runner. That runner also compiles and cooks a separate C++ project with a reflected plugin using GameplayAbilities. Published releases include per-file reasons, hashes and the sizes before and after trimming. A Game package includes the Editor commandlet and libraries needed to cook a code project.

Set `UE_PACKAGES_APP_ID` as a repository variable and `UE_PACKAGES_APP_PRIVATE_KEY` as an Actions secret. Install the organization's GitHub App on the private producer with read-only Contents permission. Each job uses a short-lived installation token to download its own release. The installer checks the target, architecture, verification record, archive parts and file inventory. [Package installer](../Scripts/install_ci_engine.py)

GitHub withholds repository secrets from fork pull requests. Public quality checks can run there. For engine validation, a maintainer reviews the submitted code and runs that revision on a branch in the source repository. Missing engine inputs fail the affected jobs. The aggregate check `Desktop build and automation` requires all 11 jobs to pass.

When the matrix passes, replace the three legacy required checks (`ci/linux-game-build`, `ci/linux-editor-build`, `ci/fork-pr-linux-build`) in the `main` ruleset with `Desktop build and automation`. Retain the quality checks. The legacy PR webhook is disabled with CIRCLECI_PR_BUILDS_ENABLED set to false. Finish retirement by disabling the CircleCI project push trigger and removing the obsolete webhook secret and enable variable.

## Run the call benchmark

From the repository root, run the [benchmark driver](../Test/Performance/run_benchmark.py) with a UE 5.6 installation and an explicit report path so the checked-in measurement is preserved.

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

The standalone benchmark runs 200 cases with 1,000 samples per available path. Win64 x64 measures host C++, host Blueprint, and the C++ and Blueprint paths inside a mounted `BenchmarkMod`. Use an engine source build capable of producing a modular Development Game, or pass the matching modular runtime via `--modular-runtime`. Other supported platforms run only the host paths. Unavailable paths are marked as `未测试` in the report. The summary reports the mean of available case means for each column and reflects the tested machine and fixtures; see the [checked-in results](../Test/Performance/ue-call-benchmark.md).

## If a check fails

Open the failed GitHub Actions job and inspect its first failing step. After Unreal starts, the job uploads the available host link receipt, automation log and report index. Include the run link and the failing target when requesting assistance.
