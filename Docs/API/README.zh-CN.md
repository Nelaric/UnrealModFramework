<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework API

UnrealModFramework 面向 Unreal Engine 5.6 提供游戏侧模组运行时与独立构建的 Mod 制作编辑器。其公开契约涵盖 `.mod` 包加载与生命周期回调、编译期生成的 API 绑定，以及 World 作用域的玩法扩展。

## 运行时核心入口

| 使用场景 | 公开契约头文件 |
| --- | --- |
| 校验、挂载与监听 `.mod` 包加载结果 | [ModLoader.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModLoader.h)、[ModInstance.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModInstance.h) |
| 注册宿主 API 提供者并签发作用域绑定 | [ModAPIHostSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModAPIHostSubsystem.h) |
| 准备 World、管理 Hook/Event 订阅，并解析内容槽、Patch 数据与跨 Mod 服务 | [ModExtensionSubsystem.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSubsystem.h) |
| 接入多人联机准入握手与存档迁移 | [ModExtensionSession.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSession.h)、[ModExtensionSave.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModAPI/Public/ModExtensionSave.h) |
| 注册独立的原生 C ABI 扩展回调 | [ModExtensionABI.h](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/Runtime/ModLoader/Public/ModExtensionABI.h) 及目标游戏生成的 SDK 头文件 |

生成的 C++ 与蓝图 API 统一经由游戏共享的 SDK Runtime 模块发起调用；每个已挂载的 Mod 都持有相互隔离的调用方（Caller）上下文，支持按 Mod 单独撤销授权。分发 `.mod` 包前，请确认其 SDK 契约、基础发行版、引擎构建与目标配置均与宿主游戏匹配（详见[连接与生命周期](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#连接与生命周期)与 [SDK 安装说明](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md#安装与验证)）。

## 契约与编码规范

- [编码规范总览](../CodingStandards/README.zh-CN.md)
- [模块职责与依赖边界](../CodingStandards/Modules.zh-CN.md)
- [公开 API、所有权、异步操作与错误](../CodingStandards/API.zh-CN.md)
- [C++ 风格与头文件](../CodingStandards/Cpp.zh-CN.md)
- [Doxygen 注释规范](../CodingStandards/Doxygen.zh-CN.md)
- [运行时、并发、性能与安全](../CodingStandards/Runtime.zh-CN.md)
- [构建脚本、工具与审查](../CodingStandards/BuildAndReview.zh-CN.md)

## 生成 API 文档

安装锁定版本的 Doxygen 后，在仓库根目录运行 `python Scripts/run_doxygen.py`，再打开 `.tools/api-docs/html/index.html` 即可浏览。构建范围覆盖仓库内所有项目自有的 `Public` 头文件目录、API 入口页以及编码规范。未编写文档注释的框架内部集成方法会自动隐藏，任何 Doxygen 警告都会使本地构建报错退出。

当仓库变量 `API_PAGES_ENABLED=true` 时，推送到 `main` 分支会以发布模式运行 Doxygen 并自动更新在线站点：

- [在线 API 文档](https://docs.nelaric.com/UnrealModFramework/API/)
- [站点发布配置](https://github.com/Nelaric/UnrealModFramework/blob/main/Docs/Hosting/README.zh-CN.md)
- [GitHub 源码仓库](https://github.com/Nelaric/UnrealModFramework)
