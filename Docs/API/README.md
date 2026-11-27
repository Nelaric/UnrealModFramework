<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework API

UnrealModFramework provides a game-side mod runtime and a standalone authoring editor for Unreal Engine 5.6. Its public contracts span `.mod` package loading and lifecycle callbacks, generated API bindings, and World-scoped gameplay extensions.

## Runtime entry points

| Capability | Public headers |
| --- | --- |
| Validate, mount, and observe `.mod` archives | [ModLoader.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModLoader.h), [ModInstance.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModInstance.h) |
| Register host API providers and issue scoped bindings | [ModAPIHostSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModAPIHostSubsystem.h) |
| Prepare a World, manage Hook/Event subscriptions, and resolve content slots, Patch data, and services | [ModExtensionSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSubsystem.h) |
| Integrate multiplayer admission checks and save migration | [ModExtensionSession.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSession.h), [ModExtensionSave.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSave.h) |
| Register standalone native C ABI extension callbacks | [ModExtensionABI.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModExtensionABI.h) plus the game's generated SDK headers |

Generated C++ and Blueprint API calls route through the game's shared SDK Runtime module, while each mounted mod receives an isolated caller context that can be revoked independently. When shipping a mod package, ensure its SDK contract, base game release, engine build, and target profile match the host game (see [Connection and lifetime](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.md#connection-and-lifetime) and [SDK installation](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.md#install-and-verify)).

## Contracts and coding standards

- [Coding standards overview](../CodingStandards/README.md)
- [Modules and dependency boundaries](../CodingStandards/Modules.md)
- [Public API, ownership, asynchronous operations, and errors](../CodingStandards/API.md)
- [C++ style and headers](../CodingStandards/Cpp.md)
- [Doxygen comments](../CodingStandards/Doxygen.md)
- [Runtime, concurrency, performance, and security](../CodingStandards/Runtime.md)
- [Build scripts, tooling, and review](../CodingStandards/BuildAndReview.md)

## Building the API documentation

Install the pinned Doxygen version, run `python Scripts/run_doxygen.py` from the repository root, and open `.tools/api-docs/html/index.html`. The build indexes every project-owned `Public` header directory alongside these entry pages and coding standards. Undocumented framework-integration members are hidden automatically, and any Doxygen warning fails the local build.

When `API_PAGES_ENABLED=true`, pushes to `main` run Doxygen in publishing mode and deploy the site automatically:

- [Published API reference](https://docs.nelaric.com/UnrealModFramework/API/)
- [Documentation hosting setup](https://github.com/Nelaric/UnrealModFramework/blob/main/Docs/Hosting/README.md)
- [GitHub repository](https://github.com/Nelaric/UnrealModFramework)
