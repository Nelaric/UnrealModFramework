<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework

[![版本](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.zh-CN.md)

UnrealModFramework 让 Unreal Engine 游戏向模组开放 C++ 与蓝图 API、Hook 和 Event。游戏接入 `ModRuntime` 后即可校验并挂载 `.mod` 包，在 World 内运行模组扩展；模组作者则在独立的 UE 工程中使用目标游戏生成的 SDK 与独立构建的制作插件 `ModDevelopmentSDKEditor`。项目开发基线为 UE 5.6。

## 快速开始

克隆仓库并进入根目录，安装构建主机对应的 C++20 工具链后运行 Setup。

```sh
git clone https://github.com/Nelaric/UnrealModFramework.git
cd UnrealModFramework
```

| 构建主机 | C++ 工具链 | Setup |
| --- | --- | --- |
| Windows | MSVC 与 Windows SDK | `.\Setup.bat` |
| Linux | GNU `g++` | `sh Setup.sh` |
| macOS | GNU `g++`（运行 Setup 与构建宿主时通过 `CXX` 指定） | `sh Setup.sh` |

Setup 会复用缓存或 `PATH` 中的 CMake 3.20 及以上版本；若未找到，则下载锁定版本并校验 SHA-256。构建生成器优先选用 Ninja，未安装时在 Windows 上回退至 NMake，在 Linux 和 macOS 上回退至 Make。[CMake 包清单](Setup/CMakePackages.txt)

对于 x64 与 ARM64 的 Windows、Linux（glibc）和 macOS 主机，Setup 还会下载并校验 LLVM 21.1.8 的 `libclang` 及配套 Clang 资源头文件，缓存至 `.tools/libclang/` 供离线复用，并写入供 UHT 自动读取的相对路径文件。首次下载需要联网。[libclang 包清单](UnrealModFramework/Plugins/ModRuntime/Tools/LibClangPackages.txt)

Setup 会将四个独立工具的 Release 可执行文件构建至 `UnrealModFramework/Plugins/ModRuntime/Tools/build/<工具名>/`。

### 构建开发宿主

安装 UE 5.6 与目标平台 SDK。在仓库根目录使用 ModBuildTool（MBT）构建启用自动 C++ Hook 分发的 Win64 x64 Development Editor 示例。

```powershell
$mbt = "./UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe"
& $mbt build-ue-target --engine "<UE 安装目录>" --project "UnrealModFramework/UnrealModFrameworkDev.uproject" `
  --target UnrealModFrameworkDevEditor --platform Win64 --configuration Development --architecture x64 --mode normal
```

如需构建 Development Game，请将 `--target` 改为 `UnrealModFrameworkDev`。当生成导出快照需要 Editor 二进制时，构建流程会自动引导已配置的 Editor Target。在 Editor 中验证时，请在构建完成后打开开发宿主工程。[开发宿主工程](UnrealModFramework/UnrealModFrameworkDev.uproject) · [Linux 示例与目标架构](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)

在 Win64 x64 MSVC 上，MBT 将生成的宿主代码编译为静态库；其他工具链则由 UBT 在所属模块内直接编译。CircleCI 使用 UE 5.6.1 构建 Linux x64 Development Game 和 Editor，并在 Editor Job 中运行 `ChangeHealthDispatch` 自动化测试。[构建与测试范围](Docs/DevelopmentWorkflow.zh-CN.md#linux-编译的范围)

## 从开发分支获取 ModRuntime

切换到目标分支并运行 Setup，再从仓库根目录执行导出脚本。

| 构建主机 | 导出命令 |
| --- | --- |
| Windows | `.\Scripts\GetDevelopmentPlugin.bat` |
| Linux 和 macOS | `sh Scripts/GetDevelopmentPlugin.sh` |

脚本会将插件源码、四个工具的可执行文件、`libclang` 及其资源头文件导出至 `Dist/ModRuntime`，成功后原子替换上一次导出结果。请在实际运行这些工具的操作系统与架构上执行 Setup 和导出脚本。将 `Dist/ModRuntime` 复制到 UE C++ 宿主工程的 `Plugins/ModRuntime`，启用插件后构建工程。请确保 `PATH` 中包含 CMake 3.20 或更高版本，以便 `ExportMetadata.ConfigureTarget` 在复制后的插件内重新构建原生工具。

## 接入游戏侧导出

1. 添加 `ExportMetadata` 模块依赖，在宿主的 Game 和 Editor Target 规则中调用 `ExportMetadata.ConfigureTarget(this)`，并在每个导出模块的构建规则中调用 `ExportMetadata.ConfigureHost(this, Target)`。Win64 x64 MSVC 还需要配套的 `<模块名>Host` External 模块。[宿主模块与链接配置](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#独立宿主编译与链接)
2. 在 `Config/ModHeaderTool.json` 中将 `mode` 设为 `generate`，填写游戏 ID、导出模块、类型身份与 `metadataEditorTarget`。如需导出蓝图，请在 `blueprintExports` 中为每个已保存资产指定对象路径、稳定的 `typeId` 及所属模块。[开发宿主配置示例](UnrealModFramework/Config/ModHeaderTool.json)
3. 在 C++ 中或通过蓝图编辑器的 **蓝图Mod导出** 面板声明导出；使用自动 C++ Hook 时，请通过 MBT 构建目标。[宿主声明与签名范围](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)

### 导出 C++ Hook

在具备 World 类型身份的成员函数声明前标注 `MOD_EXPORT_HOOK()`，并在对应的 `.cpp` 文件中提供类外定义。示例同时通过 `BlueprintCallable` 向蓝图暴露该成员。

```cpp
MOD_EXPORT_HOOK()
UFUNCTION(BlueprintCallable, Category = "Character|Health")
EXPORTTEST_API float ChangeHealth(float Delta);
```

在 `ExportTest` 中，该声明根据配置的 World 类型 ID 与成员名生成 `hook/game.character/change-health`，C++ 与蓝图调用共用同一分发入口。具备相应授权的模组可在 `Pre` 阶段修改入参或取消调用，在 `On` 阶段替换原函数实现，并在 `Post` 阶段观察返回结果。

在宏中传入 `NoPre`、`NoOn` 或 `NoPost` 可关闭对应阶段。例如，`MOD_EXPORT_HOOK(NoOn)` 会在 `Pre` 放行后直接执行原函数；若同时传入全部三个标记，构建会直接编译原函数并从 SDK 中移除该 Hook。重命名成员时，可通过 `CompatibleNames = ("ChangeHealth")` 列出曾用名——只要函数签名与阶段策略保持兼容，旧 ID 便会保留原有契约和授权。[Hook 契约与载荷](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#world-扩展产物)

### API 与 Event 的导出字段

字段名与候选值均区分大小写。[ModExportMetadataSchema.h](UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public/ModExportMetadataSchema.h)

| 宏 | 必填字段 | 可选字段 |
| --- | --- | --- |
| API 与 Event 共用 | — | `ExportVersion`（默认 `"1.0.0"`）、`DisplayName`、`Description` |
| `MOD_EXPORT_API` | `Scope`、`Thread`、`FailureRule` | `Permissions`（默认空列表）、`FullView`、`CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`、`PublishRule` | `PayloadType`、`ExportId`、`Permission`、`PublishPermission`、`UnsubscribeRule`、`Authority`（默认 `Server`）、`HostEndpoint`、`HostCommitPayload` |

MHT 会按所选构建目标校验活动声明。`HostCommitPayload` 为当前生成器的保留字段，使用时须同时提供 `HostEndpoint`。World 作用域的 Hook 和 Event 均在游戏线程执行。[生成 Event 发布函数](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#生成-c-event-发布函数)

## 构建并加载模组

1. 在 SDK 构建请求中引用 Win64 x64 MSVC 独立宿主构建产出的生成清单和生产者上下文，运行 `ModSDKTool build --request SdkBuildRequest.json` 发布 SDK 包，再将游戏 SDK 与制作插件安装到作者工程的 `Plugins/` 目录。示例工程将 `UMFDevSDK` 和 `ModDevelopmentSDKEditor` 安装在 `ExampleMods/TestMod/Plugins/` 下。[SDK 请求与产物](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)
2. 在作者工程的关卡编辑器工具栏中选择 **Mod → 打包 Mod…**，填写目标游戏的基础发布信息及匹配的 Cook 设置，再选择打包已启用的内容插件或当前 C++ 工程。打包 C++ 工程需要 Pak 后端及匹配的 Win64 x64 模块化宿主回执；测试打包后的示例游戏时，请使用支持模块化 Game Target 的 UE 源码版引擎构建 `UnrealModFrameworkDevModular`。[打包与 PIE 设置](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md)
3. 在宿主的 `Config/DefaultGame.ini` 中配置 `[ModLoader]` 的发布身份、容器后端及所需的原生代码权限，然后将 `.mod` 包放入游戏可执行文件同级的 `Mods/` 目录；若在 Windows PIE 中测试，请将与 Editor 匹配的包放入宿主工程的 `Binaries/Win64/Mods/`。[加载器配置](Docs/CodingStandards/Modules.zh-CN.md#模组加载器)

宿主会在开局前完成 World 扩展准备。`ModExtensions` 游戏模式演示了包含内容槽与 World Patch 锚点的准备流程、多人玩法计划核对以及单机存档迁移。Shipping 构建和更多工具链组合仍需按目标分别验收。[游戏端实现与里程碑](Docs/UnrealModFramework_游戏端详细设计.md#当前实现与限制)

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) | 开发宿主工程与 `ExportTest` 接入样例 |
| [ModRuntime](UnrealModFramework/Plugins/ModRuntime/) | 游戏运行时、Editor 导出模块、UHT 适配层与独立工具 |
| [TestMod](ExampleMods/TestMod/TestMod.uproject) | 独立作者工程，包含角色预览及原生与蓝图生命周期回调 |
| [技术设计](Docs/UnrealModFramework_设计文档.md) | 总体架构与分目标发布要求 |
| [调用性能基准](Test/Performance/ue-call-benchmark.md) | 独立 UE 调用测量结果和[复现步骤](Docs/DevelopmentWorkflow.zh-CN.md#运行调用性能基准) |

`Binaries`、`Intermediate`、`Saved` 和 `Dist` 目录存放生成产物。

## 独立工具文档

| 工具 | 职责 |
| --- | --- |
| [ModHeaderTool（MHT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md) | 扫描声明，生成宿主连接代码、公开契约与原生 ABI 产物 |
| [ModBuildTool（MBT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModBuildTool/README.zh-CN.md) | 驱动 UE 构建使用生成的 Hook 源码，校验静态库与最终链接产物 |
| [ModSourceTool（MST）](UnrealModFramework/Plugins/ModRuntime/Tools/ModSourceTool/src/Cli/CommandLine.cpp) | 改写 Hook 源码副本、分析失败返回与异常，并生成宿主链接头 |
| [ModSDKTool（MDT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md) | 规划、构建与检查 SDK，并单独构建制作插件 |

## 参与贡献

请参照[贡献指南](CONTRIBUTING.zh-CN.md)、[Issue 表单](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)与 [PR 模板](.github/pull_request_template.md)准备主题集中的改动。[开发流程](Docs/DevelopmentWorkflow.zh-CN.md)说明了本地检查、可信 PR 编译以及文档发布方式。

## API 文档

站点由 Doxygen 生成，包含 API 入口页、公开头文件和编码规范。[UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/) · [仓库 API 入口](Docs/API/README.zh-CN.md)

设置 `API_PAGES_ENABLED=true` 后，推送到 `main` 会触发 API Pages 工作流发布。[发布配置](Docs/Hosting/README.zh-CN.md)

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## 许可证

除非另有说明，项目原创内容均采用 [MIT 许可证](LICENSE)。Unreal Engine 及第三方内容各自保留其许可条款，归属说明见 [NOTICE.md](NOTICE.md)。

## 致谢

感谢所有为 UnrealModFramework 做出贡献的开发者，感谢 Epic Games 提供 Unreal Engine，以及 CircleCI 提供 Linux 项目构建服务。
