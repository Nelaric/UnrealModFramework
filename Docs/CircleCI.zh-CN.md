<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CircleCI.md) | 简体中文

# CircleCI Linux 构建

Push 配置与可信 PR 配置各定义了两个并行的 Unreal Engine 5.6.1 Linux x64 Development Job：`build_game_linux` 构建 `UnrealModFrameworkDev`，`build_editor_linux` 构建 `UnrealModFrameworkDevEditor`、运行 Hook 分发自动化测试，并构建独立的 `ModDevelopmentSDKEditor` 插件。[Push 配置](../.circleci/config.yml) · [可信 PR 配置](../.circleci/pr-build.yml)

两条流水线均从待测提交构建 MDT 和 MBT。共享构建脚本使用引擎自带的 .NET 宿主和 Editor BuildId，由 MDT 基于内嵌资源生成插件，并将校验通过的产物发布到 Job 暂存目录下的 `Dist/ModDevelopmentSDKEditor`。[编辑器插件构建脚本](../.circleci/build-editor-plugin.sh)

## 已启用的 PR 接入

当前仓库已设置 `CIRCLECI_PR_BUILDS_ENABLED=true`，并将触发端点保存在加密的 Actions Secret `CIRCLECI_PR_BUILD_WEBHOOK_URL` 中。`UnrealModFramework PR builds` 流水线使用 `.circleci/pr-build.yml`，其配置来源与初始检出分支均固定为 `main`。如需重建该接入，请按以下步骤操作。

## 常规 Push 构建

使用 CircleCI 的 `github/Nelaric/UnrealModFramework` GitHub OAuth 项目，将本仓库设为配置与检出源，配置路径设为 `.circleci/config.yml`，并配置针对源仓库 push 的触发器。在 CircleCI 项目环境变量中设置 `GHCR_USERNAME` 和 `GHCR_TOKEN`，确保该令牌具备拉取 `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1` 的权限，切勿将变量值写入 Git。

请保持 **Build forked pull requests** 和 **Pass secrets to builds from forked pull requests** 处于关闭状态——Fork 编译采用下方独立的可信接入流程。两类 Job 均运行在带 Docker 的 Linux machine executor 上，使用 `large` 资源规格。

## 启用可信 PR 编译

[Actions 工作流](../.github/workflows/fork-pr-linux.yml)默认按需启用：在将仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED` 设为 `true` 之前，其各 Job 均会被跳过。启用前请先完成以下接入步骤：

1. 通过正常审查流程，将工作流文件、`Scripts/pr_build_policy.py`、`Scripts/fork_pr_build_bot.py` 和 `.circleci/pr-build.yml` 合入 `main`。
2. 在同一 CircleCI 项目中添加一条 GitHub App 流水线（OAuth 组织同样可以安装 GitHub App 集成）。配置仓库与检出仓库均选择 `Nelaric/UnrealModFramework`，配置路径填写 `.circleci/pr-build.yml`，并将自定义 Webhook 的配置分支与检出分支均设为 `main`。同时保留现有的 OAuth push 流水线。
3. 为该流水线添加 **Custom webhook** 触发器，并将生成的完整 HTTPS 端点（含密钥）保存到 GitHub Actions Secret `CIRCLECI_PR_BUILD_WEBHOOK_URL` 中。机器人仅接受 `internal.circleci.com` 端点，切勿复用其他仓库的 Webhook 或流水线标识。
4. 确保 PR 流水线可以访问本项目的 `GHCR_USERNAME` 和 `GHCR_TOKEN`，并限制 Webhook 仅从可信 `main` 分支加载配置与执行初始检出。
5. 设置仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED=true`，并通过已审查的 PR 验证流程。验证通过后，在分支保护规则中要求命名、格式、API 检查及 `ci/fork-pr-linux-build` 全部通过。两个独立目标的状态名称分别为 `ci/linux-game-build` 和 `ci/linux-editor-build`。

机器人仅向 Webhook 发送 PR 编号、准确的 head SHA 与随机关联标识。在获取 `refs/pull/<number>/head` 之前，可信策略会先确认该 PR 处于开放状态且目标分支为本仓库的 `main`，并在获取后验证实际提交仍与请求的 SHA 一致。Fork PR 对受保护 CI 及构建路径的修改会被拒绝；受保护路径包括工作流与自动化目录、`.gitattributes`、`.gitmodules`、Unreal 描述文件与构建脚本、CMake 文件以及可执行辅助脚本，重命名操作会同时检查新旧路径。此类改动须由维护者在源仓库分支中处理。

Actions 与 CircleCI 均从可信的 `main` 配置启动。Actions 绝不会使用带写权限的令牌检出 PR 源码；CircleCI 则在策略校验通过后才检出已验证的提交，并随后拉取私有引擎镜像。GitHub 会收到独立的 Game 与 Editor 状态以及汇总结果，较新的 Actions 运行会接管旧运行的状态。交接脚本会对临时 API 读取失败进行有限次数的退避重试，每次交接只发送一次 Webhook 请求。[交接实现](../Scripts/fork_pr_build_bot.py)

## 构建隔离与验证

拉取私有引擎镜像后，UE 容器以 `--network none` 模式运行。各 Job 将工程复制到独立的可写目录，容器内的工程路径为 `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject`。Linux 构建调用 `ModBuildTool build-ue-target`，使用镜像内的 `libclang` 18 与配套资源头文件。Editor Job 在 `UnrealEditor` 中运行 `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`，确认编辑器退出成功且日志包含测试成功标记后，再构建制作插件。[UE 镜像依赖](../.circleci/ue-linux.Dockerfile)

发起审查前请先校验两份配置：

```sh
circleci config validate .circleci/config.yml
circleci config validate .circleci/pr-build.yml
```

检查项、日志入口、构建范围与故障排查详见[开发流程](DevelopmentWorkflow.zh-CN.md)。服务端接入与触发参数说明参见 [CircleCI 自定义 Webhook 文档](https://circleci.com/docs/guides/orchestrate/custom-webhooks/)和[流水线值参考](https://circleci.com/docs/guides/orchestrate/pipeline-variables/)。
