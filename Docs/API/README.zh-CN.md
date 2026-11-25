<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework API

UnrealModFramework 为 Unreal Engine 5.6 提供游戏侧模组运行时与独立构建的模组制作编辑器。公开契约涵盖模组包加载与生命周期、生成的 API 调用，以及 World 作用域扩展。

## 运行时入口

| 用途 | 源码契约 |
| --- | --- |
| 校验、挂载与监听 `.mod` 加载结果 | [ModLoader.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModLoader.h)、[ModInstance.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModInstance.h) |
| 注册宿主 API 提供者并签发作用域绑定 | [ModAPIHostSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModAPIHostSubsystem.h) |
| 准备 World、管理 Hook/Event 订阅，以及解析内容、Patch 数据与服务 | [ModExtensionSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSubsystem.h) |
| 接入多人准入与存档迁移 | [ModExtensionSession.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSession.h)、[ModExtensionSave.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSave.h) |
| 注册独立原生扩展回调 | [ModExtensionABI.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModExtensionABI.h) 与目标游戏生成的 SDK 头文件 |

原生兼容性由生成的 SDK 契约与目标配置共同限定。分发模组包前，请核对游戏发布、引擎构建、平台与签名是否匹配；详见 [SDK 指南](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)。

## 契约与规范

- [编码规范](../CodingStandards/README.zh-CN.md)
- [模块职责与依赖边界](../CodingStandards/Modules.zh-CN.md)
- [公开 API、所有权、异步操作与错误](../CodingStandards/API.zh-CN.md)
- [C++ 风格与头文件要求](../CodingStandards/Cpp.zh-CN.md)
- [Doxygen 注释规则](../CodingStandards/Doxygen.zh-CN.md)
- [运行时、并发、性能与安全](../CodingStandards/Runtime.zh-CN.md)
- [构建、工具与审查](../CodingStandards/BuildAndReview.zh-CN.md)

## API 文档生成

`Scripts/run_doxygen.py` 会将当前项目自有的全部 `Public` 头文件目录连同本入口页与编码规范一并纳入文档生成范围。未添加注释的框架集成方法将保持隐藏。常规构建会在出现文档警告时失败；审查通过后可使用 `--publish` 模式进行发布。

项目配置、命令行工具用法、贡献模板与 CI 配置详见[源码仓库](https://github.com/Nelaric/UnrealModFramework)。已发布的文档可通过 HTTPS 访问 [UnrealModFramework/API](https://docs.nelaric.com/UnrealModFramework/API/)，经过审查的更新合入 `main` 后会自动发布。
