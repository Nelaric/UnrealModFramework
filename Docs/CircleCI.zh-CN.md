<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](CircleCI.md) | 简体中文

# CircleCI Linux 构建

常规 Push 流水线（[.circleci/config.yml](../.circleci/config.yml)）与可信 PR 流水线（[.circleci/pr-build.yml](../.circleci/pr-build.yml)）各自包含两个并行运行的 Unreal Engine 5.6.1 Linux x64 Development 任务：

- `build_game_linux`：构建 `UnrealModFrameworkDev` 游戏目标。
- `build_editor_linux`：构建 `UnrealModFrameworkDevEditor`、运行 Hook 分发自动化测试，并独立构建 `ModDevelopmentSDKEditor` 制作插件。

两条流水线都会基于待测提交当场编译 UHT 所需的原生工具；Editor 任务还会额外构建 MDT 与 MBT，再通过共享脚本 [build-editor-plugin.sh](../.circleci/build-editor-plugin.sh) 调用引擎自带的 .NET 宿主并读取 Editor `BuildId`，由 MDT 从内嵌资源生成制作插件，验证无误后发布到暂存目录下的 `Dist/ModDevelopmentSDKEditor`。

## PR 接入概览

可信 PR 编译通过 GitHub Actions 仓库变量 `CIRCLECI_PR_BUILDS_ENABLED=true` 控制开关，并从 Actions Secret `CIRCLECI_PR_BUILD_WEBHOOK_URL` 读取触发地址。CircleCI 侧的 `UnrealModFramework PR builds` 流水线固定加载 `main` 分支上的 `.circleci/pr-build.yml` 并先执行 `main` 的初始检出，通过安全策略核验后再拉取待测 PR 提交。

## 常规 Push 构建

常规分支推送使用 CircleCI 中的 `github/Nelaric/UnrealModFramework` GitHub OAuth 项目：

- 将本仓库同时设为配置源与代码检出源，配置文件路径填 `.circleci/config.yml`，触发条件设为源仓库分支推送。
- 在 CircleCI 项目环境变量中配置 `GHCR_USERNAME` 与 `GHCR_TOKEN`，确保令牌有权拉取私有引擎镜像 `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1`。切勿将凭据写入代码仓库。
- 保持 **Build forked pull requests** 与 **Pass secrets to builds from forked pull requests** 为**关闭**状态——外部 Fork 的 PR 统一走下方的可信交接链路。

两类任务均运行在启用 Docker 的 Linux `machine` 执行器上，资源规格为 `large`。

## 启用可信 PR 编译

在仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED` 设为 `true` 之前，[fork-pr-linux.yml](../.github/workflows/fork-pr-linux.yml) 中的任务会自动跳过。首次开通前请按顺序完成以下步骤：

1. 通过常规代码审查，将工作流配置、`Scripts/pr_build_policy.py`、`Scripts/fork_pr_build_bot.py` 以及 `.circleci/pr-build.yml` 合入 `main`。
2. 在同一个 CircleCI 项目下新建一条 **GitHub App** 流水线（已使用 OAuth 的组织同样可安装 GitHub App 集成，两者互不冲突）。配置仓库与检出仓库均选择 `Nelaric/UnrealModFramework`，配置路径填 `.circleci/pr-build.yml`，并将自定义 Webhook 的配置分支与初始检出分支都锁定为 `main`；原有的 OAuth Push 流水线保持不变。
3. 为新建流水线添加 **Custom webhook** 触发器，将生成的完整 HTTPS 地址（含末尾密钥参数）存入 GitHub Actions Secret `CIRCLECI_PR_BUILD_WEBHOOK_URL`。交接脚本仅允许向 `internal.circleci.com` 发送请求，请勿跨仓库混用 Webhook 或流水线 ID。
4. 确认该 PR 流水线可读取项目的 `GHCR_USERNAME` 与 `GHCR_TOKEN`，并限制该 Webhook 仅从可信的 `main` 分支加载配置与初始代码。
5. 将仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED` 设为 `true`，在已审阅的 PR 上实测全链路。验证无误后，将 `ci/fork-pr-linux-build` 连同命名、格式与 API 文档检查一并设为分支保护必选检查（两个分目标状态名分别为 `ci/linux-game-build` 与 `ci/linux-editor-build`）。

### 安全模型与交接流程

交接机器人（[fork_pr_build_bot.py](../Scripts/fork_pr_build_bot.py)）只会向 Webhook 发送 PR 编号、精确的 head SHA 以及随机关联随机数（nonce）。CircleCI 在拉取 `refs/pull/<number>/head` 前，会先由策略脚本确认该 PR 处于开放状态且目标分支为本仓库 `main`，拉取后再次核对提交 SHA 未发生漂移。凡是来自 Fork 且修改了受保护路径的 PR——包括工作流与脚本目录、`.gitattributes`、`.gitmodules`、Unreal 描述文件与构建规则、CMake 配置以及可执行脚本（重命名文件会同时核查新旧路径）——均会被直接拒绝，交由维护者在源仓库分支中审阅执行。

在整个过程中，Actions 与 CircleCI 均从受信任的 `main` 配置启动：Actions 不会用带写权限的令牌检出未受信任的 PR 代码，CircleCI 也只有在安全策略放行后才会检出该提交并拉取私有引擎镜像。同一 PR 新触发的 Actions 运行会自动接管旧运行的状态汇报；遇到偶发的 GitHub API 读取失败时，交接脚本会按有界退避策略重试，且单次交接最多只会发出一次 Webhook 调用。

## 构建隔离与验证细节

拉取私有引擎镜像后，UE 容器全程以无网络模式（`--network none`）运行，并在独立可写目录 `/workspace/UnrealModFramework/UnrealModFrameworkDev.uproject` 中编译工程副本（镜像环境见 [ue-linux.Dockerfile](../.circleci/ue-linux.Dockerfile)）。

Linux 任务通过 `ModBuildTool build-ue-target` 驱动构建，使用容器内置的 `libclang` 18 及配套资源头文件。UBT 在各所属模块内编译生成的宿主单元，MBT 在检查完最终 Linux 二进制后写出 `host-link.json`。若当前批次生成了 `HookSources.json`，Editor 任务会运行 `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`（若无 Hook 映射则回退运行 `System.Core.String.AppendInt`），并在核实编辑器退出码与测试通过标记后，继续构建独立的制作插件。

修改任一 CircleCI 配置文件后，请在发起审查前先在本地校验语法：

```sh
circleci config validate .circleci/config.yml
circleci config validate .circleci/pr-build.yml
```

日常检查顺序、日志查看方式与故障排查请参见[开发与 CI 流程](DevelopmentWorkflow.zh-CN.md)；CircleCI 侧的触发器配置细节可参考官方[自定义 Webhook 指南](https://circleci.com/docs/guides/orchestrate/custom-webhooks/)与[流水线变量参考](https://circleci.com/docs/guides/orchestrate/pipeline-variables/)。
