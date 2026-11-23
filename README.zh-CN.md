<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework

[![版本](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.zh-CN.md)

UnrealModFramework 为 Unreal Engine 5.6 提供游戏侧模组运行时和独立构建的模组制作编辑器。开发宿主已接入 C++ 与蓝图契约生成、`.mod` 打包和加载，以及 World 作用域的模组扩展。

| 路径 | 内容 |
| --- | --- |
| [UnrealModFrameworkDev.uproject](UnrealModFramework/UnrealModFrameworkDev.uproject) | 开发宿主工程和 `ExportTest` 接入样例 |
| [ModRuntime](UnrealModFramework/Plugins/ModRuntime/) | 游戏运行时、编辑器导出、UHT 适配层和独立工具 |
| [TestMod](ExampleMods/TestMod/TestMod.uproject) | 独立作者工程，包含角色预览、原生模块和蓝图生命周期回调 |
| [技术设计](Docs/UnrealModFramework_设计文档.md) | 总体架构和分目标发布要求 |
| [调用性能基准](Test/Performance/ue-call-benchmark.md) | 独立 UE 调用测量结果，另见[复现步骤](Docs/DevelopmentWorkflow.zh-CN.md#运行调用性能基准) |

`Binaries`、`Intermediate`、`Saved` 和 `Dist` 保存生成产物。

## 快速开始

1. 按平台安装支持 C++20 的工具链。
2. 克隆仓库并进入根目录。

   ```sh
   git clone https://github.com/Nelaric/UnrealModFramework.git
   cd UnrealModFramework
   ```

3. 运行对应平台的 Setup。

   | 平台 | C++ 工具链 | 命令 |
   | --- | --- | --- |
   | Windows | MSVC 和 Windows SDK | `.\Setup.bat` |
   | Linux | GNU `g++` | `sh Setup.sh` |
   | macOS | 通过 `CXX` 指定已安装的 GNU `g++` | `sh Setup.sh` |

Setup 可使用 `PATH` 上的 CMake 3.20 或更高版本。新克隆的工作区缺少可用 CMake 时，Setup 会联网下载 Kitware 的固定版本并校验 SHA-256。[CMake 包清单](Setup/CMakePackages.txt)

Setup 的构建范围仅包括四个独立工具的 Release 版本，分别是 `ModHeaderTool`、`ModBuildTool`、`ModSourceTool` 和 `ModSDKTool`。可执行文件位于 `UnrealModFramework/Plugins/ModRuntime/Tools/build/<工具名>/`，可用 `--help` 检查工具能否启动。使用 Ninja 或 Unix Makefiles 时，可通过 `--jobs 8` 将并行任务数限制为 8。

### 构建开发宿主

准备 UE 5.6、目标平台 SDK 和构建主机的 libclang。以下命令构建当前桌面平台的 Development Editor。

```sh
python Scripts/build_hooks.py --engine "<UE 安装目录>"
```

首次构建需要引导编辑器时，工具链会自动准备。[平台选择与宿主编译](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)

### 导出 C++ Hook

在原函数声明上加一行 `MOD_EXPORT_HOOK()`，`.cpp` 里照常写业务逻辑。括号里填写任何字段都会报错。

```cpp
MOD_EXPORT_HOOK()
float ChangeHealth(float Delta);
```

生成器根据类型和函数名生成 Hook ID。C++ 和蓝图调用都会进入同一个入口。具有相应授权的 Mod 可以在 `Pre` 修改参数或取消调用，在 `On` 替换原函数，并在 `Post` 读取结果。

### API 与 Event 的导出字段

字段名和候选值区分大小写，定义见 [ModExportMetadataSchema.h](UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ExportMetadata/Public/ModExportMetadataSchema.h)。

| 宏 | 必填字段 | 可选字段 |
| --- | --- | --- |
| API 与 Event 共用 | — | `ExportVersion` 默认 `"1.0.0"`，以及 `DisplayName`、`Description` |
| `MOD_EXPORT_API` | `Scope`、`Thread`、`FailureRule` | `Permissions` 默认空列表，以及 `FullView`、`CompatibleNames` |
| `MOD_EXPORT_EVENT` | `SourceScope`、`PublishRule` | `PayloadType`、`ExportId`、`Permission`、`PublishPermission`、`UnsubscribeRule`、`Authority` |

MHT 按构建目标校验活动声明。World 作用域的 Hook 和 Event 在游戏线程执行。[宿主声明与签名范围](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)

## 构建并加载模组

1. 构建宿主导出，再用 MDT 发布匹配的游戏 SDK 和生成的 `ModDevelopmentSDKEditor` 插件。使用示例时，将 `UMFDevSDK` 和 `ModDevelopmentSDKEditor` 安装到 `ExampleMods/TestMod/Plugins`。[SDK 请求与产物](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)
2. 在作者工程的关卡编辑器工具栏选择 **Mod → 打包 Mod…**，按目标游戏的基础发布和 Cook 设置打包已启用的内容插件或当前 C++ 工程。打包当前 C++ 工程需要匹配的 Win64 x64 模块化宿主回执和 Pak 后端。[打包指南](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md)
3. 将 `.mod` 放入实际游戏可执行文件同级的 `Mods`。在 Windows PIE 中使用匹配 Editor 的包，放到宿主工程的 `Binaries/Win64/Mods`。在宿主 `[ModLoader]` 中填写发布身份、后端与需要开放的原生代码权限。[加载器配置](Docs/CodingStandards/Modules.zh-CN.md#模组加载器)

宿主在开局前显式准备 World 扩展。运行时支持 Hook/Event 订阅、内容选用、World Patch 锚点和版本化跨模组服务。开发宿主的 `ModExtensions` 模式演示 World 准备、多人玩法计划比对和单机存档迁移。Shipping 构建和更多工具链组合仍需按目标分别验收。[游戏端实现进度与里程碑](Docs/UnrealModFramework_游戏端详细设计.md)

## 独立工具文档

| 工具 | 职责 |
| --- | --- |
| [ModHeaderTool（MHT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md) | 扫描声明，生成宿主连接代码、公开契约和原生 ABI 产物 |
| [ModBuildTool（MBT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModBuildTool/README.zh-CN.md) | 编译并校验生成的静态库，核对最终链接回执 |
| [ModSourceTool（MST）](UnrealModFramework/Plugins/ModRuntime/Tools/ModSourceTool/src/Cli/CommandLine.cpp) | 跟踪失败返回和异常，维护错误目录，生成所属模块的宿主链接头 |
| [ModSDKTool（MDT）](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md) | 规划、构建与检查 SDK，并单独构建制作插件 |

## 参与贡献

请通过[贡献指南](CONTRIBUTING.zh-CN.md)、[Issue 表单](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)和 [PR 模板](.github/pull_request_template.md)准备主题集中的改动。[开发流程](Docs/DevelopmentWorkflow.zh-CN.md)说明了本地检查、可信 PR 编译及文档发布方式。

## API 文档

[API 文档入口](Docs/API/README.zh-CN.md)和编码规范由 Doxygen 构建，并发布至 [UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/)。经过审查的更新合入 `main` 后会自动发布。发布配置见[组织文档托管指南](Docs/Hosting/README.zh-CN.md)。

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## 许可证

除非另有说明，项目原创内容采用 [MIT 许可证](LICENSE)。Unreal Engine 和第三方内容保留各自的许可条款；归属说明见 [NOTICE.md](NOTICE.md)。

## 致谢

感谢所有参与 UnrealModFramework 的贡献者，感谢 Epic Games 提供 Unreal Engine，以及 CircleCI 提供 Linux 项目构建服务。
