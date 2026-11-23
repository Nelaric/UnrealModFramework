<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# MHT 客户端 ABI 生成

MHT 在 UE 视图后端之外生成纯 C 的 `ContextHandlesV1` 边界。全部生成实现由 MBT 独立编译，UBT 消费声明并链接所属模块的宿主库。

## 连接产物

以下路径均相对固定的 MHT 输出根，每次生成覆盖前一次：

| 产物 | 用途 |
| --- | --- |
| `SDK/Public/<game>.modabi.h` | 纯 C 声明、定宽类型、状态码、ABI 版本/哈希常量和类型化宿主函数表 |
| `Host/<owner>/<owner>.modabi.cpp` | 宿主 C 入口、精确成员调用、检查和导出的函数表查询入口 |
| `Client/<game>ModClient/<game>ModClient.modclient.cpp` | 独立客户端入口、兼容性校验、Connect、Disconnect 和 ReleaseObject |
| `Contract/<game>.clientabi.json` | `UMF.ClientABI` v2 连接描述，包含所属模块、函数表、查询/连接符号、表槽、签名、哈希与生命周期规则 |
| `Contract/<game>.sdkcontract.json` | 供 ModSDKTool（MDT）在已支持的适配图中使用的 `UMF.SdkContract` v2 |
| `<game>.generated-artifacts.json` | v3 完整清单，明确列出 Host/Client 编译单元及必需符号绑定 |

连接描述遵循 [client-abi.schema.json](Protocol/client-abi.schema.json)。客户端库所属模块为 `<game>ModClient`，拥有独立 MBT 生产者上下文、库路径、注册锚点和回执；不链接游戏私有代码或 UE。Win64 适配层从真实选中工具链的配方派生客户端参数，移除游戏包含目录和强制 UE 定义。

`.modviews.h` 和 `.modcalls.h` 的角色为宿主私有 `HostHeader`。公共 `SdkHeader` 包含 `.modabi.h`，并按契约发布错误目录及独立扩展 ABI 头。MDT 只复制具备公共角色的校验产物。

## 明确连接顺序

1. 根据描述中的 `ownerModule` 定位实际所属二进制，解析 `querySymbol`，使用生成的 ABI 版本、`sizeof(table)` 和哈希查询函数表。
2. 调用对应客户端的 `connectSymbol`。客户端先检查版本、结构大小、哈希和全部必需回调，再绑定函数指针。
3. 游戏批准调用者、作用域及权限。`UModAPIHostSubsystem::OpenLocalPlayer` 绑定生成的 LocalPlayer 提供者，通过 `Mod::API::FNativeAccess::Open` 签发非零 context/scope token，并在 `Native` 字段返回。
4. 将 token 交给消费者。Getter 接收 context/scope，返回 object token。成员方法接收 context/object 和生成签名定义的入参。宿主每次调用检查线程、调用者生命周期、类型、作用域与权限。
5. 模组停止时撤销对应 Caller；关闭作用域或 Registry 同样使 token 失效。ReleaseObject 显式使该 token 的所有副本失效，不拥有或销毁实际 UObject。重复投影复用仍然有效的绑定。
6. 卸载宿主或 SDK 模块前，先 Disconnect 并等待调用结束。Disconnect 清空函数指针，不等待已进入的调用，也不撤销共享该客户端模块的其他调用者。

连接表按模块共享，身份、作用域、权限和对象 token 按调用者隔离。token 在 Runtime 模块存活期间不重复使用，跨 context 使用会失败。API 分派使用具体类型的函数指针。

ABI 使用显式 `cdecl`、64 位进程内 token、定宽整数/标量和 `uint32_t` 状态码；bool 使用限定为 0/1 的 `uint8_t`。输出内存由调用者提供，失败时输出为零，成功时提交结果。0 表示成功，65536 表示未连接，65537 表示 ABI 不匹配，65538 表示意外原生异常。

`nativeABIHash` 为连接描述加入 `nativeABIHash`、`contractHash`、`projectionHash` 前的规范 SHA-256，使用 nlohmann JSON 排序键与紧凑 `dump()`。摘要覆盖 API 形状、连接符号、状态码与生命周期规则；公共头包含同一版本和哈希常量。

## World 扩展产物

- `Host/<owner>/<owner>.modextensions.h` 提供 C++ 接入点使用的类型化 `Invoke`／`Publish` 辅助函数。生成的宿主桥登记 C++ 与蓝图扩展定义，并实现声明了 `HostEndpoint` 的 C++ 成员函数。
- `Contract/<game>.sdkcontract.json` 的 `extensions` 记录扩展 ID、签名、授权、阶段或发布规则及载荷投影。打包器以该契约为依据校验 v2 扩展清单。
- 有生命周期源码和扩展定义时，`SDK/Public/ModExtensionABI.h` 与 `SDK/Public/<game>.modextensionsabi.h` 提供独立 DLL 的回调契约、ID、签名和线格式。它与生成 API 的 `ContextHandlesV1` 调用表配合使用。

Hook 接收一个载荷并返回同类型结果。Event 的发布函数接收一个载荷并返回 `void`。支持 `float`、`double`、`int32`、`int64`、`bool`、`uint8`，以及受支持的反射值结构体或具备生成 World 视图的 UObject。结构体只复制声明的公开字段，对象投影在回调期间只读。蓝图的标量 API 及已连线的 Hook/Event 来自 Editor 快照。[宿主导出步骤](../../Source/ModRuntimeUht/README.zh-CN.md)

无参 `MOD_EXPORT_HOOK()` 为有 `.cpp` 定义的标量成员启用 Win64 x64 原型。默认 ID 由 World 类型 ID 和带连字符的成员名组成，例如 `hook/game.character/change-health`。`Pre` 支持 `ModifyInputs` 和 `Cancel`，`On` 支持 `Replace`，`Post` 观察结果。基础授权使用 Hook ID，写操作授权在该 ID 后加操作后缀。MHT 与 MST 生成 `Diagnostics/HookSources.json`，Win64 构建驱动据此编译对应源码副本。[Win64 构建驱动](../../Source/ModRuntimeUht/README.zh-CN.md#win64-自动-hook-原型)

### 生成 C++ 成员函数体

将 `HostEndpoint` 设为 `Mod::Extension::FEndpoint` 成员的名称，即可在 `<owner>.modhost.cpp` 中生成声明对应的函数定义。生成的 Event 函数体将参数转发给 `Publish`。游戏也可以自行定义成员函数并调用辅助函数。

生成类型为 `T` 的标量 Hook 时，`HostCore` 指定签名为 `T(T)` 的业务函数，`HostReadState` 指定签名为 `T()` 的状态读取函数，`HostValidateInput` 和 `HostValidateResult` 指定签名为 `bool(T)` 的校验函数。生成的函数在调用 Hook 前读取状态，核心操作使状态变化时标记为已提交。Hook 报错时，函数记录错误并返回当前状态。

`HostPublishOnCommit` 接受同一类中带引号的 Event 成员名列表。列表内的 Event 需要声明 `HostEndpoint` 和 `HostCommitPayload`。载荷函数接收操作前的状态并返回该事件的载荷。核心操作提交后，生成的函数按声明顺序构造并发布载荷，即使随后 Post Hook 报错也会发布。显式生成成员函数时，`MOD_API_BOOTSTRAP=1` 的构建使用游戏源码里的兜底定义。

## API 兼容名

显式 `MOD_EXPORT_API` 由所属类型的 ID 和方法名生成 ID，例如 `api/game.character/increasehealth`。填写 `CompatibleNames = ("AddHealth")` 后，MHT 为同一实现再导出 `api/game.character/addhealth`。SDK 中的两个名字使用相同的参数、返回类型和调用策略。

省略 `CompatibleNames` 时，MHT 使用 UE 从 Engine 配置及启用插件配置解析出的 `FunctionRedirects`。同一类内指向已导出宿主 `UFUNCTION` 的精确重定向会贡献一个旧名。函数名可能重复时，用反射类名和模块限定重定向。连续改名会导出链上的各个旧名。显式填写 `CompatibleNames = ()` 表示兼容名列表为空。

MHT 会在宏声明或 INI 条目处报告非法名称、名称冲突和适用范围内的错误重定向。UBT 构建会自动刷新默认配置快照。独立运行 MHT 时，修改重定向后用 `ModExportOptions` 刷新快照。宿主和 SDK 使用同一份生成契约。

## FullView 与对象输入

`MOD_EXPORT_API` 的可选字段 `FullView` 接受带引号的 C++ 类名列表，例如 `FullView = ("USkeletalMesh")`。每个名字必须匹配该签名中的 UObject 指针入参。MHT 按类合并选择，从目标编译中收集类和公开基类的具体公开实例方法。显式导出方法使用自己的契约。自动生成成员要求可写绑定、所有选择声明的权限，以及宿主授予的 `FullView.<TypeId>` 权限。任何成员缺少投影时，生成失败并报告具体成员。成员涉及的其他类按其普通投影规则处理。

UE SDK 的调用者传入真实 UObject 指针。ContextHandlesV1 对每个此类参数使用 `const char*` 和 `uint32_t`，分别传入借用的 UTF-8 对象路径和长度，最大 4,096 字节。空对象使用空指针和零长度。宿主解析已加载的对象，验证类和 World，并在同步调用期间借用该对象。对象结果以宿主签发的视图 token 返回。公开引擎类的头文件从构建输入的 `Engine` 路径根解析。

## MBT 与 MDT 接续

每个宿主模块有桥、ABI 入口、校验、UE 视图实现四个编译单元；独立客户端另有一个单元。清单 `compilationUnits` 逐项列出。`requiredSymbols` 同时记录宿主和客户端入口，关联相同 ExportId、签名与投影哈希；ExportId 唯一性按库所属模块检查。查询、连接、断开和释放函数同样是必需符号，宿主注册锚点确保 ABI 对象参与最终链接。

UHT 适配器执行 `generate <GenerationRequest.json>`。请求格式为 `UMF.ModHeaderGenerationRequest` v1，必填字段为 `buildInput`、`outputDirectory` 和 `hostContexts`。上下文集覆盖各宿主模块和独立客户端。请求还可以携带当前 Target 的 `metadataOptions`、`libclang` 和 `lifecycleSource` 路径。相对路径从请求所在目录解析。生成完成后，UHT 调用 MBT 构建清单中的库。

MDT 消费 `.sdkcontract.json`、Host/Client 上下文及同一份 v3 清单。它保留原有对象 Getter 和 float 方法适配器，并按签名生成支持标量和公开 UE 对象入参的类型化方法。SDK 契约无法表示完整 API 集时标记为 `Unsupported`。

Win64 x64 MSVC 的 UBT 接入自动准备输入，MBT 在目标链接后核对实际产物。生成的 SDK 支持由 `UModInstance` 取得已授权的宿主蓝图 World 提供者，LocalPlayer 等作用域由游戏接入层交付。独立扩展通过运行时在 World 准备期间接入。[UHT 接入](../../Source/ModRuntimeUht/README.zh-CN.md) · [独立扩展打包](../ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#独立原生回调)
