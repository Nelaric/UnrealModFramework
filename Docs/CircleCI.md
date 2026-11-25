<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CircleCI.zh-CN.md)

# CircleCI Linux Builds

The push and trusted PR configurations define two parallel Unreal Engine 5.6.1 Linux x64 Development jobs: `build_game_linux` builds `UnrealModFrameworkDev`, while `build_editor_linux` builds `UnrealModFrameworkDevEditor`, runs the Hook dispatch automation test, and builds the independent `ModDevelopmentSDKEditor` plugin. [Push configuration](../.circleci/config.yml) · [Trusted PR configuration](../.circleci/pr-build.yml)

Both pipelines build MDT and MBT from the commit under test. The shared build script uses the engine's bundled .NET host and Editor BuildId; MDT generates the plugin from embedded resources and publishes the verified package to `Dist/ModDevelopmentSDKEditor` in the job's staging directory. [Editor plugin build script](../.circleci/build-editor-plugin.sh)

## Active PR integration

This repository sets `CIRCLECI_PR_BUILDS_ENABLED=true` and stores the trigger endpoint in the encrypted Actions secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`. The `UnrealModFramework PR builds` pipeline uses `.circleci/pr-build.yml`, with both its configuration and initial checkout pinned to `main`. The setup steps below explain how to recreate this integration.

## Regular push builds

Use the CircleCI `github/Nelaric/UnrealModFramework` GitHub OAuth project with this repository as both the configuration and checkout source, `.circleci/config.yml` as the configuration path, and a trigger for source-repository pushes. Set `GHCR_USERNAME` and `GHCR_TOKEN` in the CircleCI project's environment variables; the token must be able to pull `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`. Never commit these values to Git.

Keep **Build forked pull requests** and **Pass secrets to builds from forked pull requests** disabled, as fork builds use the separate trusted integration described below. Both job types run on a Linux machine executor with Docker using the `large` resource class.

## Enable trusted PR builds

The [Actions workflow](../.github/workflows/fork-pr-linux.yml) is opt-in: its jobs are skipped until the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED` is set to `true`. Complete the following setup before enabling it:

1. Commit the workflow, `Scripts/pr_build_policy.py`, `Scripts/fork_pr_build_bot.py`, and `.circleci/pr-build.yml` to `main` through the normal review process.
2. In the same CircleCI project, add a GitHub App pipeline (an OAuth organization can also install the GitHub App integration). Select `Nelaric/UnrealModFramework` as both the configuration and checkout repository, `.circleci/pr-build.yml` as the configuration path, and `main` as the configuration and checkout branch for the custom webhook. Keep the existing OAuth push pipeline intact.
3. Add a **Custom webhook** trigger to that pipeline, then copy the complete generated HTTPS endpoint (including its secret) into the GitHub Actions secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`. The bot accepts only `internal.circleci.com` endpoints; do not reuse a webhook or pipeline identifier from another repository.
4. Ensure the PR pipeline has access to this project's `GHCR_USERNAME` and `GHCR_TOKEN`, and restrict the webhook to the trusted configuration and checkout from `main`.
5. Set the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED=true` and verify the pipeline on a reviewed PR. Once verified, require the naming, format, and API checks together with `ci/fork-pr-linux-build` in branch protection. The individual target statuses are `ci/linux-game-build` and `ci/linux-editor-build`.

The bot sends only the PR number, exact head SHA, and a random correlation nonce to the webhook. Before fetching `refs/pull/<number>/head`, the trusted policy confirms that the PR is open and targets this repository's `main` branch, then verifies that the fetched commit still matches the requested SHA. Fork PR changes to protected CI and build paths are rejected—these include workflow and automation directories, `.gitattributes`, `.gitmodules`, Unreal descriptors and build scripts, CMake files, and executable helper scripts, with renames checked against both old and new paths. Maintainers handle such changes in source-repository branches.

Both Actions and CircleCI start from the trusted `main` configuration. Actions never checks out PR source code with a write-enabled token, while CircleCI checks out the validated commit only after the policy passes and pulls the private engine image afterward. GitHub receives separate Game and Editor statuses along with an aggregate result. Newer Actions runs supersede older status owners. The handoff bot retries transient API read failures with a bounded backoff and sends one webhook request per handoff. [Handoff implementation](../Scripts/fork_pr_build_bot.py)

## Build isolation and verification

After pulling the private engine image, UE containers run with `--network none`. Each job stages a writable copy of the project at `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject`. The Linux jobs run `ModBuildTool build-ue-target` and use the image's libclang 18 and matching resource headers. The Editor job runs `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` in `UnrealEditor` and requires a successful exit and test log marker before building the authoring plugin. [UE image dependencies](../.circleci/ue-linux.Dockerfile)

Validate both configurations before requesting review:

```sh
circleci config validate .circleci/config.yml
circleci config validate .circleci/pr-build.yml
```

See the [development workflow](DevelopmentWorkflow.md) for checks, log links, build scope, and troubleshooting. For service-side setup and trigger values, see the [CircleCI custom webhook documentation](https://circleci.com/docs/guides/orchestrate/custom-webhooks/) and [pipeline values reference](https://circleci.com/docs/guides/orchestrate/pipeline-variables/).
