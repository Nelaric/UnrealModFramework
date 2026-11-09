<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This page explains contribution checks, Linux builds, and documentation publishing. Start with the [contribution guide](../CONTRIBUTING.md) when preparing a change.

## From a change to `main`

1. Make a focused change on a branch or fork. Run the relevant [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request to `main` using the [PR template](../.github/pull_request_template.md). Link the related issue when applicable.
3. Resolve failures and complete maintainer review before merging. Maintainers should configure branch protection to require the applicable checks.
4. Once [API Pages is enabled](Hosting/README.md), a merge to `main` publishes the generated API documentation.

## Checks on a pull request

The [Quality workflow](../.github/workflows/quality.yml) checks PR and commit naming, formatting and text conventions, and API documentation. It uses the pinned versions in the workflow and `.config/dotnet-tools.json`.

The [trusted PR build workflow](../.github/workflows/fork-pr-linux.yml) is enabled with the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED=true` after completing the [CircleCI setup](CircleCI.md). Before that variable is enabled, its jobs are skipped. The existing CircleCI push pipeline remains separate.

The PR workflow validates the submitted commit, starts CircleCI, and displays parallel Game and Editor jobs followed by a result summary. Each job reports its own status as `ci/linux-game-build` or `ci/linux-editor-build`; `ci/fork-pr-linux-build` summarizes both. The summary links to CircleCI logs, and the Actions jobs mirror checkout and compile output without copying authentication output. Contributors do not need a CircleCI account.

For fork PRs, changes to CI workflows, automation scripts, Unreal descriptors and build scripts, CMake files, or executable helper scripts require a maintainer to handle them in a source-repository branch. The policy checks both old and new paths for renamed files. Source-repository PRs can change those files and still use the trusted workflow. Native mod ABI changes also require maintainer review.

## Linux build scope

Both the push configuration and trusted PR configuration build `UnrealModFramework/UnrealModFrameworkDev.uproject` with Unreal Engine 5.6.1 on Linux in Development configuration:

| CircleCI job | Work |
| --- | --- |
| `build_game_linux` | Compile `UnrealModFrameworkDev` and its enabled runtime modules. |
| `build_editor_linux` | Compile `UnrealModFrameworkDevEditor` and its supported editor modules, then package the independent `ModDevelopmentSDKEditor` plugin with UAT `BuildPlugin`. |

The jobs use separate writable staging directories and run the UE containers without network access after pulling the private engine image. Automatic host generation currently requires Win64 x64 MSVC. See the [host generation workflow](../README.md#mht-api-source-generation) for tool and input preparation.

There is no Server target. These jobs validate compilation and plugin packaging; they do not publish downloadable packages or run Unreal automation tests. Coverage of other platforms and newer supported engine releases remains a build coverage gap.

Each independent framework capability requires behavior-focused automated tests under the [review standard](CodingStandards/BuildAndReview.md#automation-tests), without a numeric coverage threshold. For an existing Unreal automation run, `Scripts/check_automation_report.py` can validate the exported report, log, and editor exit code:

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## If a check fails

Open the failed Actions job to read its summary and follow its CircleCI log link. The [CircleCI project](https://app.circleci.com/pipelines/github/Nelaric/UnrealModFramework) also shows the regular push builds. If the PR build is skipped, maintainers should check its enable variable, webhook setup, and secret as described in [CircleCI setup](CircleCI.md). Include the failed run link and attempted diagnosis when asking for review assistance.

The workflow definitions are [Quality](../.github/workflows/quality.yml), [trusted PR builds](../.github/workflows/fork-pr-linux.yml), [API Pages](../.github/workflows/pages.yml), [CircleCI push builds](../.circleci/config.yml), and [CircleCI PR builds](../.circleci/pr-build.yml).
