<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModRuntime UHT 适配层

`ModRuntime.ubtplugin.csproj` 从当前引擎继承 UHT Exporter 的目标框架（UE 5.6 对应 .NET 8，UE 5.8 对应 .NET 10）。UBT 会在已启用的 ModRuntime 插件内发现该工程，构建程序集并加载 `[UnrealHeaderTool]` 类与 `[UhtExporter]` 入口。该工程直接引用当前引擎的程序集，无需项目引用或修改引擎。Exporter 名称为 `ModRuntime`，反射运行时模块 `ModLoader` 在 Game 和 Editor 清单中均会启用它。

适配层从当前 UBT Target 准备分析输入并执行 MHT。MHT 负责 C++ 投影和生成，ExportCodeGenerator 负责蓝图导出，UBT 编译并链接宿主。

## 宿主生成

通过 MBT 使用项目的 Development Editor 目标准备元数据，再为正常目标构建消费同一 Metadata Bundle。UBT 提供目标、模块及编译动作，UHT 提供反射类型身份、所属模块和生成文件位置。MHT 从活动 Clang AST 发现声明和签名依赖。Game 读取已有蓝图快照，并按自己的工具链分析和生成原生代码。

项目标识来自 `.uproject` 文件名。反射类型默认从 UHT 的 `/Script/<Module>.<Type>` 推导 `ue.<module>.<type>`，类型 ID 使用 ASCII 小写。原生逻辑身份由模块及完整限定 C++ 名称构成。已发布的身份可用 `UCLASS(meta = (ModTypeId = "..."))` 保留，开发宿主以此延续 `game.character` 和 `game.local_player_api`。布局和 ABI 始终由当前目标的编译命令计算。

Scope 可从声明或无歧义的继承关系得到。LocalPlayer 子系统采用 `Player`。同一类型的成员需要满足一致的作用域契约。API 返回值具有显式失败语义时，可声明 `FailureReturn = "false"` 等标量哨兵。业务失败可在返回点使用 `MOD_SOURCE_FAILURE(MOD_ERROR_NAME, Code, Value)` 固定错误码。所属函数、签名、源码文件和目标编译参数由 AST 与实际构建信息提供。

固定业务错误码使用 `(Domain << 28) | (Type << 16) | Detail`。Business Domain 为 `3`。发布后保持符号和编号稳定。MST 在同一组导出中检查重复使用的编号或符号冲突，并在源码报告中记录各目标实际存在的失败和异常传播路径。

可用 `UMF_MHT_EXECUTABLE`、`UMF_MHT_LIBCLANG` 和 `UMF_MOD_SOURCE_TOOL_EXECUTABLE` 选择合法的本地工具安装。构建输入、反射清单、蓝图快照和生成回执由工具写入 `Intermediate/ModTools`。[开发工作流](../../../../../../Docs/DevelopmentWorkflow.zh-CN.md)

### 模块选择

可在 `.uproject` 同目录放置 `<项目名>.modexport`。文件缺省或内容为 `{}` 时，MHT 通过 `*.Build.cs` 递归发现工程 `Source` 下的模块。下例纳入 Combat 和 Audio 两个插件，在工程范围排除 GameEditor，在 Combat 内排除 CombatEditor。

```json
{
  "ExcludeModules": ["GameEditor"],
  "Plugins": [
    { "Name": "Combat", "ExcludeModules": ["CombatEditor"] },
    "Audio"
  ]
}
```

`Plugins` 的字符串条目可以是工程 `Plugins` 目录树中的插件名，也可以是具体的 `.uplugin` 路径。要给插件设置排除项，可用对象条目，通过 `Name` 指定插件名，或通过 `Path` 指定描述文件。相对路径以工程目录为起点。

顶层 `ExcludeModules` 适用于工程和所有已选插件。插件条目中的 `ExcludeModules` 只对该插件生效，两级规则都按 UE 模块名匹配并忽略大小写。同一插件通过名称和路径重复选中时，其局部排除项合并。插件是否参与当前 Target 的构建仍由 UE 决定。

独立扫描和自动生成使用同一份选择规则。对于含导出标记或配置了蓝图导出的模块，UHT 从当前 Target 的编译数据库提取实际参与编译的 C++ 翻译单元。新增模块或 `.cpp` 会在下次构建时自动发现。`ModHeaderTool discover <项目.uproject>` 可查看所选模块及其是否包含导出标记。调用方自行提供的 `buildInput` 仍可固定精确输入。

### 从宿主蓝图导出 API

Editor 根据 Asset Registry 中的 UMF 标记和声明依赖发现导出资产，仅加载和编译候选蓝图。默认类型身份来自 Blueprint 包的持久 GUID，宿主模块由项目或项目插件的运行时模块推导。在 **蓝图Mod导出 → 发行身份** 中可以保留已发布的类型 ID，或在存在多个可能归属时指定模块。移动或重命名资产时保留原声明 GUID。发布新 API 前设置 `PublicName`，已有声明的空版本按 `1.0.0` 读取。

准备元数据时，`ModExportBlueprints` commandlet 会编译已保存的资产，从 Kismet 生成的 `UFunction` 提取签名，并将快照交给 MHT。当前支持 `World`、`GameThread`、`ReturnError` 规格的实例 API，参数为按值传递的布尔或数值标量，结果为一个标量输出或 `void`。配置中的资产包会自动加入宿主 Cook。

游戏在实例就绪后调用 `UModAPIHostSubsystem::RegisterWorldProvider` 并持有返回的注册凭据，再通过 `SetWorldCallerPolicy` 指定允许访问的模组、类型和权限。生成的 SDK 在 `<SDK>API.h` 中提供 `Acquire_<对象引用类型>`；原生 Mod 在活跃的 `UModInstance` 中调用它即可建立函数表连接并取得对象引用，随后调用同一头文件中生成的类型化方法。宿主会在实际的蓝图实例上执行 `ProcessEvent`。模组卸载、世界关闭或宿主更换授权策略时，相应的调用者会被撤销。

例如，样例工程的 `game.blueprint_test` 会在 `UMFDevSDK` 中生成以下调用（需在 `UModInstance` 子类的方法中使用，且前提是宿主已注册该 Actor 并授权当前模组；取得引用后，可在当前世界存续期间重复调用）：

```cpp
FModObject_T_f4f57c0936062c84 Provider;
if (UUMFDevSDKLibrary::Acquire_FModObject_T_f4f57c0936062c84(*this, Provider))
{
	EUMFDevSDKCallOutcome Outcome;
	int64 ErrorCode;
	UUMFDevSDKLibrary::TestModAPIExport(Provider, 1.0, Outcome, ErrorCode);
}
```

### 从宿主蓝图导出 Hook 和 Event

先在 `blueprintExports` 中配置资产。在 **蓝图Mod导出** 面板中声明 Hook 或 Event 后，在业务图中连接对应 **Hook Point** 或 **Publish Event** 节点的执行输入；未连线的声明将跳过导出。Hook 的原版逻辑图接收一个载荷并返回同类型结果，节点同时输出结果、状态码、取消标志和已提交标志。Event 支持 `Immediate` 和 `NextTick`，并支持可选的立即退订。载荷类型支持受支持的标量、反射值结构体或具有已生成 World 视图的 UObject。

C++ Hook/Event 通过导出宏关联具备 World 类型身份的成员签名。MBT 构建目标时，无参 `MOD_EXPORT_HOOK()` 会包装已有的 `.cpp` 定义。C++ Event 既可以直接调用生成的发布函数，也可以使用 `HostEndpoint` 自动生成发布函数体。详见[生成契约与载荷限制](../../Tools/ModHeaderTool/README.zh-CN.md#world-扩展产物)。

### 自动 Hook 构建

在执行 UBT 编译动作的主机上运行 MBT，并确保已安装目标平台的 UE SDK。每次调用构建一个架构。Apple 目标的链接校验还需要 `python3` 和 Xcode 的 `nm`。以下示例从仓库根目录构建 Linux Development Editor。

[MBT 构建与自动化命令](../../../../../Docs/DevelopmentWorkflow.zh-CN.md#本地构建)

独立 Hook 用例覆盖下列 UBT 平台和架构标识。Win64 的 `arm64ec` 用例还需提供匹配的 Windows SDK 运行库。[Hook 目标用例](../../../../../Scripts/test_hook_targets.py)

| UBT 平台 | 架构 |
| --- | --- |
| `Win64` | `x64`、`arm64`、`arm64ec` |
| `Linux` | `x64` |
| `LinuxArm64` | `arm64` |
| `Mac`、`Android` | `x64`、`arm64` |
| `IOS`、`VisionOS` | `arm64`、`iossimulator` |
| `TVOS` | `arm64`、`tvossimulator` |

`UMF_MHT_LIBCLANG` 指定构建主机上的 `libclang`，`UMF_MHT_CLANG_RESOURCE_DIR` 指定配套的资源目录。项目也可通过 `hostLibclang` 按构建主机设置库路径。目标 SDK、头文件路径和架构均来自 UBT 的编译数据库。[UE 集成构建与自动化测试范围](../../../../../Docs/DevelopmentWorkflow.zh-CN.md)

MHT 和 MST 通过 Clang 定位标记的成员函数并生成源码副本。MBT 会替换对应 UBT 编译动作及嵌套响应文件中的源码输入，同时沿用原定的对象文件和链接输出。生成的注册代码引用源码副本中定义的版本符号，因此 COFF 和 ELF 链接器会拒绝未经替换的原目标文件。Apple 构建还会使用 Xcode 的 `nm` 检查实际的 Mach-O 产物，即便在动态查找模式下也能发现遗留的 Hook 导入。C++ 与蓝图调用最终进入同一个公开函数。仅 `prepare-metadata` 可以通过 `-MhtBootstrap` 调用 UBT，生成的 Development Editor 直接执行原函数。`Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` 测试覆盖了这两种调用入口。

各目标生成 `UMF.GeneratedArtifacts` v4 清单。宿主编译单元列出所属模块和用途，Client 源交给 MDT。

## 宿主编译与链接

UHT 通过 `CompileOutput` 将宿主与编译校验单元交给所属 UBT 模块。MDT 将 `ClientSource` 放入共享 SDK Runtime。MBT 驱动 Hook 源替换、跟踪输入，并用 UBT 回执、模块清单和最终二进制中的生成标记写出当前变体的 `HostTargets/.../host-link.json`。

## 构建语义与范围

原生诊断会转发至 UHT 日志。非零退出码、超时或无效产物都会导致 UHT 失败。子进程由 UE `ManagedProcessGroup` 管理。采用 UBT 宿主编译的目标通过 `CompileOutput` 将 `*.umfhost.cpp` 编译单元登记到所属模块。UBT 编译宿主单元，MHT 将独立客户端源码作为单独产物输出。

UHT 的 NoOutput、reference、引导模式及 `-Mode=GenerateClangDatabase` 调用会跳过原生生成；正常构建在准备完成后运行 MHT。由于 MHT 尚无等价的 dry-run 协议，`-FailIfGeneratedCodeChanges` 下拒绝完整生成；`scan` 模式的最终报告仍由 UHT 执行常规的变更检查。

UHT 通过 `AddExternalDependency` 跟踪工具、生成输入、配置、快照及相关源文件。源文件集合变化时 MBT 更新监视标记，使 UBT 重新收集编译动作。

UBT 会自动生成包含 `EngineDir` 的邻接 `.ubtplugin.csproj.props` 文件（已加入 Git 忽略）。手工编译适配程序集可运行：

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
