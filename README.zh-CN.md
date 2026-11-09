<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework

[![版本](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.zh-CN.md)

UnrealModFramework 是以 UE 5.6 为开发基线的工程，目标是提供可迁移的游戏侧 Mod Runtime 和独立构建的模组开发编辑器。仓库目前只包含工程及插件模块骨架；模组包加载、内容解析、原生模组 API 和编辑器工作流仍待实现与验证。

下表路径均相对于仓库根目录。UE 工程位于 `UnrealModFramework/`，仓库配置、文档和质量检查脚本位于与其同级的根目录中。

| 路径 | 用途 |
| --- | --- |
| `UnrealModFramework/UnrealModFrameworkDev.uproject` | 开发宿主工程 |
| `UnrealModFramework/Plugins/ModRuntime/` | 游戏侧源码插件；`Source/Runtime/` 与 `Source/Editor/` 按目标归类模块 |
| `UnrealModFramework/SourcePlugins/ModDevelopmentEditor/` | 独立构建的模组制作插件 |
| `Docs/CodingStandards/` | 项目编码与贡献规范 |
| `.circleci/config.yml` | UE 5.6.1 Linux 构建配置 |
| `Scripts/` | 本地质量检查 |

`Binaries`、`Intermediate`、`Saved` 和 `Dist` 是生成产物，不进入 Git。制作插件从 `UnrealModFramework/SourcePlugins` 下的描述文件通过 UAT `BuildPlugin` 构建；Windows 上应使用较短的临时 `-Package` 路径，成功后再将插件复制到发布目录。

修改代码或构建文件前，请阅读[编码规范](Docs/CodingStandards/README.zh-CN.md)、[贡献指南](CONTRIBUTING.zh-CN.md)和 [CircleCI 接入说明](Docs/CircleCI.zh-CN.md)。

## MHT 的 C++ 导出参数校验

MHT 是独立 CMake C++20 工具，只处理 `MOD_EXPORT_API`、`MOD_EXPORT_EVENT`、`MOD_EXPORT_HOOK` 的 C++ 声明，不扫描或校验蓝图。工程采用 `include/ModHeaderTool` 和 `src` 布局，`main.cpp` 只转发入口。构建工具：

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModHeaderTool
```

编译 UE Editor Target 后，先从共用的 `ExportMetadata::FOptions` 提供者导出实际选项。它使用 UE 已解析的 GameMod 配置和全局 GameplayTags 字典，包含原生及数据表标签；MHT 不另行实现一套近似的 INI/标签解析器，也不依赖 `BlueprintIntegration`：

```powershell
$project = (Resolve-Path UnrealModFramework/UnrealModFrameworkDev.uproject).Path
& "C:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=ModExportOptions -unattended -NullRHI -nosound
.\UnrealModFramework\Plugins\ModRuntime\Tools\build\Release\bin\Release\ModHeaderTool.exe scan --project $project --output UnrealModFramework/Intermediate/ModHeaderTool/Exports.json
```

选项目录默认写入工程的 `Saved/ModHeaderTool/MetadataOptions.json`，也可以用导出命令的 `-Output=<文件.json>` 和 MHT 的 `--options <文件.json>` 指定。配置输入新增、删除或改变时，目录指纹检查会报错，须重新导出。原生标签模块重新编译或数据表更新后也应重新导出。未找到任何导出调用时可以不提供选项目录。`.modexpot` 仍为可选的额外扫描目录配置，不会自动创建。

字段规则定义在 `ModExportMetadataSchema.h`，字段名和候选值区分大小写：

| 宏 | 必填字段 | 可选字段 |
| --- | --- | --- |
| 全部 | `ExportId`：已注册 GameplayTag 的字符串字面量 | `ExportVersion` 默认 `"1.0.0"`；`DisplayName`、`Description` |
| `MOD_EXPORT_API` | `Scope`、`Thread`、`FailureRule` | `Permissions` 默认空列表 |
| `MOD_EXPORT_EVENT` | `SourceScope`、`PublishRule` | `PayloadType` |
| `MOD_EXPORT_HOOK` | 仅公共必填字段 | `ContextType`；`AllowedStages` 默认 `(Observe)` |

`Scope` 等候选值接受标识符或字符串。权限和阶段使用括号列表，如 `Permissions = ("WorldRead", "WorldWrite")`、`AllowedStages = (Observe, Modify)`；阶段至少有一个，列表项不能重复。`ExportVersion` 为两段或三段数字版本字符串，各段不允许多余的前导零。`PayloadType`、`ContextType` 校验 C++ 类型名语法，类型是否存在及声明绑定仍留给 AST 阶段。

```cpp
MOD_EXPORT_API(
    ExportId = "ModTest.APIExpot.Blueprint.TestFunction",
    Scope = World,
    Thread = GameThread,
    FailureRule = ReturnError,
    Permissions = ("WorldRead"))
```

解析支持注释、续行、相邻字符串、普通/raw string、常规转义、Unicode 转义及 `TEXT("...")`。不接受任意 C++ 表达式、命名常量、用户自定义字符串后缀或嵌套列表。未知/重复字段、缺少必填字段、类型错误、未注册 ID、非法候选值和重复列表项均输出 `文件(行,列): error: 宏名: 具体原因`；行列从 1 开始，列按 UTF-8 字节计。

有错误时继续收集可恢复的诊断，返回 `1`，不写入成功报告；成功返回 `0`。JSON 报告版本为 `2`，保留原始参数并新增解析后的 `Metadata` 和 `ParametersValidated`。`PreprocessorEvaluated` 仍为 `false`：条件分支尚不求值，C++ AST、类型存在性和运行时策略语义不属于本阶段。

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
