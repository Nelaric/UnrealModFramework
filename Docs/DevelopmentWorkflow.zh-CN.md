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

[Development CI](../.github/workflows/development.yml) 在面向 `main` 的 PR 上自动触发，对测试合并提交运行 Code Quality。Native Tools 依赖 Code Quality。同仓库 PR 的四个 Editor 和六个 Game Prepare 也依赖 Code Quality，并使用受保护的 `development-engine` 环境。Fork PR 运行无引擎任务。每个 Game 同时等待自己的 Game Prepare 和指定 Editor。Windows 和 Linux ARM64 使用同系统 X64 Editor 的 Metadata。两个 MacOS Game 分别使用各自架构的 Editor Metadata。Development Result 汇总 18 个上游 Job。

Editor 导出、校验并发布 Metadata，然后构建正常 Editor，以无界面模式运行 `Nelaric.*` Automation Tests。Game Prepare 执行所有不依赖 Metadata 的 Action。Game 在另一台原生 Runner 上安装同一份锁定的私有 SDK，恢复项目 Checkpoint、导入 Metadata，并完成 Monolithic Development 目标。Development Result 记录测试数量、Artifact 字节数、阶段耗时、Action 复用次数和不可移植原因。

### 配置引擎任务

1. 配置 `development-engine` GitHub 环境，由维护者审查候选提交。将可读取私有 UMFCI 安装器与发行版的 `UMFCI_READ_TOKEN` 放入该环境。为回传 `ci/development` 的 GitHub App 设置 `UMF_STATUS_APP_ID` 和 `UMF_STATUS_APP_PRIVATE_KEY`。
2. 把 [Development CI](../.github/workflows/development.yml) 中的 `UMFCI_RELEASE_TAG` 设为包含全部十个 SDK 目标的不可变 UMFCI 正式发行版标签。Runner 使用该标签上的安装器和 .NET SDK 配置，并按发行版索引为各目标选择对应的 SDK 归档和安装清单。
3. 如需手动运行已审查的 PR，从受保护的默认分支使用 PR 编号、PR Head SHA、当前 `main` Base SHA 和 GitHub 测试合并 SHA 发起运行。同一 PR 更新时会取消旧运行。

### 接入构建阶段

[Development 命令配置](../.github/development-commands.json) 将 `schemaVersion` 设为 `1`，并为各阶段配置调用 `Scripts/development_commands.py` 的参数数组。CI 从候选源码目录直接执行参数数组。可引用 `{source}`、`{control}`、`{work}`、`{sdk}`、`{evidence}`、`{target}`、`{configuration}`、`{architecture}`、`{ubt_platform}`、`{ubt_architecture}`、`{commit}`、`{lock_sha256}`、`{engine_version}`、`{upstream_commit}`、`{toolchain}`、`{metadata_target}`、`{metadata_payload}` 和 `{checkpoint_payload}`。引擎阶段也能读取对应的大写 `UMF_` 环境变量。原生 Checkpoint 格式负责保留可执行权限和路径重定位资料。

| 命令键 | 职责 |
| --- | --- |
| `build-native-tools`、`prepare-libclang` | 每个生产者 Job 构建一次必要工具并准备对应宿主 libclang。Game Prepare 将可恢复的工具与依赖纳入 Checkpoint。 |
| `prepare-editor-bootstrap`、`export-metadata`、`validate-metadata`、`build-editor` | 导出器确实需要时才执行 Bootstrap。将原生 Metadata 写入 `{metadata_payload}`，通过项目接口校验，然后构建正常 Editor。CI 直接运行 Editor 测试。Game 在指定的 `{metadata_target}` 上调用同一校验接口。 |
| `prepare-game`、`save-checkpoint`、`validate-checkpoint` | 解析真实 Target、Module 和 Action Graph，执行所有可在缺少 Metadata 时执行的工作。将项目 Checkpoint 写入 `{checkpoint_payload}`，由项目校验 Action、输入指纹、工具链、原生工具、ABI 输入和路径重定位。 |
| `restore-checkpoint`、`import-metadata`、`resume-game`、`verify-game` | 使用重新安装的锁定 SDK 还原项目状态，继续执行剩余工作。校验 Monolithic 目标的原生链接回执，并把已经链接的可执行文件交给 MBT 检查架构。 |
| `test-native-development` | 在 Native Tools Job 内运行已实现的 Metadata、Checkpoint、SDK Plan 和 Manifest 测试。该命令可引用 `{source}`、`{control}` 和已经生成工具的 `{native}` 目录。 |

项目校验接口应在 `{evidence}` 生成如下报告。报告必须源于真实工具输出和构建状态。Checkpoint 的序列化和构建续跑继续由项目接口完成。

- 所有报告均包含 `target`、`source_commit`、`engine_lock_sha256`、`engine_version`、`configuration`、`architecture` 和 `input_sha256`。`engine_lock_sha256` 使用 CI 提供的 `UMF_LOCK_SHA256`，其值根据 UMFCI 发行版 ID 和标签生成。Metadata 使用生产者 Editor 的目标与架构。
- `metadata.json` 包含 `bundle_valid`。校验器要在 Editor 和 Game Runner 上独立计算相同的 Metadata 输入。
- `checkpoint.json` 还包含 `toolchain_sha256`、`build_manifest_sha256`、`paths_valid`、`native_tools_runnable`、`inputs_valid`、`actions_valid`、`completed_actions`、`pending_actions`、`metadata_independent_pending_actions` 和记录原因及数量的 `nonportable_actions` 对象。CI 独立配置续跑 Runner 的工具链，在恢复 Checkpoint 之前将规范化 `{toolchain}` 文件的 SHA-256 与生产者的记录比对。
- `game.json` 写入为 `Monolithic` 的 `link_type`、`link_receipt_valid`、相对源码目录的 `link_receipt` 和 `binary`、相对工作目录的 `mbt`、`metadata_input_sha256`、`reused_actions`、`reexecuted_actions`、`reexecuted_portable_actions`、`pending_actions`、`metadata_dependent_seconds` 和 `link_seconds`。重执行 Action 必须在 Checkpoint 标明的不可移植范围内，可移植 Action 的重复执行数量必须为零。

工作流将 Metadata 或项目 Checkpoint 及其构建记录上传为 GitHub Actions Artifact。项目产物路径只存放不含 Unreal Engine 文件的构建输出。每个 Game 在同一次运行中按 ID 下载两个上游 Artifact。Checkpoint 记录锁定的 UMFCI SDK，Runner 直接从对应的私有发行版安装 SDK。构建日志随 Actions Job 展示。产物封存后一小时内可用于续跑。GitHub 最短保留期为一天，Development Result 成功后清理已使用的 Artifact。超过续跑窗口或产物已清理时，重跑整个工作流。

### 切换合并门禁

[main 规则集](../.github/rulesets/protect-main.json)定义 Development CI 的合并门禁。启用规则集时，将 `ci/development` 绑定到授权的 GitHub App，将 `Code Quality` 绑定到 GitHub Actions。

## Quality 工作流

Quality 工作流检查 PR 与提交命名、格式、文本规范及 API 文档。独立的 Linux 用例检查面向 COFF、ELF 与 Mach-O 目标的自动 Hook 编译链接，以及基于 GCC 和 Clang 编译数据库的 MST 跨文件异常分析。工具版本锁定在工作流文件及 `.config/dotnet-tools.json` 中。[Quality 工作流](../.github/workflows/quality.yml)

## 查看 Unreal 自动化测试结果

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

打开失败的 Actions Job 查看日志与摘要。请求协助排查时，请附上失败运行的链接及已尝试的排查步骤。

各工作流配置分别位于 [Development CI](../.github/workflows/development.yml)、[Quality](../.github/workflows/quality.yml)和 [API Pages](../.github/workflows/pages.yml)。
