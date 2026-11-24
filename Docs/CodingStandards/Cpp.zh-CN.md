<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Cpp.md) | 简体中文

# C++ 风格与头文件

必须同时遵守 [Epic C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与下文的项目规则。若发现冲突，应创建 Issue 报告，不得自行择一采用。项目使用 clang-format 管理代码布局；提交 PR 前请运行锁定版本的格式化工具。

C++ 和 UE C# 构建脚本的块缩进统一使用制表符，制表符宽度为 4 个字符；非制表符字符后可使用空格对齐文本。这遵循 Epic 的缩进规则，并由格式化工具配置强制执行。

## 命名与类型

- 遵循所有适用的 Unreal 类型前缀及命名规则，包括按要求使用 `A`、`U`、`F`、`I` 和 `E`。这些类型前缀与可选的项目前缀相互独立。
- 项目自有的非反射 C++ 类型和 API 统一使用 `Mod::` 作为顶层命名空间，并在有益时添加领域子命名空间，例如 `Mod::FModId` 和 `Mod::IModService`。由于 Unreal Header Tool 不支持将反射类型放入命名空间，反射类型须声明在全局作用域中。
- `Mod::` 及其子命名空间内的类型不得带有 `Nelaric` 项目前缀——例如应命名为 `Mod::FModId`，而非 `Mod::FNelaricModId`——同时保留 Unreal 必需的类型前缀。模块、文件、全局作用域类型和日志类别既不强制使用 `Nelaric` 项目前缀，也不禁止使用。命名应清晰传达职责，相关声明、文件和引用应保持一致。
- 名称不得与现有引擎、第三方或项目内其他名称冲突。需要区分重名时可使用项目前缀，例如用 `UNelaricModContext` 和 `UNelaricModComponent` 区分框架类型与 Unreal 的 `UObject`、`UActorComponent`。引入新名称前应检查类型名、模块名和头文件名。
- 模块和日志类别应按所属领域保持命名一致。优先选择描述机制或职责的名称，而非针对某款游戏具体规则的名称。
- 遵循 Epic 关于标准库的指导原则。优先使用 UE 容器和字符串；除互操作代码外，避免使用 C++ 标准库容器和字符串。在 Epic 允许且效果更佳的场景下，可以使用其他标准库设施。面向 UE 的跨模块 API 使用 UE 类型；对外原生模组 C ABI 使用固定宽度且兼容 C 的类型。不要在同一个 API 中混用 UE 与标准库约定。
- 优先使用带类型的常量和 `constexpr`，避免新增宏。按常规使用必需的 UE 宏。新增项目宏必须有书面理由，并遵循 Epic 的全大写 `UE_` 命名规则；跨模块功能开关必须集中定义。

## 头文件与依赖

- 公开头文件必须自包含：使用者应能单独包含它，而不依赖偶然的包含顺序。仅包含声明真正需要的头文件，能用前向声明满足时优先使用前向声明。
- 仅供实现使用的包含和声明应放在 `Private` 中。Runtime 的公开头文件不得包含具体的可选集成或厂商 SDK 头文件。
- 尽量精简头文件依赖。不要仅为获取可前向声明的类型而引入宽泛的头文件。
- 反射声明必须遵守 Unreal Header Tool 的要求（包括生成头文件的放置位置）。这些要求优先于机械式的头文件排序。

## UCLASS 导出

- 所有项目自有的 `UCLASS` 都必须在 `UCLASS(...)` 声明中显式包含 `MinimalAPI`。该要求适用于所有模块，以及位于 `Public` 和 `Private` 中的类，包括抽象类、内部类、编辑器类和测试类。
- 不得在 `UCLASS` 的类声明上添加所属模块的 `*_API` 宏。禁止整类导出；增加跨模块功能时仍必须保留 `MinimalAPI`。
- 仅对需要跨模块 C++ 链接的非内联方法，在其方法声明上单独添加所属模块的 `*_API` 宏。若支持跨模块派生，则按需导出构造函数及其他必需方法。不得仅因方法是公开的或参与反射，就导出仅供实现使用的方法。
- `MinimalAPI` 仅控制原生符号导出，不能替代 `UFUNCTION` 或 Blueprint 暴露说明符。类型可见并不代表非内联方法的实现已导出。引擎语义详见 Epic 的[类说明符文档](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/class-specifiers)。

## 源文件文本格式

代码和人工编写的文档使用带 BOM 的 UTF-8 与 CRLF。为保证互操作性，工具配置和可执行脚本可按要求使用不带 BOM 的 UTF-8 与 LF，具体例外见[构建脚本与工具规范](BuildAndReview.zh-CN.md)。同一文件中不得混用换行风格。
