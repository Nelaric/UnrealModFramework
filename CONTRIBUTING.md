<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](CONTRIBUTING.zh-CN.md)

# Contributing to UnrealModFramework

Reports, design feedback, documentation improvements, and code contributions are welcome in English or Chinese. The current repository contains a UE 5.6 host project, a game-side Runtime plugin, and an independently built mod development editor. Proposals should explain the host-game or mod-author problem, the expected behavior, and the owning module.

Before opening an issue, search the [existing issues](https://github.com/Nelaric/UnrealModFramework/issues). Include reproduction steps, expected and actual behavior, UE version, platform, and build target for a bug. Keep reports focused and omit private information.

External contributors should fork the repository, create a working branch in their fork, and open a pull request to `Nelaric/UnrealModFramework:main`. Keep one pull request focused on one change. Explain the design or compatibility impact, tests and build results, and any relevant limitations or standards exceptions. Changes to CI, build scripts, plugin descriptors, or native ABI contracts require maintainer review.

Follow [Epic's C++ standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the [project coding standards](Docs/CodingStandards/README.md). The standards define copyright notices, text encoding, formatters, public API documentation, testing, and branch, commit, and PR naming. Run the [relevant local checks](Docs/CodingStandards/BuildAndReview.md#local-checks) before review. When a change affects Unreal targets, report UE 5.6 Game and Editor build results; report a Server build once a Server target exists.

The GitHub Actions quality workflow checks naming, text, formatting, and API documentation on pull requests. It does not build UE targets. Do not add Git LFS tracking rules or large binary dependencies without discussing storage and review implications with maintainers.
