<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CONTRIBUTING.zh-CN.md)

# Contributing to UnrealModFramework

Thank you for your interest in UnrealModFramework. Bug reports, design feedback, documentation improvements, and code contributions are welcome in either English or Chinese.

The framework's host Unreal project lives under `UnrealModFramework/`. When proposing a new capability, start from the concrete host-game integration or mod-authoring problem you want to solve and walk through the intended developer workflow.

## Before opening an issue

Search [existing issues](https://github.com/Nelaric/UnrealModFramework/issues) first, and skim the [project overview](README.md) and [coding standards](Docs/CodingStandards/README.md) for context. Then pick the matching form from the [issue chooser](https://github.com/Nelaric/UnrealModFramework/issues/new/choose):

- **Bug report:** Provide clear reproduction steps, expected versus actual behavior, and environment details. When Unreal Engine is involved, include the engine version, OS, affected component, and build target (such as Game, Editor, or the standalone authoring plugin).
- **Capability proposal:** Describe the developer problem, target use case, desired behavior, and which layer should own it. A rough public API sketch is welcome but optional.
- **Documentation issue:** Link to the relevant page or section and point out what is unclear, outdated, or missing.

Blank issues remain open for topics that do not fit those templates. Keep each issue focused on a single topic, and never include credentials or private data in public reports.

## Project scope

Our development baseline is Unreal Engine 5.6. This repository houses the development host project and its `ModRuntime` plugin; ModSDKTool (MDT) generates the game-side SDK source and builds both the matching author SDK and the standalone `ModDevelopmentSDKEditor` plugin (see [README.md](README.md) and [Modules and Dependency Boundaries](Docs/CodingStandards/Modules.md)).

When designing a change, make clear which boundary owns it: the game runtime, the authoring editor, the `.mod` package protocol, the export/build toolchain, or a game-specific SDK. Reusable framework modules must stay free of game-specific rules. If a change touches runtime extensions, note the affected manifest or SDK contract version, target configuration, and host integration point. Call out any impact on public APIs, the native mod C ABI, archive compatibility, or standalone plugin builds.

## Preparing a pull request

For non-trivial code changes, we recommend opening an issue first so we can align on whether the change belongs in the framework and how it should be scoped. Small bug fixes and documentation updates can go straight to a pull request.

If you do not have write access to this repository, fork it, create a topic branch in your fork, and open a pull request against `Nelaric/UnrealModFramework:main`.

Keep each pull request focused on one cohesive change and fill out the [pull request template](.github/pull_request_template.md):

- Explain the problem, your approach, any public API or integration impact, and how you verified the result.
- Link related issues or proposals, using an auto-closing keyword only when the PR resolves the issue completely.
- Write `None` for design/compatibility impact on documentation-only changes, for risks/exceptions when none apply, and for reviewer notes when nothing needs special attention; otherwise, point reviewers directly to key files, tradeoffs, or open questions.
- If you need an exception to a project coding guideline, state the rule, affected scope, rationale, and alternatives considered.

All code must follow [Epic Games' Unreal Engine C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) alongside our [project coding standards](Docs/CodingStandards/README.md), which also cover file encodings, local checks, and naming rules for branches, commits, and PR titles (for example, branch `docs/clarify-contribution-guide` and title `docs: clarify contribution guide`).

### Verification and CI

Run the local checks that apply to your change as listed in [Build Scripts, Tooling, and Review](Docs/CodingStandards/BuildAndReview.md), and record both what you ran and anything you could not run locally in the PR description. You can preview API documentation updates locally with Doxygen (see [Preview locally](Docs/Hosting/README.md#preview-locally)), and verify SDK runtime changes with the packaged shared-Runtime regression described in [Development and CI Workflow](Docs/DevelopmentWorkflow.md). Every independent framework capability requires behavior-focused automated tests, though CI does not enforce a numeric coverage percentage.

On every pull request:

1. **GitHub Actions** checks branch/commit/PR naming, formatting, text encodings, and Doxygen API docs, and runs Linux fixtures covering cross-platform Hook compilation/linking and MST source analysis.
2. **Trusted Linux PR builds** validate the exact submitted commit and trigger CircleCI Game and Editor builds on Unreal Engine 5.6.1. The Editor job also runs the Hook dispatch automation test and builds `ModDevelopmentSDKEditor`. Contributors do not need a CircleCI account—both fork PRs and in-repo PRs are supported automatically.

For security reasons, fork PRs that touch protected CI workflows, automation scripts, Unreal or CMake build files, executable helper scripts, or `.uproject`/`.uplugin` descriptors are blocked from triggering privileged builds and must be carried by a maintainer on a source-repository branch. See [Development and CI Workflow](Docs/DevelopmentWorkflow.md) for details.

All pull requests target `main`. Merging requires six green status checks—naming, format and text, API documentation, Linux Game, Linux Editor, and the Linux build summary—plus approval from a maintainer who was not the last pusher. Maintainers review for correctness, module boundaries, API contracts, performance, security, and UE 5.6+ compatibility.

## Large files and Git LFS

Do not add large binary assets, introduce Git LFS tracking, or edit LFS rules without opening an issue first to agree on storage and licensing with a maintainer. This repository has no Git LFS endpoint configured, and push access to a GitHub fork does not grant access to external artifact storage. Keep build outputs, caches, local AI agent instructions, credentials, and personal IDE settings out of commits, and keep third-party attributions in [NOTICE.md](NOTICE.md) up to date.
