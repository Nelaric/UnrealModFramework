<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CONTRIBUTING.md) | 简体中文

# 参与 UnrealModFramework 开发

欢迎使用英语或中文提交问题报告、设计反馈、文档改进和代码贡献。仓库目前包含 UE 5.6 开发宿主工程、游戏侧 Runtime 插件，以及独立构建的模组开发编辑器。提出改动时，请说明宿主游戏或模组作者遇到的问题、期望行为和负责该能力的模块。

创建 Issue 前先搜索[现有问题](https://github.com/Nelaric/UnrealModFramework/issues)。缺陷报告应包含复现步骤、预期与实际行为、UE 版本、平台和构建 Target。每个报告聚焦一件事，并避免公开私人信息。

外部贡献者应先 Fork 仓库，在自己的 Fork 中建立工作分支，再向 `Nelaric/UnrealModFramework:main` 发起 PR。每个 PR 聚焦一项改动，说明设计或兼容性影响、测试与构建结果，以及相关限制或规范例外。CI、构建脚本、插件描述文件和原生 ABI 契约的改动需要维护者审查。

同时遵守 [Epic C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)和[项目编码规范](Docs/CodingStandards/README.zh-CN.md)。项目规范规定版权声明、文本编码、格式化工具、公开 API 文档、测试及分支、提交和 PR 命名。审查前运行[相关本地检查](Docs/CodingStandards/BuildAndReview.zh-CN.md#本地检查)。改动影响 Unreal Target 时，报告 UE 5.6 Game 和 Editor 构建结果；未来添加 Server Target 后也报告相应构建。

GitHub Actions 在 PR 中检查命名、文本、格式和 API 文档，目前不编译 UE Target。新增 Git LFS 跟踪规则或大型二进制依赖前，应先与维护者讨论存储和审查影响。
