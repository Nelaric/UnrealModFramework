<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CircleCI.md) | 简体中文

# CircleCI Linux 构建

仓库的 [CircleCI 配置](../.circleci/config.yml)定义了两个并行的 UE 5.6.1 Linux 作业。`build_game_linux` 编译 `UnrealModFrameworkDev` Game Target 及启用的 `ModRuntime` 插件。`build_editor_linux` 编译包含 `ModRuntimeEditor` 的 `UnrealModFrameworkDevEditor`，随后用 UAT `BuildPlugin` 将独立的 `SourcePlugins/ModDevelopmentEditor` 插件打包为 Linux 版本。仓库目前没有 Server Target，也没有框架自动化测试。这些作业只验证构建，不发布安装包。

## 接入仓库

1. 将 `Nelaric/UnrealModFramework` 加入 `Nelaric` 组织现有 CircleCI GitHub App 安装可访问的仓库范围。
2. 在 CircleCI 中创建 `UnrealModFramework` 项目。配置来源和检出来源都选此 GitHub 仓库，配置路径为 `.circleci/config.yml`。为源仓库的分支创建 push 触发器。不要启用会把项目凭据暴露给不可信代码的 Fork PR 触发器。
3. 在 CircleCI 项目的 **Environment Variables** 中，将 `GHCR_USERNAME` 设为已关联 Epic Games 的 GitHub 账号，将 `GHCR_TOKEN` 设为具有读取软件包权限的令牌。令牌必须能拉取 `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`。不要把这两个值写入 Git。若 Nelaric 现有 CircleCI 项目的凭据满足要求，可在新项目中重新录入；CircleCI 不会显示已保存的值。
4. 用 `circleci config validate` 验证配置，从项目触发一次流水线，并确认两个作业均通过。作业使用带 Docker 的 CircleCI Linux machine executor 和 `large` 资源规格。

push 触发器为源仓库中的工作分支和 `main` 提供构建反馈。GitHub Actions 的[质量检查](../.github/workflows/quality.yml)仍在 PR 上运行。若要使用私有 UE 镜像构建 Fork PR，需要另行设计可信触发流程；不要直接向 Fork 作业传递项目密钥。

拉取私有引擎镜像后，容器在无网络环境中运行。每个作业先把检出的文件复制到可写的临时目录，供 UE 容器用户写入生成文件。Editor 作业将制作插件打包到该临时目录，不上传为构建产物。
