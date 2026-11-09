<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CircleCI.zh-CN.md)

# CircleCI Linux Builds

The [push configuration](../.circleci/config.yml) and [trusted PR configuration](../.circleci/pr-build.yml) each define two parallel Unreal Engine 5.6.1 Linux jobs. `build_game_linux` compiles `UnrealModFrameworkDev` and its enabled runtime modules. `build_editor_linux` compiles `UnrealModFrameworkDevEditor`, including `ExportMetadata`, `BlueprintIntegration`, and `ExportCodeGenerator`, then packages the independent `ModDevelopmentSDKEditor` plugin with UAT `BuildPlugin` for Linux. There is no Server target, package publication, or Editor automation test step.

## Active PR integration

The repository enables `CIRCLECI_PR_BUILDS_ENABLED=true` and stores the current trigger endpoint in the encrypted Actions secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`. The `UnrealModFramework PR builds` pipeline uses `.circleci/pr-build.yml`, with both configuration and initial checkout fixed to `main`. The setup steps below describe how to recreate this integration.

## Regular push builds

Use the CircleCI `github/Nelaric/UnrealModFramework` GitHub OAuth project with this repository as its configuration and checkout source, `.circleci/config.yml` as the configuration path, and a trigger for source-repository pushes. Set `GHCR_USERNAME` and `GHCR_TOKEN` in the CircleCI project's environment variables. The token must be able to pull `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`. Keep the values out of Git.

Keep **Build forked pull requests** and **Pass secrets to builds from forked pull requests** disabled. Fork builds use the separate trusted integration below. Both job types use a Linux machine executor with Docker and the `large` resource class.

## Enable trusted PR builds

The [Actions workflow](../.github/workflows/fork-pr-linux.yml) is opt-in. Its jobs remain skipped until the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED` is set to `true`. Set up the following integration before enabling it:

1. Commit the workflow, `Scripts/pr_build_policy.py`, `Scripts/fork_pr_build_bot.py`, and `.circleci/pr-build.yml` to `main` through the normal review process.
2. In the same CircleCI project, add a GitHub App pipeline. An OAuth organization can also install the GitHub App integration. Select `Nelaric/UnrealModFramework` as both configuration and checkout repository, `.circleci/pr-build.yml` as its configuration path, and `main` for the custom webhook's configuration and checkout branch. Preserve the existing OAuth push pipeline.
3. Add a **Custom webhook** trigger for that pipeline. Copy the complete generated HTTPS endpoint, including its secret, into the GitHub Actions secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`. The bot accepts only an `internal.circleci.com` endpoint. Do not copy a webhook or pipeline identifier from another repository.
4. Ensure the PR pipeline can access this project's `GHCR_USERNAME` and `GHCR_TOKEN`. Restrict the webhook to the trusted configuration and checkout from `main`.
5. Set the repository Actions variable `CIRCLECI_PR_BUILDS_ENABLED=true` and verify a reviewed PR. Require the naming, format, and API checks plus `ci/fork-pr-linux-build` in branch protection once the integration has been verified. The independent target statuses are `ci/linux-game-build` and `ci/linux-editor-build`.

The bot sends only the PR number, exact head SHA, and a random correlation nonce to the webhook. The trusted policy validates an open PR targeting this repository's `main` before fetching `refs/pull/<number>/head`, and verifies that the fetched commit still matches the requested SHA. Fork changes to protected CI and build paths are rejected. The protected paths include workflow and automation directories, `.gitattributes`, `.gitmodules`, Unreal descriptors and build scripts, CMake files, and executable helper scripts. Renames are checked using both paths. Maintainers handle such changes in source-repository branches.

Both Actions and CircleCI start from trusted `main` configuration. Actions never checks out the PR source with a write-enabled token. CircleCI checks out the validated commit only after the policy succeeds and pulls the private engine image afterward. GitHub receives separate Game and Editor statuses plus an aggregate result. Newer Actions runs supersede older status owners.

## Build isolation and verification

After pulling the private engine image, UE containers run with `--network none`. Each job copies `UnrealModFramework/` to its own writable temporary directory. The staged project is `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject`; the Editor job packages the independent plugin inside that staging directory. Generated packages are not uploaded.

Validate both configurations before requesting review:

```sh
circleci config validate .circleci/config.yml
circleci config validate .circleci/pr-build.yml
```

The [development workflow](DevelopmentWorkflow.md) describes checks, log links, build scope, and troubleshooting. See the [CircleCI custom webhook documentation](https://circleci.com/docs/guides/orchestrate/custom-webhooks/) and [pipeline values reference](https://circleci.com/docs/guides/orchestrate/pipeline-variables/) for service-side setup and trigger values.
