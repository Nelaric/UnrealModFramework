<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 模组开发 SDK 编辑器

启用 `ModDevelopmentSDKEditor` 后，关卡编辑器主工具栏会在“平台”区域旁显示 **Mod** 下拉菜单。选择 **打包 Mod…**，在详情面板中选定内容插件或当前工程，填写版本号、目标游戏和发布参数，然后点击 **开始打包** 即可开始构建。

运行 `ModSDKTool build-editor` 通过独立的 UE 5.6 项目 Editor 目标构建制作插件。将产物安装到作者工程的 `Plugins` 目录，并在该工程中启用。制作模块独立于游戏侧的 `ModRuntime` 模块。

## 打包输入

- 已在工程中启用的内容插件，须满足 `CanContainContent` 为 `true`、不含 C++ 模块、至少包含一个 `.uasset` 或 `.umap`，并使用独立的 `/<插件名>/` 包根；如果是 C++ 工程，请选择 **打包当前工程内容**。
- 三段式数字形式的 Mod 版本号，例如 `1.0.0`。
- 来自目标游戏 SDK 的游戏标识和基础发布版本。基础发布目录中应包含 `<基础发布版本>/<Cook 平台>/AssetRegistry.bin`；若目录留空，则默认使用作者工程的 `Releases` 目录。`Win64` 对应的 Cook 平台名为 `Windows`。
- 与目标游戏发布保持一致的目标平台、烘焙配置（Development 或 Shipping）和 Pak/IoStore 选项。供 ModLoader 加载的包应关闭“允许新增引擎内容”并使用独立包根；作者工程还必须具备对应平台的 UE Cooker、目标 SDK 以及 Mod 实际引用的公开类型和资产。
- 输出目录，留空时默认使用 `Saved/PackagedMods`。输出文件命名为 `<插件名>-<Mod版本>-<目标平台>.mod`。指定架构时，在 `.mod` 前加入 `-<目标架构>`。输出文件一经生成即不可覆盖。

以上选项同样可在 **项目设置 → 插件 → Mod 打包** 中调整，并保存在本地项目的 EditorPerProjectUserSettings 中。

## 打包当前工程内容

1. 获取目标游戏的基础发布和 SDK。在作者工程勾选 **打包当前工程内容**，选择与发行匹配的平台、烘焙配置和容器后端。
2. 使用 SDK 提供的 C 头文件将原生回调编译成独立的 C 或 C++ 共享库。为 Pak 发行版打包时，选择共享库、目标架构、版本 2 扩展清单和游戏 SDK 契约。
3. 打包器将工程资产由 `/Game` 迁移到 `/<工程名>/`，再执行 DLC Cook。使用资产引用可随迁移自动重映射。蓝图使用的原生父类、函数及反射类型须由游戏及其作者 SDK 提供。
4. 将生成的 `.mod` 放入实际游戏可执行文件同级的 `Mods` 目录。Modular 和 Monolithic 在发行及 SDK 身份匹配时使用相同的包和注册协议。

在 Windows PIE 中验证时，使用 `-UsePaks` 启动宿主 Editor，并将包放入宿主工程的 `Binaries/Win64/Mods`。带渲染的 PIE 如果使用共享着色器，需要将基础发布 Cook 产出的匹配 `ShaderArchive-*.ushaderbytecode` 文件放在宿主工程的 `Content` 下。用 `-NullRHI` 可在无渲染模式验证生命周期回调。

## 运行行为

开始打包前，编辑器会提示保存未保存的资产，随后启动独立的 `UnrealEditor-Cmd` 任务。该进程直接调用当前引擎附带的原生 dotnet 程序运行 `AutomationTool.dll BuildCookRun`（不通过 Shell 拼接命令），以执行完整的 DLC Cook/Stage。每次任务均使用独有的 Cook/Stage 目录，不复用旧任务的载荷。

任务仅收集所选 Mod 的 Stage 产物，保留 UE 生成的容器文件名、路径和 IoStore 分片，并连同生成的 CookReceipt 写入临时 `.mod` 归档。基础发布及工程/插件配置会在 Cook 前后接受核对；所有载荷都会通过流式 SHA-256 校验，待临时归档重新读取并完整复核后，才会提交为最终文件。

打包期间编辑器保持响应，显示进度与完成/失败通知，并将日志实时写入 Output Log。可通过 **Mod → 取消打包** 终止任务进程树（超时或插件卸载时也会自动终止），也可通过 **打开输出目录** 快速定位产物。任务请求、日志与结果均保留在 `Saved/ModPackaging/<任务ID>/`。如果进程异常中断，可能会残留 `.partial` 文件，该文件并非完整的包。

也可使用相同的版本化请求执行无界面打包：

```text
UnrealEditor-Cmd <作者工程.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

请求格式为 `UMF.ModPackageBuildRequest` v1，包含 `pluginName`、`modVersion`、`gameId`、`baseReleaseVersion`、`baseReleaseDirectory`、`targetPlatform`、`outputFile`、`useIoStore`、`includeEngineContent` 和 `timeoutMinutes`。原生打包还需提供 `targetArchitecture`、`nativeExtensionFile`、`extensionsFile` 和 `sdkContractFile`。编辑器会将生成的请求保存在任务目录。其中的路径均相对于作者工程解析；请求所在目录将作为本次任务的工作目录，且不能预先存在 `Stage` 子目录。

打包工程内容时使用请求 `schemaVersion: 3`，省略 `pluginName`，并填写 `cookConfiguration`。版本 1 内容插件请求也可填写 `cookConfiguration`，默认值为 Shipping。每次运行使用全新的任务目录。

## `.mod` 布局

所有整数均采用无符号小端编码，绝不直接序列化 C++ 结构体内存。外层归档不压缩、不签名。上限为单文件 64 GiB、清单 1 MiB、索引 64 MiB、条目数 65,536。文件布局依次为 Header、UTF-8 Manifest JSON、连续载荷和 UTF-8 Index JSON。

| Header 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 8 | `UMFMOD` 后跟两个零字节 |
| 8 | 2 | 主版本（内容包为 `1`，带扩展清单的包为 `3`） |
| 10 | 2 | 次版本（`0`） |
| 12 | 4 | Header 字节数（`64`） |
| 16 | 8 | Manifest 偏移（`64`） |
| 24 | 8 | Manifest 字节数 |
| 32 | 8 | Index 偏移 |
| 40 | 8 | Index 字节数 |
| 48 | 8 | 文件总字节数 |
| 56 | 8 | Flags（必须为 `0`） |

在打包设置中选择 **Mod 扩展清单**，或在打包请求中填写 `extensionsFile`。带扩展清单的包使用归档 v3。扩展清单支持 `schemaVersion` 为 `1` 或 `2`；使用版本 2 时，须在打包设置中指定目标游戏发布的 `SdkContract.json`，或在请求中填写 `sdkContractFile`。打包器会自动补全缺省的 `signature`，并拒绝与 SDK 不一致的值。清单支持按需配置以下可选数组：

- `contracts`：每项包含生成的 Hook/Event `id`，以及填入 `signature` 字段的 SDK `signatureHash`。
- `dependencies`：每项包含 `modId`，以及可选的 `minimum`（包含下界）、`before`（排除上界）和 `required`（默认为 `true`）。
- `services`：每项包含服务 `id`、提供者 Mod ID（`provider`）、生成的 `typeId` 与调用表 `signature`，并支持相同的可选版本范围和必需性字段。
- `content`：每项包含 `slot`、`id`、`action`（支持 `provide`、`replace` 或 `disable`）以及可选的 `priority`。`provide` 和 `replace` 要求 `asset` 指向本包内的软对象路径。
- `patches`：每项包含 `id`、`anchor` 和一种效果，可通过 `actorClass` 生成 Actor。版本 2 还支持用 `componentClass` 在宿主开放的挂接点添加场景组件，或用 `dataId` 与 `dataAsset` 注册已 Cook 的数据资产。可选字段包括 `conflictKey`、`required` 以及同一锚点下的 `after`（Patch ID 列表）。版本 2 额外支持通过 `requiresServices` 引用本包已声明的服务依赖，以及通过 `waitSeconds` 为可选 Patch 的首次锚点设置等待时间（大于 0 且不超过 3600 秒）。
- `provides`（版本 2）：每项包含服务 `id`、`typeId`、`version` 以及可选的 `signature`、`permission` 和 `patch`。指定 `patch` 时，框架会将服务绑定到对应 Patch 生成的 Actor 上；通过注册回调提供的服务则必须与清单声明一致。
- `incompatible` 与 `requiredGrants`（版本 2）：`incompatible` 列出互斥的 `modId`，`requiredGrants` 列出挂载前所需的权限 `id`，宿主会在挂载前统一检查。

在 `UModInstance` 的 **On Mod Register** 事件中，调用 **Register Mod Extension** 并传入 SDK 生成的 ID 即可完成注册。观察函数接收与 SDK 一致的标量；修改函数接收并返回同类型标量；取消函数接收该标量并返回 Bool，其取消值由注册节点的 Float 参数指定，应选用可精确表示的数值。发布规则 `Immediate` 会同步调用监听器，`NextTick` 则将标量复制到当前 World 的有界队列中。返回的订阅令牌可用于退订；当宿主为通道开启立即退订时，本轮发布会跳过尚未执行到的已退订回调。跨 Mod 服务由提供者在 **On Mod Register** 中通过 **Provide Mod Service** 节点或 C++ 的 `Provide` 方法登记，需传入实现宿主 World 接口的服务对象、SDK 类型契约中的 `id` 与 `serviceSignature`，以及服务版本。消费者可在 **On Mod Bind** 中使用 SDK 生成的 **Acquire Service** 节点获取同一类型化 API 对应的服务引用。

值结构体载荷会复制公开的标量字段及受支持的嵌套结构体。在独立制作工程中，蓝图回调接收 SDK 按扩展 ID 生成的值结构体，并在 **On Mod Register** 中完成订阅；结构体修改回调返回同类型结构体，取消回调则返回完整的取消结果。UObject 回调接收 `FModPayloadObject`，并通过生成的 `ProjectPayload_<View>` 节点读取只读视图，视图的有效期仅持续至本次回调返回。

### 独立原生回调

包含 SDK 提供的 `ModExtensionABI.h` 和 `<GameId>.modextensionsabi.h`，并使用 `UMF_EXTENSION_EXPORT` 与 `UMF_EXTENSION_CALL` 导出 `UMF_RegisterExtensionsV1`。注册前先校验宿主表版本和结构体大小，再使用生成头文件中的精确 ID 和签名注册回调。将同一契约加入包的 schema 2 扩展清单，通过生成的宿主函数表调用游戏 API。UObject 和 UE C++ 类由宿主管理。

在作者 Editor 选择目标平台和架构，或在构建请求中填写 `targetPlatform` 和 `targetArchitecture`。用 `nativeExtensionFile` 指定已编译的共享库，并选择 Pak。目标宿主在 `[ModLoader]` 中启用 `bAllowNativeExtensions=True`。每个包只包含一份对应目标的共享库。

| 目标平台 | 架构 | 共享库 |
| --- | --- | --- |
| `Win64` | `x64` 或 `arm64` | PE `.dll` |
| `Linux` | `x64` | ELF `.so` |
| `LinuxArm64` | `arm64` | ELF `.so` |
| `Mac` | `x64` 或 `arm64` | 单架构 Mach-O `.dylib` |

归档清单和 Cook 回执都会记录目标架构。打包器和宿主分别核对库的二进制格式及 CPU。旧 Win64 原生包省略架构时按 x64 处理。其他目标的原生打包请求必须显式填写架构。

将私有原生依赖静态链接进扩展。Windows 允许直接或延迟导入系统 DLL。Linux 允许受支持的 glibc 和 GNU 运行库，并按其版本要求核对未定义符号。请保留动态符号表，并使用 `-Wl,-z,defs` 链接。macOS 使用双层命名空间，依赖库必须位于 `/usr/lib/` 或 `/System/Library/`。使用 `-Wl,-undefined,error` 链接，并按宿主的库验证策略为 dylib 签名。[Apple Hardened Runtime](https://developer.apple.com/documentation/security/hardened-runtime)

- 注册入口会在 Game Thread 上为每个 World 调用一次，由宿主签发会话和独占注册令牌。可选的 `Close` 回调会在所有在途调用排空之后、共享库引用释放之前执行。扩展必须在 `Close` 返回前结束自身派发的所有后台工作。
- 输入、输出、对象和续调令牌仅在当前回调期间有效。`Wrap` 须使用生成头文件中声明的精确大小调用一次 `Next`。`Replace` 与 `Wrap` 返回 `UMF_ExtensionWrite` 及完整结果；在获得授权的前提下，`Pre` 可返回 `UMF_ExtensionStop` 及取消结果。
- 结构体线格式采用生成 C 头文件中定义的无填充小端布局，布尔值占 1 字节，对象值以 64 位不透明句柄表示。回调可通过 `FindHostQuery` 接通生成的客户端 API，并使用 `ObjectContext` 与句柄调用只读方法。

### 清单、索引与载荷

Manifest 格式为 `UMF.ModPackage`，其版本与 Header 主版本一致，用于记录 Mod 标识、版本、插件与包根、构建配置及基础发布指纹。Index 的 `entries` 按区分大小写的路径升序排列，每项记录 `path`、`offset`、`size`、`storage: "None"` 及精确的载荷 `sha256`。条目路径不得为绝对路径、包含目录穿越或存在仅大小写不同的重复路径。

当作者 SDK 提供 `ModInstance` 和 `ModInstanceSubSystem` 时，打包器会将所选内容来源中的具体派生蓝图类写入可选的 `lifecycleClasses` 字段，游戏在挂载后依据这些类路径创建会话入口。

载荷中的 `Content/<Stage相对路径>` 保留 UE 原生 Cook 产物，`Metadata/CookReceipt.json` 记录资源包根、Stage 路径、构建配置及发布来源信息。结果格式为 `UMF.ModPackageBuildResult` v1，状态 `Packaged` 表示打包与字节级校验已完成，并附带最终文件的 SHA-256。

携带 UE 模块的旧包（归档 v2 或含 `nativeCode` 字段）需要迁移。将动态代码迁移到 `UMF_RegisterExtensionsV1`，并重新打包资产和独立共享库。`Native/` 存放唯一已声明的共享库，归档索引通过 SHA-256 校验其内容。

Manifest 与构建结果均记录 `shippingValidated: false`。当打包器依据提供的 SDK 契约校验过扩展清单时，Manifest 的 `exportRequirements` 为 `SdkVerified`，否则为 `NotVerified`。宿主在准入挂载时仍会核对实际的宿主扩展契约与授予权限。
