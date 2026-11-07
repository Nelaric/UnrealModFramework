<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework Coding Standards

These standards apply to every contribution to UnrealModFramework. Follow both [Epic Games' Unreal Engine C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and these project rules. If they conflict, open an issue identifying the exact requirements before proceeding. Pinned formatter output governs layout only when it satisfies both standards.

The development baseline is Unreal Engine 5.6. The repository contains a host project, the game-side `ModRuntime` plugin, and the independently built `ModDevelopmentEditor` plugin. The runtime, authoring editor, shared protocol, and any game-specific SDK have separate ownership and dependency boundaries. A native mod C ABI is an interoperability boundary; it does not inherit UE C++ ABI conventions.

## Standards

- [C++ style and header hygiene](Cpp.md)
- [Modules and dependency boundaries](Modules.md)
- [Public API, errors, and documentation](API.md)
- [Doxygen comments](Doxygen.md)
- [Runtime, concurrency, performance, and security](Runtime.md)
- [Build scripts, tooling, and review](BuildAndReview.md)

The [copyright notice rule](BuildAndReview.md#copyright-notices) applies to every project-authored file, subject to format-specific exceptions.

**Must** is mandatory. **Should** is the default unless a pull request explains a justified exception. **May** is optional. Public API means declarations intended for host-game integration or mod authors, including Blueprint entry points and C ABI contracts. A second `public:` section may expose framework integration methods without making them consumer API.

## Compliance and conflicts

1. Meet correctness and the requirements of the supported UE version first.
2. Follow Epic's and this project's rules together. Report a conflict in an issue; neither set takes automatic precedence.
3. Use the pinned formatter for whitespace and layout only where its result satisfies both standards.
4. A pull request seeking an exception must identify the rule, reason, scope, and alternatives. Maintainer approval cannot override an Epic requirement or settle a conflict between the standards.

These standards were adapted from [Nelaric Unreal Gameplay](https://github.com/Nelaric/nelaric-unreal-gameplay/tree/d976a66fc2e41dee11d605af5b387ed290453d6b/Docs/CodingStandards) at commit `d976a66fc2e41dee11d605af5b387ed290453d6b`. Rules specific to the source project's gameplay modules, CI services, and Git LFS storage were replaced with this repository's structure.
