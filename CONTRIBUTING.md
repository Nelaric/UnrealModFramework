<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CONTRIBUTING.zh-CN.md)

# Contributing to UnrealModFramework

Thank you for helping build UnrealModFramework. Issue reports, design feedback, documentation improvements, and code contributions are welcome in English or Chinese.

The `UnrealModFramework/` directory contains the framework's Unreal project. When proposing a capability, describe the host-game integration or mod-authoring problem and the intended workflow.

## Before opening an issue

Search [existing issues](https://github.com/Nelaric/UnrealModFramework/issues) and read the [project overview](README.md) and [coding standards](Docs/CodingStandards/). Use the [issue chooser](https://github.com/Nelaric/UnrealModFramework/issues/new/choose) to select the form that fits:

- **Bug report:** Include reproduction steps, expected and actual behavior, and relevant environment details. If Unreal Engine is involved, include its version, operating system, and the affected component and build target, such as Game, Editor, or the independently built authoring plugin.
- **Capability proposal:** Explain the developer problem and use case, desired behavior, and where the responsibility should live. A public API sketch is helpful but optional.
- **Documentation issue:** Link the page or section and explain what is unclear, incorrect, or missing.

Blank issues remain available for topics that do not fit these forms. Keep each issue focused on a single problem or proposal, and omit private information from public reports.

## Project scope

The development baseline is Unreal Engine 5.6. This repository contains the development host project and the game-side `ModRuntime` plugin, while MDT generates and builds the independent `ModDevelopmentSDKEditor` plugin. See the [project overview](README.md) and [module boundaries](Docs/CodingStandards/Modules.md) for current implementations and planned capabilities.

When proposing a change, identify whether it belongs in the runtime, authoring editor, shared package protocol, export/build tooling, or a host-game SDK. Keep host-game-specific behavior outside reusable framework modules. For a runtime extension change, specify the affected manifest or SDK contract version, target configuration, and host integration point. Describe any impact on public APIs, the native mod ABI, package compatibility, or independent builds when relevant.

## Before opening a pull request

Before contributing code, we recommend opening an issue to discuss whether the change is needed, how it fits the project, and what its scope should be. Reaching agreement early makes the resulting pull request easier to review and more likely to be accepted. Small fixes and documentation corrections can still go directly to a pull request.

If you are not a member of this repository, fork it first, create a branch in your fork, and open a pull request from that branch to `Nelaric/UnrealModFramework:main`.

Keep each pull request focused. Use the [pull request template](.github/pull_request_template.md) to describe the problem, chosen approach, public API or integration impact, and verification steps. Link any related issue or capability proposal, and use a closing keyword only when the pull request fully resolves it. Write `None` for design and compatibility impact on documentation-only changes, for risks or exceptions when none apply, and for reviewer notes when no specific guidance is needed. Otherwise, point reviewers to the files, decisions, questions, or tradeoffs that need attention. When requesting an exception to a project rule, state the rule, affected scope, rationale, and alternatives considered.

All code contributions must follow [Epic Games' Unreal Engine C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the [project coding standards](Docs/CodingStandards/README.md). The project standards also define text encoding, local checks, and allowed PR title, branch, and commit prefixes (for example, branch `docs/clarify-contribution-guide` and PR title or commit subject `docs: clarify contribution guide`).

Run the checks relevant to your change as described in [Build Scripts, Tooling, and Review](Docs/CodingStandards/BuildAndReview.md). In your pull request, list the checks you ran and note any you could not run.

GitHub Actions checks naming, formatting and text conventions, API documentation, standalone tools, and automatic Hook targets. Fork PRs can run the public checks. A maintainer reviews the exact PR head and dispatches the UE Development matrix in a private authorized environment. Independent capabilities require automation tests for observable behavior and meaningful failure cases. [Development and CI workflow](Docs/DevelopmentWorkflow.md)

Pull requests target `main`. The migrated ruleset requires `Development public` and `ci/development`. The existing live checks remain required until the full matrix first passes. Maintainers review correctness, module boundaries, API contracts, performance, and UE compatibility.

## Large files and Git LFS

Before adding large binary dependencies, introducing Git LFS tracking, or changing LFS rules, open an issue to agree on storage, licensing, and upload arrangements with a maintainer. This repository does not configure a Git LFS endpoint, and permission to push to a GitHub fork does not grant access to a separate storage service. Keep generated builds, caches, local agent instructions, credentials, and personal configuration out of commits, and preserve third-party notices in [NOTICE.md](NOTICE.md).
