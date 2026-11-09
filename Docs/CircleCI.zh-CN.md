<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CircleCI.md) | 简体中文

# CircleCI Linux 构建

[push 配置](../.circleci/config.yml)和[可信 PR 配置](../.circleci/pr-build.yml)都定义了两个并行的 Unreal Engine 5.6.1 Linux Job。`build_game_linux` 编译 `UnrealModFrameworkDev` 及其已启用的运行时模块。`build_editor_linux` 编译 `UnrealModFrameworkDevEditor`，包括 `ExportMetadata`、`BlueprintIntegration` 和 `ExportCodeGenerator`，再通过 UAT `BuildPlugin` 为 Linux 打包独立 `ModDevelopmentSDKEditor` 插件。仓库没有 Server Target，也未配置包发布或 Editor 自动化测试步骤。

## 已启用的 PR 接入

仓库已设置 `CIRCLECI_PR_BUILDS_ENABLED=true`，当前触发端点保存在加密的 Actions Secret `CIRCLECI_PR_BUILD_WEBHOOK_URL` 中。`UnrealModFramework PR builds` 流水线使用 `.circleci/pr-build.yml`，配置和初始检出均固定为 `main`。下方步骤用于重建该接入。

## 常规 push 构建

使用 CircleCI 的 `github/Nelaric/UnrealModFramework` GitHub OAuth 项目，配置与检出源均选择本仓库，配置路径为 `.circleci/config.yml`，触发器覆盖源仓库 push。在 CircleCI 项目环境变量中设置 `GHCR_USERNAME` 和 `GHCR_TOKEN`。令牌必须能够拉取 `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`；不要把变量值写入 Git。

保持 **Build forked pull requests** 和 **Pass secrets to builds from forked pull requests** 关闭。Fork 编译使用下方独立的可信接入流程。两类作业都使用带 Docker 的 Linux machine executor 和 `large` 资源规格。

## 启用可信 PR 编译

[Actions 工作流](../.github/workflows/fork-pr-linux.yml)按需启用：仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED` 设为 `true` 前，各 Job 会跳过。启用前完成以下接入：

1. 通过正常审查流程，将工作流、`Scripts/pr_build_policy.py`、`Scripts/fork_pr_build_bot.py` 和 `.circleci/pr-build.yml` 合入 `main`。
2. 在同一 CircleCI 项目中添加 GitHub App 流水线。OAuth 组织也可以安装 GitHub App 集成。配置与检出仓库均选择 `Nelaric/UnrealModFramework`，配置路径选择 `.circleci/pr-build.yml`，自定义 Webhook 的配置与检出分支均为 `main`。保留现有 OAuth push 流水线。
3. 为该流水线添加 **Custom webhook** 触发器。将生成的完整 HTTPS 地址及其密钥保存到 GitHub Actions Secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`。机器人仅接受 `internal.circleci.com` 端点，不应复用其他仓库的 Webhook 或流水线编号。
4. 确保 PR 流水线可以读取本项目的 `GHCR_USERNAME` 和 `GHCR_TOKEN`，并限制其配置与初始检出都来自可信 `main`。
5. 设置仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED=true`，使用已审查的 PR 验证流程。接入验证后，在分支保护中要求命名、格式、API 检查及 `ci/fork-pr-linux-build` 通过。独立目标状态为 `ci/linux-game-build` 和 `ci/linux-editor-build`。

机器人仅向 Webhook 发送 PR 编号、准确的 head SHA 和随机关联标识。可信策略先验证 PR 仍开放且目标为本仓库 `main`，再获取 `refs/pull/<number>/head`，并确认实际提交仍与请求 SHA 一致。Fork 对受保护 CI 与构建路径的改动会被拒绝。受保护路径包括工作流与自动化目录、`.gitattributes`、`.gitmodules`、Unreal 描述文件与构建脚本、CMake 文件和可执行辅助脚本；重命名同时检查新旧路径。此类修改由维护者在源仓库分支处理。

Actions 与 CircleCI 都从可信 `main` 配置开始。Actions 不会使用具有写权限的令牌检出 PR 源码；CircleCI 在策略通过后才检出验证过的提交，并随后拉取私有引擎镜像。GitHub 会收到独立 Game、Editor 状态及汇总结果。较新的 Actions 运行会接管旧运行的状态所有权。

## 构建隔离与验证

拉取私有引擎镜像后，UE 容器使用 `--network none` 运行。每个 Job 把 `UnrealModFramework/` 复制到独立可写的临时目录。暂存工程路径为 `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject`；Editor Job 在该目录中打包独立插件，不上传生成包。

请求审查前验证两个配置：

```sh
circleci config validate .circleci/config.yml
circleci config validate .circleci/pr-build.yml
```

检查、日志入口、编译范围与故障排查见[开发流程](DevelopmentWorkflow.zh-CN.md)。服务端接入和触发值见 [CircleCI 自定义 Webhook 文档](https://circleci.com/docs/guides/orchestrate/custom-webhooks/)及[流水线值参考](https://circleci.com/docs/guides/orchestrate/pipeline-variables/)。
