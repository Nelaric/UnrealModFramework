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
