<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CONTRIBUTING.md) | 简体中文

# 参与 UnrealModFramework 开发

感谢你参与 UnrealModFramework 的建设。我们欢迎使用英语或中文提交问题报告、设计反馈、文档改进和代码贡献。

`UnrealModFramework/` 目录包含框架的 Unreal 项目。提出新能力时，请说明宿主游戏集成或模组制作中遇到的问题以及预期流程。

## 创建 Issue 前

请先搜索[现有 Issue](https://github.com/Nelaric/UnrealModFramework/issues)，并阅读[项目概览](README.zh-CN.md)与[编码规范](Docs/CodingStandards/README.zh-CN.md)，然后通过 [Issue 选择页](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)选择合适的表单：

- **缺陷报告**：说明复现步骤、预期行为与实际行为，以及相关环境信息。若涉及 Unreal Engine，请提供引擎版本、操作系统，以及受影响的组件与构建目标（例如 Game、Editor 或独立构建的制作插件）。
- **能力提案**：说明开发者遇到的问题、使用场景、期望行为，以及该能力应由哪一层负责。可附上公开 API 草案（非必需）。
- **文档问题**：附上对应页面或章节链接，并说明内容中不清晰、不正确或缺失之处。

不属于上述类别的话题可使用空白 Issue。每个 Issue 请聚焦于单个问题或提案，切勿在公开报告中包含私人信息。

## 项目范围

项目开发基线为 Unreal Engine 5.6。本仓库包含开发宿主工程和游戏侧 `ModRuntime` 插件，独立的 `ModDevelopmentSDKEditor` 插件由 MDT 生成并构建。当前实现与规划能力详见[项目概览](README.zh-CN.md)和[模块边界](Docs/CodingStandards/Modules.zh-CN.md)。

提出改动时，请明确其归属：运行时、制作编辑器、共享模组包协议、导出/构建工具，还是宿主游戏 SDK。宿主游戏的特定行为应保持在可复用框架模块之外。若涉及运行时扩展改动，请注明受影响的清单或 SDK 契约版本、目标配置与宿主接入点；若涉及公开 API、原生模组 ABI、模组包兼容性或独立构建，请一并说明影响。

## 创建 Pull Request 前

贡献代码前，建议先创建 Issue，讨论改动是否必要、是否契合项目定位以及预期范围。提前达成共识有助于降低审查成本，也能提高 PR 被合入的几率。小型修复与文档勘误仍可直接提交 PR。

如果你不是本仓库成员，请先 Fork 仓库并在自己的 Fork 中创建分支，再从该分支向 `Nelaric/UnrealModFramework:main` 发起 PR。

请保持 PR 主题集中，并使用 [PR 模板](.github/pull_request_template.md)说明待解决的问题、所选方案、公开 API 或集成影响以及验证方式。如有相关 Issue 或能力提案请予以关联，仅当该 PR 完全解决对应问题时才使用自动关闭关键词。对于纯文档改动的设计与兼容性影响、不存在风险或规范例外的情况，以及无需特别提示审查者的情况，可在对应栏目填写 `None`；否则请向审查者指出需要重点关注的文件、设计决策、疑问或权衡。若申请项目规则例外，请写明具体规则、影响范围、理由以及评估过的替代方案。

所有代码贡献均须遵守 [Epic Games 的 Unreal Engine C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)及[项目编码规范](Docs/CodingStandards/README.zh-CN.md)。项目规范同时规定了文本编码、本地检查，以及允许使用的 PR 标题、分支名和提交信息前缀（例如分支名 `docs/clarify-contribution-guide`、PR 标题或提交主题 `docs: clarify contribution guide`）。

请按[构建脚本、工具与审查规范](Docs/CodingStandards/BuildAndReview.zh-CN.md)运行与改动相关的检查，并在 PR 中注明已执行的检查以及因工具或环境限制未能运行的检查。

GitHub Actions 会检查命名、格式、文本规范、API 文档、原生工具及自动 Hook 目标。普通 Fork PR 运行公开检查。同仓库 PR 可在受保护的 `development-engine` 环境中运行引擎构建。新增的独立能力须配备自动化测试，覆盖用户可观察的行为和重要的失败场景。[开发与 CI 流程](Docs/DevelopmentWorkflow.zh-CN.md)

PR 以 `main` 为目标。合并前须满足该分支配置的必需检查，并获得最后一次推送者以外的维护者批准。维护者审查正确性、模块边界、API 契约、性能、安全性及 Unreal Engine 5.6 和更高版本的兼容性。

## 大文件与 Git LFS

在新增大型二进制依赖、引入 Git LFS 跟踪或修改 LFS 规则前，请先创建 Issue，与维护者商定存储、许可及上传方案。本仓库未配置 Git LFS 存储端点，拥有 GitHub Fork 的推送权限也并不等同于拥有独立存储服务的访问权限。提交时请排除构建产物、缓存、本地 Agent 指令、凭据与个人配置，并保留 [NOTICE.md](NOTICE.md) 中的第三方归属说明。
