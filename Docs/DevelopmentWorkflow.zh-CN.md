<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与发布验证

日常开发使用 UE 5.6.1 Development SDK。SDK 来源为 UMFCI 独立验收后发布的私有不可变发行版。[引擎锁](../.github/engine-lock.json)固定安装器提交、UE 上游提交、目标配置、制包版本、Manifest SHA-256、.NET SDK 和平台工具链。发行字段为空的目标会明确失败。安装器以流式方式读取归档，逐文件校验，并检查 5 GB 体积上限。

| Editor 生产方 | Game 消费方 |
| --- | --- |
| Windows x86_64 | Windows x86_64、ARM64 |
| Linux x86_64 | Linux x86_64、ARM64 |
| macOS x86_64 | macOS x86_64 |
| macOS ARM64 | macOS ARM64 |

四个 Editor Job 各自调度。一组 Editor 成功后，对应的 Game Job 即可启动。Development Game 通过 UBT 编译和链接 Monolithic 目标并校验 Host Link Receipt。Editor Job 构建正常目标，在无界面环境下运行 `Nelaric.*`，并检查报告中的发现和执行情况。日志记录 SDK 安装、原生工具、元数据、反射与生成、编译链接和自动化阶段耗时。完整矩阵的冷构建关键路径目标为 5～10 分钟。

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

## 合并检查

Fork PR 在只读权限下运行 [Development](../.github/workflows/development.yml)。`Development public` 汇总命名、文本和格式、API 文档、四个原生工具及 COFF、ELF 和 Mach-O Hook 链接用例。Pages 仍独立发布。 [API Pages](../.github/workflows/pages.yml)

维护者审查准确的 PR head 后，从受授权的**私有**仓库手动发起引擎验证。审查该提交的构建脚本、依赖和 UE 源码后再调度。私有调用方配置零付费预算，设置 `UMF_ZERO_SPEND=true`。`UMFCI_READ_TOKEN` 只需读取私有 SDK 发行和安装器仓库，独立的 `UMF_STATUS_TOKEN` 只需对 UnrealModFramework 写入提交检查状态。只有最终汇总步骤取得状态令牌。

私有调用方使用固定并已审查的工作流提交。

```yaml
name: Reviewed Development
on:
  workflow_dispatch:
    inputs:
      pull_request: { type: number, required: true }
      commit: { type: string, required: true }
jobs:
  development:
    uses: Nelaric/UnrealModFramework/.github/workflows/development.yml@<reviewed-workflow-commit>
    with:
      pull_request: ${{ inputs.pull_request }}
      commit: ${{ inputs.commit }}
    secrets:
      UMFCI_READ_TOKEN: ${{ secrets.UMFCI_READ_TOKEN }}
      UMF_STATUS_TOKEN: ${{ secrets.UMF_STATUS_TOKEN }}
```

工作流核对 SHA 与当前打开的 PR head，并调度标准 GitHub-hosted runner。完整运行会向该 SHA 上报 `ci/development`。Bundle 只通过私有调用仓库中短期保留的 Artifact 传递。引擎文件、构建产物、含受限 UE 数据的日志与缓存留在授权存储中。SDK 缺失、免费运行资源不可用或 Job 未完成时，聚合检查无法成功。

首次在授权环境完整通过矩阵且公开汇总可用后，应用[版本化 Ruleset](../.github/rulesets/protect-main.json)。在此之前保留线上必需检查。新检查接管准入后，在服务端清理旧 CircleCI 触发器和 webhook。

## 发布验证

从受授权的私有仓库以明确的 Release Candidate SHA 调用 [Release Validation](../.github/workflows/release-validation.yml)。Win64 x64 的 `umf-release` runner 通过 `UMF_FULL_UE` 提供可执行 Cook 和 Package 的完整 UE。可以使用既有的授权工作站，同一入口也可在该工作站直接运行。

```text
python -m Test.SharedRuntime.release_validation --engine <完整 UE> --work <新目录> --commit <完整 SHA>
```

发布流程按原有包名暂存集成资源，构建 Monolithic Game，执行 MDT plan/build/inspect，构建独立作者工程，并完成宿主和 Mod 的 Cook、Stage 与 Package。各工程只启用 UMF 自有插件。MDT 使用禁用引擎默认插件的独立 UBT Editor 目标。基础发行版本来自候选提交，摘要来自实际 Cook 输出的 `AssetRegistry.bin`。暂存游戏加载两个独立 C ABI 扩展，验证 Hook 回调、API 权限、资源替换与卸载，随后运行 200 项性能基准。已有 Modular Game 产物的完整引擎可加 `--game-target UnrealModFrameworkDevModular`，使用同一套示例游戏验证。报告保留在授权工作区。 [SharedRuntime 流程](../Test/SharedRuntime/README.md)

## 运行调用性能基准

使用 `python Test/Performance/run_benchmark.py --engine <完整 UE> --report <结果.md>` 构建、Cook 并测量独立性能基准。脚本为目标桌面 Game 平台和架构编译原生扩展，并覆盖宿主和打包 Mod 的 200 项 API 用例。Mod C++ 栏包含 C ABI Hook 往返和宿主 UE 调用的耗时。Mod 蓝图栏使用从同一个归档中加载的蓝图。

## Phase 1 验证情况

当前锁定 Windows x86_64 Editor，以及 Windows x86_64、Windows ARM64、Linux x86_64、Linux ARM64 和 macOS ARM64 五个 Game SDK。其余四个目标等待正式发行。本地使用完整 UE 5.6.1 验证 Windows Editor，并使用正式的 Windows x86_64 最小 Game SDK 验证消费链路。四组关键路径、两个 ARM64 Game Job 以及完整发布验证仍需在各自环境中运行。
