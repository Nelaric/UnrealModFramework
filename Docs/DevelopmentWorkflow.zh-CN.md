<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与 CI 流程

本页介绍 Development CI、本地构建、发布验证和文档发布。准备改动前，请先阅读[贡献指南](../CONTRIBUTING.zh-CN.md)。

## 从改动到 `main`

1. 在分支或 Fork 中完成主题集中的改动，并运行相关的[本地检查](CodingStandards/BuildAndReview.zh-CN.md#本地检查)。
2. 使用 [PR 模板](../.github/pull_request_template.md)向 `main` 发起 PR，并在适用时关联相关 Issue。
3. 修复所有检查失败并完成维护者审查后再合并。维护者应通过分支保护规则要求相关检查通过。
4. [启用 API Pages](Hosting/README.zh-CN.md) 后，合并到 `main` 会发布生成的 API 文档。

## Development CI

[Development CI](../.github/workflows/development.yml) 在面向 `main` 的 PR 上自动触发。Code Quality 固定后续 Job 共用的 GitHub 测试合并提交。Native Tools 在 Code Quality 之后使用 Linux x86_64 Runner。本仓库分支的 PR 在同一次运行中，由 Code Quality 启动四个 Editor 和六个 Game Prepare。Fork PR 先执行 Code Quality 和 Native Tools，维护者审查后可为确切的测试合并 SHA 发起完整运行。每个 Game 等待自己的 Game Prepare 和指定 Editor。Windows 和 Linux ARM64 使用同系统 x86_64 Editor 的 Metadata。两个 MacOS Game 分别使用各自架构的 Editor Metadata。Development Result 汇总 18 个上游 Job，并在报告结果前核对当前 PR 和 `main`。

每个 macOS Editor 和 Game Prepare Job 都使用 AppleClang 构建原生工具。Editor 导出、校验并发布 Metadata，然后构建正常 Editor，以无界面模式运行 `Nelaric.*` Automation Tests。Game Prepare 执行所有不依赖 Metadata 的 Action。Game 在另一台原生 Runner 上安装同一份锁定的私有 SDK，恢复项目 Checkpoint、导入 Metadata，并完成 Monolithic Development 目标。Development Result 记录测试数量、Artifact 字节数、阶段耗时、Action 复用次数和不可移植原因。

### 配置引擎任务

1. 配置 `development-engine` 环境，使其接受 PR 测试合并引用和按确切 SHA 发起的请求。在 **Deployment branches and tags** 中选择 **No restriction**，使 GitHub 能接受只有 Commit SHA 的运行。
2. 将仅有私有 UMFCI 仓库 `Contents: read` 权限的 `UMFCI_READ_TOKEN` 保存为环境 Secret。
3. 将 [Development CI](../.github/workflows/development.yml) 中的 `UMFCI_RELEASE_TAG` 设为包含全部十个 SDK 目标的不可变 UMFCI 正式发行版标签。Runner 使用该发行版中的安装器和 .NET SDK 配置，并按发行版索引选择对应的 SDK 归档和安装清单。

### 为 Fork 申请完整验证

审查确切的 GitHub 测试合并提交，包括其中的工作流和构建脚本。在可信的工作树中，使用拥有仓库 Maintain 或 Admin 权限的账号登录 GitHub CLI 后执行以下命令。

```text
python Scripts/CI/approve_development.py <PR 编号> <已审查的测试合并 SHA>
```

命令核对该 SHA 是否对应当前 PR Head 和 `main`，再为该提交创建 GitHub Deployment 请求。GitHub 将原生检查关联到该测试合并提交。同一 PR 更新会取消旧运行。测试合并 SHA 变化后，请为新候选提交发起验证。重试同一候选提交时，在该次运行中选择 **Re-run all jobs**。**Run workflow** 为所选引用运行公开检查。

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

当前的合并条件由 Quality 工作流提供。配置文件中的 Development CI 规则集可在完整引擎检查可用后启用。

[main 规则集](../.github/rulesets/protect-main.json)使用 GitHub Actions 原生检查 `Code Quality` 和 `Development Result`。Development Result 要求当前测试合并提交具备完整的引擎验证。Fork 候选提交由维护者审查后发起这次运行。

## Quality 工作流

Quality 提供目前用于合并的 PR 和提交命名、格式与文本、API 文档三项检查。[Quality 工作流](../.github/workflows/quality.yml)

## 本地构建

运行一次 [Setup](../README.zh-CN.md#构建开发宿主)准备四个工具和 libclang。然后使用与 CI 相同的 MBT 阶段。下例适用于合法的完整 Windows UE 5.6.1 Editor。有已验收的 Game SDK 时，将其路径传入 Game 构建。

```powershell
$mbt = "./UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe"
$project = "UnrealModFramework/UnrealModFrameworkDev.uproject"
$engine = "<完整 UE 5.6.1 或 Editor SDK>"
$gameSdk = "<Win64 Game SDK 或完整 UE 5.6.1>"
$bundle = "UnrealModFramework/Intermediate/ModTools/Metadata"
$commit = git rev-parse HEAD
$revision = (Get-Content .github/engine-lock.json -Raw | ConvertFrom-Json).upstreamCommit
$platform = @("--project", $project, "--platform", "Win64", "--architecture", "x64")
$identity = @("--commit", $commit, "--engine-commit", $revision)
$inputs = @("--configuration", "Development", "--metadata", $bundle)

& $mbt prepare-metadata --engine $engine --target UnrealModFrameworkDevEditor @platform @identity --output $bundle
& $mbt build-ue-target --engine $engine --target UnrealModFrameworkDevEditor @platform @identity @inputs --mode normal
& $mbt test-ue-target --engine $engine --target UnrealModFrameworkDevEditor @platform --report ".tools/automation"
& $mbt build-ue-target --engine $gameSdk --target UnrealModFrameworkDev @platform @identity @inputs --mode normal
```

每次自动化运行使用一个新的报告目录。使用 UMFCI SDK 时，设置 `UE_USE_SYSTEM_DOTNET=1`，让 `DOTNET_ROOT` 指向锁定的原生 .NET SDK。可用 `UMF_MSVC_VERSION` 与 `UMF_WINDOWS_SDK_VERSION` 选择 Windows 工具链，CI 会在调用 MBT 前解析和校验版本。Linux ARM64 使用 `LinuxArm64` 和 `arm64`，Windows ARM64 使用 `Win64` 和 `arm64`。两者均消费 Game SDK。

`prepare-metadata` 会复用已验证的 Bundle。Bundle 缺失或过期时，它通过 Bootstrap 规则构建本机 Development Editor，并调用导出 Commandlet。`validate-metadata` 可独立核对 Bundle 与目标。正常构建先在 MBT 中校验 Bundle，再进入 UBT。各目标独立生成编译输入。CI 将同一经审查的提交传给所有 Job。本地编辑工程时，工具也会计算工程文件和蓝图发现输入的摘要。MBT 从 UBT 查询生产者和消费目标类型。Editor 按保存的声明标记及依赖发现导出蓝图，各目标使用自身编译命令推导原生身份、布局和错误路径。

Bundle 的四个文件位于 `Metadata/`。文件名基于 `Project/` 和 `Engine/` 逻辑根。Options 和 Blueprint 数据使用反射名、参数角色与标量字段结构。消费目标会检查配置投影与生产方输入是否一致，不兼容的重定向、架构专用配置、蓝图包变化、源码漂移和负载损坏都会导致失败。C++ 布局、编译参数、Hook 对象和 ABI 由目标自身的编译数据库生成。Blueprint 使用反射结构体时，目标会以加载的反射类型校验字段结构。`Intermediate/` 下的 Bundle 为构建产物。

## 查看 Unreal 自动化测试结果

每项独立框架功能均须按[审查规范](CodingStandards/BuildAndReview.zh-CN.md#自动化测试)配备面向行为的自动化测试，但不设数值化覆盖率门槛。对于已完成的 Unreal 自动化运行，可使用 `Scripts/check_automation_report.py` 校验导出报告、日志及编辑器退出码：

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## 发布验证

Release Validation 使用[引擎锁](../.github/engine-lock.json)固定完整引擎的上游版本。

从受授权的私有仓库以明确的 Release Candidate SHA 调用 [Release Validation](../.github/workflows/release-validation.yml)。Win64 x64 的 `umf-release` runner 通过 `UMF_FULL_UE` 提供可执行 Cook 和 Package 的完整 UE。可以使用既有的授权工作站，同一入口也可在该工作站直接运行。

```text
python -m Test.SharedRuntime.release_validation --engine <完整 UE> --work <新目录> --commit <完整 SHA>
```

发布流程按原有包名暂存集成资源，构建 Monolithic Game，执行 MDT plan/build/inspect，构建独立作者工程，并完成宿主和 Mod 的 Cook、Stage 与 Package。各工程只启用 UMF 自有插件。MDT 使用禁用引擎默认插件的独立 UBT Editor 目标。基础发行版本来自候选提交，摘要来自实际 Cook 输出的 `AssetRegistry.bin`。暂存游戏加载两个独立 C ABI 扩展，验证 Hook 回调、API 权限、资源替换与卸载，随后运行 200 项性能基准。已有 Modular Game 产物的完整引擎可加 `--game-target UnrealModFrameworkDevModular`，使用同一套示例游戏验证。报告保留在授权工作区。 [SharedRuntime 流程](../Test/SharedRuntime/README.md)

## 运行调用性能基准

使用 `python Test/Performance/run_benchmark.py --engine <完整 UE> --report <结果.md>` 构建、Cook 并测量独立性能基准。脚本为目标桌面 Game 平台和架构编译原生扩展，并覆盖宿主和打包 Mod 的 200 项 API 用例。Mod C++ 栏包含 C ABI Hook 往返和宿主 UE 调用的耗时。Mod 蓝图栏使用从同一个归档中加载的蓝图。

## 检查失败时

打开失败的 Actions Job 查看日志与摘要。请求协助排查时，请附上失败运行的链接及已尝试的排查步骤。

各工作流配置分别位于 [Development CI](../.github/workflows/development.yml)、[Quality](../.github/workflows/quality.yml)和 [API Pages](../.github/workflows/pages.yml)。
