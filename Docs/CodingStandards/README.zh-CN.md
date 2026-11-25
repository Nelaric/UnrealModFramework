<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework 编码规范

本规范适用于 UnrealModFramework 的所有贡献。贡献者必须同时遵守 [Epic Games 的 Unreal Engine C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与本项目规则。若两者存在冲突，应先创建 Issue 指明冲突的具体要求，再继续相关工作。仅当锁定版本格式化工具的输出同时符合两套规范时，才由其决定代码布局。

开发基线为 Unreal Engine 5.6。仓库包含开发宿主工程、游戏侧的 `ModRuntime` 插件，以及 MDT 用于独立生成并构建 `ModDevelopmentSDKEditor` 的内嵌源码。运行时、制作编辑器、共享协议和游戏专属 SDK 各有独立的职责与依赖边界。原生模组的 C ABI 属于跨语言互操作边界，不沿用 UE C++ ABI 约定。

## 规范目录

- [C++ 风格与头文件](Cpp.zh-CN.md)
- [模块与依赖边界](Modules.zh-CN.md)
- [公开 API、错误与文档](API.zh-CN.md)
- [Doxygen 注释](Doxygen.zh-CN.md)
- [运行时、并发、性能与安全](Runtime.zh-CN.md)
- [构建脚本、工具与审查](BuildAndReview.zh-CN.md)

[版权声明规则](BuildAndReview.zh-CN.md)适用于所有项目自有文件，但受特定文件格式的例外规则约束。

- **Must（必须）** 表示强制要求。
- **Should（应该）** 表示默认做法，除非 PR 说明了合理的例外情况。
- **May（可以）** 表示可选项。

“公开 API”指面向宿主游戏集成或模组作者的声明，包括 Blueprint 入口和 C ABI 契约。第二个 `public:` 区域可以暴露框架集成方法，但不会因此使其成为面向使用者的 API。

## 规范遵循与冲突

1. 优先满足正确性以及受支持 UE 版本的要求。
2. 同时遵守 Epic 规范与本项目规则。发现冲突时应创建 Issue 报告；两套规范均不自动优先。
3. 仅当输出结果同时满足两套规范时，才以锁定版本的格式化工具决定空白字符和代码布局。
4. 申请例外的 PR 必须说明所涉规则、原因、影响范围和替代方案。维护者的批准不能覆盖 Epic 的要求，也不能单独解决两套规范之间的冲突。

本规范改编自 [Nelaric Unreal Gameplay](https://github.com/Nelaric/nelaric-unreal-gameplay/tree/d976a66fc2e41dee11d605af5b387ed290453d6b/Docs/CodingStandards) 的提交 `d976a66fc2e41dee11d605af5b387ed290453d6b`。源项目中针对其玩法模块、CI 服务和 Git LFS 存储的专属规则已替换为适配本仓库结构的规定。
