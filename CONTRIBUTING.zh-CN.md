<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CONTRIBUTING.md) | 简体中文

# 参与 UnrealModFramework 开发

感谢你关注并参与 UnrealModFramework。我们欢迎使用中文或英文提交缺陷报告、设计讨论、文档改进与代码贡献。

框架的宿主 Unreal 工程位于 `UnrealModFramework/` 目录。提议新功能时，建议从宿主游戏接入或 Mod 制作中的具体痛点出发，说明预期的开发与使用流程。

## 提交 Issue 前

请先搜索[现有 Issue](https://github.com/Nelaric/UnrealModFramework/issues) 避免重复，并浏览[项目概览](README.zh-CN.md)与[编码规范](Docs/CodingStandards/README.zh-CN.md)了解背景，随后在 [Issue 选择页](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)选用对应的表单：

- **缺陷报告（Bug report）**：提供清晰的复现步骤、预期行为与实际结果，以及运行环境。涉及 Unreal Engine 时，请写明引擎版本、操作系统，以及受影响的模块与构建目标（如 Game、Editor 或独立构建的制作插件）。
- **能力提案（Capability proposal）**：说明要解决的开发问题、使用场景、期望行为，以及该职责应归属哪一层；如有公开 API 设想可一并附上（非必需）。
- **文档问题（Documentation issue）**：附上具体页面或小节链接，指出表述不清、过时或遗漏之处。

不属于以上三类的话题可使用空白 Issue。请让每个 Issue 聚焦单一问题或提案，切勿在公开内容中附带密钥、凭据或个人敏感信息。

## 项目范围与职责边界

本项目以 Unreal Engine 5.6 为开发基线。仓库内包含开发宿主工程及 `ModRuntime` 插件；ModSDKTool（MDT）负责生成游戏侧 SDK 源码，并编译配套的作者侧 SDK 与独立的 `ModDevelopmentSDKEditor` 插件（参见[项目概览](README.zh-CN.md)与[模块与依赖边界](Docs/CodingStandards/Modules.zh-CN.md)）。

规划改动时，请先厘清职责归属：游戏侧运行时、Mod 制作编辑器、`.mod` 包协议、导出与构建工具链，还是特定游戏的 SDK。可复用的框架模块不应混入具体游戏的业务规则。若改动涉及运行时扩展，请注明受影响的清单或 SDK 契约版本、构建目标及宿主接入点；若影响公开 API、原生 C ABI、`.mod` 包兼容性或独立插件构建，也请一并说明。

## 准备 Pull Request

对于非琐碎的代码改动，建议先通过 Issue 讨论需求是否契合框架定位及合理的改动边界，达成共识后再编码，这样审查会顺畅许多。小范围缺陷修复与文档修订可直接发起 PR。

如果你没有本仓库的直接推送权限，请先 Fork 仓库，在自己的 Fork 中创建特性分支，再向 `Nelaric/UnrealModFramework:main` 提交 PR。

每个 PR 请保持主题单一，并按 [PR 模板](.github/pull_request_template.md)填写说明：

- 简述背景问题、实现方案、公开 API 或集成影响，以及验证步骤与结果。
- 关联对应的 Issue 或提案；仅当 PR 完整解决该 Issue 时才使用自动关闭关键词。
- 纯文档改动的“设计与兼容性”、无风险或无规范例外的“风险与例外”、无需额外提示的“审查者注意事项”可填写 `None`；其余情况请直接标出需要重点审阅的文件、设计权衡或待确认问题。
- 如需申请项目编码规范例外，请列明所涉条款、影响范围、理由及评估过的替代方案。

所有代码贡献均须同时遵循 [Epic Games 的 Unreal Engine C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与[本项目编码规范](Docs/CodingStandards/README.zh-CN.md)。后者对文件编码、本地检查脚本，以及分支名、提交标题和 PR 标题的前缀格式均有明确规定（例如分支名 `docs/clarify-contribution-guide`，提交或 PR 标题 `docs: clarify contribution guide`）。

### 本地验证与 CI

提交审查前，请参照[构建脚本、工具与审查](Docs/CodingStandards/BuildAndReview.zh-CN.md)运行与改动相关的本地检查，并在 PR 描述中如实列出已执行的检查及受环境所限未能运行的项目。修改公开头文件注释或规范文档时，可在本地用 Doxygen 预览站点效果（见[本地预览](Docs/Hosting/README.zh-CN.md#本地预览)）；修改 SDK 运行时接入时，可按[开发与 CI 流程](Docs/DevelopmentWorkflow.zh-CN.md)执行共享 Runtime 的打包回归。每项独立的框架能力都应配备验证外部可观察行为的自动化测试，CI 不强制要求固定数值的行覆盖率。

PR 提交后会自动运行两类流水线：

1. **GitHub Actions**：检查分支/提交/PR 命名、代码格式、文本编码与换行符、Doxygen API 文档，并在 Linux 环境下运行跨平台自动 Hook 编译链接用例与 MST 源码分析测试。
2. **可信 Linux PR 编译**：核验当前提交后触发 CircleCI，使用 Unreal Engine 5.6.1 并行编译 Linux Game 与 Editor 目标；其中 Editor 任务还会执行 Hook 分发自动化测试并独立构建 `ModDevelopmentSDKEditor`。该流程原生支持 Fork PR，贡献者无需注册或配置 CircleCI。

出于安全隔离考虑，若 Fork PR 修改了受保护的 CI 工作流、自动化脚本、Unreal 或 CMake 构建脚本、可执行辅助脚本，或 `.uproject`/`.uplugin` 描述文件，可信编译策略会拒绝自动触发，需由维护者在源仓库分支中协助验证。完整检查顺序与编译范围详见[开发与 CI 流程](Docs/DevelopmentWorkflow.zh-CN.md)。

所有 PR 均合入 `main`。分支保护要求命名、格式与文本、API 文档、Linux Game、Linux Editor 及 Linux 构建汇总共六项检查全部通过，并由非最后一次推送者的维护者批准。维护者将围绕正确性、模块边界、API 契约、性能、安全性及 UE 5.6+ 兼容性开展审查。

## 大文件与 Git LFS

在引入体积较大的二进制依赖、启用 Git LFS 跟踪或调整 LFS 规则之前，请先创建 Issue 与维护者确认存储方案和许可合规性。本仓库未配置 Git LFS 远端服务，向个人 GitHub Fork 推送的权限也无法访问外部大文件存储。提交代码时请注意排除构建产物、缓存、本地 AI Agent 指令文件、密钥凭据和个人开发环境配置，并确保 [NOTICE.md](NOTICE.md) 中的第三方版权归属完整准确。
