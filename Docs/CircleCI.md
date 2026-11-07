<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CircleCI.zh-CN.md)

# CircleCI Linux builds

The repository's [CircleCI configuration](../.circleci/config.yml) defines two parallel UE 5.6.1 Linux jobs. `build_game_linux` compiles the `UnrealModFrameworkDev` Game target and its enabled `ModRuntime` plugin. `build_editor_linux` compiles `UnrealModFrameworkDevEditor`, including `ModRuntimeEditor`, then packages the independent `SourcePlugins/ModDevelopmentEditor` plugin with UAT `BuildPlugin` for Linux. The repository has no Server target or framework automation tests yet. The jobs validate builds; they do not publish packages.

## Connect the repository

1. Grant the existing CircleCI GitHub App installation for the `Nelaric` organization access to `Nelaric/UnrealModFramework`.
2. In CircleCI, create the `UnrealModFramework` project. Use the GitHub repository as both the configuration and checkout source, with `.circleci/config.yml` as the configuration path. Create a push trigger for branches in the source repository. Do not enable a fork PR trigger that exposes project credentials to untrusted code.
3. In the CircleCI project's **Environment Variables** settings, set `GHCR_USERNAME` to a GitHub account linked to Epic Games and `GHCR_TOKEN` to its package-read token. The token must be able to pull `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`. Keep both values out of Git. If the existing Nelaric CircleCI project uses credentials that meet these requirements, enter them again here; CircleCI does not reveal stored values.
4. Validate the configuration with `circleci config validate`, trigger a pipeline from the project, and confirm both jobs pass. CircleCI's Linux machine executor needs Docker and a `large` resource class for these jobs.

The push trigger gives feedback for branches in the source repository and for `main`. The GitHub Actions [quality workflow](../.github/workflows/quality.yml) continues to run on pull requests. Fork PR builds that require the private UE image need a separate trusted trigger design; project secrets must not be passed directly to fork jobs.

The container runs without network access after the private engine image is pulled. Each job copies the checkout to a writable temporary directory before compiling because the UE container user must write generated files. The Editor job packages the authoring plugin into that temporary directory and does not upload the package as an artifact.
