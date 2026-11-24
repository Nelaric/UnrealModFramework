<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework API

UnrealModFramework provides a game-side mod runtime and an independently built authoring editor for Unreal Engine 5.6. Its public contracts cover package loading and lifecycle, generated API calls, and World-scoped extensions.

## Runtime entry points

| Use case | Source contract |
| --- | --- |
| Validate, mount, and observe `.mod` loading | [ModLoader.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModLoader.h), [ModInstance.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModInstance.h) |
| Register host API providers and issue scoped bindings | [ModAPIHostSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModAPIHostSubsystem.h) |
| Prepare a World, manage Hook/Event subscriptions, and resolve content, Patch data, and services | [ModExtensionSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSubsystem.h) |
| Integrate multiplayer admission and save migration | [ModExtensionSession.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSession.h), [ModExtensionSave.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSave.h) |
| Register independent native extension callbacks | [ModExtensionABI.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModExtensionABI.h) and the game's generated SDK headers |

Native compatibility is bound to the generated SDK contract and target profile. Verify that the game release, engine build, platform, and signatures match before distributing a package; see the [SDK guide](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md).

## Contracts and standards

- [Coding standards](../CodingStandards/README.md)
- [Module responsibilities and dependency boundaries](../CodingStandards/Modules.md)
- [Public API, ownership, asynchronous operations, and errors](../CodingStandards/API.md)
- [C++ style and header requirements](../CodingStandards/Cpp.md)
- [Doxygen comment rules](../CodingStandards/Doxygen.md)
- [Runtime, concurrency, performance, and security](../CodingStandards/Runtime.md)
- [Build, tooling, and review](../CodingStandards/BuildAndReview.md)

## API documentation generation

`Scripts/run_doxygen.py` includes every current project-owned `Public` header directory alongside this entry page and the coding standards. Undocumented framework-integration methods remain hidden. Standard builds fail on documentation warnings; after review, publishing mode is run with `--publish`.

For project setup, command-line tool usage, contribution templates, and CI configuration, see the [source repository](https://github.com/Nelaric/UnrealModFramework). The published documentation is available over HTTPS at [UnrealModFramework/API](https://docs.nelaric.com/UnrealModFramework/API/), and approved updates are published automatically after reaching `main`.
