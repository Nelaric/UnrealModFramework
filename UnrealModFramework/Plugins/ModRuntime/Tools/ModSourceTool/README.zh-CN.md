<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModSourceTool（MST）

ModSourceTool（MST）基于目标真实的 C++ 编译数据库与 `libclang` 开展两项工作：静态分析导出 API 的错误返回与异常传播路径，以及改写自动 C++ Hook 的类外函数定义。在标准构建流程中，ModHeaderTool（MHT）会在代码生成阶段调用 MST 产出改写结果，再由 ModBuildTool（MBT）驱动 UnrealBuildTool（UBT）编译生成的源码副本（参见 [MHT 指南](../ModHeaderTool/README.zh-CN.md)与 [MBT 指南](../ModBuildTool/README.zh-CN.md)）。

## 构建与命令行用法

在仓库根目录执行构建脚本即可单独编译 MST：Windows 下运行 `.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSourceTool`，Linux 或 macOS 下运行 `sh UnrealModFramework/Plugins/ModRuntime/Tools/Build.sh --target ModSourceTool`。

```text
ModSourceTool analyze-errors SourceAnalysisRequest.json
ModSourceTool update-catalog SourceAnalysisRequest.json
ModSourceTool rewrite-hooks HookSourceRequest.json
ModSourceTool generate-host-link <所属模块> <输出头文件>
```

## 分析 API 错误路径

准备一份 `UMF.SourceAnalysisRequest` v1 请求文件，填好 `compilationDatabase`、`target`、`catalog`、`member`、`libclang`、`resourceDirectory` 及 `analysisArguments` 数组：

- 编译数据库须包含目标完整的编译参数，但**不能携带预编译头（PCH）参数**，且 `libclang` 动态库版本必须与 `resourceDirectory` 中的 Clang 资源头文件配套。
- 请求文件中的路径相对于当前工作目录解析；错误目录内各成员条目的 `source` 路径则相对于该错误目录文件所在目录解析。

`member` 用于指定 `UMF.SourceErrorCatalog` v2 错误目录中的某个 API ID。目录中的每个成员条目包含带完整限定名的 `function`、所属 `module`、`source` 源文件路径以及保存已知分类的 `reasons` 对象（并通过 `nextDetailByType` 记录错误码细项的自增分配进度）：

- 遇到同名重载函数时，补充 `signature` 字段以精确定位目标重载。
- 需要追踪错误返回值时，在 `failureReturn` 中填写代表失败的布尔值、整数或空指针常量；若仅需追踪 C++ 异常调用链，直接省略 `failureReturn` 即可（测试样例见 [quality.yml](../../../../../.github/workflows/quality.yml)）。

`analyze-errors` 与 `update-catalog` 均向 stdout 输出 `UMF.SourceErrorReport` v1 JSON 报告（诊断信息写入 stderr），列出本次扫描覆盖的源文件、失败触发点，以及顺着编译数据库可见函数体追踪出的跨文件调用链：

- `analyze-errors`：对照目录中已保存的分类做只读核验，一旦发现尚未分配错误码的新失败分支即报错退出。
- `update-catalog`：为新发现的失败原因分配稳定的细项错误码，并在持有文件独占锁的前提下原子写回目录文件。日常修改宿主代码的条件判断表达式时，请保留既有的原因键名与错误码以维持契约兼容。

## Hook 源码副本改写

在 `generate` 阶段，MHT 会组装 `UMF.HookSourceRequest` v1 请求并调用 `rewrite-hooks`。MST 在 Clang AST 中定位目标成员函数的类外定义，将原始源码与注入分发逻辑后的改写文本一并返回给 MHT，由 MHT 落盘源码副本及 `Diagnostics/HookSources.json` 映射表供 MBT 替换 UBT 编译输入。若未找到类外定义，或分析期间源文件发生并发改动，改写会直接报错终止（详见[自动 Hook 构建](../../Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)）。

`generate-host-link` 保留给旧版 Win64 x64 MSVC 静态库归档接入方案使用，用于生成强制链接器保留宿主静态库符号的头文件；当前的标准 UBT Target 已改为通过 UHT 直接将宿主生成源码注册进所属模块编译（见[旧版归档命令](../ModBuildTool/README.zh-CN.md#旧版归档命令)）。
