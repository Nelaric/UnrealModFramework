<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Doxygen.md) | 简体中文

# Doxygen 注释规范

本规范约束项目自有 C++ 声明的 API 文档注释，请结合 [Epic C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与[公开 API、错误与文档](API.zh-CN.md)一并执行。所有公开 API 注释（含暴露给 Blueprint 的声明）一律使用**英文**编写。

## 目的与覆盖范围

API 注释的价值在于阐明**设计意图**与**外部可观察的契约行为**，而不是把函数名逐字复述一遍，或罗列内部实现细节。请保持标识符本身语义清晰，并在契约变动时同步更新注释。

- **必须（Must）** 为所有公开类型、第一个 `public:` 区域中面向使用者的方法、枚举及枚举值、常量、模组扩展契约与 Blueprint 入口编写 Doxygen 注释。公开属性与字段（含 `UPROPERTY`）同样必须注释；当派生类依赖某个 `protected` 成员的契约时，也应为其补充注释。第二个 `public:` 区域中专供框架内部衔接或重写 Unreal 生命周期的方法无需编写 Doxygen 注释，且两段 `public:` 之间不得夹杂 `protected:` 或 `private:`。Doxygen 会自动隐藏未注释成员，无需手动维护符号排除列表。
- **应该（Should）** 仅在内部不变量或关键意图无法直接从代码看清时，才为 `private` 成员编写 Doxygen 注释；普通实现说明使用常规 `//` 注释即可。
- **必须（Must）** 每行只声明一个变量，便于单独注释。除下文允许的字段/枚举短尾随注释（`///<`）外，所有 Doxygen 注释都应紧贴在目标声明正上方。公开契约只在头文件声明处写一次，不要在 `.cpp` 定义处重复粘贴。
- **必须（Must）** 在每个公开头文件的版权声明正下方添加 `@file` 注释，确保文件作用域的声明能被正常收录进 API 文档。

## 注释形式与单行长度限制

请根据说明文字的长短选择对应写法：

| 形式 | 适用场景与排版要求 |
| --- | --- |
| 字段或枚举值右侧的 `///<` | 单行短尾随注释，正文**最多 25 个字符**。同一枚举内所有 `///<` 的首个 `/` 必须对齐到同一视觉列（制表符按 4 列宽度折算）。只要枚举中有**任意一个**值的说明超过 25 个字符，整个枚举的所有值都必须统一改为前置注释。 |
| 声明正上方的单行 `///` | 单行前置注释，正文**最多 75 个字符**；无需写 `@brief` 等 `@` 标签。 |
| 声明正上方的 `/** ... */` | 当单行 75 个字符放不下时使用的多行注释块。每个自然段都必须以 `@` 标签开头——首段通常用 `@brief`，后续段落用 `@details`、`@par` 或参数/返回值标签；禁止连续堆叠多行不带标签的 `///`。 |

计算字符长度时，注释正文内的空格和 `@` 标签均计入长度，前导缩进、注释起始符号（`///`、`///<`、`/**`、`*`）及其后紧跟的一个分隔空格不计入。所有物理行的注释正文上限均为 **75 个字符**（尾随 `///<` 为 **25 个字符**）。多行注释中用单独的空 `*` 行分隔段落，同一段内折行续写时不要重复标签。每个标签独占一行开头，且统一采用 `@` 前缀（如 `@file`、`@param`、`@return`），不使用 `\`。紧贴在声明上方的注释无需冗余添加 `@class`、`@fn` 或 `@var`。CI 脚本会自动检查上述格式规则（见[构建脚本、工具与审查](BuildAndReview.zh-CN.md)）。

例如，下面这行注释正文刚好 75 个字符，可直接使用单行 `///`：

```cpp
/// The world keeps the settings chosen by its level until the map is unloaded.
```

而下面这句正好有 80 个字符，写成单行会超出上限：

```text
The map retains the world settings chosen by its level until this world unloads.
```

此时应改用带标签的 `/** ... */` 多行块，并在英文单词边界处自然换行：

```cpp
/** @brief The map retains the world settings chosen by its level
 * until this world unloads.
 */
```

## 注释应交代的内容

| 声明种类 | 适用时应写明的契约信息 |
| --- | --- |
| 类或结构体 | 解决什么问题、承担什么职责，以及调用方的典型用法。 |
| 方法或函数 | 功能用途、允许调用的线程、触发回调的线程、前置条件、外部可见副作用及失败行为。 |
| 参数 | 含义、单位、合法取值范围、特殊值语义，以及输入/输出方向。 |
| 返回值 | 返回数据或状态码的含义。若 `@brief` 已把简单返回值讲清楚，或返回类型为 `void`，应省略 `@return`。 |
| 属性、字段或常量 | 含义、单位、取值范围、特殊值、所有权或生命周期。 |
| 枚举及枚举值 | 整体表示的状态或结果集合，以及每个枚举值的具体语义。 |
| 异步操作 | 取消句柄、终结回调触发保证、超时与失败规则，以及任务已完成时调用取消的表现。 |

## 常用 Doxygen 标签

仅在标签能提供有效契约信息时按需选用，切勿当成每条注释都要填满的模板：

| 标签 | 用途说明 |
| --- | --- |
| `@file FileName.h` | 声明公开头文件上下文，使文件级符号进入 API 文档。 |
| `@brief Description` | 多行注释开头的简明摘要。 |
| `@details Description` | 在摘要之后展开详细说明段落。 |
| `@par [Title]` | 另起一段正文（可选附带小标题），具体内容写在下一行。 |
| `@param Name Description` | 说明输入参数，`Name` 须与函数声明完全一致。 |
| `@param[out] Name Description` | 说明输出参数。 |
| `@param[in,out] Name Description` | 说明既读取又修改的参数。 |
| `@tparam Name Description` | 说明用途或约束不直观的模板参数。 |
| `@return Description` | 补充说明 `@brief` 未涵盖的返回值含义（`void` 函数不写）。 |
| `@pre Description` / `@post Description` | 声明调用前提条件或调用完成后的可观察保证。 |
| `@note Description` | 补充正文之外值得留意的调用细节。 |
| `@warning Description` | 警示误用后可能引发错误、崩溃或数据丢失的情形。 |
| `@see Reference` | 关联直接相关的类型或接口。 |
| `@deprecated Description` | 标记接口已弃用，并指明替代接口或迁移方式。 |

每个标签必须位于注释行首，禁止留下空标签，也禁止直接把参数名抄一遍充当说明。描述较长时请折行续写，保证每行正文不超过 75 字符：

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

一旦枚举中某个值的说明超过 25 个字符，整个枚举的所有枚举值都应改为前置注释（单个说明超过 75 个字符时再按通用规则升级为 `/** ... */`）：

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

CI 会自动校验公开头文件的 `@file` 注释、首个 `public:` 区域的方法注释覆盖率、Doxygen 零警告构建以及行长等机械规则；审查者则重点核对英文注释是否准确反映了真实契约、所有权关系与运行时语义。提交前请参照[构建脚本、工具与审查](BuildAndReview.zh-CN.md)运行相关文档与文本检查。
