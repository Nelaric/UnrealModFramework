<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CONTRIBUTING.md) | 简体中文

# 参与 UnrealModFramework 开发

感谢你参与 UnrealModFramework。我们欢迎使用英语或中文提交问题报告、设计反馈、文档改进和代码贡献。

`UnrealModFramework/` 目录包含框架的 Unreal 项目。提出新能力时，请说明宿主游戏集成或模组制作中遇到的问题和预期流程。

## 创建 Issue 前

先搜索[现有 Issue](https://github.com/Nelaric/UnrealModFramework/issues)，并阅读[项目概览](README.zh-CN.md)和[编码规范](Docs/CodingStandards/README.zh-CN.md)。通过 [Issue 选择页](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)选择合适的表单：

- **缺陷报告**：说明复现步骤、预期和实际行为，以及相关环境信息。涉及 Unreal Engine 时，请提供引擎版本、操作系统，以及受影响组件与构建目标，例如 Game、Editor 或独立构建的制作插件。
- **能力提案**：说明开发者遇到的问题和使用场景、期望行为，以及能力应由哪一层负责。可以附上公开 API 草案，但不是必需的。
- **文档问题**：链接到对应页面或章节，并说明内容为何不清晰、不正确或缺失。

不属于上述类别的问题可以使用空白 Issue。每个 Issue 应聚焦一个问题或提案，不要在公开报告中包含私人信息。

## 项目范围

开发基线为 Unreal Engine 5.6。仓库包含开发宿主工程和游戏侧 `ModRuntime` 插件。独立的 `ModDevelopmentSDKEditor` 插件由 MDT 生成并构建。当前实现与规划能力见[项目概览](README.zh-CN.md)和[模块边界](Docs/CodingStandards/Modules.zh-CN.md)。

提出改动时，请说明它属于运行时、制作编辑器、共享模组包协议、导出/构建工具还是宿主游戏 SDK。具体游戏行为应位于可复用框架模块之外。涉及公开 API、原生模组 ABI、模组包兼容性或独立构建时，应说明影响。

## 创建 Pull Request 前

贡献代码前，建议先创建 Issue，讨论改动是否必要、是否符合项目定位以及预期范围。提前达成共识有助于后续审查，也能提高 PR 被接受的可能性。小型修复和文档更正仍可直接提交 PR。

如果你不是本仓库成员，请先 Fork 仓库，在自己的 Fork 中创建分支，再从该分支向 `Nelaric/UnrealModFramework:main` 发起 PR。

保持 PR 的主题集中。使用 [PR 模板](.github/pull_request_template.md)说明问题、方案、公开 API 或集成影响，以及验证方式。有相关 Issue 或能力提案时请关联；仅当 PR 完全解决该问题时才使用自动关闭关键词。纯文档改动的设计和兼容性影响、没有风险或规范例外的情况，以及没有特别审查提示的情况，可在对应栏目填写 `None`。如有审查重点，请指出相关文件、设计决定、疑问或取舍。申请项目规则例外时，请说明规则、影响范围、原因及考虑过的替代方案。

所有代码贡献都必须遵守 [Epic Games 的 Unreal Engine C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)和[项目编码规范](Docs/CodingStandards/README.zh-CN.md)。项目规范还规定了文本编码、本地检查，以及允许使用的 PR 标题、分支名和提交信息前缀。例如，分支名可用 `docs/clarify-contribution-guide`，PR 标题或提交标题可用 `docs: clarify contribution guide`。

按照[构建脚本、工具与审查规范](Docs/CodingStandards/BuildAndReview.zh-CN.md)运行与改动相关的检查。在 PR 中写明已运行的检查，以及因工具或环境限制未能运行的检查。

GitHub Actions 会检查命名、格式和 API 文档。可信 PR 编译工作流验证准确的提交版本，再触发 CircleCI 使用 Unreal Engine 5.6.1 编译 Linux Game 和 Editor Target；Editor Job 还会打包独立制作插件。该流程支持 Fork 和源仓库 PR，贡献者无需 CircleCI 账号或额外 CI 配置。Fork PR 如需修改受保护的 CI 工作流、自动化脚本、Unreal 或 CMake 构建脚本、可执行辅助脚本或插件描述文件，应由维护者在源仓库分支处理。检查顺序和构建范围见[开发与 CI 流程](Docs/DevelopmentWorkflow.zh-CN.md)。每项独立框架功能都必须有自动化测试；CI 不强制数值化的代码覆盖率门槛。

PR 的目标分支是 `main`。仓库保护要求六项检查通过，并由最后推送者以外的维护者批准后合并：命名、格式与文本、API 文档、Linux Game、Linux Editor 和 Linux 构建汇总。维护者会审查正确性、模块边界、API 契约、性能、安全性，以及与 Unreal Engine 5.6 及以上版本的兼容性。

## 大文件与 Git LFS

新增大型二进制依赖、引入 Git LFS 跟踪或修改 LFS 规则前，请先创建 Issue，与维护者商定存储、许可和上传安排。本仓库未配置 Git LFS 存储端点。拥有 GitHub Fork 的推送权限并不代表拥有独立存储服务的权限。提交中应排除构建产物、缓存、本地 Agent 指令、凭据及个人配置，并保留 [NOTICE.md](NOTICE.md) 中的第三方归属说明。
