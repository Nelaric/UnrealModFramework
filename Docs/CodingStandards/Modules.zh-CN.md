<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Modules.md) | 简体中文

# 模块与依赖边界

本仓库包含开发宿主工程和游戏侧的 `ModRuntime` 插件；独立的 Mod 制作插件（`ModDevelopmentSDKEditor`）则由 ModSDKTool（MDT）从内嵌源码资源生成并编译。每个模块只承担单一明确的职责，只声明自身直接用到的依赖，并将内部实现收纳在 `Private/` 下。公开头文件即对外契约：必须能够单独包含并通过编译，且只有当 `Public/` 头文件确实直接依赖某模块时，才能在 `.Build.cs` 中将其列为 `Public` 依赖。严禁出现循环模块依赖。

Unreal 工程位于 `UnrealModFramework/`，下文列出的所有模块与配置路径均相对于该目录。`ModRuntime` 插件包含 `HAL`、`ExportMetadata`、`ModAPI`、`ModLoader`、`BlueprintIntegration` 和 `ExportCodeGenerator` 六个模块，`ModDevelopmentSDKEditor` 插件包含 `DevelopmentEditor` 模块：

| 职责 | 相对于 `UnrealModFramework/` 的路径 | UE 模块类型 |
| --- | --- | --- |
| 原生文件系统目录变更通知 | `Plugins/ModRuntime/Source/Runtime/Core/HAL/` | `Runtime` |
| 导出元数据 Schema 与配置项读取 | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| 生成的 API 绑定、World 扩展分发、内容槽、Patch、跨 Mod 服务与联机/存档辅助 | `Plugins/ModRuntime/Source/Runtime/ModAPI/` | `Runtime` |
| `.mod` 归档准入校验、解包提取与容器挂载 | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| 蓝图 Mod 导出面板、独立声明模型与 Kismet 编译接入 | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| 导出 GameMod 配置项与蓝图声明快照的 Commandlet | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| 独立 Mod 制作与打包界面及 Commandlet | `Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/Source/DevelopmentEditor/` | `Editor` |
| 开发宿主游戏接入示例 | `Source/ExportTest/` | `Runtime` |

注意：`Plugins/ModRuntime/Source/` 下的 `Runtime` 与 `Editor` 只是组织源码的目录分层，本身并非 UE 模块，真正的模块均逐一登记在 `ModRuntime.uplugin` 中。宿主工程在 `Config/DefaultEngine.ini` 的 `[CoreRedirects]` 中将 `/Script/ModBlueprintIntegration` 重定向至 `/Script/BlueprintIntegration`，以兼容既有蓝图资产中的序列化引用。

### 核心模块与 SDK 边界

- **`ExportMetadata`**：同时参与 Game 与 Editor Target 编译，仅公开依赖 `Core`（用于 UE 字符串与配置数组），严禁依赖任何编辑器模块、特定宿主游戏代码、内容分发平台或 `DevelopmentEditor`。后续新增运行时服务时，应按具体领域拆分为独立命名的模块，不要把无关职责塞进 `ExportMetadata`。
- **`ModAPI`**：面向 Game 与 Editor Target 的 Runtime 模块，公开依赖 `Core`、`CoreUObject`、`Engine` 和 `ModLoader`，私有依赖 `Json`。其中 `UModAPIHostSubsystem` 按 `GameInstance` 管理 API 提供者、调用方授权策略与生成绑定；`UModExtensionSubsystem` 按 `World` 准备和停止扩展，统筹 Hook/Event 快照、内容槽、Patch 锚点、服务租约与玩法指纹。具体的对局准入与存档逻辑由宿主侧（如 `ExportTest`）负责接入。
- **`DevelopmentEditor` 与双端 SDK 变体**：MDT 将内嵌的 `DevelopmentEditor` 源码释放到临时工作区，调用 UAT `BuildPlugin` 独立编译制作插件；游戏侧与制作端之间仅通过版本化 JSON 契约和生成的公开头文件交互（跨进程实时预览目前仍处于设计阶段）。与此同时，UHT 会把生成的宿主 C++ 包装单元直接登记到各声明所属模块中由 UBT 编译；而游戏侧与作者侧两套 SDK 插件共享同一个 `<pluginName>Runtime` 模块名、反射类型及 Client 源码——游戏侧版本依赖真实的 `ModRuntime`，作者侧版本则提供 `/Script/ModLoader` 的公开反射镜像（见[宿主编译与链接](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#宿主编译与链接)与 [SDK 安装说明](https://github.com/Nelaric/UnrealModFramework/blob/main/UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md#安装与验证)）。
- **`ExportCodeGenerator`**：Editor 模块，公开依赖 `ExportMetadata`，私有依赖 `Core`、`CoreUObject`、`Engine`、`Json`、`Projects`、`AssetRegistry`、`BlueprintIntegration`、`ModAPI` 和 `UnrealEd`。其中 `ModExportOptions` 负责导出解析后的 `[ModExportMetadata]` 候选值与 UE 重定向配置，`ModExportBlueprints` 负责编译配置中指定的蓝图资产并导出声明快照；随后由 MHT 结合目标编译数据库的 `libclang` AST 合并生成宿主桥接代码、公开契约、C ABI 头文件及编译期校验单元。
- **UE 接口与原生 C ABI 分离**：面向 UE 模块的 C++ 契约使用标准 UE 类型；而独立原生二进制之间不能假设 `UObject` 或 C++ 标准库内存布局兼容，因此对外 C ABI 一律采用定宽标量、显式字节长度、不透明句柄及文档化的缓冲区归属规则。

每当需要新增模块时，都必须清楚说明其单一职责、直接依赖、适用的 Target 类型，以及为何现有模块不适合承载该功能。可选扩展能力应做成独立模块或插件，切勿仅仅为了补齐某个缺失的 `#include` 而随意拉入模块依赖。

### 蓝图导出接入（`BlueprintIntegration`）

`BlueprintIntegration` 承载 Editor Target 下的蓝图 Mod 导出编辑能力，直接依赖 `Core`、`CoreUObject`、`Engine`、`BlueprintGraph`、`GraphEditor`、`Kismet`、`KismetCompiler`、`ExportMetadata`、`ModAPI`、`Slate`、`SlateCore`、`ToolWidgets`、`UMGEditor` 和 `UnrealEd`，并在引擎初始化完成后加载以便 `Kismet` 注册编辑器服务。`ExportMetadata` 与 `DevelopmentEditor` 均不依赖该模块。其中 `GraphEditor` 与 `ToolWidgets` 提供与原生“我的蓝图”一致的动作列表和添加按钮，`UMGEditor` 则保证在 Widget 蓝图切换编辑模式时标签页不会丢失。

打开蓝图编辑器后，模块会在“我的蓝图”旁停靠 **蓝图Mod导出** 标签页（接口、宏库、Widget 与动画蓝图等不同布局由后备标签页生成器兜底；无法承载运行时实现的蓝图类型会自动禁用新建入口）。面板复用“我的蓝图”的 `SGraphActionMenu`、`API` / `Event` / `Hook` 可折叠分组、圆形添加按钮、搜索框、行内重命名（`F2`）与 `Delete` 删除交互：单击条目即可在原有的“细节”面板编辑属性，双击直接打开对应的实现图，不额外嵌套独立的属性栏。

所有导出项均作为支持撤销/重做的事务对象存储在当前蓝图的 `UBlueprintExtension` 中，完全独立于普通蓝图函数或事件分发器（不支持从旧式普通函数或分发器导入、迁移或修复归属；不兼容的旧声明需重新创建）：

| 声明类型 | 保存的属性字段 | 关联的编辑器图表 / 节点 |
| --- | --- | --- |
| API | 版本、显示名与公开名、说明、作用域、线程、失败规则、权限列表 | 专属的 API 实现图 |
| Event | 版本、显示名、说明、载荷类型、来源作用域、发布与退订规则、权限、网络权威 | 放置在宿主业务执行图中的 **Publish Event** 节点 |
| Hook | 版本、显示名、说明、载荷类型、`AllowedStages`、`AllowedOperations`、权限、网络权威 | 专属的原版逻辑图 + 宿主业务执行图中的 **Hook Point** 节点 |

下拉候选项由 `Mod::ExportMetadata::FOptions` 从 `Config/DefaultGameMod.ini` 的 `[ModExportMetadata]` 节读取（API 使用 `APIScopes`、`APIThreads`、`APIPermissions`、`APIFailureRules`；Event 使用 `EventSourceScopes`、`EventPublishRules`，均通过 `+键=值` 追加）。

规范声明名统一为 `ModAPI_<名称>`、`ModEvent_<名称>` 和 `ModHook_<名称>`。重命名时无论输入不带前缀的基础名还是带对应前缀的完整名，内部都只保存基础名，避免反复编辑导致前缀叠加；空名称、重名、超长名称或非法字符会直接报错拦截，绝不用 GUID 或自动序号凑数。API 与 Hook 的实现图绑定专用的 K2 派生 Schema，且严格校验归属关系（缺失或脱离声明的图表会直接报诊断错误，而不会当作孤立图表打开）。蓝图编译期间，编译器会将规范声明名统一绑定到中间图、入口/返回节点、局部变量作用域与显示元数据上，不在编译过程中篡改源图对象名；用户在编辑器中重命名时，则会在单个撤销事务内同步更新图表名、作用域及已加载的函数引用。此外，已保存蓝图的导出项会自动同步到“在蓝图中查找”和原生“查找引用”索引中。导出快照时，`ModExportBlueprints` 会编译已保存蓝图，并收录执行输入引脚已连线的 **Hook Point** 与 **Publish Event** 声明。

## 模组加载器

`ModLoader` 负责扫描、校验、解包提取并挂载 `.mod` 归档，将加载结果同步给游戏会话。它支持三种归档主版本：**v1**（纯 Cooked 内容包）、**v2**（内容加匹配宿主的 UE 原生模块）和 **v3**（携带扩展清单，可选附带 UE 模块或独立 C ABI 回调 DLL）。其公开依赖为 `Core`、`CoreUObject` 和 `Engine`，私有依赖为 `Json`、`PakFile`、`AssetRegistry`、`HAL`、`RenderCore` 与引擎内置 OpenSSL（Editor 构建还会私有依赖 `UnrealEd` 以管理 PIE 生命周期和包释放）。职责划分上，`ModLoader` 掌管进程级准入与包租约，`ModAPI` 则安装生成的扩展契约校验器并管理 World 级回调与绑定。

### 启动扫描、目录热监听与手动加载

打包后的游戏在启动时默认扫描可执行文件同级 `Mods/` 目录下的顶层 `.mod` 文件（标准 Win64 打包路径为 `<项目>/Binaries/Win64/Mods`）；在 `Config/DefaultGame.ini` 的 `[ModLoader]` 节设置 `bLoadAtStartup=False` 可关闭启动自动扫描。开启启动加载时，`HAL` 模块默认还会在 Win64、Linux 与 macOS 上监听该目录（设 `bWatchForChanges=False` 可关闭热监听）：检测到新文件落盘稳定后，先在后台工作线程完成校验与解包，再切回 Game Thread 提交挂载；若归档声明的 `ModId` 当前已挂载，则直接跳过且不触发重复通知。

在 `.Build.cs` 中依赖了 `ModLoader` 的宿主 C++ 模块，也可在引擎初始化后、对局开始前于 Game Thread 调用 `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` 或 `LoadDirectory(Directory, Policy)` 同步加载，并通过 `OnLoadCompleted()` 订阅完成通知（每次启动或手动操作广播一次，空目录返回空列表，目录级扫描失败返回单条带 `Error` 与 `Detail` 的记录）。

### 宿主配置与准入策略

请在宿主 `Config/DefaultGame.ini` 的 `[ModLoader]` 节填入与打包时一致的发行身份，并指定游戏实际采用的容器后端（`Pak` 或 `IoStore`）：

```ini
[ModLoader]
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<基础发行版 AssetRegistry.bin 的 SHA-256>
Backend=IoStore
PreparationTimeoutSeconds=120
```

- **原生代码开关（Win64 x64 Pak）**：设置 `bAllowNativeModules=True` 允许加载匹配宿主的 UE 原生模块，设置 `bAllowNativeExtensions=True` 允许加载独立的 C ABI 扩展 DLL。
- **整包白名单**：未配置摘要且未显式设置开关时默认关闭。为每个获批的 `.mod` 添加 `+TrustedPackageSha256=<整包 sha256>`（取自打包输出）即可开启校验；显式设置 `bEnforcePackageAllowlist=False` 会绕过已有白名单，设为 `True` 则即便名单为空也会强制拦截所有包。手动加载可通过 `FLoadPolicy` 传入同名策略。
- **启用集合、版本锁定与权限授予**：同一节还支持配置 `+EnabledMods=<ModId>`、`+RequiredMods=<ModId>`、`+PinnedArchives=<ModId>=<整包 SHA-256>` 和 `+ExtensionGrants=<ModId>=<权限 ID>`。将 `bEnableAllMods=False` 与空启用列表搭配即可进入零模组纯净模式。在单局 World 层面，宿主可通过 `FWorldPolicy::Grants` 覆盖某个 Mod 的权限集，未指定时默认继承进程准入授权；同一个 `GameInstance` 内首个完成准备的 World 会冻结所选包集合与授权，供后续切图复用。

### World 准备、多人联机握手与存档校验

调用 `UModExtensionSubsystem::Prepare` 前，宿主应先登记内容槽与 Patch 锚点（需要开放组件挂接或数据资产注册时，分别设置 `FPatchAnchorRules::Attachment` 或 `DataAssetType`）。运行期间，宿主可通过 `ResolvePatchData` 查询锚点数据、通过 `GetPatchOrigin` 反查 Actor 或组件来自哪个 Mod、通过 `SetFailureHandler` 监听 World 异常关闭原因、通过 `GetPackageRevisions` 读取已准入归档的精确哈希，并用 `GetGameplayDigest` 与 `ExpectedGameplayDigest` 在开局前核对权威玩法指纹。

多人联机在 `PreLogin` 阶段统一核对游戏标识、基础发行版、生成契约与模组发行集合：服务端在 UE `Welcome` 前下发地图名与方案指纹，客户端切图前先校验地图，并在 `Join` 前确认本地方案指纹一致。若客户端与专用服务端的基础 `AssetRegistry.bin` 天然不同，可在两端统一配置相同的 `NetworkBaseCompatibilitySha256`；若同一款 Mod 需要为客户端与服务端分发不同二进制但保持同一玩法身份，可按产物配置 `+GameplayReleases=<整包 SHA-256>=<ReleaseId>=<玩法兼容 SHA-256>`。

单机存档方面，`FModExtensionSave` 会在 World 准备阶段核对存档必需的 Mod，并原样保留当前未安装的可选 Mod 扩展数据块。在示例 `ModExtensions` 模式下，控制台命令 `ModWriteSave <槽位>` 与 `ModLoadSave <槽位>` 读写 `Saved/ModSaves`，写入时先发布至新槽位，若迁移失败则完整保留原存档文件。

### 缓存、卸载、PIE 调试与生命周期回调

解包后的载荷缓存在 `Saved/ModCache/<整包 SHA-256>` 下，每次复用前都会重新校验哈希。每个挂载的 Mod 独占自己的 `/<PluginName>/` 包根（同一 `ModId` 同时只能挂载一个版本），并在包含 Cooked `AssetRegistry` 时将其追加到引擎资产索引。从热监听目录移走或删除 `.mod` 源文件会触发平滑卸载：待会话租约与外部对象引用全部释放后，加载器依次卸载容器、释放 Shader 库并注销包根；若仍有残留引用，则记录日志并暂时保留挂载。

在 Windows PIE 下调试时，`ModLoader` 复用同一份 `[ModLoader]` 配置，并扫描宿主工程的 `Binaries/Win64/Mods`：

- 在 **项目设置 → 引擎 → Cooker** 中勾选 **Allow Cooked Content in the Editor**。
- `Backend=Pak` 时加载器会在编辑器启动阶段自动挂载 Pak 平台文件；`Backend=IoStore` 时请带 `-UsePaks -UseIoStore` 参数启动编辑器。
- 若加载无版本信息的 Cook 资产，需在 `Config/DefaultEngine.ini` 的 `[ConsoleVariables]` 下添加 `s.AllowUnversionedContentInEditor=1`。

同一编辑器进程内的多个 PIE 世界共享底层挂载容器，但各自拥有独立的 `UModInstance`。退出 PIE 时，加载器会先停止结果分发，等 PIE 世界销毁后再释放包并卸载无编辑器引用的容器。

MDT 根据宿主导出的生命周期契约生成保留 `/Script/ModLoader` 路径的 `ModInstance` 与 `ModInstanceSubSystem`，打包器则会自动收集内容插件中的具体蓝图子类。挂载完成后，每个活动实例都会收到 **On Mod Load Completed**（C++ 重写 `OnModLoadCompleted` 并调用 `Super::OnModLoadCompleted` 以派发蓝图事件）；而在卸载开始前、包内资产仍然可用时，会在 Game Thread 触发一次 **On Mod Before Unload**（C++ 重写 `OnModBeforeUnload`），供 Mod 还原游戏状态并释放资产引用。宿主 C++ 也可订阅 `IModLoaderModule::OnBeforeUnload()` 获取即将卸载的包记录。

## 仅供框架集成的接口

请遵循[公开 API、错误与文档](API.zh-CN.md)的布局约定：面向外部使用者的方法放在类的第一个 `public:` 区域，仅供框架内部衔接或 Unreal 生命周期调用的方法紧接着放在第二个 `public:` 区域。即使未来引入内部访问令牌（Passkey），它也只是模块间的工程衔接手段而非安全隔离边界；公开头文件中不得暴露私有实现头，也不得将非反射令牌塞进 `UFUNCTION` 参数。所有跨模块调用都必须建立在明确且有文档记载的契约之上。
