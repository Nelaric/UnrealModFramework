<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework 编码规范

UnrealModFramework 的所有贡献都必须同时遵守 [Epic Games 的 Unreal Engine C++ 编码规范](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)与本目录收录的项目规则。如果两套规范在某处存在冲突，请先提交 Issue 指明冲突的具体条款，待明确结论后再推进编码。锁定版本的格式化工具仅在其输出同时满足两套规范时决定代码排版。

项目开发基线为 Unreal Engine 5.6。本仓库包含开发宿主工程、游戏侧 `ModRuntime` 插件，以及供 ModSDKTool（MDT）独立生成并编译 `ModDevelopmentSDKEditor` 的内嵌源码。游戏运行时、制作编辑器、共享 `.mod` 协议与游戏专属 SDK 之间有着清晰的归属和依赖边界；原生 Mod 的 C ABI 则是跨二进制互操作边界，不沿用 UE C++ ABI 假设。

## 规范目录

- [C++ 风格与头文件](Cpp.zh-CN.md)
- [模块与依赖边界](Modules.zh-CN.md)
- [公开 API、错误与文档](API.zh-CN.md)
- [Doxygen 注释](Doxygen.zh-CN.md)
- [运行时、并发、性能与安全](Runtime.zh-CN.md)
- [构建脚本、工具与审查](BuildAndReview.zh-CN.md)

除受格式限制的特殊文件外，所有项目原创文件均须遵守[版权声明规则](BuildAndReview.zh-CN.md)。

### 术语约定

- **Must（必须）**：强制要求，不可违反。
- **Should（应该）**：默认遵循的做法；如确需偏离，须在 PR 中说明充分理由。
- **May（可以）**：按需选用的可选项。

本系列规范所称的**公开 API**，是指面向宿主游戏接入方或 Mod 作者提供的声明，涵盖 Blueprint 节点入口与 C ABI 契约。类定义中可以使用第二个 `public:` 区域放置仅供框架内部衔接或 Unreal 生命周期回调使用的方法，这类方法不视为面向使用者的公开 API。

## 优先级与例外申请

1. 首先保证逻辑正确，并满足所支持 Unreal Engine 版本的引擎约束。
2. 同时满足 Epic 编码规范与本项目规则；两者没有自动覆盖关系，一旦发现冲突请创建 Issue 讨论。
3. 仅当锁定版本的格式化工具输出同时符合两套规范时，以其排版结果为准。
4. 在 PR 中申请项目规则例外时，必须写明所涉规则、申请理由、影响范围及评估过的替代方案。维护者的审查批准不能豁免 Epic 规范要求，也不能绕过 Issue 单独裁定两套规范间的冲突。

本套规范改编自 [Nelaric Unreal Gameplay](https://github.com/Nelaric/nelaric-unreal-gameplay/tree/d976a66fc2e41dee11d605af5b387ed290453d6b/Docs/CodingStandards)（提交 `d976a66fc2e41dee11d605af5b387ed290453d6b`），已将原仓库中面向具体玩法模块、CI 服务和 Git LFS 存储的条款替换为适配本项目的规定。
