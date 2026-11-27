<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](BuildAndReview.md) | 简体中文

# 构建脚本、工具与审查

## 版权声明

凡是文件格式支持注释的项目原创文件，都必须在首个逻辑行写明 Nelaric 版权声明——位于标题、代码、配置项或正文之前（UTF-8 BOM 不占用行号）。声明格式固定为 `Copyright (c) <year> Nelaric Contributors`，其中 `<year>` 为该文件首次发布的年份，后续修改文件时不要顺带改年份。

请按文件格式选用对应的原生注释语法：

| 文件格式 | 首行写法 |
| --- | --- |
| C++、C# 与 JavaScript 源码 | `// Copyright (c) 2026 Nelaric Contributors` |
| Markdown 与 HTML | `<!-- Copyright (c) 2026 Nelaric Contributors -->` |
| Python、Shell、YAML 及 `#` 注释配置 | `# Copyright (c) 2026 Nelaric Contributors` |

如果可执行脚本首行必须是 `#!` shebang，请将 shebang 留在第 1 行，把版权声明紧接在第 2 行；若某种格式要求其他首行指令，同样将版权声明放在最早允许出现注释的位置。切勿向严格 JSON 等不支持注释的格式强加注释，也不要在 `LICENSE` 顶部加注释或改动其正文。二进制资产、生成产物及未修改的第三方文件保留各自的原始归属说明，无需添加 Nelaric 版权头；对于无法在文件内部写注释的项目原创文件，请在 `LICENSE` 或 [NOTICE.md](https://github.com/Nelaric/UnrealModFramework/blob/main/NOTICE.md) 中统一登记归属，切勿破坏文件格式。

新建项目原创文件或修改既有项目原创文件时均适用本规则。审查者需确认文件已包含合规版权头，或属于有据可查的格式例外；严禁删改第三方原有的版权声明，也不得对非 Nelaric 所有的代码或资产声明版权。

## Unreal 构建脚本

- **使用 CSharpier 格式化构建规则**：所有 `.Build.cs` 和 `.Target.cs` 文件都必须经过 CSharpier 格式化，其强制性与 C++ 的 `clang-format` 完全等同。
- **最小化模块依赖**：只声明模块实际用到的依赖，并根据 `Public/` 头文件是否直接暴露该依赖来划分 `Public` 与 `Private` 依赖。严禁在 Runtime 模块中引入第三方厂商 SDK 依赖。
- **基于 UE 5.6+ 验证构建**：发起审查前，请使用 UE 5.6 编译受影响的开发宿主 Game 与 Editor Target（后续引入 Server Target 后一并纳入）。长远来看 CI 将同时覆盖最低与最新受支持引擎版本；当前由 GitHub Actions 检查命名、格式、文本规范、API 文档、自动 Hook 多平台用例与 MST 源码分析，由 CircleCI 编译 Linux Game 与 Editor Target、运行 Editor Hook 分发自动化测试并独立构建制作插件。启用自动 C++ Hook 时，请通过 MBT 调用 UBT，确保改写后的源码副本参与编译并完成最终链接核对（见 [Linux 编译的范围](https://github.com/Nelaric/UnrealModFramework/blob/main/Docs/DevelopmentWorkflow.zh-CN.md#linux-编译的范围)）。

## 自动化测试

每项独立的框架能力都必须配套验证其外部可观察行为的自动化测试；新增能力或调整行为时应同步增补或更新用例，并在影响对外契约的边界条件和失败路径上给出覆盖。框架编写的 Unreal 自动化测试统一归入 `Nelaric.*` 层级下命名，便于 CI 筛选运行。

请围绕核心场景将用例组织为少量清晰、聚焦的测试，不要机械地为每个方法、分支或细微参数组合各拆一个测试，也不要在同一模块内堆砌大量重复用例。测试数量与代码行覆盖率并非追求指标，审查时重点考察核心契约行为与关键风险点是否得到了有效验证。

## 本地检查

请在仓库根目录（与 `UnrealModFramework/` 目录同级）配合 `.github/workflows/quality.yml` 和 `.config/dotnet-tools.json` 锁定的工具版本运行下列检查：

1. `python Scripts/check_text.py` —— 检查 UTF-8 BOM 与 CRLF/LF 换行符是否符合分类规则。
2. `python Scripts/check_format.py` —— 恢复本地 `.NET` 工具清单并安装锁定版 `clang-format` 后，检查 C++ 与 UE C# 构建脚本排版。
3. `python Scripts/check_public_docs.py` —— 检查每个公开头文件是否包含 `@file` 注释、类第一个 `public:` 区域的方法是否具备 Doxygen 注释，并拦截两段 `public:` 之间穿插其他访问说明符的写法。
4. `python Scripts/check_doxygen_style.py` —— 检查 25/75 字符单行上限、单行与多行注释形式、多行段落首标签，以及同一枚举内尾随注释的对齐与风格统一性。
5. `python Scripts/run_doxygen.py` —— 安装锁定版 Doxygen 后生成 API 文档站点（输出至 `.tools/api-docs/html/index.html`），出现任何文档警告均会报错退出。

提交审查前请先跑通与本次改动相关的本地检查；若改动影响 Game 或 Editor Target，请在 PR 中一并附上编译结果。所有 PR 都必须通过命名、文本、格式与 API 文档自动检查；涉及 CI 工作流、构建脚本、插件描述文件或原生 C ABI 契约的改动须经维护者审查。现阶段暂不强制要求 `clang-tidy` 或固定数值的覆盖率门槛。

## 提交、PR 与分支命名

所有新提交、PR 标题及开发分支只允许使用下表中的前缀：

| 前缀 | 适用场景 |
| --- | --- |
| `feat` | 新增功能或能力。 |
| `fix` | 修复缺陷。 |
| `docs` | 仅修改文档。 |
| `style` | 仅调整代码排版格式，不改变行为。 |
| `refactor` | 重构代码结构，不改变外部行为。 |
| `perf` | 优化运行或构建性能。 |
| `test` | 新增或调整测试用例。 |
| `build` | 修改构建配置、脚本或外部依赖。 |
| `ci` | 修改自动化脚本或 CI 工作流。 |
| `chore` | 上述类别以外的日常仓库维护。 |
| `revert` | 回退先前的提交。 |

- **提交标题与 PR 标题**：统一采用 `<prefix>: <short English summary>` 或 `<prefix>(<scope>): <short English summary>`。`scope`（如有）全部小写，英文摘要以小写祈使动词开头，例如 `feat(session): add reservation support`、`docs: clarify provider boundaries`。Squash 合并标题同样遵循此格式。CI 会自动校验 PR 标题、源分支名及 PR 内每条提交的前缀、语法结构与摘要首字母小写；审查者则人工确认首词为动词且摘要准确概括了改动。
- **同步主干合并提交例外**：通过 GitHub 页面同步主干产生的合并提交允许使用固定标题 `Merge branch 'main' into <当前 PR 源分支>`，前提是命名检查器通过 GitHub API 确认该提交恰好有两个父提交，且目标分支与当前 PR 源分支完全一致。单父提交套用该文案、合并自其他分支或合入其他目标分支均不享受此例外。
- **开发分支名**：统一采用 `<prefix>/<lowercase-kebab-case-description>`（如 `fix/admission-timeout`），前缀须取自上表。除默认主干分支 `main` 外无其他例外；若确需扩充前缀列表，须先修改本规范。

## 文本文件编码分类

- **带 BOM 的 UTF-8 + CRLF**：人工编写的 Markdown（`.md`）、`LICENSE`、C++ 源码与头文件，以及 UE C# 构建脚本（`.cs`）。
- **无 BOM 的 UTF-8 + LF**：`.gitattributes`、`.editorconfig`、`.clang-format`、`.csharpierrc`、`.gitignore`、`.json`、`.modexport`、`.ini`、`.uproject`、`.uplugin`、`.yml`、`.yaml`、`.html`、`Doxyfile`、`CNAME` 以及 `.py` 脚本。

`Scripts/check_text.py` 会对工作区内的受控与新增未忽略文件强制检查上述两类规则。

## 代码审查要点

审查时请综合把关逻辑正确性、版权头完整性、模块职责边界、公开 API 文档与实现的一致性、取消与错误处理语义、对象所有权、性能开销、安全边界，以及独立功能配套的自动化测试。CI 仅负责检查 Doxygen 的机械排版格式，注释是否准确表达契约、测试是否覆盖关键行为且无冗余堆砌仍需人工把关。

若 PR 申请豁免某条项目指导性规则，请核实其已写明所涉条款、原因、受影响范围与替代方案并获维护者批准；注意此类豁免不能凌驾于 Epic 编码规范之上，也不能绕开 Issue 直接裁决两套规范间的冲突。

最后，凡涉及 C++ 类的改动，审查者都须对照 [C++ 风格与头文件](Cpp.zh-CN.md)确认每个项目自有的 `UCLASS` 均显式写明 `MinimalAPI`、未在类声明上使用 `*_API` 整类导出，且仅对确需跨模块链接的方法单独标注导出宏。
