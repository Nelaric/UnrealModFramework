<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Modules.md) | 简体中文

# 模块与依赖边界

仓库包含一个开发宿主工程和两个框架插件。每个模块只承担明确职责，仅声明实际使用的依赖，并将实现细节放入 `Private`。公开头文件是契约：它们须能够独立包含，且只有当公开头文件确实需要某依赖时，才将该依赖列为 `.Build.cs` 的公开依赖。禁止循环依赖。

| 职责 | 位置 | UE 模块类型 |
| --- | --- | --- |
| 游戏侧模组服务 | `Plugins/ModRuntime/Source/ModRuntime/` | `Runtime` |
| 宿主接入与 PIE 检查 | `Plugins/ModRuntime/Source/ModRuntimeEditor/` | `Editor` |
| 独立的制作工具 | `SourcePlugins/ModDevelopmentEditor/Source/ModDevelopmentEditor/` | `Editor` |
| 开发宿主 | `Source/UnrealModFrameworkDev/` | `Runtime` |

`ModRuntime` 必须在 Game 和 Editor Target 中编译。它不得依赖编辑器模块、某个宿主游戏的代码、特定内容分发服务或制作编辑器。`ModRuntimeEditor` 可以依赖 `ModRuntime`，不能反向依赖。宿主游戏可以依赖 Runtime 插件，并显式提供扩展点。

`ModDevelopmentEditor` 通过 UAT `BuildPlugin` 独立构建。它不得静态依赖项目侧的 `ModRuntime` 模块或宿主游戏模块。未来共享的包 Schema 与 SDK 契约放入版本化的 `Shared/ModProtocol` 定义，两个插件分别编译这些定义。跨进程预览使用版本化桥接或 IPC，不通过 UBT 依赖游戏工程。

原生模组 ABI 类型应与面向 UE 的公开 API 分离。UE 内部的 C++ 契约使用 UE 类型；外部 C ABI 使用固定宽度字段、显式长度、不透明句柄及已说明的所有权规则，因为独立构建的二进制不能依赖 UE 对象或 C++ ABI 布局保持可移植。

新增模块时须说明职责、直接依赖、支持的 Target，以及现有模块为何不适合承载该能力。可选集成应放在独立模块或插件中。不要为了让缺失的包含文件通过编译而随意增加依赖。

## 仅供框架集成的接口

面向使用者的方法放在第一个 `public:` 区域。仅供框架集成或 Unreal 生命周期调用的方法可以放在第二个 `public:` 区域，详见[公开 API](API.zh-CN.md)。未来若引入内部访问 Key，它只是集成约定，不是授权边界。公开头文件不得泄漏私有实现头文件，也不得将非反射 Key 作为 `UFUNCTION` 参数。跨模块访问需要明确且有文档的契约。
