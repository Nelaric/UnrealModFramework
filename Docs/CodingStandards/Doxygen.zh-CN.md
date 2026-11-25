<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Doxygen.md) | 简体中文

# Doxygen 注释规范

本规范适用于项目自有的 C++ 声明及其 API 文档，并与 [Epic C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)及[公开 API 契约规范](API.zh-CN.md)配合使用。公开 API 的代码注释一律使用英文，向 Blueprint 暴露的声明亦然。

## 目的与覆盖范围

注释用于解释意图和可观察行为，而不是复述声明字面或实现步骤。命名应清晰准确；契约发生变更时须同步更新注释。

- **必须** 为所有公开类型、第一个 `public:` 区域中面向使用者的方法、枚举及枚举值、常量、模组扩展契约和 Blueprint 入口编写文档注释。公开属性与字段（包括反射属性）也必须编写注释；当派生类型依赖某受保护声明的契约时，应为其编写注释。第二个 `public:` 区域中专供框架集成或 Unreal 生命周期调用的方法不要求 Doxygen 注释，且两段 `public:` 之间不得出现其他访问说明符。Doxygen 会自动隐藏未注释的成员，无需逐个列出排除项。
- **应该** 仅在意图或不变量无法从代码中直接看清时，为私有声明编写 Doxygen 注释。局部实现细节如需说明，可使用普通 `//` 注释；此类注释不属于 API 文档。
- **必须** 每行仅声明一个变量，以便分别说明其含义。除下文所述的变量和枚举值短尾随注释外，Doxygen 注释应紧邻并置于声明之前。公开声明的契约只需在声明处编写一次，不在定义处重复。
- **必须** 在每个公开头文件的版权声明之后紧接一条 Doxygen `@file` 注释，使文件级声明具备文档上下文。

## 格式与单行长度

按所需说明长度选择形式：

| 形式 | 规则 |
| --- | --- |
| 变量或枚举值后的 `///<` | 单行正文最多 25 个字符。同一枚举内所有 `///<` 的首个 `/` 必须对齐至同一视觉列。若任一枚举值的说明超过 25 个字符，该枚举的所有值均改为前置注释。 |
| 声明前一行的 `///` | 单行正文最多 75 个字符；无需使用 `@` 标签。 |
| 声明前的 `/** ... */` | 当说明无法在 75 字符的单行内容纳时使用。每段均须以 Doxygen `@` 标签开头：首段通常使用 `@brief`，后续段落使用 `@details`、`@par` 或专门的标签；不得用多行无标签的 `///` 替代。 |

无论采用哪种形式，空格和 Doxygen 标签均计入正文长度，缩进、注释标记及其后的分隔空格不计入。每个物理行的正文最多 75 个字符；`///<` 适用更严格的 25 字符上限。对齐枚举注释时，制表符按 4 列制表位计算。注释中的空行用于分隔段落；同一段落内的折行无需重复标签。每个标签单独占一行，命令统一使用 `@` 风格，例如 `@file`、`@param` 和 `@return`。紧邻声明的注释不需要 `@class`、`@fn` 或 `@var`。[CI 风格检查](BuildAndReview.zh-CN.md#本地检查)会强制执行这些机械规则。

下例中的注释正文恰好是 75 个字符：

```cpp
/// The world keeps the settings chosen by its level until the map is unloaded.
```

下面这句话恰好有 80 个字符，写在单行会超出上限：

```text
The map retains the world settings chosen by its level until this world unloads.
```

在单词边界处换行，保留原句含义：

```cpp
/** @brief The map retains the world settings chosen by its level
 * until this world unloads.
 */
```

## 注释应说明的内容

| 声明 | 适用时应说明的内容 |
| --- | --- |
| 类或结构体 | 解决的问题、职责，以及调用方如何使用。 |
| 方法或函数 | 用途、允许调用的线程、回调线程、前置条件、可观察的副作用与失败行为。 |
| 参数 | 含义、单位、有效范围、特殊值，以及输入或输出方向。 |
| 返回值 | 结果和状态值的含义。简明结果已在用途说明中解释时，可以省略 `@return`。 |
| 属性、字段或常量 | 含义、单位、范围、特殊值、所有权或生命周期。 |
| 枚举及枚举值 | 所表示的状态或结果，以及每个值的含义。 |
| 异步操作 | 取消句柄、终结回调、超时和失败语义，以及取消过晚时的行为。 |

## 常用 Doxygen 标签

下列标签仅在能补充契约信息时使用，并非每条注释都必须包含的检查清单。

| 标签 | 用途 |
| --- | --- |
| `@file FileName.h` | 标识公开头文件，使文件级声明进入 API 文档。 |
| `@brief Description` | 给出多行注释的简短摘要。 |
| `@details Description` | 在简短摘要后开启详细说明段落。 |
| `@par [Title]` | 另起说明段落，可附标题；正文写在下一行。 |
| `@param Name Description` | 说明输入参数；名称必须与声明一致。 |
| `@param[out] Name Description` | 说明输出参数。 |
| `@param[in,out] Name Description` | 说明函数既读取又修改的参数。 |
| `@tparam Name Description` | 说明作用或约束不明显的模板参数。 |
| `@return Description` | 当用途说明尚未讲清结果含义时说明返回值；`void` 函数省略。 |
| `@pre Description` / `@post Description` | 说明调用前提或调用后的可观察保证。 |
| `@note Description` | 补充主要描述之外、对使用者有帮助的细节。 |
| `@warning Description` | 提醒可能导致错误使用或数据丢失的情况。 |
| `@see Reference` | 链接直接相关的类型或操作。 |
| `@deprecated Description` | 说明接口已弃用，并指明替代接口或迁移方式。 |

每个标签单独占一个注释行。不要添加空标签，也不要直接用名称本身充当说明。说明较长时可折行续写，确保每行注释正文均不超过 75 字符上限。

```cpp
/** @brief Returns whether an activity may start with this player count.
 *
 * @details Call on the game thread. A count below the minimum fails
 * the readiness check; a minimum of zero disables that check.
 *
 * @param PlayerCount Number of admitted players; must be non-negative.
 */
bool CanStartActivity(int32 PlayerCount) const;

/// Maximum admitted players; zero leaves the maximum unrestricted.
int32 MaxPlayers = 0;

enum class ETaskResult
{
    Success,          ///< Task completed.
    RetryableFailure, ///< Retry later.
};
```

只要有一个枚举值的说明超过 25 个字符，就必须为该枚举的所有值使用前置注释。单个枚举值的说明超过 75 个字符时，该值按通用规则使用带标签的 `/** ... */` 文档块。

```cpp
enum class ETaskResult
{
    /// Task completed.
    Success,

    /// Task failed but can be retried after a short delay.
    RetryableFailure,
};
```

## 审查

CI 会检查公开头文件的 `@file` 注释、第一个 `public:` 区域中方法注释是否存在、Doxygen 文档错误以及机械格式。审查者则负责判断英文注释是否准确描述代码契约，包括相关的所有权与运行时行为。提交前请运行改动所需的[文档与文本检查](BuildAndReview.zh-CN.md#本地检查)。Doxygen 构建无警告并不能替代对注释内容的人工审查。
