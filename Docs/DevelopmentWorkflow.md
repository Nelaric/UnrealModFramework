<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This guide walks through local verification, pull request checks, CircleCI Linux builds, and API documentation publishing. If you are preparing your first change, start with the [contribution guide](../CONTRIBUTING.md).

## From a change to `main`

1. Make a focused change on a topic branch or fork and run the applicable [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request against `main` using the [PR template](../.github/pull_request_template.md), linking any related issue.
3. Address any CI failures and reviewer feedback. Branch protection requires all mandatory checks and a maintainer review before merging.
4. When [API Pages publishing](Hosting/README.md) is enabled, merging into `main` automatically rebuilds and deploys the API documentation site.

## Checks on a pull request

Every pull request triggers two workflows:

- **Quality ([quality.yml](../.github/workflows/quality.yml)):** Validates branch, commit, and PR naming, source formatting, UTF-8/newline conventions, and Doxygen API docs. It also runs unit tests for the CircleCI handoff bot, standalone Linux fixtures that verify automatic Hook compilation and linking across COFF, ELF, and Mach-O targets, and MST cross-file exception analysis against GCC and Clang compilation databases. Pinned tool versions live in the workflow file and `.config/dotnet-tools.json`.
- **Trusted Linux PR builds ([fork-pr-linux.yml](../.github/workflows/fork-pr-linux.yml)):** Runs once a maintainer completes [CircleCI setup](CircleCI.md) and sets the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED=true` (until then, its jobs are skipped). Regular push builds on `main` run through a separate CircleCI pipeline.

When triggered, the trusted PR workflow verifies the exact commit SHA, dispatches CircleCI, and surfaces parallel Game (`ci/linux-game-build`) and Editor (`ci/linux-editor-build`) statuses alongside an overall summary (`ci/fork-pr-linux-build`). Each Actions job mirrors checkout and compiler output while stripping authentication details, and the summary links directly to the CircleCI run. Contributors do not need a CircleCI account.

For fork PRs, any modification to CI workflows, automation scripts, Unreal `.uproject`/`.uplugin`/`.Build.cs`/`.Target.cs` files, CMake scripts, or executable helper scripts is rejected by the handoff policy (checking both old and new paths on renames) and must be tested by a maintainer in a source-repository branch. In-repository PRs can modify these files normally. Changes to native mod ABI contracts also require maintainer review.

## Linux build scope

Both push builds and trusted PR builds compile `UnrealModFramework/UnrealModFrameworkDev.uproject` for **Linux x64 Development** on **Unreal Engine 5.6.1**:

| CircleCI job | What it runs |
| --- | --- |
| `build_game_linux` | Compiles `UnrealModFrameworkDev` and its enabled runtime modules. |
| `build_editor_linux` | Compiles `UnrealModFrameworkDevEditor`, runs the C++ and Blueprint Hook dispatch automation test (`Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`), and builds and verifies the standalone authoring plugin via `ModSDKTool build-editor`. |

Each job stages a writable copy of the workspace and runs the UE container with network access disabled (`--network none`) after pulling the private engine image. `ModBuildTool build-ue-target` drives UBT with rewritten Hook source copies, UBT compiles generated host bindings inside their owning modules, and MBT writes `host-link.json` once final image verification succeeds. On the Editor side, the job checks both the process exit code and the automation success marker before building the authoring plugin. Dedicated Server, Shipping, and other OS or engine combinations must be validated separately (see the [ModRuntime UHT adapter guide](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.md)).

Every independent framework capability must include behavior-focused automated tests per the [review standard](CodingStandards/BuildAndReview.md#automation-tests), without a numeric coverage target. To validate an existing Unreal automation report offline, run:

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## Run the packaged shared Runtime regression

Whenever you touch SDK Runtime ownership, packaging dependencies, or caller token lifetimes, run the Win64 x64 packaged regression in [Test/SharedRuntime](../Test/SharedRuntime/README.md). It stages a modular Development Game with a Pak base release and two independently packaged mods, cross-checks staged DLLs against `host-link.json` and the published SDK, and verifies that unloading one mod revokes its caller access while the remaining mod continues calling the shared Runtime uninterrupted.

## Run the call benchmark

From the repository root, run [run_benchmark.py](../Test/Performance/run_benchmark.py) with a UE 5.6 installation and an explicit `--report` output path so you do not overwrite the checked-in baseline:

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

The standalone benchmark project executes 200 API cases at 1,000 samples per available path. On Win64 x64, it measures four columns—host C++, host Blueprint, and mounted `BenchmarkMod` C++ and Blueprint calls (requiring either a UE source build that produces a modular Development Game or a matching modular runtime passed via `--modular-runtime`). Other supported platforms run the two host columns and mark unavailable paths as `未测试`. Column summaries report the mean across all measured case means for the current machine; compare your numbers against the [checked-in baseline](../Test/Performance/ue-call-benchmark.md).

## Troubleshooting failed checks

Open the failed GitHub Actions job to inspect its summary and follow the link to the underlying CircleCI build logs (push builds are also listed on the [CircleCI pipelines page](https://app.circleci.com/pipelines/github/Nelaric/UnrealModFramework)). If a trusted PR build was skipped unexpectedly, maintainers should check `CIRCLECI_PR_BUILDS_ENABLED`, the custom webhook trigger, and `CIRCLECI_PR_BUILD_WEBHOOK_URL` against [CircleCI Linux Builds](CircleCI.md). When asking maintainers for help diagnosing a failure, share the failed Actions run URL and what you have already ruled out.

Workflow definitions: [quality.yml](../.github/workflows/quality.yml) · [fork-pr-linux.yml](../.github/workflows/fork-pr-linux.yml) · [pages.yml](../.github/workflows/pages.yml) · [.circleci/config.yml](../.circleci/config.yml) · [.circleci/pr-build.yml](../.circleci/pr-build.yml)
