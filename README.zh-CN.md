<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework

UnrealModFramework 是以 UE 5.6 为开发基线的工程，目标是提供可迁移的游戏侧 Mod Runtime 和独立构建的模组开发编辑器。仓库目前只包含工程及插件模块骨架；模组包加载、内容解析、原生模组 API 和编辑器工作流仍待实现与验证。

| 路径 | 用途 |
| --- | --- |
| `UnrealModFrameworkDev.uproject` | 开发宿主工程 |
| `Plugins/ModRuntime/` | 包含 Runtime 与 Editor 模块的游戏侧源码插件 |
| `SourcePlugins/ModDevelopmentEditor/` | 独立构建的模组制作插件 |
| `Docs/CodingStandards/` | 项目编码与贡献规范 |
| `Scripts/` | 本地质量检查 |

`Binaries`、`Intermediate`、`Saved` 和 `Dist` 是生成产物，不进入 Git。制作插件从 `SourcePlugins` 下的描述文件通过 UAT `BuildPlugin` 构建；Windows 上应使用较短的临时 `-Package` 路径，成功后再将插件复制到发布目录。

修改代码或构建文件前，请阅读[编码规范](Docs/CodingStandards/README.zh-CN.md)和[贡献指南](CONTRIBUTING.zh-CN.md)。
