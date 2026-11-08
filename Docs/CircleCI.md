<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CircleCI.zh-CN.md)

# CircleCI Linux builds

The repository's [CircleCI configuration](../.circleci/config.yml) defines two parallel UE 5.6.1 Linux jobs. `build_game_linux` compiles the `UnrealModFrameworkDev` Game target and its enabled `ModRuntime` plugin. `build_editor_linux` compiles `UnrealModFrameworkDevEditor`, including `RuntimeEditor` and `BlueprintIntegration`, then packages the independent `UnrealModFramework/SourcePlugins/ModDevelopmentEditor` plugin with UAT `BuildPlugin` for Linux. The repository has no Server target. The jobs validate builds but do not run Editor automation tests or publish packages.

## Project connection

The CircleCI project uses the `github/Nelaric/UnrealModFramework` GitHub OAuth connection. Its configuration and checkout source are this repository, its configuration path is `.circleci/config.yml`, and its trigger runs on all pushes. `GHCR_USERNAME` and `GHCR_TOKEN` were imported from the existing Nelaric gameplay project into this project's **Environment Variables**. The token must be able to pull `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`; keep both values out of Git. CircleCI's **Build forked pull requests** and **Pass secrets to builds from forked pull requests** settings are off.

If the CircleCI project is recreated, select the repository's committed configuration on `main`, import the two GHCR variables from `nelaric-unreal-gameplay`, keep both fork settings off, and confirm that both jobs pass. Validate configuration changes with `circleci config validate` before pushing them. CircleCI's Linux machine executor needs Docker and a `large` resource class for these jobs.

The push trigger gives feedback for branches in the source repository and for `main`. The GitHub Actions [quality workflow](../.github/workflows/quality.yml) continues to run on pull requests. Fork PR builds that require the private UE image need a separate trusted trigger design; project secrets must not be passed directly to fork jobs.

The container runs without network access after the private engine image is pulled. Each job copies the UE project from `UnrealModFramework/` into a writable temporary directory before compiling because the UE container user must write generated files. The staged project remains at `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject` in the container. The Editor job packages the authoring plugin into that temporary directory and does not upload the package as an artifact.
