<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CircleCI.zh-CN.md)

# CircleCI Linux Builds

Both our push pipeline ([.circleci/config.yml](../.circleci/config.yml)) and trusted PR pipeline ([.circleci/pr-build.yml](../.circleci/pr-build.yml)) run two parallel Unreal Engine 5.6.1 Linux x64 Development jobs:

- `build_game_linux` compiles the `UnrealModFrameworkDev` game target.
- `build_editor_linux` compiles `UnrealModFrameworkDevEditor`, runs the Hook dispatch automation test, and builds the standalone `ModDevelopmentSDKEditor` authoring plugin.

Both pipelines build the native UHT tools directly from the commit under test. In addition, the Editor job compiles MDT and MBT so [build-editor-plugin.sh](../.circleci/build-editor-plugin.sh) can invoke the engine's bundled .NET host and Editor `BuildId`, generate the authoring plugin from embedded resources, and publish the verified package to `Dist/ModDevelopmentSDKEditor` inside the job's staging directory.

## PR integration overview

Trusted PR builds are gated by the GitHub Actions repository variable `CIRCLECI_PR_BUILDS_ENABLED=true` and dispatched through the Actions secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`. On CircleCI, the `UnrealModFramework PR builds` pipeline always loads `.circleci/pr-build.yml` and performs its initial checkout from `main` before validating and fetching the PR commit.

## Regular push builds

Push builds run under the CircleCI `github/Nelaric/UnrealModFramework` GitHub OAuth project:

- Set this repository as both the configuration and checkout source, using `.circleci/config.yml` and triggering on source-repository pushes.
- Configure `GHCR_USERNAME` and `GHCR_TOKEN` in CircleCI project environment variables with read access to `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`. Never commit credentials to Git.
- Keep **Build forked pull requests** and **Pass secrets to builds from forked pull requests** turned **off**—fork PRs are handled exclusively through the trusted handoff described below.

Both jobs run on Linux `machine` executors with Docker using the `large` resource class.

## Enable trusted PR builds

The [fork-pr-linux.yml](../.github/workflows/fork-pr-linux.yml) workflow stays dormant until `CIRCLECI_PR_BUILDS_ENABLED` is set to `true`. Complete all five steps below before flipping that switch:

1. Merge the Actions workflow, `Scripts/pr_build_policy.py`, `Scripts/fork_pr_build_bot.py`, and `.circleci/pr-build.yml` into `main` through normal code review.
2. In the same CircleCI project, create a **GitHub App** pipeline (an OAuth organization can install the GitHub App integration alongside existing OAuth pipelines). Point both configuration and checkout to `Nelaric/UnrealModFramework`, set the config path to `.circleci/pr-build.yml`, and pin both the configuration branch and checkout branch for custom webhooks to `main`. Leave the existing OAuth push pipeline unchanged.
3. Add a **Custom webhook** trigger to the new pipeline and store the full generated HTTPS URL (including its secret query parameter) in the GitHub Actions secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`. The handoff bot accepts only `internal.circleci.com` endpoints; never reuse a webhook or pipeline ID across repositories.
4. Confirm that the PR pipeline can read `GHCR_USERNAME` and `GHCR_TOKEN`, and restrict the webhook so both configuration and initial checkout come strictly from `main`.
5. Set the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED=true` and test the end-to-end flow on a reviewed PR. Once confirmed, add `ci/fork-pr-linux-build` alongside the naming, formatting, and API documentation checks in branch protection (`ci/linux-game-build` and `ci/linux-editor-build` report the per-target statuses).

### Security model and handoff flow

The handoff bot ([fork_pr_build_bot.py](../Scripts/fork_pr_build_bot.py)) sends only the PR number, exact head commit SHA, and a random correlation nonce to the webhook. Before CircleCI fetches `refs/pull/<number>/head`, the policy script verifies that the PR is open against `Nelaric/UnrealModFramework:main` and checks after fetching that `HEAD` still matches the approved SHA. Any fork PR that touches protected CI or build files—workflow and script directories, `.gitattributes`, `.gitmodules`, Unreal descriptors and build rules, CMake files, or executable scripts (checking both old and new paths on renames)—is rejected so a maintainer can review and run it from an in-repository branch.

Both GitHub Actions and CircleCI boot from trusted `main` definitions: Actions never checks out untrusted PR code with a write token, and CircleCI checks out the PR commit only after policy validation passes and before pulling the private UE image. Newer Actions runs supersede older status owners, transient GitHub API read errors are retried with bounded backoff, and each handoff sends at most one webhook trigger.

## Build isolation and verification

Once the private engine image is pulled, UE build containers run completely offline (`--network none`) against an isolated writable project copy at `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject` (see [ue-linux.Dockerfile](../.circleci/ue-linux.Dockerfile)).

Linux jobs invoke `ModBuildTool build-ue-target` using the container image's `libclang` 18 and matching Clang resource headers. UBT compiles generated host units inside their owning modules, and MBT writes `host-link.json` after verifying the final Linux binaries. When `HookSources.json` is present, the Editor job runs `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` (falling back to `System.Core.String.AppendInt` when a generation has no Hook map) and verifies both the editor exit code and the test pass marker before building `ModDevelopmentSDKEditor`.

Whenever you edit either CircleCI configuration, validate both files locally before requesting review:

```sh
circleci config validate .circleci/config.yml
circleci config validate .circleci/pr-build.yml
```

For check order, log navigation, and day-to-day troubleshooting, see [Development and CI Workflow](DevelopmentWorkflow.md). For CircleCI webhook specifics, refer to the [custom webhooks guide](https://circleci.com/docs/guides/orchestrate/custom-webhooks/) and [pipeline values reference](https://circleci.com/docs/guides/orchestrate/pipeline-variables/).
