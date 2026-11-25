<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This page explains contribution checks, Linux builds, and documentation publishing. Start with the [contribution guide](../CONTRIBUTING.md) when preparing a change.

## From a change to `main`

1. Make a focused change on a branch or fork, and run the relevant [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request to `main` using the [PR template](../.github/pull_request_template.md), and link any related issue when applicable.
3. Resolve any check failures and complete maintainer review before merging. Maintainers should configure branch protection to require the applicable checks.
4. Once [API Pages is enabled](Hosting/README.md), merging to `main` publishes the generated API documentation.

## Checks on a pull request

The Quality workflow checks PR and commit naming, formatting, text conventions, and API documentation. It also runs unit tests for the CircleCI handoff and uses standalone Linux fixtures to verify automatic Hook compilation and linking across COFF, ELF, and Mach-O targets, as well as MST cross-file exception analysis with GCC and Clang compilation databases. Tool versions are pinned in the workflow and `.config/dotnet-tools.json`. [Quality workflow](../.github/workflows/quality.yml)

The [trusted PR build workflow](../.github/workflows/fork-pr-linux.yml) is enabled by setting the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED=true` after completing the [CircleCI setup](CircleCI.md). Until that variable is enabled, its jobs are skipped. The existing CircleCI push pipeline remains separate.

The PR workflow validates the submitted commit, triggers CircleCI, and displays parallel Game and Editor jobs followed by a result summary. Each job reports its own status (`ci/linux-game-build` or `ci/linux-editor-build`), while `ci/fork-pr-linux-build` summarizes both. The summary links to the CircleCI logs, and the Actions jobs mirror checkout and compile output without exposing authentication output. Contributors do not need a CircleCI account.

For fork PRs, changes to CI workflows, automation scripts, Unreal descriptors and build scripts, CMake files, or executable helper scripts must be handled by a maintainer in a source-repository branch. The policy checks both old and new paths for renamed files. Source-repository PRs can modify these files and still run the trusted workflow. Native mod ABI changes also require maintainer review.

## Linux build scope

The push and trusted PR configurations build `UnrealModFramework/UnrealModFrameworkDev.uproject` for Linux x64 Development with Unreal Engine 5.6.1.

| CircleCI job | Tasks |
| --- | --- |
| `build_game_linux` | Compiles `UnrealModFrameworkDev` and its enabled runtime modules. |
| `build_editor_linux` | Compiles `UnrealModFrameworkDevEditor`, runs the C++ and Blueprint Hook dispatch automation test `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`, then builds and verifies the independent authoring plugin with `ModSDKTool build-editor`. |

Each job builds in an isolated writable staging directory and runs the UE container without network access after pulling the private engine image. `ModBuildTool build-ue-target` drives UBT with the generated Hook source copies, and UBT compiles the generated host bindings directly inside their owning module. The Editor job checks both the editor process exit code and the test success marker, retaining the verified authoring plugin in the job's staging directory. Server, Shipping, and other platform or engine combinations require additional validation. [ModRuntime UHT integration](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md)

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

Open the failed Actions job to read its summary and follow the link to its CircleCI logs. Regular push builds are also listed in the [CircleCI project](https://app.circleci.com/pipelines/github/Nelaric/UnrealModFramework). If a PR build is skipped, maintainers should verify the enable variable, webhook setup, and secret as described in [CircleCI setup](CircleCI.md). When asking for review assistance, include the failed run link and the diagnosis you have already tried.

Workflow definitions live in [Quality](../.github/workflows/quality.yml), [trusted PR builds](../.github/workflows/fork-pr-linux.yml), [API Pages](../.github/workflows/pages.yml), [CircleCI push builds](../.circleci/config.yml), and [CircleCI PR builds](../.circleci/pr-build.yml).
