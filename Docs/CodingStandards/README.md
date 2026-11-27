<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework Coding Standards

Every contribution to UnrealModFramework must follow both [Epic Games' Unreal Engine C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the project standards indexed here. If you find a conflict between the two, open an issue describing the exact rules before writing code around it. Output from our pinned formatter governs whitespace and layout only where it satisfies both standards.

Our baseline engine version is Unreal Engine 5.6. This repository contains the sample host project, the game-side `ModRuntime` plugin, and the embedded source files that ModSDKTool (MDT) compiles into the standalone `ModDevelopmentSDKEditor` plugin. The runtime, authoring editor, shared `.mod` protocol, and game-specific SDKs maintain strict ownership and dependency boundaries. Likewise, the native mod C ABI is an external interoperability boundary and never assumes Unreal C++ ABI compatibility.

## Standards index

- [C++ style and headers](Cpp.md)
- [Modules and dependency boundaries](Modules.md)
- [Public API, errors, and documentation](API.md)
- [Doxygen comments](Doxygen.md)
- [Runtime, concurrency, performance, and security](Runtime.md)
- [Build scripts, tooling, and review](BuildAndReview.md)

The [copyright notice rule](BuildAndReview.md#copyright-notices) applies to every project-authored file, with documented exceptions for strict machine-readable formats.

### Terminology

- **Must** denotes a mandatory requirement.
- **Should** denotes the expected default unless a pull request documents a sound reason to do otherwise.
- **May** denotes an optional choice.

Throughout these documents, **public API** means declarations meant for host-game integrators or mod authors—including Blueprint-exposed entry points and C ABI contracts. Classes may declare a second `public:` section for internal framework wiring or Unreal lifecycle hooks without turning those methods into consumer-facing API.

## Resolving conflicts and requesting exceptions

1. Correctness and compatibility with supported Unreal Engine versions come first.
2. Apply Epic's standard and these project rules together. Neither automatically overrides the other—if they clash, open an issue so we can resolve it explicitly.
3. Let the pinned formatter handle layout only when its output complies with both standards.
4. Any pull request requesting a rule exception must identify the rule, rationale, affected scope, and alternatives considered. Note that maintainer approval on a PR cannot waive an Epic requirement or silently settle a conflict between the two standards.

These standards were adapted from [Nelaric Unreal Gameplay](https://github.com/Nelaric/nelaric-unreal-gameplay/tree/d976a66fc2e41dee11d605af5b387ed290453d6b/Docs/CodingStandards) at commit `d976a66fc2e41dee11d605af5b387ed290453d6b`, replacing rules specific to its gameplay modules, CI setup, and Git LFS storage with conventions tailored to UnrealModFramework.
