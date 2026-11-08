<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework API

UnrealModFramework 以 Unreal Engine 5.6 为开发基线，提供可迁移的游戏侧模组运行时和独立构建的模组制作编辑器。本文档说明项目的公开契约与编码规范。已实现的导出声明及工具尚不构成稳定的原生模组 ABI 或完整的模组包加载运行时。

## 契约与规范

- [编码规范](../CodingStandards/README.zh-CN.md)
- [模块职责与依赖边界](../CodingStandards/Modules.zh-CN.md)
- [公开 API、所有权、异步操作与错误](../CodingStandards/API.zh-CN.md)
- [C++ 风格与头文件要求](../CodingStandards/Cpp.zh-CN.md)
- [Doxygen 注释规则](../CodingStandards/Doxygen.zh-CN.md)
- [运行时、并发、性能与安全](../CodingStandards/Runtime.zh-CN.md)
- [构建、工具与审查](../CodingStandards/BuildAndReview.zh-CN.md)

## API 文档生成

`Scripts/run_doxygen.py` 会把当前项目自有的所有 `Public` 头文件目录加入输入，与本入口和编码规范一起生成文档。没有注释的框架集成方法保持隐藏。普通构建在出现文档警告时失败；发布模式在审查后使用 `--publish`。

项目配置、命令行工具用法、贡献模板与 CI 配置见[源码仓库](https://github.com/Nelaric/UnrealModFramework)。维护者启用 GitHub Pages 后，配置的发布路径为 [UnrealModFramework/API](https://docs.nelaric.com/UnrealModFramework/API/)。
