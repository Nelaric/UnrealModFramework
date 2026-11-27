<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](DevelopmentWorkflow.md) | 简体中文

# 开发与 CI 流程

本文介绍本地检查、PR 自动校验、CircleCI Linux 编译以及 API 文档站点的发布机制。如果是首次参与贡献，请先阅读[贡献指南](../CONTRIBUTING.zh-CN.md)。

## 从改动到 `main`

1. 在特性分支或个人 Fork 中完成聚焦单一主题的改动，并运行对应的[本地检查](CodingStandards/BuildAndReview.zh-CN.md#本地检查)。
2. 按 [PR 模板](../.github/pull_request_template.md)向 `main` 发起 PR，并关联相关 Issue（如有）。
3. 处理所有失败检查并完成维护者代码审查。仓库通过分支保护规则要求必选检查与审查均通过后方可合并。
4. 启用 [API Pages 发布](Hosting/README.zh-CN.md)后，合并至 `main` 的提交会自动构建并更新在线 API 文档。

## PR 会运行哪些检查

每个 PR 都会触发两条检查链路：

- **Quality 工作流（[quality.yml](../.github/workflows/quality.yml)）**：检查分支、提交与 PR 标题命名、代码格式、UTF-8/换行符规范以及 Doxygen API 文档；同时运行 CircleCI 交接脚本的单元测试、验证 COFF/ELF/Mach-O 自动 Hook 编译与链接的独立 Linux 用例，以及基于 GCC 和 Clang 编译数据库的 MST 跨文件异常分析。所用工具版本均锁定在工作流文件与 `.config/dotnet-tools.json` 中。
- **可信 Linux PR 编译（[fork-pr-linux.yml](../.github/workflows/fork-pr-linux.yml)）**：完成 [CircleCI 接入](CircleCI.zh-CN.md)并将仓库 Actions 变量 `CIRCLECI_PR_BUILDS_ENABLED` 设为 `true` 后生效；启用前相关 Job 会自动跳过。`main` 分支的常规 Push 构建仍由独立的 CircleCI 流水线执行。

触发可信编译后，工作流会先核验待测提交 SHA，再调用 CircleCI 并行运行 Game（`ci/linux-game-build`）和 Editor（`ci/linux-editor-build`）两个构建目标，最后由 `ci/fork-pr-linux-build` 汇总状态。Actions 页面会同步镜像检出与编译日志（自动过滤认证信息），并在摘要中附上 CircleCI 原始日志链接，贡献者无需准备 CircleCI 账号。

对于来自 Fork 的 PR，一旦改动涉及 CI 工作流、自动化脚本、Unreal 描述文件或构建脚本、CMake 配置，以及可执行辅助脚本，交接策略就会拦截（文件重命名会同时检查新旧路径），改由维护者在源仓库分支中处理；源仓库内部创建的分支不受此限制。此外，原生模组 C ABI 的变更同样需要维护者审查。

## Linux 编译的范围

常规 Push 流水线与可信 PR 流水线均基于 **Unreal Engine 5.6.1**，为 `UnrealModFramework/UnrealModFrameworkDev.uproject` 编译 **Linux x64 Development** 目标：

| CircleCI Job | 执行内容 |
| --- | --- |
| `build_game_linux` | 编译 `UnrealModFrameworkDev` 及其已启用的运行时模块。 |
| `build_editor_linux` | 编译 `UnrealModFrameworkDevEditor`，运行覆盖 C++ 与蓝图 Hook 分发的自动化测试 `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch`，最后通过 `ModSDKTool build-editor` 独立构建并校验制作插件。 |

每个 Job 都在独立的可写暂存目录中构建，拉取私有引擎镜像后以完全断网模式（`--network none`）运行 UE 容器。构建时由 `ModBuildTool build-ue-target` 驱动 UBT 编译改写后的 Hook 源码副本，生成的宿主桥接代码在所属模块内直接编译，链接完成后再由 MBT 校验宿主二进制并写出 `host-link.json`。Editor Job 会在确认编辑器退出码正常且测试成功标记存在后，继续构建独立制作插件。Dedicated Server、Shipping 以及其他操作系统或引擎版本需另行验证（详见 [ModRuntime UHT 适配层说明](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)）。

每项独立的框架能力都须按[审查规范](CodingStandards/BuildAndReview.zh-CN.md#自动化测试)补充面向可观察行为的自动化测试，但不要求数值化的覆盖率指标。若已有现成的 Unreal 自动化测试输出，可通过以下脚本离线核验报告、日志与退出码：

```text
python Scripts/check_automation_report.py <report/index.json> <automation.log> <editor-exit-code>
```

## 运行共享 Runtime 打包回归

如果改动涉及 SDK Runtime 归属关系、打包依赖或调用方（Caller）令牌生命周期，请运行 [Test/SharedRuntime](../Test/SharedRuntime/README.zh-CN.md) 提供的 Win64 x64 打包回归测试。该用例基于模块化 Development Game、Pak 基础发行版以及两个独立打包的 Mod 工程，逐项核对 Stage 后的 DLL、游戏最终链接记录与已发布 SDK，并验证在运行期卸载其中一个 Mod 后，其调用权限会被立即撤销，而另一个 Mod 仍能正常调用共享 Runtime。

## 运行调用性能基准

在仓库根目录运行[基准测试脚本](../Test/Performance/run_benchmark.py)，传入 UE 5.6 安装路径，并通过 `--report` 指定单独的输出文件，以免覆盖仓库中已提交的基线报告：

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

独立基准工程共测试 200 个常见 UE API，每条可用调用路径采样 1,000 次。在 Win64 x64 下会同时测量宿主原生 C++、宿主原生蓝图，以及已挂载 `BenchmarkMod` 内的 Mod C++ 与 Mod 蓝图四条路径（需使用能产出模块化 Development Game 的引擎源码构建，或通过 `--modular-runtime` 提供匹配的模块化运行时）；其他受支持平台仅测量两条宿主路径，未运行的路径在表中记为 `未测试`。顶部汇总值为各列已测 API 均值的算术平均，结果与测试机器环境相关，可与[仓库基线数据](../Test/Performance/ue-call-benchmark.md)对照参考。

## 排查检查失败

检查未通过时，请先点开失败的 GitHub Actions Job 阅读摘要，并顺着摘要中的链接查看完整的 CircleCI 编译日志（常规分支推送记录也可在 [CircleCI 项目页](https://app.circleci.com/pipelines/github/Nelaric/UnrealModFramework)查看）。若可信 PR 编译意外显示跳过，维护者可对照 [CircleCI Linux 构建说明](CircleCI.zh-CN.md)核查开关变量、Webhook 触发器与密钥配置。如需请求维护者协助排查，请附上失败的 Actions 链接以及你已排除的原因。

相关配置文件：[quality.yml](../.github/workflows/quality.yml) · [fork-pr-linux.yml](../.github/workflows/fork-pr-linux.yml) · [pages.yml](../.github/workflows/pages.yml) · [.circleci/config.yml](../.circleci/config.yml) · [.circleci/pr-build.yml](../.circleci/pr-build.yml)
