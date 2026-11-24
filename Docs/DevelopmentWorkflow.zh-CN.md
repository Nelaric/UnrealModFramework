<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与 CI 流程

本文说明贡献检查、Linux 构建与文档发布流程。准备改动前，请先阅读[贡献指南](../CONTRIBUTING.zh-CN.md)。

## 从改动到 `main`

1. 在分支或 Fork 中完成主题集中的改动，并运行相关的[本地检查](CodingStandards/BuildAndReview.zh-CN.md#本地检查)。
2. 使用 [PR 模板](../.github/pull_request_template.md)向 `main` 发起 PR，并在适用时关联相关 Issue。
3. 修复所有检查失败并完成维护者审查后再合并。维护者应通过分支保护规则要求相关检查通过。
4. [启用 API Pages](Hosting/README.zh-CN.md) 后，合并到 `main` 会发布生成的 API 文档。

## PR 会运行哪些检查

[Quality 工作流](../.github/workflows/quality.yml)负责检查 PR 与提交命名、格式与文本规范以及 API 文档。另有独立的 Linux Job 构建 MST，并使用 GCC 和 Clang 编译数据库验证跨文件异常分析。工具版本固定在工作流文件及 `.config/dotnet-tools.json` 中。

完成 [CircleCI 接入](CircleCI.zh-CN.md)后，可通过设置仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED=true` 启用[可信 PR 编译工作流](../.github/workflows/fork-pr-linux.yml)。在该变量启用前，其各 Job 会被跳过；现有的 CircleCI push 流水线保持独立运行。

PR 工作流会先验证提交并触发 CircleCI，随后并行展示 Game 与 Editor 两个 Job 并汇总结果。两个目标分别上报 `ci/linux-game-build` 和 `ci/linux-editor-build` 状态，`ci/fork-pr-linux-build` 负责汇总两者。汇总摘要包含 CircleCI 日志链接，Actions Job 会镜像输出检出与编译日志，同时排除认证相关输出。贡献者无需 CircleCI 账号。

对于 Fork PR，如涉及修改 CI 工作流、自动化脚本、Unreal 描述文件与构建脚本、CMake 文件或可执行辅助脚本，须由维护者在源仓库分支中处理。策略会同时检查重命名文件的新旧路径。源仓库分支的 PR 可以修改这些文件并继续使用可信编译流程。原生模组 ABI 变更同样需要维护者审查。

## Linux 编译的范围

push 配置与可信 PR 配置均使用 Unreal Engine 5.6.1，以 Linux Development 配置构建 `UnrealModFramework/UnrealModFrameworkDev.uproject`：

| CircleCI Job | 工作内容 |
| --- | --- |
| `build_game_linux` | 编译 `UnrealModFrameworkDev` 及其已启用的运行时模块。 |
| `build_editor_linux` | 编译 `UnrealModFrameworkDevEditor` 及其支持的编辑器模块，再通过 `ModSDKTool build-editor` 构建并校验独立的 `ModDevelopmentSDKEditor` 插件。 |

各 Job 使用独立的可写暂存目录，拉取私有引擎镜像后，UE 容器在无网络环境中运行。MBT 的 `build-ue-target` 命令负责构建 Linux 目标，并在所属 UBT 模块中编译生成的宿主代码；Editor Job 还会运行 ChangeHealth 的 C++ 与蓝图用例。工具与输入准备步骤详见 [ModRuntime UHT 接入](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)。

仓库目前没有 Server 目标。这些 Job 仅验证编译与插件打包，不发布可下载包，也不运行 Unreal 自动化测试。其他平台及更新的支持引擎版本仍属于构建覆盖缺口。

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
