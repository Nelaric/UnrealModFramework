<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework API

UnrealModFramework develops a portable game-side mod runtime and an independently built mod authoring editor for Unreal Engine 5.6. This documentation describes the project's public contracts and coding standards. Implemented export declarations and tooling do not yet constitute a stable native mod ABI or a complete package-loading runtime.

## Contracts and standards

- [Coding standards](../CodingStandards/README.md)
- [Module responsibilities and dependency boundaries](../CodingStandards/Modules.md)
- [Public API, ownership, asynchronous operations, and errors](../CodingStandards/API.md)
- [C++ style and header requirements](../CodingStandards/Cpp.md)
- [Doxygen comment rules](../CodingStandards/Doxygen.md)
- [Runtime, concurrency, performance, and security](../CodingStandards/Runtime.md)
- [Build, tooling, and review](../CodingStandards/BuildAndReview.md)

## API documentation generation

`Scripts/run_doxygen.py` includes every current project-owned `Public` header directory alongside this entry page and the coding standards. Undocumented framework-integration methods remain hidden. The normal build fails on documentation warnings; the publishing mode uses `--publish` after review.

For project setup, command-line tool usage, contribution templates, and CI configuration, see the [source repository](https://github.com/Nelaric/UnrealModFramework). The configured publication route is [UnrealModFramework/API](https://docs.nelaric.com/UnrealModFramework/API/) once maintainers enable GitHub Pages.
