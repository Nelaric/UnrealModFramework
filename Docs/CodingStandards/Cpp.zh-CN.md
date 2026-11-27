<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Cpp.md) | 简体中文

# C++ 风格与头文件

编写 C++ 代码时，请同时遵循 [Epic C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与下文的项目约定。若遇到两套规则不一致的情况，请先提 Issue 讨论，不要自行取舍。提交 PR 前请使用仓库锁定版本的 `clang-format` 格式化代码。

C++ 源码与 UE C# 构建脚本统一使用制表符（Tab，宽度为 4 列）缩进代码块；非制表符字符之后可用空格做对齐。这与 Epic 的缩进要求一致，并由格式化配置自动保证。

## 命名与类型

- **保留 Unreal 类型前缀**：按引擎规范使用 `A`、`U`、`F`、`I`、`E` 等类型前缀；是否加项目前缀不影响这一规则。
- **非反射类型统一放入 `Mod::`**：项目自有的非反射 C++ 类型与接口放在顶层命名空间 `Mod::` 下，必要时再划分子命名空间（如 `Mod::FModId`、`Mod::Loader::IModLoaderModule`）。由于 Unreal Header Tool 不支持命名空间内的反射类型，`UCLASS`、`USTRUCT`、`UENUM` 和 `UINTERFACE` 仍声明在全局作用域。
- **`Mod::` 内部不重复加 `Nelaric`**：已在 `Mod::` 及其子命名空间下的类型不要再带 `Nelaric` 前缀——应写 `Mod::FModId`，而非 `Mod::FNelaricModId`（Unreal 类型前缀照常保留）。对于模块名、文件名、全局反射类型和日志类别，`Nelaric` 前缀按需选用，主要用于避免与引擎或第三方重名（如 `UNelaricModContext`、`UNelaricModComponent`）。新增标识符前请先排查引擎、插件与仓库内是否已有同名定义。
- **按职责而非具体游戏命名**：模块与日志类别应围绕通用机制和领域职责命名，保持前后一致，避免绑定某款特定游戏的业务概念。
- **优先使用 UE 容器与字符串**：遵循 Epic 对标准库的使用建议——在 UE 模块中优先使用 UE 容器与字符串，仅在跨边界互操作代码或独立命令行工具中使用 `std::` 容器与字符串；在 Epic 允许且表达更清晰的场景下可使用其他标准库工具。同一套接口内不要混用两套体系：面向 UE 的跨模块 API 使用 UE 类型，对外原生 C ABI 使用定宽 C 兼容类型。
- **少建新宏，优先 `constexpr`**：正常使用必需的 UE 反射与模块导出宏，但能用类型化常量、`constexpr` 或内联函数表达时不要新增宏。确需引入项目级宏时，须说明理由并遵守 Epic 全大写 `UE_` 前缀约定；跨模块特性开关应集中定义在同一处。

## 头文件与依赖管理

- **公开头文件必须自包含**：`Public/` 下的每个头文件都应支持被单独 `#include` 在翻译单元首行而不报错，不依赖碰巧排在前面的其他头文件。只包含声明本身必需的头，能用前向声明解决的指针或引用一律用前向声明。
- **隐藏内部实现依赖**：仅供模块内部使用的头文件与重型依赖放在 `Private/`。Runtime 模块的公开头文件不得包含可选扩展模块或第三方厂商 SDK 的头文件。
- **服从 UHT 头文件顺序**：涉及 UE 反射的头文件须严格遵守 Unreal Header Tool 要求（例如 `*.generated.h` 必须排在头文件所有 `#include` 的最后一行），不能因机械排序破坏编译。

## `UCLASS` 符号导出

- **一律显式声明 `MinimalAPI`**：项目编写的每一个 `UCLASS` 都必须在 `UCLASS(...)` 中写明 `MinimalAPI`，覆盖所有模块的 `Public` 与 `Private` 类（含抽象基类、内部类、编辑器类和测试类）。
- **禁止整类导出**：不得在 `UCLASS` 的 `class` 声明上挂载模块 `*_API` 宏；即便该类需要跨模块调用，也必须保留 `MinimalAPI`。
- **按方法最小化导出**：仅对确需跨模块 C++ 链接的非内联方法（以及受支持的跨模块继承所需的构造函数、虚函数等），在对应成员声明前单独标注 `*_API`。不要因为某个内部辅助方法是 `public` 或带 `UFUNCTION` 就顺手导出。
- **区分原生导出与反射可见性**：`MinimalAPI` 只控制原生 C++ 链接符号导出，不会替代 `UFUNCTION` 或 Blueprint 暴露说明符；反过来，反射类型可见也不代表非内联成员函数体已被导出（引擎语义参见 Epic 的[类说明符文档](https://dev.epicgames.com/documentation/zh-cn/unreal-engine/class-specifiers)）。

## 源文件编码与换行符

C++ 源码/头文件与人工编写的 Markdown 文档统一采用 **带 BOM 的 UTF-8** 与 **CRLF** 换行；工具配置、JSON 描述文件与可执行脚本为兼顾跨平台工具兼容性，采用 **无 BOM 的 UTF-8** 与 **LF** 换行，详细分类列表见[构建脚本、工具与审查](BuildAndReview.zh-CN.md)。严禁在同一文件内混用不同换行符。
