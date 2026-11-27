<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework

[![版本](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.zh-CN.md)

UnrealModFramework 让 Unreal Engine 游戏能够通过编译期生成的契约，向 Mod 安全开放 C++ 与蓝图 API、Hook 和 Event。宿主游戏接入 `ModRuntime` 后即可校验并挂载 `.mod` 包，在 World 作用域内运行模组扩展，而无需暴露游戏私有模块；Mod 作者则在独立的 UE 工程中，搭配目标游戏生成的 SDK 与独立构建的 `ModDevelopmentSDKEditor` 插件进行开发与打包。

当前开发基线为 **Unreal Engine 5.6**。

## 快速开始

克隆仓库并进入根目录，准备好构建主机对应的 C++20 工具链后运行 Setup 脚本：

```sh
git clone https://github.com/Nelaric/UnrealModFramework.git
cd UnrealModFramework
```

| 构建主机 | C++20 工具链 | Setup 命令 |
| --- | --- | --- |
| Windows | MSVC 与 Windows SDK | `.\Setup.bat` |
| Linux | GNU `g++` | `sh Setup.sh` |
| macOS | GNU `g++`（运行 Setup 与构建宿主时均通过 `CXX` 指定） | `sh Setup.sh` |

Setup 会优先复用本地缓存或 `PATH` 中 3.20 及以上版本的 CMake；若未检测到，则自动下载锁定版本并校验 SHA-256（见 [CMake 包清单](Setup/CMakePackages.txt)）。构建生成器优先使用 Ninja，未安装时在 Windows 上回退至 NMake，在 Linux 和 macOS 上回退至 Make。

在 x64 与 ARM64 架构的 Windows、Linux（glibc）和 macOS 主机上，Setup 还会下载并校验 LLVM 21.1.8 的 `libclang` 与配套 Clang 资源头文件（见 [libclang 包清单](UnrealModFramework/Plugins/ModRuntime/Tools/LibClangPackages.txt)）。首次运行需要联网；下载完成后相关文件会缓存至 `.tools/libclang/` 供离线复用，同时生成供 UHT 自动读取的相对路径文件。

执行完毕后，四个独立工具的 Release 可执行文件将输出至 `UnrealModFramework/Plugins/ModRuntime/Tools/build/<工具名>/`。

### 构建开发宿主

安装好 UE 5.6 与目标平台 SDK 后，在仓库根目录通过 ModBuildTool（MBT）构建带自动 C++ Hook 分发的 Win64 x64 Development Editor 示例工程：

```powershell
$mbt = "./UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe"
& $mbt build-ue-target --engine "<UE 安装目录>" --project "UnrealModFramework/UnrealModFrameworkDev.uproject" `
  --target UnrealModFrameworkDevEditor --platform Win64 --configuration Development --architecture x64 --mode normal
```

如需构建 Development Game，将 `--target` 改为 `UnrealModFrameworkDev` 即可。如果导出快照所需但尚未编译的 Editor 二进制缺失，构建流程会自动引导已配置的 Editor Target。构建完成后，直接打开 [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) 即可在编辑器中体验。Linux 构建命令及受支持的目标架构见[自动 Hook 构建](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)。

编译期间，UBT 会在各声明所属模块内部直接编译生成的宿主桥接代码，并在 `Intermediate/ModTools/ModHeaderTool/Generated/` 下生成 v4 清单。对于受支持的桌面目标，MBT 会在链接完成后校验最终宿主二进制，并在 `Intermediate/ModTools/ModHeaderTool/HostTargets/` 对应变体目录写出 `host-link.json`。CircleCI 持续验证 UE 5.6.1 Linux x64 Development Game 与 Editor 构建、Editor Hook 分发测试以及独立制作插件构建（详见 [Linux 编译的范围](Docs/DevelopmentWorkflow.zh-CN.md#linux-编译的范围)）。

## 从开发分支获取 ModRuntime

若要将某个开发分支的 `ModRuntime` 集成到你自己的项目中，请先切到目标分支运行 Setup，再在仓库根目录执行导出脚本：

| 构建主机 | 导出命令 |
| --- | --- |
| Windows | `.\Scripts\GetDevelopmentPlugin.bat` |
| Linux 和 macOS | `sh Scripts/GetDevelopmentPlugin.sh` |

脚本会将插件源码、四个工具的可执行文件、`libclang` 及其资源头文件完整导出到 `Dist/ModRuntime`，并在成功后原子替换上一次导出目录。请务必在后续实际运行这些工具的操作系统与架构上执行 Setup 和导出脚本。

导出后，将 `Dist/ModRuntime` 复制到目标 UE C++ 宿主工程的 `Plugins/ModRuntime`，启用插件即可编译工程。如果构建 Target 需要在预构建阶段重新编译原生工具，请确保 `PATH` 中有 CMake 3.20+；你也可以让 Target 直接使用预先准备好的工具二进制，配置方式见[工具发现与默认调用](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#工具发现与默认调用)。

## 接入游戏侧导出

1. **配置 Target 规则**：在每个参与导出的 Game 或 Editor `.Target.cs` 中调用一次 `ExportMetadata.ConfigureTarget(this)`。需要导出接口的模块在 `.Build.cs` 中按常规方式依赖 `ModAPI`，即可引入导出宏和宿主运行时接口（见[宿主编译与链接](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#宿主编译与链接)）。
2. **编写生成配置**：在 `Config/ModHeaderTool.json` 中将 `mode` 设为 `generate`，填写游戏 ID、类型身份映射文件与 `metadataEditorTarget`。若要导出蓝图接口，请在 `blueprintExports` 中列出已保存蓝图的资产路径、稳定的 `typeId` 以及归属的 Runtime 模块（参考[开发宿主配置示例](UnrealModFramework/Config/ModHeaderTool.json)）。
3. **选择扫描模块**：MHT 默认自动扫描工程 `Source/` 目录下的模块。如需纳入插件或排除特定模块，可在 `.uproject` 同级目录放置可选的 `<项目名>.modexport`——通过 `Plugins` 选择插件，通过顶层或插件条目内的 `ExcludeModules` 排除模块（见[模块选择](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#模块选择)）。
4. **声明并构建**：在 C++ 头文件中使用导出宏，或在蓝图编辑器的 **蓝图Mod导出** 面板中创建声明。启用自动 C++ Hook 时，请通过 MBT 构建目标（详见 [ModRuntime UHT 适配层说明](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)）。

### 导出 C++ Hook

在已配置 World 类型身份的成员函数声明前标注 `MOD_EXPORT_HOOK()`，并在 `.cpp` 文件中提供类外定义。该宏可与 `BlueprintCallable` 等标准 UE 反射说明符搭配使用：

```cpp
MOD_EXPORT_HOOK()
UFUNCTION(BlueprintCallable, Category = "Character|Health")
EXPORTTEST_API float ChangeHealth(float Delta);
```

在 `ExportTest` 示例中，这条声明会根据角色的 World 类型 ID 与函数名派生出 `hook/game.character/change-health`，C++ 直接调用与蓝图调用都会汇入同一个分发入口。获得相应授权的 Mod 可在 `Pre` 阶段修改入参或取消调用，在 `On` 阶段接管并替换原函数实现，在 `Post` 阶段读取返回结果。

向宏传入 `NoPre`、`NoOn` 或 `NoPost` 可按需关闭对应阶段。例如 `MOD_EXPORT_HOOK(NoOn)` 会在 `Pre` 放行后始终执行游戏原函数；若同时传入全部三个标记，则会直接编译原函数并从生成的 SDK 中移除该 Hook。后续如需重命名函数，可通过 `CompatibleNames = ("ChangeHealth")` 登记旧名称——只要函数签名与阶段策略保持兼容，旧 ID 的契约和授权就会完整保留。更多细节见 [World 扩展产物](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#world-扩展产物)。

### API 与 Event 的导出字段

宏字段名与可选取值均区分大小写（定义见 [ModExportMetadataSchema.h](UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public/ModExportMetadataSchema.h)）：

| 宏 | 必填字段 | 可选字段 |
| --- | --- | --- |
| 共用字段（API 与 Event） | — | `ExportVersion`（默认 `"1.0.0"`）、`DisplayName`、`Description` |
| `MOD_EXPORT_API` | `Scope`、`Thread`、`FailureRule` | `Permissions`（默认空列表）、`FullView`、`CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`、`PublishRule` | `PayloadType`、`ExportId`、`Permission`、`PublishPermission`、`UnsubscribeRule`、`Authority`（默认 `Server`）、`HostEndpoint`、`HostCommitPayload` |

MHT 会结合当前编译目标校验活动声明。`HostCommitPayload` 目前为生成器保留字段，指定时必须同时提供 `HostEndpoint`。所有 World 作用域的 Hook 与 Event 均在游戏线程（Game Thread）执行。C++ 事件发布辅助代码的生成方式见[生成 C++ Event 发布函数](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#生成-c-event-发布函数)。

## 构建并加载模组

1. **构建并安装 SDK**：先构建目标 Game 与 Editor，将两者的 v4 清单填入 MDT v3 请求文件，运行 `ModSDKTool build --request SdkBuildRequest.json`。随后将发布包中的 `Host/Plugins/<游戏 SDK>` 安装并启用到宿主工程，再构建并 Cook 游戏基础发行版；每个独立的 Mod 工程则安装发布包 `Plugins/` 下的两个插件（详见[构建样例 SDK](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md#构建样例-sdk)与[安装与验证](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md#安装与验证)）。
2. **打包 `.mod` 文件**：在 Mod 工程的关卡编辑器工具栏中点击 **Mod → 打包 Mod…**，填入目标游戏的基础发行版路径及一致的 Cook 参数，选择打包某个已启用的内容插件或打包当前 C++ 工程。打包 C++ 工程需要使用 Pak 后端以及匹配的 Win64 x64 模块化宿主 `.target` 回执；测试本仓库的示例宿主时，请使用支持模块化 Game Target 的源码版引擎构建 `UnrealModFrameworkDevModular`（详见[模组开发 SDK 编辑器](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md)）。
3. **配置加载并运行**：在宿主的 `Config/DefaultGame.ini` 中为 `[ModLoader]` 配置发行身份、容器后端及所需的原生代码开关（见[模组加载器配置](Docs/CodingStandards/Modules.zh-CN.md#模组加载器)）。将生成的 `.mod` 放入游戏可执行文件同级的 `Mods/` 目录；若在 Windows PIE 下调试，则将面向 Editor 构建的 `.mod` 放入宿主工程的 `Binaries/Win64/Mods/`。

宿主会在正式进入对局前完成 World 扩展准备。仓库内的 `ModExtensions` 游戏模式完整演示了内容槽与 World Patch 锚点的准备流程、多人联机方案核对以及单机存档迁移（见[启动编排与失败处理](Docs/UnrealModFramework_设计文档.md#6-启动编排与失败处理)）。Shipping 构建及更多平台/工具链组合仍需按具体目标逐一验收。

## 仓库结构

| 路径 | 说明 |
| --- | --- |
| [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) | 开发宿主工程及 `ExportTest` 接入示例 |
| [ModRuntime](UnrealModFramework/Plugins/ModRuntime/) | 游戏侧运行时、Editor 导出模块、UHT 适配层与四个独立命令行工具 |
| [TestMod](ExampleMods/TestMod/TestMod.uproject) | 独立的 Mod 示例工程，包含角色预览以及 C++ 与蓝图生命周期回调 |
| [技术设计文档](Docs/UnrealModFramework_设计文档.md) | 框架总体架构、包协议设计与分目标验收标准 |
| [共享 Runtime 回归](Test/SharedRuntime/README.zh-CN.md) | 在 Stage 后的 Win64 游戏中验证两个独立打包的 Mod 共用同一套 SDK Runtime |
| [调用性能基准](Test/Performance/ue-call-benchmark.md) | 独立工程下的 UE 调用耗时对比数据及[复现步骤](Docs/DevelopmentWorkflow.zh-CN.md#运行调用性能基准) |

`Binaries`、`Intermediate`、`Saved` 和 `Dist` 为构建与导出产物目录，不纳入版本控制。

## 独立工具文档

| 工具 | 职责 |
| --- | --- |
| [ModHeaderTool（MHT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md) | 解析导出声明，生成宿主桥接代码、公开契约与原生 C ABI 头文件 |
| [ModBuildTool（MBT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModBuildTool/README.zh-CN.md) | 驱动 UE 编译改写后的 Hook 源码副本，并校验桌面目标的最终链接结果 |
| [ModSourceTool（MST）](UnrealModFramework/Plugins/ModRuntime/Tools/ModSourceTool/README.zh-CN.md) | 改写类外 Hook 函数定义，并静态分析 API 失败返回与异常调用链 |
| [ModSDKTool（MDT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md) | 规划、构建与校验游戏 SDK 发布包，以及独立构建 Mod 制作插件 |

## 参与贡献

准备提交改动前，请参阅[贡献指南](CONTRIBUTING.zh-CN.md)，按需选用 [Issue 表单](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)和 [PR 模板](.github/pull_request_template.md)，保持每次改动聚焦单一主题。本地检查命令、可信 PR 编译与文档发布机制详见[开发与 CI 流程](Docs/DevelopmentWorkflow.zh-CN.md)。

## API 文档

在线文档站点由 Doxygen 构建，汇集了 API 入口说明、公开头文件参考与项目编码规范：

- **在线站点**：[UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/)
- **仓库入口**：[Docs/API/README.zh-CN.md](Docs/API/README.zh-CN.md)

当仓库变量 `API_PAGES_ENABLED=true` 时，合入 `main` 分支的提交会自动触发 API Pages 工作流构建并发布站点（配置细节见[组织文档托管](Docs/Hosting/README.zh-CN.md)）。

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## 许可证

除另有说明外，本项目原创内容采用 [MIT 许可证](LICENSE)授权。Unreal Engine 及第三方组件各自保留原有许可条款，详见 [NOTICE.md](NOTICE.md)。

## 致谢

感谢每一位向 UnrealModFramework 贡献代码与建议的开发者，感谢 Epic Games 提供 Unreal Engine，并感谢 CircleCI 提供 Linux 构建支持。
