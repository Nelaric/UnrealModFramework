<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CONTRIBUTING.zh-CN.md)

# Contributing to UnrealModFramework

Thank you for helping build UnrealModFramework. Reports, design feedback, documentation improvements, and code contributions are welcome in English or Chinese.

The `UnrealModFramework/` directory contains the framework's Unreal project. Describe the host-game integration or mod-authoring problem and intended workflow when proposing a capability.

## Before opening an issue

Search [existing issues](https://github.com/Nelaric/UnrealModFramework/issues) and read the [project overview](README.md) and [coding standards](Docs/CodingStandards/). Use the [issue chooser](https://github.com/Nelaric/UnrealModFramework/issues/new/choose) to select the form that fits:

- **Bug report:** Include reproduction steps, expected and actual behavior, and relevant environment details. If Unreal Engine is involved, include its version, operating system, and the affected component and build target, such as Game, Editor, or the independently built authoring plugin.
- **Capability proposal:** Explain the developer problem and use case, desired behavior, and where the responsibility should live. A public API sketch is useful but optional.
- **Documentation issue:** Link the page or section and explain what is unclear, incorrect, or missing.

Blank issues remain available for topics that do not fit these forms. Keep each issue focused on one problem or proposal, and leave private information out of public reports.

## Project scope

The development baseline is Unreal Engine 5.6. The repository contains the development host project, the game-side `ModRuntime` plugin, and the independently built `ModDevelopmentEditor` plugin. See the [project overview](README.md) and [module boundaries](Docs/CodingStandards/Modules.md) for the current implementation and planned capabilities.

When proposing a change, identify whether it belongs in the runtime, authoring editor, shared package protocol, export/build tooling, or a host-game SDK. Keep host-game behavior outside reusable framework modules. Explain public API, native mod ABI, package compatibility, and independent-build impacts when relevant.

## Before opening a pull request

Before contributing code, we recommend opening an issue to discuss whether the change is needed, how it fits the project, and its proposed scope. Reaching agreement early makes the resulting pull request easier to review and more likely to be accepted. Small fixes and documentation corrections may still go directly to a pull request.

If you are not a member of this repository, fork it first. Create a branch in your fork, then open a pull request from that branch to `Nelaric/UnrealModFramework:main`.

Keep a pull request focused. Use the [pull request template](.github/pull_request_template.md) to describe the problem, the chosen approach, any public API or integration impact, and how you verified the change. Link a related issue or capability proposal when one exists; use a closing keyword only when the pull request fully resolves it. Use `None` for design and compatibility impact on documentation-only changes, for risks or exceptions when there are none, and for reviewer notes when no specific guidance is needed. Otherwise, point reviewers to the files, decisions, questions, or tradeoffs that need attention. If you request an exception to a project rule, name the rule, affected scope, reason, and alternatives considered.

All code contributions must follow [Epic Games' Unreal Engine C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the [project coding standards](Docs/CodingStandards/README.md). The project standards also define text encoding, local checks, and the allowed PR title, branch, and commit prefixes. Use a branch name such as `docs/clarify-contribution-guide` and a PR title or commit subject such as `docs: clarify contribution guide`.

Run the checks relevant to your change as described in [Build Scripts, Tooling, and Review](Docs/CodingStandards/BuildAndReview.md). In your pull request, say which checks you ran and mention any you could not run.

GitHub Actions checks naming, formatting, and API documentation. The trusted PR build workflow validates the exact submitted commit and starts CircleCI Game and Editor builds with Unreal Engine 5.6.1 on Linux; the Editor job also packages the independent authoring plugin. The workflow supports both fork and source-repository PRs. Contributors do not need a CircleCI account or additional CI setup. Fork PRs that change protected CI workflows, automation, Unreal or CMake build scripts, executable helper scripts, or plugin descriptors need a maintainer to handle those changes in a source-repository branch. See the [development and CI workflow](Docs/DevelopmentWorkflow.md) for check order and build scope. Independent framework capabilities require automated tests; CI does not enforce a numeric code coverage threshold.

Pull requests target `main`. Repository protection requires six passing checks and a maintainer approval from someone other than the last pusher before merging: naming, format and text, API documentation, Linux Game, Linux Editor, and the Linux build summary. Maintainers review correctness, module boundaries, API contracts, performance, security, and compatibility with Unreal Engine 5.6 and later.

## Large files and Git LFS

Before adding large binary dependencies, introducing Git LFS tracking, or changing LFS rules, open an issue to agree on storage, licensing, and upload arrangements with a maintainer. This repository does not configure a Git LFS endpoint. Permission to push to a GitHub fork does not grant access to a separate storage service. Keep generated builds, caches, local agent instructions, credentials, and personal configuration out of commits, and preserve third-party notices in [NOTICE.md](NOTICE.md).
