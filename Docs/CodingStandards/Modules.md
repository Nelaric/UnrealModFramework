<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Modules.zh-CN.md)

# Modules and Dependency Boundaries

The repository contains a development host project and two framework plugins. Each module has one responsibility, declares only the dependencies it uses, and keeps implementation details in `Private`. Public headers are contracts: they must compile when included on their own, and their `.Build.cs` dependencies must be public only when those headers require them. Circular module dependencies are forbidden.

| Responsibility | Location | UE module type |
| --- | --- | --- |
| Game-side mod services | `Plugins/ModRuntime/Source/ModRuntime/` | `Runtime` |
| Host integration and PIE checks | `Plugins/ModRuntime/Source/ModRuntimeEditor/` | `Editor` |
| Independent authoring tools | `SourcePlugins/ModDevelopmentEditor/Source/ModDevelopmentEditor/` | `Editor` |
| Development host | `Source/UnrealModFrameworkDev/` | `Runtime` |

`ModRuntime` must compile in Game and Editor targets. It cannot depend on editor modules, a particular host game's code, a content-distribution provider, or the authoring editor. `ModRuntimeEditor` may depend on `ModRuntime`, never the reverse. The host game may depend on the Runtime plugin to provide explicit extension points.

`ModDevelopmentEditor` is built independently with UAT `BuildPlugin`. It must not statically depend on the project-side `ModRuntime` module or a host game's modules. Shared package schemas and SDK contracts belong in versioned `Shared/ModProtocol` definitions when introduced; both plugins compile against those definitions separately. Runtime preview across processes uses a versioned bridge or IPC rather than a UBT dependency on the game project.

Keep native mod ABI types separate from UE-facing public APIs. UE-facing C++ contracts use UE types. The external C ABI uses fixed-width fields, explicit lengths, opaque handles, and documented ownership because UE object and C++ ABI layouts are not portable across independently built binaries.

New modules must document their responsibility, direct dependencies, supported targets, and why an existing module cannot own the capability. Add optional integrations as separate modules or plugins. Do not introduce a dependency merely to make a missing include compile.

## Framework-only integration

Consumer-facing methods belong in the first `public:` section. Methods used only for framework integration or Unreal lifecycle may occupy a second `public:` section, as specified in [Public API](API.md). An internal access key, if introduced, is an integration convention rather than an authorization boundary. Do not expose a private implementation header through a public header or use a non-reflected key as a `UFUNCTION` parameter. Cross-module access needs an explicit documented contract.
