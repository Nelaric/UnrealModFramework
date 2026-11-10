<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与 CI 流程

本文说明贡献检查、Linux 编译和文档发布流程。准备改动时，请先阅读[贡献指南](../CONTRIBUTING.zh-CN.md)。

## 从改动到 `main`

1. 在分支或 Fork 中完成聚焦的改动，运行相关的[本地检查](CodingStandards/BuildAndReview.zh-CN.md#本地检查)。
2. 使用 [PR 模板](../.github/pull_request_template.md)向 `main` 发起 PR，并在适用时关联 Issue。
3. 修复检查失败并完成维护者审查后再合并。维护者应通过分支保护要求相关检查通过。
4. [启用 API Pages](Hosting/README.zh-CN.md)后，合并到 `main` 会发布生成的 API 文档。

## PR 会运行哪些检查

[Quality 工作流](../.github/workflows/quality.yml)检查 PR 和提交命名、格式与文本规范，以及 API 文档。工具版本锁定在工作流和 `.config/dotnet-tools.json` 中。

完成 [CircleCI 接入](CircleCI.zh-CN.md)后，通过仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED=true` 启用[可信 PR 编译工作流](../.github/workflows/fork-pr-linux.yml)。变量启用前，其 Job 会跳过；现有 CircleCI push 流水线独立运行。

PR 工作流先验证提交并触发 CircleCI，再并行显示 Game 和 Editor Job，最后汇总结果。两个目标分别报告 `ci/linux-game-build` 和 `ci/linux-editor-build`，`ci/fork-pr-linux-build` 汇总两者。摘要包含 CircleCI 日志链接，Actions Job 会复制检出与编译输出，并排除认证输出。贡献者无需 CircleCI 账号。

Fork PR 如需修改 CI 工作流、自动化脚本、Unreal 描述文件与构建脚本、CMake 文件或可执行辅助脚本，应由维护者在源仓库分支处理。策略同时检查重命名前后的路径。源仓库分支的 PR 可以修改这些文件并使用可信编译流程。原生模组 ABI 变更也需要维护者审查。

## Linux 编译的范围

push 配置和可信 PR 配置都使用 Unreal Engine 5.6.1，以 Linux Development 配置构建 `UnrealModFramework/UnrealModFrameworkDev.uproject`：

| CircleCI Job | 工作内容 |
| --- | --- |
| `build_game_linux` | 编译 `UnrealModFrameworkDev` 及其已启用的运行时模块。 |
| `build_editor_linux` | 编译 `UnrealModFrameworkDevEditor` 及其支持的编辑器模块，再通过 `ModSDKTool build-editor` 构建并校验独立的 `ModDevelopmentSDKEditor` 插件。 |

各 Job 使用独立的可写暂存目录，拉取私有引擎镜像后，UE 容器在无网络环境中运行。宿主自动生成接入当前要求 Win64 x64 MSVC。工具与输入准备步骤见[宿主生成流程](../README.zh-CN.md#mht-api-代码生成)。

仓库没有 Server Target。这些 Job 验证编译和插件打包，不发布可下载包，也不运行 Unreal 自动化测试。其他平台及更新的支持引擎版本仍是构建覆盖缺口。

每项独立框架功能应按[审查规范](CodingStandards/BuildAndReview.zh-CN.md#自动化测试)提供关注行为的自动化测试，不设数值化覆盖率门槛。对于已有的 Unreal 自动化运行，可使用 `Scripts/check_automation_report.py` 验证导出报告、日志和编辑器退出码：

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## 检查失败时

打开失败的 Actions Job，查看摘要并进入对应的 CircleCI 日志。[CircleCI 项目](https://app.circleci.com/pipelines/github/Nelaric/UnrealModFramework)也会显示常规 push 构建。如果 PR 编译被跳过，维护者应按 [CircleCI 接入说明](CircleCI.zh-CN.md)检查启用变量、Webhook 和密钥。请求协助时，请附上失败运行链接和已经尝试的排查步骤。

配置分别位于 [Quality](../.github/workflows/quality.yml)、[可信 PR 编译](../.github/workflows/fork-pr-linux.yml)、[API Pages](../.github/workflows/pages.yml)、[CircleCI push 编译](../.circleci/config.yml)和 [CircleCI PR 编译](../.circleci/pr-build.yml)。
