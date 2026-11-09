<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework 编码规范

本规范适用于 UnrealModFramework 的所有贡献。贡献者须同时遵守 [Epic Games 的 Unreal Engine C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与本项目规则。如果两者冲突，应先创建 Issue 指明具体要求，再继续相关工作。只有输出同时符合两套规范时，才以锁定版本的格式化工具决定布局。

开发基线为 Unreal Engine 5.6。仓库包含开发宿主工程、游戏侧的 `ModRuntime` 插件，以及独立构建的 `ModDevelopmentSDKEditor` 插件。Runtime、制作编辑器、共享协议和游戏专属 SDK 各有独立的职责与依赖边界。原生模组的 C ABI 属于跨语言互操作边界，不沿用 UE C++ ABI 约定。

## 规范目录

- [C++ 风格与头文件](Cpp.zh-CN.md)
- [模块与依赖边界](Modules.zh-CN.md)
- [公开 API、错误与文档](API.zh-CN.md)
- [Doxygen 注释](Doxygen.zh-CN.md)
- [运行时、并发、性能与安全](Runtime.zh-CN.md)
- [构建脚本、工具与审查](BuildAndReview.zh-CN.md)

[版权声明规则](BuildAndReview.zh-CN.md)适用于所有项目自有文件，并遵守格式规定的例外。

**Must（必须）** 表示强制要求。**Should（应该）** 是默认做法，除非 PR 说明合理例外。**May（可以）** 是可选项。“公开 API”指面向宿主游戏或模组作者的声明，包括 Blueprint 入口和 C ABI 契约。第二个 `public:` 区域可以承载框架集成方法，但不因此成为面向使用者的 API。

## 规范遵循与冲突

1. 首先满足正确性和受支持 UE 版本的要求。
2. 同时遵守 Epic 与项目规则。发现冲突时创建 Issue；任何一方都不自动优先。
3. 只有在两套规范均得到满足时，才以锁定版本的格式化工具输出决定空白字符和布局。
4. PR 申请例外时须说明规则、原因、范围和替代方案。维护者批准的例外不能覆盖 Epic 要求，也不能单独解决规范冲突。

本规范改编自 [Nelaric Unreal Gameplay](https://github.com/Nelaric/nelaric-unreal-gameplay/tree/d976a66fc2e41dee11d605af5b387ed290453d6b/Docs/CodingStandards) 的提交 `d976a66fc2e41dee11d605af5b387ed290453d6b`。源项目专属的玩法模块、CI 服务和 Git LFS 存储规则已按本仓库实际结构调整。
