<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# MHT 客户端 ABI 生成

MHT 生成 UE 视图与 `ContextHandlesV1` C ABI。UBT 在各宿主模块内编译 Host 实现，在共享 SDK Runtime 中编译 Client 实现。

## 连接产物

以下路径均相对于固定的 MHT 输出根目录，每次生成都会覆盖前一次产物：

| 产物 | 用途 |
| --- | --- |
| `SDK/Public/<game>.modabi.h` | 纯 C 声明、定宽类型、状态码、ABI 版本/哈希常量及类型化宿主函数表 |
| `Host/<owner>/<owner>.modabi.cpp` | 宿主 C 入口、精确成员调用、运行时校验及导出的函数表查询入口 |
| `Client/<game>ModClient/<game>ModClient.modclient.cpp` | 独立客户端入口、带兼容性校验的 Connect、Disconnect 及 ReleaseObject |
| `Contract/<game>.clientabi.json` | `UMF.ClientABI` v2 连接描述，包含所属模块、函数表、查询/连接符号、表槽、签名、哈希与生命周期规则 |
| `Contract/<game>.sdkcontract.json` | 供 ModSDKTool（MDT）在受支持的适配图中使用的已解析 `UMF.SdkContract` v2 |
| `<game>.generated-artifacts.json` | v3 完整清单，显式列出 Host/Client 编译单元及必需符号绑定 |

MHT 将 C 实现声明为 `ClientSource`，连接描述中的符号归属为 `<game>ModClient`。MDT 将实现放入共享 SDK Runtime 模块。[客户端 ABI Schema](Protocol/client-abi.schema.json)

`.modviews.h` 和 `.modcalls.h` 属于宿主私有的 `HostHeader` 产物；公共 `SdkHeader` 产物包含 `.modabi.h`，并按契约发布错误码目录及独立扩展 ABI 头。MDT 仅复制具备公共角色的已校验产物。

## 连接与生命周期

1. 根据描述中的 `ownerModule` 定位实际所属二进制，解析 `querySymbol`，并使用生成的 ABI 版本、`sizeof(table)` 和 ABI 哈希查询函数表。
2. 使用该函数表调用对应客户端的 `connectSymbol`。Connect 会先校验版本、结构体大小、哈希和全部必需回调，再绑定函数指针。
3. 游戏代码批准调用者、作用域与权限。仅限宿主调用的 `Mod::API::FNativeAccess::Open` 会签发非零的 context 和 scope token；`UModAPIHostSubsystem::OpenLocalPlayer` 会绑定生成的 LocalPlayer 提供者，并在 `Native` 字段中返回这些 token。
4. 将这些 token 交付给消费者。Getter 接收 context/scope 并返回 object token；成员方法接收 context/object 及生成签名定义的入参。宿主在每次调用时都会重新校验线程、调用者生命周期、类型、作用域与权限。
5. 模组停止时撤销对应 Caller；关闭作用域或 Registry 同样会使 token 失效。ReleaseObject 会显式使该 token 的所有副本失效，既不拥有也不销毁底层的 UObject。重复投影会复用仍然有效的绑定。
6. 卸载宿主或 SDK 模块前，须先调用 Disconnect 并等待既有调用结束。Disconnect 仅清空函数指针，不会等待正在执行的调用，也不会撤销共享该客户端模块的其他调用者。

连接表在模块范围内共享，身份、作用域、权限和对象 token 则按调用者隔离，不会推断全局当前玩家或世界。token 绝非原始地址，在 Runtime 模块存活期间也不会复用；跨 context 使用 token 会导致查找失败。客户端通过具体类型的回调函数指针分发调用。

C 边界采用显式 `cdecl`、64 位进程内 token、定宽整数/标量参数和 `uint32_t` 状态码；bool 参数使用取值限定为 0 或 1 的 `uint8_t`。输出内存由调用者提供，失败时置零，成功时提交结果。状态码 0 表示成功，65536 表示未连接，65537 表示 ABI 不匹配，65538 表示意外原生异常。

`nativeABIHash` 为连接描述在加入 `nativeABIHash`、`contractHash` 和 `projectionHash` 前的规范 SHA-256 摘要，采用 nlohmann JSON 排序键与紧凑 `dump()` 序列化。摘要覆盖 API 形状、连接符号、状态码与生命周期规则，公共头文件中内嵌了相同的版本和哈希常量。

## World 扩展产物

- `Host/<owner>/<owner>.modextensions.h` 为 C++ 接入点提供类型化的 `Invoke` 和 `Publish` 辅助函数。生成的宿主桥负责登记 C++ 与蓝图扩展定义，并实现声明了 `HostEndpoint` 的 C++ 成员函数。
- `Contract/<game>.sdkcontract.json` 的 `extensions` 记录扩展 ID、签名、授权、阶段或派发规则及载荷投影；打包器以该契约为依据校验 v2 扩展清单。
- 存在生命周期源码与扩展定义时，`SDK/Public/ModExtensionABI.h` 与 `SDK/Public/<game>.modextensionsabi.h` 提供独立 DLL 的回调契约、ID、签名与线格式，与用于生成 API 调用的 `ContextHandlesV1` 调用表配合使用。

Hook 接收一个载荷并返回同类型结果；Event 的发布函数接收一个载荷并返回 `void`。支持的载荷类型包括 `float`、`double`、`int32`、`int64`、`bool`、`uint8`，以及受支持的反射值结构体和具备已生成 World 视图的 UObject。结构体投影仅复制声明的公开字段，对象投影在回调期间保持只读。蓝图的标量 API 及已连线的 Hook/Event 声明均来自 Editor 快照，详见[宿主导出步骤](../../Source/ModRuntimeUht/README.zh-CN.md)。

`MOD_EXPORT_HOOK()` 用于为 `.cpp` 中类外定义的标量成员启用自动分发。生成的 ID 由 World 类型 ID 和带连字符的成员名拼接而成，例如 `hook/game.character/change-health`。Hook 包含 `Pre`（支持 `ModifyInputs` 和 `Cancel`）、`On`（支持 `Replace`）与 `Post`（观察结果）三个阶段。基础授权使用 Hook ID，写操作授权则在该 ID 后附加操作后缀。MHT 与 MST 会生成 `Diagnostics/HookSources.json` 替换映射表，UBT 构建驱动据此按目标平台和架构编译对应源码副本，详见[自动 Hook 构建](../../Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)。

`NoPre`、`NoOn`、`NoPost` 直接写裸标记名即可，顺序任意。`NoPre` 会同时关闭 `ModifyInputs` 和 `Cancel`；`NoOn` 会关闭 `Replace`，使通过 `Pre` 的调用执行游戏原函数。三个标记同时指定时，MHT 会从 SDK 中移除当前及曾用 Hook ID，MST 也会保持该函数的原始实现不变。

重命名成员时，可在同一个宏中列出曾用名，例如 `MOD_EXPORT_HOOK(NoOn, CompatibleNames = ("ChangeHealth", "UpdateHealth"))`。新旧名称共用相同的阶段策略和有序回调链。仅重命名函数且保持签名与阶段策略兼容时，每个旧名都会保留原来的 ID、签名和授权名；调整阶段策略则会改变所有名称的契约签名。MHT 会拒绝重复 ID，包括映射到同一连字符拼写的不同名称。

### 生成 C++ Event 发布函数

将 `HostEndpoint` 设为 `Mod::Extension::FEndpoint` 成员的名称，即可在 `<owner>.modhost.cpp` 中生成对应的函数定义，将参数转发给 `Publish`。游戏代码也可自行定义成员函数并调用辅助函数。`HostCommitPayload` 是当前生成器的保留字段，使用时须提供 `HostEndpoint`。

## API 兼容名

显式 `MOD_EXPORT_API` 由所属类型的 ID 和方法名生成 ID，例如 `api/game.character/increasehealth`。填写 `CompatibleNames = ("AddHealth")` 后，MHT 会为同一实现额外导出 `api/game.character/addhealth`，SDK 中的两个函数名共享相同的签名与调用策略。

省略 `CompatibleNames` 时，MHT 会使用 UE 从 Engine 配置及已启用插件配置中解析出的 `FunctionRedirects`。同一类内指向已导出宿主 `UFUNCTION` 的精确重定向会贡献对应的旧名；函数名可能重复时，请用反射类名和模块名限定重定向。连续改名链会导出链上的各个旧名。显式填写 `CompatibleNames = ()` 则表示兼容名列表为空。

MHT 会在宏声明或 INI 条目处报告非法名称、名称冲突及适用范围内的无效重定向。UBT 构建会自动刷新默认配置快照；独立运行 MHT 时，修改重定向后请使用 `ModExportOptions` 刷新快照。宿主与 SDK 必须使用同一份生成契约构建。

## FullView 与对象输入

`MOD_EXPORT_API` 的可选字段 `FullView` 接受带引号的限定 C++ 类名列表，例如 `FullView = ("USkeletalMesh")`，其中每个名称都必须匹配该签名中的 UObject 指针入参。生成阶段会按类合并请求，从目标编译中收集该类及其公开基类的具体公开实例方法，同时保留显式导出方法各自的既有契约。自动合成的成员要求可写绑定、所有选择声明的权限，以及宿主授予的 `FullView.<TypeId>` 权限。只要任一成员缺少投影，生成即会失败并报告具体的受影响成员；这些成员涉及的其他类按常规投影规则处理。

UE SDK 调用者直接传入类型化的 UObject 指针。`ContextHandlesV1` 则为每个此类参数使用 `const char*`（借用的 UTF-8 对象路径字节）与 `uint32_t` 长度（最大 4,096 字节），空对象以 `nullptr` 和零长度表示。宿主解析已加载的对象、校验其类型和 World，并在同步调用期间借用该对象。对象结果以宿主签发的视图 token 返回。公开引擎类的头文件从构建输入的 `Engine` 路径根下解析。

## MBT 与 MDT 接续

每个宿主模块生成四个编译单元——桥接、ABI 入口、校验与 UE 视图实现，独立客户端另有一个单元；清单的 `compilationUnits` 逐项列出所有源文件。`requiredSymbols` 同时记录宿主和客户端入口，关联相同的 ExportId、签名哈希与投影哈希，并按库所属模块检查 ExportId 唯一性。查询、连接、断开与释放函数同样是必需符号，宿主注册锚点可确保 ABI 对象参与最终链接。

UHT 将 `buildInput`、`outputDirectory`、`hostCompiler`（`UBT`）和 `sourceTool` 写入生成请求。MHT 发布 `UMF.GeneratedArtifacts` v4，列出 `HostBinding`、`CompileVerification` 和 `ClientSource` 的文件、归属和哈希。

MDT 消费 `.sdkcontract.json` 和各目标的 v4 `UMF.GeneratedArtifacts` 生成清单。它保留原有的对象 Getter 和 float 方法适配器，并按签名生成支持标量和公开 UE 对象入参的类型化方法。当 SDK 契约无法表示完整 API 集时，标记为 `Unsupported`。

在 Win64 x64 MSVC 平台上，UBT 适配层自动准备输入，MBT 则在目标链接后核对实际产物。生成的 SDK 支持通过活跃的 `UModInstance` 获取已授权的宿主蓝图 World 提供者，LocalPlayer 等其他作用域由游戏接入层交付。独立原生回调通过运行时在 World 准备期间接入。详见 [UHT 接入](../../Source/ModRuntimeUht/README.zh-CN.md) 与[独立扩展打包](../ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#独立原生回调)。
