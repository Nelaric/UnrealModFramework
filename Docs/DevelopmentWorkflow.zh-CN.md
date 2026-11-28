<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与 CI 流程

本文说明贡献检查、Linux 构建与文档发布流程。准备改动前，请先阅读[贡献指南](../CONTRIBUTING.zh-CN.md)。

## 从改动到 `main`

1. 在分支或 Fork 中完成主题集中的改动，并运行相关的[本地检查](CodingStandards/BuildAndReview.zh-CN.md#本地检查)。
2. 使用 [PR 模板](../.github/pull_request_template.md)向 `main` 发起 PR，并在适用时关联相关 Issue。
3. 修复所有检查失败并完成维护者审查后再合并。维护者应通过分支保护规则要求相关检查通过。
4. [启用 API Pages](Hosting/README.zh-CN.md) 后，合并到 `main` 会发布生成的 API 文档。

## Development CI

[Development CI](../.github/workflows/development.yml) 对 GitHub 的 PR 测试合并提交运行 Code Quality，通过后启动 Native Tools。公开 PR 和手动运行可以执行不需要引擎的检查。在 Nelaric 授权私有仓库手动启动完整验证时，四个 Editor 和六个 Game Prepare 也会在 Code Quality 后并行启动。每个 Game 同时等待自己的 Game Prepare 和指定 Editor。Windows 和 Linux ARM64 使用同系统 X64 Editor 的 Metadata。两个 MacOS Game 分别使用各自架构的 Editor Metadata。Development Result 汇总 18 个上游 Job。

Editor 在 Metadata 校验通过后立即上传，然后构建正常 Editor，并以无界面模式运行 `Nelaric.*` Automation Tests。Game Prepare 必须推进至所有剩余 Action 都依赖 Metadata 的状态。Game 在另一台原生 Runner 上恢复已安装的 SDK 和项目 Checkpoint，验证恢复状态、导入 Metadata，并完成 Monolithic Development 目标。汇总包含 Automation Test 实际数量、Artifact 字节数、阶段耗时、Action 复用次数和不可移植原因。冷构建目标为 5～10 分钟，Runner 排队不计入。API Pages 和发布测试源码各有独立用途。

### 启动授权完整验证

1. 将这个工作流、本地 Composite Action、`Scripts/CI/`、`Scripts/check_git_naming.py` 和 `Scripts/check_automation_report.py` 部署到 Nelaric 授权私有仓库的受保护默认分支。仅维护者可修改工作流和启动运行。给 `UMFCI_READ_TOKEN` 授予读取私有 UMFCI 安装器与发行版的权限。安装仅可向公开 UMF 仓库写入 Commit Status 的 GitHub App，并在私有仓库设置 `UMF_STATUS_APP_ID` 和 `UMF_STATUS_APP_PRIVATE_KEY`。
2. 在私有副本的[引擎锁](../.github/engine-lock.json)填入十个通过 UMFCI 验收的不可变 SDK 发行版、Recipe、Manifest 摘要、安装器提交、上游引擎提交、确定的 .NET SDK 及工具链约束。仓库中的目标清单为待验收目标保留空发行标识。授权运行会在调度引擎 Job 前拒绝不完整的锁。
3. 在受保护分支添加下述经过审查的命令文件。私有 Runner 与 Artifact 存储的额外支出由组织层面的预算统一限制。
4. 审查准确的公开 PR Head、当前 `main` Base 和 GitHub 测试合并 SHA，然后使用 PR 编号及三个 SHA 启动验证。工作流核对合并提交的两个父提交，在调度引擎任务和回传结果前再次检查当前 PR，并将 `ci/development` 绑定到通过验证的测试合并 SHA。同一 PR 更新时会取消旧的公开运行。为同一 PR 发起新的授权运行会取消旧的私有运行。公开 PR 更新无法直接取消另一仓库中的运行，过期的私有运行无法回传通过状态。

### 接入构建阶段

私有仓库的 `.github/development-commands.json` 包含值为 `1` 的 `schemaVersion` 和 `commands` 对象。每项都是调用已实现接口的真实工具或经审查适配器的非空参数数组。CI 从候选源码目录直接执行参数数组。可引用 `{source}`、`{control}`、`{work}`、`{sdk}`、`{evidence}`、`{target}`、`{configuration}`、`{architecture}`、`{ubt_platform}`、`{ubt_architecture}`、`{commit}`、`{lock_sha256}`、`{engine_version}`、`{upstream_commit}`、`{toolchain}`、`{metadata_target}`、`{metadata_payload}` 和 `{checkpoint_payload}`。构建命令也能读取对应的大写 `UMF_` 环境变量。原生 Checkpoint 格式负责保留可执行权限和路径重定位资料。

| 命令键 | 验收要求 |
| --- | --- |
| `build-native-tools`、`prepare-libclang` | 每个生产者 Job 构建一次必要工具并准备对应宿主 libclang。Game Prepare 将可恢复的工具与依赖纳入 Checkpoint。 |
| `prepare-editor-bootstrap`、`export-metadata`、`validate-metadata`、`build-editor` | 导出器确实需要时才执行 Bootstrap。将原生 Metadata 写入 `{metadata_payload}`，通过项目接口校验，然后构建正常 Editor。CI 直接运行 Editor 测试。Game 在指定的 `{metadata_target}` 上调用同一校验接口。 |
| `prepare-game`、`save-checkpoint`、`validate-checkpoint` | 解析真实 Target、Module 和 Action Graph，执行所有可在缺少 Metadata 时执行的工作。将项目 Checkpoint 写入 `{checkpoint_payload}`，由项目校验 Action、输入指纹、工具链、原生工具、ABI 输入和路径重定位。CI 将已安装的 SDK 随项目 Checkpoint 传递。 |
| `restore-checkpoint`、`import-metadata`、`resume-game`、`verify-game` | 使用已恢复的 SDK 还原项目状态并校验实际宿主工具链，继续执行剩余工作。校验 Monolithic 目标的原生链接回执，并把已经链接的可执行文件交给 MBT 检查架构。 |
| `test-native-development` | 在 Native Tools Job 内运行已实现的 Metadata、Checkpoint、SDK Plan 和 Manifest 测试。该命令可引用 `{source}`、`{control}` 和已经生成工具的 `{native}` 目录。 |

项目校验接口应在 `{evidence}` 生成如下报告。报告必须源于真实工具输出和构建状态。Checkpoint 的序列化和构建续跑继续由项目接口完成。

- 所有报告均包含 `target`、`source_commit`、`engine_lock_sha256`、`engine_version`、`configuration`、`architecture` 和 `input_sha256`。Metadata 使用生产者 Editor 的目标与架构。
- `metadata.json` 包含 `bundle_valid`。校验器要在 Editor 和 Game Runner 上独立计算相同的 Metadata 输入。
- `checkpoint.json` 还包含 `toolchain_sha256`、`build_manifest_sha256`、`paths_valid`、`native_tools_runnable`、`inputs_valid`、`actions_valid`、`completed_actions`、`pending_actions`、`metadata_independent_pending_actions` 和记录原因及数量的 `nonportable_actions` 对象。恢复适配器必须核验当前 Runner 的工具链，并重建同一份规范化 `{toolchain}` 文件。CI 会与生产者的 SHA-256 比较，并要求可提前执行的待处理 Action 为零。
- `game.json` 写入为 `Monolithic` 的 `link_type`、`link_receipt_valid`、相对源码目录的 `link_receipt` 和 `binary`、相对工作目录的 `mbt`、`metadata_input_sha256`、`reused_actions`、`reexecuted_actions`、`reexecuted_portable_actions`、`pending_actions`、`metadata_dependent_seconds` 和 `link_seconds`。重执行 Action 必须在 Checkpoint 标明的不可移植范围内，可移植 Action 的重复执行数量必须为零。

两个 Artifact 都使用实际生产者的不可变 ID，并记录目标、Commit 和 Attempt。失败 Job 重跑时，只能在上传后一小时内使用同一 Run 中仍可校验通过的上游产物。超过窗口或汇总成功后产物已清理时，须重跑整个工作流。GitHub 原生最短保留期为一天。CI 在成功后删除已消费的产物，并对失败或取消后等待 GitHub 清理的产物继续执行一小时下载限制。[GitHub Artifact 保留期限](https://github.com/actions/upload-artifact#retention-period)。

### 切换合并门禁

[main 规则集](../.github/rulesets/protect-main.json)以禁用状态提交。完整授权运行通过后，在实际规则集中把 `ci/development` 绑定到批准的 GitHub App，把 `Code Quality` 绑定到 GitHub Actions。启用要求候选提交基于最新 `main` 的规则，与现有保护一起运行，并在当前测试合并提交上验收这两项状态。门禁验收通过后，再清理 CircleCI 状态、配置、Webhook 和 Fork PR Build Bot。过渡期仍遵守目前的线上必需检查。

## 过渡期的现有检查

Quality 工作流检查 PR 与提交命名、格式、文本规范及 API 文档，并运行 CircleCI 构建交接的单元测试；独立的 Linux 用例进一步验证面向 COFF、ELF 与 Mach-O 目标的自动 Hook 编译链接，以及基于 GCC 和 Clang 编译数据库的 MST 跨文件异常分析。工具版本锁定在工作流文件及 `.config/dotnet-tools.json` 中。[Quality 工作流](../.github/workflows/quality.yml)

完成 [CircleCI 接入](CircleCI.zh-CN.md)后，可通过设置仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED=true` 启用[可信 PR 编译工作流](../.github/workflows/fork-pr-linux.yml)。在该变量启用前，其各 Job 会被跳过；现有的 CircleCI Push 流水线保持独立运行。

PR 工作流会先验证提交并触发 CircleCI，随后并行展示 Game 与 Editor 两个 Job 并汇总结果。两个目标分别上报 `ci/linux-game-build` 和 `ci/linux-editor-build` 状态，`ci/fork-pr-linux-build` 负责汇总两者。汇总摘要包含 CircleCI 日志链接，Actions Job 会镜像输出检出与编译日志，同时过滤认证相关输出。贡献者无需 CircleCI 账号。

对于 Fork PR，如涉及修改 CI 工作流、自动化脚本、Unreal 描述文件与构建脚本、CMake 文件或可执行辅助脚本，须由维护者在源仓库分支中处理。策略会同时检查重命名文件的新旧路径。源仓库分支的 PR 可以修改这些文件并继续使用可信编译流程。原生模组 ABI 变更同样需要维护者审查。

## Linux 编译的范围

Push 配置与可信 PR 配置均使用 Unreal Engine 5.6.1，为 `UnrealModFramework/UnrealModFrameworkDev.uproject` 构建 Linux x64 Development 目标。

| CircleCI Job | 工作内容 |
| --- | --- |
| `build_game_linux` | 编译 `UnrealModFrameworkDev` 及其已启用的运行时模块。 |
| `build_editor_linux` | 编译 `UnrealModFrameworkDevEditor`，运行覆盖 C++ 与蓝图 Hook 分发的 `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`，再通过 `ModSDKTool build-editor` 构建并校验独立制作插件。 |

各 Job 在独立的可写暂存目录中执行构建，并在拉取私有引擎镜像后以无网络模式运行 UE 容器。`ModBuildTool build-ue-target` 驱动 UBT 编译生成的 Hook 源码副本，UBT 则在所属模块内直接编译生成的宿主连接代码。Editor Job 同时核对编辑器退出码与测试成功标记，并将校验通过的制作插件保留在 Job 暂存目录中。Server、Shipping 及更多平台与引擎版本的组合仍需单独验证。[ModRuntime UHT 接入](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)

每项独立框架功能均须按[审查规范](CodingStandards/BuildAndReview.zh-CN.md#自动化测试)配备面向行为的自动化测试，但不设数值化覆盖率门槛。对于已完成的 Unreal 自动化运行，可使用 `Scripts/check_automation_report.py` 校验导出报告、日志及编辑器退出码：

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## 运行调用性能基准

在仓库根目录运行[基准脚本](../Test/Performance/run_benchmark.py)，并指定 UE 5.6 安装目录与报告输出路径。通过 `--report` 显式指定路径可保留仓库中已提交的测量结果，便于后续比对。

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

独立基准工程包含 200 项用例，每条可用路径采样 1,000 次。Win64 x64 会测量宿主 C++、宿主蓝图，以及已挂载 `BenchmarkMod` 中的 C++ 与蓝图调用路径。请使用能够构建模块化 Development Game 的引擎源码版，或通过 `--modular-runtime` 传入匹配的模块化运行时；其他受支持的平台仅运行宿主侧路径。不可用路径在报告中标为 `未测试`。汇总值为每列可用用例均值的平均，反映当前测试机器与测试场景的表现；详见[仓库中的测量结果](../Test/Performance/ue-call-benchmark.md)。

## 检查失败时

请打开失败的 Actions Job 查看摘要，并点击其中的链接进入对应 CircleCI 日志；常规 push 构建也会显示在 [CircleCI 项目](https://app.circleci.com/pipelines/github/Nelaric/UnrealModFramework)中。如果 PR 编译被跳过，维护者应参照 [CircleCI 接入说明](CircleCI.zh-CN.md)核对启用变量、Webhook 配置及密钥。请求协助排查时，请附上失败运行的链接及已尝试的排查步骤。

各工作流配置分别位于 [Quality](../.github/workflows/quality.yml)、[可信 PR 编译](../.github/workflows/fork-pr-linux.yml)、[API Pages](../.github/workflows/pages.yml)、[CircleCI push 编译](../.circleci/config.yml)和 [CircleCI PR 编译](../.circleci/pr-build.yml)。
