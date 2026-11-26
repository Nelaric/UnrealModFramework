<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与 CI 流程

本文说明贡献检查、桌面构建与文档发布流程。准备改动前，请先阅读[贡献指南](../CONTRIBUTING.zh-CN.md)。

## 从改动到 `main`

1. 在分支或 Fork 中完成主题集中的改动，并运行相关的[本地检查](CodingStandards/BuildAndReview.zh-CN.md#本地检查)。
2. 使用 [PR 模板](../.github/pull_request_template.md)向 `main` 发起 PR，并在适用时关联相关 Issue。
3. 修复所有检查失败并完成维护者审查后再合并。维护者应通过分支保护规则要求相关检查通过。
4. [启用 API Pages](Hosting/README.zh-CN.md) 后，合并到 `main` 会发布生成的 API 文档。

## PR 会运行哪些检查

[Quality 工作流](../.github/workflows/quality.yml)检查命名、文本、格式和 API 文档。Linux 用例还验证 COFF、ELF、Mach-O 的自动 Hook 编译链接，以及 GCC 和 Clang 下的 MST 跨文件源码分析。

## 桌面构建范围

[桌面 CI](../.github/workflows/desktop.yml) 在向 `main` 提交 PR、推送到 `main` 或手动触发时运行。以下六种 GitHub 官方托管环境各运行独立的 Game 和 Editor 作业，均使用 UE 5.6.1 Development。

| 系统 | x64 runner | ARM64 runner |
| --- | --- | --- |
| Windows | `windows-2022` | `windows-11-arm` |
| macOS | `macos-15-intel` | `macos-15` |
| Linux | `ubuntu-24.04` | `ubuntu-24.04-arm` |

每项先编译四个原生 Mod 工具，验证可执行文件架构并测试自动化报告检查脚本，再由 MBT 编译对应目标。Game 作业烹饪、暂存并启动打包游戏；Editor 作业启动编辑器，另用 MDT 构建并校验独立制作插件。两种目标都验证生命值变化、C++ Hook 的 Pre、On、Post 阶段。Editor 还验证已编译蓝图的调用。作业保留宿主链接凭据，并要求本次 Unreal 报告中两个指定用例都通过。[CI 脚本](../Scripts/run_desktop_ci.py)

每个作业使用自己的目标包，仓库变量 `UE_PACKAGE_RECIPE` 固定到制包仓库的完整提交 SHA。`Nelaric/UnrealEngine-CI` 从固定的 Epic 上游输入制包，按目标裁剪并记录每个文件，再用另一台原生 runner 校验解压结果。该 runner 还会编译和烹饪一个独立 C++ 工程，其反射插件依赖 GameplayAbilities。发布记录包含各文件的保留理由、摘要和裁剪前后大小。Game 包保留代码工程烹饪所需的 Editor 命令行程序及其依赖。

在框架仓库设置变量 `UE_PACKAGES_APP_ID` 和 Actions 密钥 `UE_PACKAGES_APP_PRIVATE_KEY`。组织的 GitHub App 只需安装到私有制包仓库，并授予 Contents 只读权限。每个作业使用短期安装令牌下载对应的 Release。安装器核对目标、架构、验证记录、分卷及文件清单。[安装脚本](../Scripts/install_ci_engine.py)

GitHub 不向 Fork PR 提供仓库密钥。公共质量检查和原生工具编译可在 Fork PR 中运行。需要验证引擎时，由维护者审查后在源仓库分支上运行相应版本。缺少引擎配置会使对应作业失败。汇总检查 `Desktop build and automation` 要求 12 项全部通过。

矩阵通过后，将 `main` 规则集的三个旧检查（`ci/linux-game-build`、`ci/linux-editor-build`、`ci/fork-pr-linux-build`）替换为 `Desktop build and automation`，保留质量检查。在 CircleCI 停用旧项目的推送触发器，再移除 GitHub Actions 中的旧 webhook 密钥和启用变量。

## 运行调用性能基准

在仓库根目录运行[基准脚本](../Test/Performance/run_benchmark.py)，并指定 UE 5.6 安装目录与报告输出路径。通过 `--report` 显式指定路径可保留仓库中已提交的测量结果，便于后续比对。

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

独立基准工程包含 200 项用例，每条可用路径采样 1,000 次。Win64 x64 会测量宿主 C++、宿主蓝图，以及已挂载 `BenchmarkMod` 中的 C++ 与蓝图调用路径。请使用能够构建模块化 Development Game 的引擎源码版，或通过 `--modular-runtime` 传入匹配的模块化运行时；其他受支持的平台仅运行宿主侧路径。不可用路径在报告中标为 `未测试`。汇总值为每列可用用例均值的平均，反映当前测试机器与测试场景的表现；详见[仓库中的测量结果](../Test/Performance/ue-call-benchmark.md)。

## 检查失败时

打开失败的 GitHub Actions 作业，查看第一个失败步骤。Unreal 启动后，作业会上传已生成的链接凭据、自动化日志和报告索引。请求协助时可附上运行链接及失败目标。
