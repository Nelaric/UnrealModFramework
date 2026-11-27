<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](BuildAndReview.md) | 简体中文

# 构建脚本、工具与审查

## 版权声明

只要文件格式允许，所有项目自有文件都必须包含 Nelaric 版权声明。在支持注释的文本文件中，版权声明必须位于第一个逻辑行，置于标题、代码、配置项或任何正文之前。UTF-8 BOM 不计为一行。声明格式为 `Copyright (c) <year> Nelaric Contributors`，其中 `<year>` 为文件首次发布的年份；不要仅因修改文件而更新年份。

使用文件格式原生的注释语法：

| 文件格式 | 首行示例 |
| --- | --- |
| C++、C# 和 JavaScript 源码 | `// Copyright (c) 2026 Nelaric Contributors` |
| Markdown 和 HTML | `<!-- Copyright (c) 2026 Nelaric Contributors -->` |
| Python、Shell、YAML 和使用 `#` 注释的配置 | `# Copyright (c) 2026 Nelaric Contributors` |

如果可执行脚本需要 shebang，请将 `#!...` 保留在第一行，并将版权声明紧接在第二行。若文件格式要求其他首行指令，则将版权声明放在第一个合法的注释位置。不要向严格 JSON 或其他禁止注释的格式中添加注释。不要在 `LICENSE` 开头添加注释或修改其许可文本；文件中现有的版权行具有权威性。二进制资源、生成文件和未修改的第三方文件各自保留其适用的归属说明，免于在文件内添加 Nelaric 版权头。对于无法在文件内写入版权声明的项目自有文件，应通过仓库许可证或邻近的声明文件记录所有权，而不要破坏文件格式。

本规则适用于所有新增的项目自有文件，以及在修改现有项目自有文件时一并适用。审查者必须核查版权头或有文档依据的格式例外。保留既有的第三方版权声明；不得对 Nelaric 不拥有的代码或资源主张版权。

## Unreal 构建脚本

- 对 `.Build.cs` 和 `.Target.cs` 文件运行 CSharpier。其强制要求程度与 C++ 的 clang-format 相同。
- 保持模块依赖最小化，并依据头文件实际需求区分公开与私有依赖。不得在 Runtime 中引入厂商 SDK 依赖。
- 面向 UE 5.6 及以上版本。Development CI 使用已锁定的 UE 5.6.1 SDK 构建 Game 与 Editor，并执行无界面自动化。完整引擎、模块化 Game、独立 SDK、打包与性能测试由 Release Validation 负责。提交审查时说明实际运行的环境和测试。 [CI 构建范围](https://github.com/Nelaric/UnrealModFramework/blob/main/Docs/DevelopmentWorkflow.zh-CN.md)

## 自动化测试

框架的每项独立功能都必须具备自动化测试，以验证其对外可观察的行为。新增功能或变更行为时，应同步新增或更新测试，并在影响契约时覆盖有意义的失败路径与边界情况。框架自有的 Unreal 自动化测试应统一使用 `Nelaric.*` 层级命名，以便 CI 筛选执行。按功能将相关场景组织为少量聚焦的测试，不要为每个方法、分支或细微变体单独创建测试，也不要在同一模块中堆积大量重叠用例。测试数量和行覆盖率并非目标；审查者应判断关键行为与主要风险是否已得到充分验证。

## 本地检查

请在仓库根目录（与 `UnrealModFramework/` 工程目录同级）运行以下命令，并使用 `.github/workflows/development.yml` 和 `.config/dotnet-tools.json` 中锁定的工具版本：

1. `python Scripts/check_text.py` 检查文本编码和换行符。
2. `python Scripts/check_format.py` 检查 C++ 和 UE 构建脚本的格式。运行前需先安装锁定版本的 clang-format，并恢复本地 .NET 工具清单。
3. `python Scripts/check_public_docs.py` 检查每个公开头文件是否包含 `@file` 注释，以及每个类第一个 `public:` 区域中的方法是否有 Doxygen 注释。若第一段与第二段 `public:` 之间出现其他访问说明符，检查将报错；第二段不检查方法注释。
4. `python Scripts/check_doxygen_style.py` 检查 25 与 75 字符的行长上限、单行及多行格式、多行注释每段开头的标签，以及枚举注释风格与对齐的一致性。
5. `python Scripts/run_doxygen.py` 构建 API 文档站点，并在出现任何 Doxygen 警告时失败。运行前需先安装锁定版本的 Doxygen。

请求审查前，请运行与改动相关的本地检查；若涉及 Game 或 Editor Target，还须在 PR 中报告构建结果。PR 必须通过现有的命名、文本、格式和 API 文档检查。涉及 CI、构建脚本、插件描述文件或原生 ABI 契约的改动需要维护者审查。现阶段不要求 clang-tidy 或数值化的代码覆盖率门槛。

## 提交与分支命名

新提交和工作分支只能使用以下前缀：

| 前缀 | 用途 |
| --- | --- |
| `feat` | 新增能力。 |
| `fix` | 修复缺陷。 |
| `docs` | 仅修改文档。 |
| `style` | 仅调整格式，不改变行为。 |
| `refactor` | 重构代码，不改变行为。 |
| `perf` | 改进性能。 |
| `test` | 新增或修改测试。 |
| `build` | 修改构建配置或依赖。 |
| `ci` | 修改自动化和 CI 工作流。 |
| `chore` | 执行上述类别之外的仓库维护。 |
| `revert` | 撤销先前的改动。 |

提交标题必须采用 `<prefix>: <short English summary>` 或 `<prefix>(<scope>): <short English summary>` 格式。包含 scope 时必须使用小写，且英文摘要应以动词开头。Squash 合并的提交标题也必须遵守同一规则。例如：`feat(session): add reservation support` 和 `docs: clarify provider boundaries`。

PR 标题必须遵循与提交标题相同的格式。CI 会检查 PR 标题、源分支名称以及 PR 中的每条提交标题，校验前缀是否合法、格式是否合规，以及英文摘要是否以小写英文字母开头；至于首词是否为动词、摘要是否准确描述改动，则由审查者确认。

GitHub 同步分支生成的提交适用一项受限例外：恰好包含两个父提交的合并提交，可使用完整标题 `Merge branch 'main' into <当前 PR 源分支>`。命名检查器会从 GitHub API 读取父提交数量，并核对目标分支是否与当前 PR 源分支一致。缺少父提交元数据、普通提交套用相同标题、从其他分支合并或合并到其他目标分支，均不适用此例外。PR 标题、分支名、普通提交和 Squash 合并标题仍必须遵循标准格式。

工作分支必须采用 `<prefix>/<lowercase-kebab-case-description>` 格式，例如 `fix/admission-timeout`。前缀必须取自上表；保留的默认分支 `main` 是唯一例外。如需引入新前缀，必须先更新本规范。

## 文本文件例外

人工编写的 Markdown、 `LICENSE`、C++ 源码与头文件，以及 UE C# 构建脚本使用带 BOM 的 UTF-8 和 CRLF。为保证工具兼容性， `.gitattributes`、 `.editorconfig`、 `.clang-format`、 `.csharpierrc`、 `.gitignore`、 `.json`、 `.ini`、 `.uproject`、 `.uplugin`、 `.yml`、 `.yaml`、 `Doxyfile` 以及可执行 `.py` 脚本使用不带 BOM 的 UTF-8 和 LF。检查脚本会强制执行这些分类规则。

## 审查

审查应重点关注正确性、版权声明、模块边界、公开 API 文档的准确性、取消与失败行为、所有权、性能、安全性，以及每项独立框架功能的自动化测试。CI 负责校验 Doxygen 注释的机械格式；审查者负责判断注释是否准确描述实际契约，以及测试是否覆盖关键行为且避免不必要的重复。申请项目指导性规则例外的 PR 必须写明规则、原因、受影响代码及替代方案，并经维护者批准；例外不能覆盖 Epic 的要求，也不能单独解决两套规范之间的冲突，此类冲突应提交 Issue 报告。当前 CI 通过即可作为自动准入门槛，但不能替代对必需测试的人工审查。

审查者必须依据 [UCLASS 导出规则](Cpp.zh-CN.md#uclass-导出)，确认每个项目自有的 `UCLASS` 均显式包含 `MinimalAPI`，类声明上不含 `*_API` 宏，且仅对需要跨模块调用的方法单独导出。
