<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# 模组开发 SDK 编辑器

在独立 Mod 工程中启用 `ModDevelopmentSDKEditor` 后，关卡编辑器主工具栏会在“平台”按钮旁新增 **Mod** 下拉菜单。点击 **打包 Mod…**，在弹出的设置面板中选择要打包的内容插件或当前工程，填好版本号、目标游戏标识及基础发行版参数，点击 **开始打包** 即可生成 `.mod` 文件。

运行 `ModSDKTool build-editor` 可释放内嵌源码并通过 UE 5.6 UAT `BuildPlugin` 独立编译出该插件；将其安装到 Mod 工程的 `Plugins/` 目录（或对应引擎的 `Engine/Plugins/` 目录）并启用即可使用。`DevelopmentEditor` 在构建期与游戏侧 `ModRuntime` 模块完全解耦。若你的 Mod 需要调用游戏导出的 API 或继承 `UModInstance` 生命周期类，请同时安装并启用同批 SDK 发布包中 `Plugins/<游戏 SDK>` 下的作者版插件（游戏发行方则会在构建基础发行版前，把配套的 `Host/Plugins/<游戏 SDK>` 安装到宿主工程）。

## 打包输入参数

- **打包对象**：工程内已启用的纯内容插件（要求 `CanContainContent: true`、不含 C++ 模块、至少包含一个 `.uasset` 或 `.umap`，且拥有独立的 `/<插件名>/` 包根）；若要连同 C++ 代码一起打包，请勾选 **打包当前工程**。
- **Mod 版本号**：三段式纯数字语义版本号，例如 `1.0.0`。
- **目标游戏与基础发行版**：由目标游戏 SDK 公布的 `GameId` 与基础发行版本号。基础发行版目录内必须存在 `<基础发行版本>/<Cook 平台>/AssetRegistry.bin`（留空时默认读取当前 Mod 工程下的 `Releases` 目录；注意 `Win64` 目标对应的 Cook 平台目录名为 `Windows`）。
- **目标平台与容器后端**：与游戏基础发行版一致的目标平台及 `Pak` / `IoStore` 选项。供 `ModLoader` 加载的归档应保持关闭“允许包含引擎内容”并使用独立包根；同时请确认 Mod 工程所在环境已安装目标平台的 UE Cooker、平台 SDK 以及 Mod 实际引用的公开类型或资产。
- **输出目录**：留空时默认输出至 `Saved/PackagedMods`，文件命名为 `<插件名>-<Mod版本>-<目标平台>.mod`，且绝不会覆盖已存在的同名文件。

上述配置同样可以在 **项目设置 → 插件 → Mod 打包** 中查看与修改，并按本地工程保存在 `EditorPerProjectUserSettings` 中。

## 打包当前 C++ 工程

1. **准备模块化宿主与 Pak 基础发行版**：在宿主游戏工程中启用 `Host/Plugins/<游戏 SDK>`，编译 Win64 x64 模块化宿主并导出 Pak 基础发行版。以本仓库开发宿主为例，请使用支持产出模块化 Game DLL 与导入库的 UE 5.6 源码版引擎，编译 `UnrealModFrameworkDevModular` 目标，并保留该次构建生成的 UBT `.target` 回执文件。
2. **填写 Mod 工程打包配置**：打开 Mod 工程，勾选 **打包当前工程**，目标平台选 `Win64`，取消勾选 **使用 IoStore**，并在 **宿主 Target 回执** 中填入上一步的 `.target` 文件路径（若宿主与当前编辑器使用的不是同一个引擎安装目录，还需填写 **宿主 Engine 目录**，并保证两边引擎的版本号与 Changelist 完全一致）。回执所引用的宿主工程与 `.modules` 模块清单在磁盘上必须可读。
3. **自动迁移与编译**：打包器会读取当前 `.uproject` 中的 Runtime 模块与已启用插件依赖，对照宿主 Target 回执编译临时插件，并将工程资产自动迁移至该 Mod 独享的 `/<工程名>/` 包根下执行 DLC Cook——同时保留对共享游戏 SDK 插件的依赖，并维持 Mod 自身模块的 `/Script/` 路径不变，确保 Cooked 蓝图能正常解析其 C++ 父类。
4. **放入宿主加载**：在宿主 `Config/DefaultGame.ini` 的 `[ModLoader]` 节开启 `bAllowNativeModules=True`，配好一致的基础发行版信息与 `Backend=Pak`，再把生成的 `.mod` 放到游戏可执行文件同级的 `Mods/` 目录下。加载器会在实例化生命周期蓝图之前先加载并注册包内 DLL。

传入 Editor `.target` 回执打出的包专供 Windows PIE 调试，传入 Game `.target` 回执打出的包则用于独立打包后的游戏宿主。在示例工程 `TestMod` 中，`/TestMod/BP_CallbackProbe.BP_CallbackProbe_C` 派生自工程内的 C++ `UModInstance` 子类，加载完成回调会向 `LogTestModCallback` 输出日志。请注意：C++ 代码里若有硬编码的资产路径，应直接写迁移后的 `/<Mod名>/...` 包路径，或改用可随资产迁移自动重映射的软/硬引用属性。

### 在 Windows PIE 中验证 Pak 模组

1. 在宿主工程的 **项目设置 → 引擎 → Cooker** 中勾选 **Allow Cooked Content in the Editor**，并将面向 Editor 目标打包的 `.mod` 放入宿主工程的 `Binaries/Win64/Mods`。
2. 如需在编辑器启动时挂载 Pak，请附带 `-UsePaks` 启动参数；若加载的 Cook 资产不带版本信息，还需在宿主 `Config/DefaultEngine.ini` 的 `[ConsoleVariables]` 下设置 `s.AllowUnversionedContentInEditor=1`。
3. 若包内含有共享着色器库（Shader Archive），开启渲染的 PIE 还要求宿主工程 `Content/` 目录下存在基础发行版 Cook 产出的 `ShaderArchive-*.ushaderbytecode` 且着色器平台一致；若只需验证非渲染的生命周期回调，可直接加 `-NullRHI` 启动。

## 运行行为

点击开始打包后，编辑器会先提示保存未落盘的修改，随后在外部启动独立的 `UnrealEditor-Cmd` 进程。该进程直接调用当前引擎内置的原生 `dotnet` 执行 `AutomationTool.dll BuildCookRun`（不经由任何 Shell 字符串拼接），在每次任务独享的全新目录中完成完整的 DLC Cook 与 Stage，绝不混用历史任务的残留产物。

Stage 结束后，打包器只提取当前 Mod 自身的容器产物（完整保留 UE 容器文件名、相对路径及 IoStore 分片结构），连同生成的 `Metadata/CookReceipt.json` 一并写入临时归档。Cook 前后会双重核验基础发行版与工程配置，所有载荷均流式计算 SHA-256 摘要，且临时归档落盘后会完整重读复核一遍字节内容，确认无误后才正式改名为最终 `.mod` 文件。

整个打包期间编辑器主界面始终保持流畅响应，实时向 Output Log 输出日志并弹出进度与结果通知；你也可以随时通过 **Mod → 取消打包** 终止整个任务进程树（任务超时或编辑器关闭时同样会自动清理），或点击 **打开输出目录** 直达产物文件夹。每次任务的请求参数、完整日志与结果文件均归档在 `Saved/ModPackaging/<任务ID>/` 下（若进程意外中断，目录中可能留下未完成的 `.partial` 临时文件，它不会被视作合法的 `.mod` 包）。

### 无界面命令行打包（Headless）

自动化流水线可直接调用 `ModPackage` Commandlet 传入同款版本化请求 JSON：

```text
UnrealEditor-Cmd <作者工程.uproject> -run=ModPackage -Request=<Request.json> -unattended -NullRHI
```

- **打包内容插件（`schemaVersion: 1`）**：`UMF.ModPackageBuildRequest` 包含 `pluginName`、`modVersion`、`gameId`、`baseReleaseVersion`、`baseReleaseDirectory`、`targetPlatform`、`outputFile`、`useIoStore`、`includeEngineContent` 与 `timeoutMinutes`（可直接打开任意一次编辑器已完成任务的目录参考生成的 `Request.json`）。请求中的相对路径基于 `.uproject` 目录解析；`Request.json` 所在的目录即作为本次任务的工作目录，运行前不得已存在 `Stage/` 子目录。
- **打包当前 C++ 工程（`schemaVersion: 2`）**：将 `schemaVersion` 设为 `2`，用 `hostTargetReceipt`（及按需填写的 `hostEngineDirectory`）替换 `pluginName`，并确保每次打包都在全新的空任务目录中启动。

## `.mod` 布局

归档内所有整数统一采用显式的无符号小端序编码，绝不直接拷贝 C++ 结构体内存布局。当前外层容器不压缩、不签名，硬性上限为单文件 **64 GiB**、Manifest **1 MiB**、Index **64 MiB**、载荷条目 **65,536** 项。文件按顺序由 64 字节 Header、UTF-8 Manifest JSON、连续二进制载荷区和末尾 UTF-8 Index JSON 组成：

| Header 字节偏移 | 字节长度 | 字段说明 |
| --- | --- | --- |
| `0` | `8` | 魔数 `UMFMOD` 加两个零字节（`UMFMOD\0\0`） |
| `8` | `2` | 主版本号（纯内容包为 `1`，含 UE 原生模块的包为 `2`，带扩展清单的包为 `3`） |
| `10` | `2` | 次版本号（固定为 `0`） |
| `12` | `4` | Header 字节长度（固定为 `64`） |
| `16` | `8` | Manifest 起始偏移（固定为 `64`） |
| `24` | `8` | Manifest 字节长度 |
| `32` | `8` | Index 起始偏移 |
| `40` | `8` | Index 字节长度 |
| `48` | `8` | 归档文件总字节数 |
| `56` | `8` | 标志位 `Flags`（固定为 `0`） |

### 扩展清单与 World 注册绑定

在打包设置中指定 **Mod 扩展清单**（或在请求 JSON 中填写 `extensionsFile`）即可打出归档 **v3** 包。扩展清单支持 `schemaVersion` 为 `1` 或 `2`；使用版本 `2` 时，必须同时提供目标游戏发布的 `Contract/SdkContract.json`（请求字段为 `sdkContractFile`），打包器会自动补齐省略的 `signature` 哈希，并拦截任何与 SDK 不符的签名。扩展清单支持按需声明以下可选数组：

- `contracts`：要订阅的 Hook/Event 列表，每项包含生成的扩展 `id` 以及对应 SDK `signatureHash` 的 `signature`。
- `dependencies`：前置 Mod 依赖列表，每项包含 `modId`，以及可选的下界 `minimum`（含）、上界 `before`（不含）和 `required`（默认 `true`）。
- `services`：跨 Mod 服务消费声明，每项包含服务 `id`、提供方 `provider`（`modId`）、生成的 `typeId`、调用表 `signature`，以及与依赖一致的版本范围和 `required` 标记。
- `content`：逻辑内容注册项，包含内容槽 `slot`、逻辑 `id`、`action`（`provide`、`replace` 或 `disable`）及可选的 `priority`；其中 `provide` 与 `replace` 必须附带指向本包内资产的软对象路径 `asset`。
- `patches`：World Patch 列表，每项包含 `id`、目标锚点 `anchor` 及一种效果：可通过 `actorClass` 生成 Actor，在 v2 中还可通过 `componentClass` 向宿主开放的挂接点添加场景组件，或通过 `dataId` + `dataAsset` 注册 Cooked 数据资产。可选字段包括互斥键 `conflictKey`、`required`、同锚点拓扑先后顺序 `after`，以及 v2 新增的前置服务依赖 `requiresServices` 和可选 Patch 等待首个锚点出现的超时秒数 `waitSeconds`（取值大于 `0` 且不超过 `3600`）。
- `provides`（*仅 v2*）：跨 Mod 服务提供声明，包含服务 `id`、`typeId`、`version` 以及可选的 `signature`、`permission` 和 `patch`（填 `patch` 时自动绑定到对应 Patch 生成的 Actor 上；通过回调动态提供的服务则须与此处声明严格匹配）。
- `incompatible` 与 `requiredGrants`（*仅 v2*）：分别声明互斥的 `modId` 列表以及挂载前宿主必须授予的权限 `id` 列表。

在 Mod 自身的蓝图或 C++ 代码中，各项 World 能力通过 `UModInstance` 生命周期事件完成挂接：

- **订阅 Hook 与 Event（`On Mod Register`）**：调用 **Register Mod Extension** 并传入 SDK 生成的扩展 ID。只读观察回调接收标量载荷，修改回调接收并返回同类型标量，取消回调接收标量并返回 `Bool`（注册节点上的 `Float` 取消值引脚请填入可精确表示的数值）。投递策略 `Immediate` 会同步执行回调，`NextTick` 则将载荷拷入当前 World 的有界队列延后派发；保存好返回的订阅令牌即可随时退订（若宿主通道开启了立即退订，同一轮广播中尚未执行到的该监听器回调会被当场跳过）。
- **提供与获取跨 Mod 服务**：提供方在 **On Mod Register** 中调用 **Provide Mod Service** 节点（或 C++ `Provide` 方法），传入实现了宿主生成接口的对象、SDK 类型契约中的 `id` 与 `serviceSignature` 以及服务版本号；消费方则在随后的 **On Mod Bind** 中调用生成的 **Acquire Service** 节点拿到强类型引用。
- **值结构体与 `UObject` 载荷**：反射值结构体载荷会自动拷贝公开标量字段与受支持的嵌套结构体，在作者工程蓝图中直接体现为按扩展 ID 命名的 SDK 结构体；`UObject` 载荷回调则接收 `FModPayloadObject`，通过生成的 `ProjectPayload_<View>` 节点读取仅在当次回调返回前有效的只读视图。

### 独立原生回调

若要编写不耦合 UE 模块的纯原生扩展 DLL，只需包含 SDK 中的 `ModExtensionABI.h` 与 `<GameId>.modextensionsabi.h`，使用 `UMF_EXTENSION_EXPORT` 和 `UMF_EXTENSION_CALL` 导出 `UMF_RegisterExtensionsV1`。入口内请先核对宿主函数表的 `Version` 与 `StructSize`，再用生成头文件中的精确 ID 和签名常量注册回调，并同步将这些契约写入包的 v2 扩展清单。打包时在界面选中编译好的 x64 DLL（或在请求中填写 `nativeExtensionFile`），并在目标 Win64 Pak 宿主的 `[ModLoader]` 中开启 `bAllowNativeExtensions=True`：

- `UMF_RegisterExtensionsV1` 会在每个 World 准备期间于 Game Thread 调用一次，由宿主分配会话句柄与独占注册令牌。可选登记的 `Close` 回调会在所有在途调用排空之后、DLL 引用释放之前触发；DLL 自身启动的任何异步或后台任务都必须在 `Close` 返回前彻底收尾。
- 回调收到的输入缓冲区、输出缓冲区、对象句柄及 `Wrap` 续调令牌均只在当前栈帧内有效。`Wrap` 回调须按生成头文件给定的精确字节大小调用一次 `Next`；`Replace` 与 `Wrap` 均返回 `UMF_ExtensionWrite` 并写满完整输出，获授权的 `Pre` 回调可返回 `UMF_ExtensionStop` 并写入取消结果。
- 跨边界结构体采用生成 C 头文件定义的紧凑无填充小端布局（布尔占 `1` 字节，对象以 `64` 位不透明句柄传递）。回调内部还可通过 `FindHostQuery` 接通生成的客户端 API 表，配合 `ObjectContext` 与对象句柄调用宿主开放的只读方法。

### 清单、索引与载荷细节

外层 `UMF.ModPackage` 清单的版本与 Header 主版本一致，完整记录 Mod 标识、版本号、源插件与独立包根、构建选项及基础发行版指纹。末尾 Index 的 `entries` 严格按区分大小写的规范化路径升序排列，逐条记录 `path`、`offset`、`size`、`storage: "None"` 及精确的载荷 `sha256`（直接拒绝绝对路径、`..` 目录穿越及仅大小写不同的重名路径）：

- `lifecycleClasses`：当作者版 SDK 包含 `ModInstance` 与 `ModInstanceSubSystem` 时，打包器会自动扫描所选内容插件并将具体的蓝图子类路径写入该可选字段，供宿主挂载后自动创建会话实例。
- `Content/<Stage相对路径>` 与 `Metadata/CookReceipt.json`：原样保存 UE Cook 容器产物与来源回执；构建结果 `UMF.ModPackageBuildResult` v1 在完成字节级回读复核后将状态记为 `"Packaged"` 并附上整包 `sha256`。
- `nativeCode`（*归档 v2，及携带 UE 模块的归档 v3*）：记录宿主 Target、配置、架构、`BuildId` 与模块列表，编译产出的 `.modules` 清单与 Mod DLL 统一存放在包内 `Native/` 下并纳入哈希索引。共享 SDK Runtime DLL 由宿主游戏统一提供，各 Mod 通过相互隔离的 Caller 上下文调用并在卸载时单独吊销（已加载进内存的 Win64 UE 原生模块会驻留至进程退出，因此更新原生模块版本需重启游戏）。
- 校验状态标记：Manifest 与构建结果均固定记录 `shippingValidated: false`；若打包阶段已对照 `SdkContract.json` 核验过扩展清单，则 `exportRequirements` 记为 `"SdkVerified"`，否则记为 `"NotVerified"`（宿主在运行期挂载准入时仍会再次对照真实宿主契约与权限表做最终审查）。
