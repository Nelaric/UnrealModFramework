<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CircleCI.md) | 简体中文

# CircleCI Linux 构建

仓库的 [CircleCI 配置](../.circleci/config.yml)定义了两个并行的 UE 5.6.1 Linux 作业。`build_game_linux` 编译 `UnrealModFrameworkDev` Game Target 及启用的 `ModRuntime` 插件。`build_editor_linux` 编译包含 `ExportMetadata`、`BlueprintIntegration` 和 `ExportCodeGenerator` 的 `UnrealModFrameworkDevEditor`，随后用 UAT `BuildPlugin` 将独立的 `UnrealModFramework/SourcePlugins/ModDevelopmentEditor` 插件打包为 Linux 版本。仓库目前没有 Server Target。这些作业只验证构建，不运行 Editor 自动化测试，也不发布安装包。

## 项目接入状态

CircleCI 项目通过 `github/Nelaric/UnrealModFramework` GitHub OAuth 连接。配置来源和检出来源都是此仓库，配置路径为 `.circleci/config.yml`，触发器覆盖所有 push。`GHCR_USERNAME` 和 `GHCR_TOKEN` 已从 Nelaric 现有 Gameplay 项目导入新项目的 **Environment Variables**。令牌必须能拉取 `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`；不要把变量值写入 Git。项目的 **Build forked pull requests** 和 **Pass secrets to builds from forked pull requests** 开关均已关闭。

若需重建 CircleCI 项目，应选择 `main` 上已提交的配置，从 `nelaric-unreal-gameplay` 导入两个 GHCR 变量，保持两个 Fork 开关关闭，并确认两个作业都通过。修改配置后先运行 `circleci config validate`。作业使用带 Docker 的 CircleCI Linux machine executor 和 `large` 资源规格。

push 触发器为源仓库中的工作分支和 `main` 提供构建反馈。GitHub Actions 的[质量检查](../.github/workflows/quality.yml)仍在 PR 上运行。若要使用私有 UE 镜像构建 Fork PR，需要另行设计可信触发流程；不要直接向 Fork 作业传递项目密钥。

拉取私有引擎镜像后，容器在无网络环境中运行。每个作业先把 `UnrealModFramework/` 中的 UE 工程复制到可写的临时目录，供 UE 容器用户写入生成文件。暂存工程在容器中的路径仍为 `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject`。Editor 作业将制作插件打包到该临时目录，不上传为构建产物。
