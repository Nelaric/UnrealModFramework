<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Modules.md) | 简体中文

# 模块与依赖边界

仓库包含开发宿主工程和游戏侧的 `ModRuntime` 插件；MDT 从内嵌源码资源生成并构建独立制作插件。每个模块只承担单一职责，仅声明实际使用的依赖，并将实现细节放入 `Private`。公开头文件即契约：它们必须能够独立包含并通过编译，且只有当公开头文件确实需要某依赖时，才将该依赖声明为 `.Build.cs` 的公开依赖。禁止循环模块依赖。

UE 工程位于 `UnrealModFramework/`。本文中的所有模块与配置路径均相对于该工程目录。`ModRuntime` 插件包含 `HAL`、`ExportMetadata`、`ModAPI`、`ModLoader`、`BlueprintIntegration` 和 `ExportCodeGenerator` 模块；`ModDevelopmentSDKEditor` 插件包含 `DevelopmentEditor` 模块。

| 职责 | 位置 | UE 模块类型 |
| --- | --- | --- |
| 原生目录通知 | `Plugins/ModRuntime/Source/Runtime/Core/HAL/` | `Runtime` |
| 导出元数据与配置选项 | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| 生成 API 绑定、World 扩展分发、内容、Patch、服务及兼容检查 | `Plugins/ModRuntime/Source/Runtime/ModAPI/` | `Runtime` |
| 模组加载器 | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| Blueprint 接入 | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| 导出代码生成 | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| 独立制作工具 | `Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/Source/DevelopmentEditor/` | `Editor` |
| 开发宿主 | `Source/ExportTest/` | `Runtime` |

宿主工程通过 `Config/DefaultEngine.ini` 中的 `[CoreRedirects]` 将 `/Script/ModBlueprintIntegration` 映射到 `/Script/BlueprintIntegration`，以保留已有蓝图资产中的引用。

`ExportMetadata` 必须在 Game 和 Editor Target 中编译，且不得依赖编辑器模块、特定宿主游戏的代码、内容分发服务或制作编辑器。运行时模块按具体职责划分：`ExportMetadata` 承载现有契约类型与配置选项，后续服务放入按具体功能命名的模块。宿主游戏可以依赖 `ModRuntime` 插件，以显式提供扩展点。

`Runtime` 和 `Editor` 只是单个 `ModRuntime` 插件内用于归类模块的目录，本身并非 UE 模块。各模块须在 `ModRuntime.uplugin` 中声明。`ExportMetadata` 公开依赖 `Core`，以使用 UE 字符串和配置数组。

`BlueprintIntegration` 负责 Editor Target 中的蓝图 Mod 导出声明，直接依赖 `Core`、`CoreUObject`、`Engine`、`BlueprintGraph`、`GraphEditor`、`Kismet`、`KismetCompiler`、`ExportMetadata`、`ModAPI`、`Slate`、`SlateCore`、`ToolWidgets`、`UMGEditor` 和 `UnrealEd`。由于导出面板及实现图依赖运行时契约不使用的蓝图编辑器与编译器模块，它们保留在此模块中。其中，`GraphEditor` 和 `ToolWidgets` 提供“我的蓝图”原生的动作列表与“添加”按钮；`UMGEditor` 依赖确保标签页在 Widget 蓝图切换模式后仍保持注册。`ExportMetadata` 和独立的 `DevelopmentEditor` 均不依赖本模块。该模块在引擎初始化完成后加载，以便 `Kismet` 注册编辑器服务。

该模块在蓝图编辑器中注册“蓝图Mod导出”标签页，并在存在“我的蓝图”标签页时停靠在其旁边。对于布局不同的接口、宏库、Widget 和动画蓝图编辑器模式，则由后备标签页生成器负责接入；对于无法提供所需运行时实现的蓝图类型，相应的创建功能将被禁用。面板复用“我的蓝图”的 `SGraphActionMenu`、分类样式、“添加”按钮、搜索框和视图选项控件。API、Event 和 Hook 均以可折叠分类呈现，并带有圆形添加按钮。条目采用原生调色板图标、选中效果和行内重命名：单击可在蓝图编辑器已有的“细节”面板中查看属性，双击可打开实现图，右键菜单及 F2、Delete 键支持重命名与删除。导出标签页内不单设属性面板。各声明是以支持撤销的事务性对象形式保存在当前蓝图的 `UBlueprintExtension` 中。API 实现图与 Hook 原版逻辑图由各自声明持有并纳入蓝图图发现，由编译器据此生成可执行函数图；Event 声明则保存载荷类型与发布设置。本模块不注册函数、变量或事件分发器的细节面板定制。

| 声明 | 保存的字段 | 编辑器图 |
| --- | --- | --- |
| API | 版本、显示名与公开名、说明、作用域、线程、失败规则、权限 | 实现图 |
| Event | 版本、显示名、说明、载荷、来源作用域、发布与退订规则、权限、权威设置 | 宿主业务图中的 `Publish Event` 节点 |
| Hook | 版本、显示名、说明、载荷、`AllowedStages`、`AllowedOperations`、权限、权威设置 | 原版逻辑图及 `Hook Point` 节点 |

配置项的候选值位于 `Config/DefaultGameMod.ini` 的 `[ModExportMetadata]` 节。API 候选值使用 `APIScopes`、`APIThreads`、`APIPermissions` 和 `APIFailureRules`；Event 候选值使用 `EventSourceScopes` 和 `EventPublishRules`。数组项通过 `+键=值` 添加。API 权限以标识符数组形式存储。

蓝图 Mod 导出仅由在该面板中创建的独立声明定义。API 和 Hook 的实现图由各自声明对象独占，Event 声明自行保存载荷与发布设置。本模块不提供针对普通函数或事件分发器旧声明的导入、迁移、归属修复或兼容路径；不受支持的声明必须重新创建。实现图的生命周期由导出面板控制。图发现、打开和编译阶段均会校验归属，图缺失或脱离声明时会产生诊断信息，而不会作为孤立图被打开。

声明名称统一采用 `ModAPI_<名称>`、`ModEvent_<名称>` 和 `ModHook_<名称>`。数据模型提供统一的显示名称和提示文字，供导出列表、细节标题、原生图标签页、面包屑路径、入口节点、搜索结果及编译诊断共用。编辑时既可输入基础名称，也可输入带对应类型前缀的完整名称；内部仅保存基础名称，因此行内编辑不会重复追加前缀。API 与 Hook 图使用派生自 K2 的 Schema 提供原生图显示名，不依赖内部对象名。图发现与打开时仅为声明独立持有的实现图绑定该 Schema，无需重命名源图或转换旧声明；单独的入口节点绘制工厂已移除。编译生成函数的实际函数名和显示名称元数据均采用声明名称，从而保持调用节点标题与动作菜单标签一致。编译器将该名称应用于中间图、入口与返回节点以及局部变量作用域，而不在编译期间重命名源图。重命名操作会在同一个编辑器事务中同步更新图名、局部变量作用域及已加载的函数引用；当源图内部名与声明编译名不同时，也会更新指向声明编译名的引用。空名称、重名、过长名称和非法名称一律拒绝，不使用 GUID 或自动编号替代。声明 GUID 仅作为内部标识，不在细节面板中显示。

蓝图扩展会向“在蓝图中查找”提供 API、Event 和 Hook 的名称与提示文字。新建、重命名、编辑属性、删除声明以及面板撤销/重做，均会刷新已保存资产的搜索索引。面板搜索同时匹配规范完整名和基础名称。刷新选中行时也会同步刷新细节标题，包括直接在细节面板中改名后的场景。源图与编译副本共用同一名称绑定流程，以同步入口和返回节点的函数引用、函数显示元数据及局部变量作用域，因此原生的“查找引用”也会使用声明的函数名。

`Mod::ExportMetadata::FOptions` 负责读取配置中的候选选项。宿主在蓝图执行图中显式放置声明对应的 **Publish Event** 或 **Hook Point** 节点；编译器按 GUID 解析声明并将节点展开为运行时调用。`ModExportBlueprints` 会编译配置中指定的已保存资产，并将已连接扩展节点的 Hook/Event 声明写入快照，随后由 MHT 消费快照并生成描述符与 SDK 投影。

`ModAPI` 是适用于 Game 与 Editor Target 的 Runtime 模块，公开依赖 `Core`、`CoreUObject`、`Engine` 和 `ModLoader`，私有依赖 `Json`。`UModAPIHostSubsystem` 按 GameInstance 管理提供者、权限和生成的 API 绑定；`UModExtensionSubsystem` 按 World 准备和停止扩展，统筹 Hook/Event 快照、内容槽、Patch 锚点、服务租约和玩法指纹。`ExportTest` 承载宿主侧的具体接入，包括玩法准入和存档处理。

MDT 将内嵌的 `DevelopmentEditor` 源码写入临时工作区，并调用 UAT `BuildPlugin` 独立构建制作插件。制作插件独立于游戏侧模块构建，通过版本化 JSON 契约和生成的公开头文件跨边界传递 SDK 与包接口。跨进程预览目前仍为设计项。

`ExportCodeGenerator` 是 Editor 模块，公开依赖 `ExportMetadata`，私有依赖 `Core`、`CoreUObject`、`Engine`、`Json`、`Projects`、`AssetRegistry`、`BlueprintIntegration`、`ModAPI` 和 `UnrealEd`。`ModExportOptions` 负责导出解析后的 GameMod 选项与 UE 重定向，`ModExportBlueprints` 负责导出配置中指定的蓝图声明。MHT 从目标的 libclang AST 解析活动 C++ 声明并读取蓝图快照，据此生成宿主桥接、公开契约、ABI 声明与校验代码。当前支持的配置档案包括同步 UObject API，以及携带受支持标量、反射值或投影对象载荷的 World Hook/Event 扩展。

原生模组 ABI 类型应与面向 UE 的公开 API 分离。面向 UE 的 C++ 契约使用 UE 类型；外部 C ABI 则采用固定宽度字段、显式长度、不透明句柄及书面说明的所有权规则，因为 UE 对象布局与 C++ ABI 布局在独立构建的二进制之间不具备可移植性。

新增模块时必须说明其职责、直接依赖、支持的 Target，以及现有模块为何不适合承载该能力。可选集成应作为独立模块或插件添加。不要仅为了让缺失的包含文件通过编译而随意引入依赖。

## 模组加载器

`ModLoader` 负责发现、校验、提取和挂载 `.mod` 归档，并将结果转发给游戏会话。它支持 v1（Cooked 内容包）、v2（包含 UE 原生模块）和 v3（包含扩展清单及可选原生代码）三种归档格式。公开依赖为 `Core`、`CoreUObject` 和 `Engine`，私有依赖为 `Json`、`PakFile`、`AssetRegistry`、`HAL`、`RenderCore` 和引擎自带的 OpenSSL 库；Editor Target 额外私有依赖 `UnrealEd`，用于跟踪 PIE 生命周期并释放已加载的包。该运行时模块随 UBT 目标编译，确保生成的 Hook 可在桌面和移动宿主上注册。挂载要求 Pak 或 IoStore 后端以及游戏、基础发布、引擎和平台身份均相匹配。Win64 x64 Pak 宿主通过 `bAllowNativeModules=True` 显式启用 UE 模块加载，通过 `bAllowNativeExtensions=True` 显式启用独立扩展 DLL 加载。`ModLoader` 负责进程准入与包租约，`ModAPI` 负责安装生成的扩展契约校验器并管理 World 回调与绑定。

引擎初始化完成后，Game Thread 代码可调用 `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` 或 `LoadDirectory(Directory, Policy)`。C++ 调用方须在自身模块的 `.Build.cs` 中声明 `ModLoader` 依赖。这些调用会同步校验文件并准备资源，应在进入玩法前完成。成功结果会列出已挂载资源的长包名，供宿主按需加载资产。

宿主 C++ 可通过 `OnLoadCompleted()` 订阅启动和手动加载事件。每次操作广播一次，携带加载来源、输入路径以及所有 `FLoadResult` 条目。目录扫描为空时结果列表为空；扫描级失败则返回一条包含 `Error` 和 `Detail` 的失败条目。回调在 Game Thread 上执行，重入或非游戏线程触发的完成事件会延后分发。

打包后的游戏在启动时默认扫描可执行文件同级 `Mods` 目录下的顶层 `.mod` 文件（标准 Win64 打包路径为 `<项目>/Binaries/Win64/Mods`）。在宿主的 `Config/DefaultGame.ini` 中配置基础发布信息；如需禁用启动扫描，可在 `[ModLoader]` 节设置 `bLoadAtStartup=False`。

运行时 `HAL` 模块在 Win64、Linux 和 Mac 平台上提供原生目录通知。启用启动加载时，加载器默认监听同一目录；若仅需启动加载和手动加载，可设置 `bWatchForChanges=False`。监听到变更后，加载器会等待文件元数据稳定，在工作线程准备新归档，再切回 Game Thread 完成挂载提交。声明已挂载 ModId 的归档会被跳过，且不触发加载完成通知。

```ini
[ModLoader]
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<基础发布 AssetRegistry.bin 的 SHA-256>
Backend=IoStore
PreparationTimeoutSeconds=120
```

将游戏和基础发布信息填写为传递给制作工具的值，并选择与游戏发布一致的容器 `Backend`。加载器会校验归档格式、目标平台、UE 构建、Cook 回执以及各载荷摘要。

在未配置摘要且未显式指定开关时，白名单处于关闭状态。要启用白名单，可从打包结果 `Result.json` 的 `sha256` 字段获取整包摘要，为每个经批准的 `.mod` 添加一条 `+TrustedPackageSha256=<摘要>`。显式设置 `bEnforcePackageAllowlist=False` 将跳过已配置的名单；设为 `True` 时，即便名单为空也会执行校验并拒绝所有包。手动加载使用 `FLoadPolicy` 中的对应字段。

进程准入还支持在同一节中配置 `+EnabledMods=<ModId>`、`+RequiredMods=<ModId>`、`+PinnedArchives=<ModId>=<整包 SHA-256>` 和 `+ExtensionGrants=<ModId>=<权限 ID>`。将 `bEnableAllMods=False` 与空的启用列表配合使用，即可选择空模组集合。宿主可通过 `FWorldPolicy::Grants` 为指定 Mod 提供完整的 World 授权集，未指定时则沿用进程准入授权。首次准备确定的包集合与授权会在同一个 GameInstance 内的各个 World 之间沿用。

调用 `Prepare` 之前，应通过 `UModExtensionSubsystem` 声明内容槽和 Patch 锚点。设置 `FPatchAnchorRules::Attachment` 或 `DataAssetType` 可启用组件挂接或数据注册。宿主可按锚点与数据 ID 调用 `ResolvePatchData`，或按 Actor 或组件调用 `GetPatchOrigin`。`SetFailureHandler` 用于接收 World 关闭原因；`GetGameplayDigest` 返回首次就绪方案的玩法指纹，`ExpectedGameplayDigest` 可在开放玩法前与权威值进行比对；`GetPackageRevisions` 返回会话已准入归档的精确摘要。

多人模式会在 `PreLogin` 阶段核对游戏、基础发布、生成契约与已准入的模组发行集合。服务器在 UE Welcome 之前发送目标地图与方案指纹；客户端在切图前检查地图，并在 Join 前确认本地方案并回传指纹，服务器仅接受已完成确认的连接。若不同目标的基础资源注册表存在差异，请将各端的 `NetworkBaseCompatibilitySha256` 设为同一份经批准的基础兼容摘要。如需以统一的玩法发行身份发布等效的客户端与服务器模组产物，可为每个精确产物分别添加一条 `+GameplayReleases=<整包 SHA-256>=<ReleaseId>=<玩法兼容 SHA-256>`；各端仍会校验自身归档。示例 `ModExtensions` 模式采用重新连接的切图路径。

`FModExtensionSave` 在 World 扩展准备阶段校验存档要求，宿主负责提供版本迁移表、逻辑内容及持久对象解析，缺失的可选模组数据块会随存档保留传递。在单机 `ModExtensions` 模式中，`ModWriteSave <槽位>` 和 `ModLoadSave <槽位>` 使用 `Saved/ModSaves` 目录；写入会发布到新槽位，迁移失败时保留原始文件。

载荷提取至 `Saved/ModCache/<整包 SHA-256>`，并在复用前重新核对。每次挂载都会保留其 Cook 时的包根，且同一个 ModId 同时仅允许挂载一个版本。从监听目录移走或删除源归档会触发卸载流程，期间缓存内容仍保持可用。待包可安全释放后，加载器会卸载容器、释放 Shader 库并注销包根；若仍存在外部引用，则保留挂载并记录日志。当前版本接受位于独立包根下的未加密内容，并在存在时将包内 Cooked Asset Registry 追加到游戏索引中。

Windows 环境下，PIE 使用宿主项目的 `Binaries/Win64/Mods` 目录及同一份 `[ModLoader]` 配置。请在 **项目设置 → 引擎 → Cooker** 中启用 **Allow Cooked Content in the Editor**。使用 `Backend=Pak` 时，`ModLoader` 会在编辑器启动时初始化 Pak 平台文件；使用 `Backend=IoStore` 时，请以 `-UsePaks -UseIoStore` 参数启动编辑器。如需加载无版本号的 Cook 资产，请在 `Config/DefaultEngine.ini` 的 `[ConsoleVariables]` 节中设置 `s.AllowUnversionedContentInEditor=1`。

同一进程内的多个 PIE 世界共享已挂载的包，但各自持有独立的 Mod 实例。每个 PIE 地图就绪后，其实例都会收到启动加载结果。结束 PIE 时，加载器会先停止向会话分发结果，待世界销毁后再释放已加载的包，随后卸载容器并注销包根。若编辑器对象仍引用包内资产，加载器会报告相关引用并保留挂载，直至可以安全清理。

`ModSDKTool`（MDT）根据宿主导出的生命周期声明生成 `ModInstance` 和 `ModInstanceSubSystem`，并保留其 `/Script/ModLoader` 类型路径。打包时会自动收集内容插件内的具体蓝图派生类。加载批次完成后，游戏会话为挂载成功的模组创建实例条目，并向活动条目逐项分发结果。C++ 子类可重写 `OnModLoadCompleted`，并通过调用 `Super::OnModLoadCompleted` 触发蓝图事件 `ReceiveModLoadCompleted`（在蓝图中显示为 **On Mod Load Completed**）。两端接收同一份结果，失败项包含错误码与失败原因。

**On Mod Before Unload** 会在 Game Thread 上为即将卸载的模组的每个实例调用一次，此时其内容仍处于挂载状态。C++ 子类可重写 `OnModBeforeUnload`，并通过调用 `Super::OnModBeforeUnload` 派发该蓝图事件。模组应在此回调中恢复游戏状态并释放对模组资产的引用。宿主 C++ 可订阅 `IModLoaderModule::OnBeforeUnload()` 以获取已挂载的包记录。

## 仅供框架集成的接口

面向使用者的方法应放在第一个 `public:` 区域；仅供框架集成或 Unreal 生命周期调用的方法可放在第二个 `public:` 区域，详见[公开 API](API.zh-CN.md)。即使未来引入内部访问 Key，它也只是集成约定，而非授权边界。公开头文件不得暴露私有实现头文件，也不得将非反射 Key 用作 `UFUNCTION` 参数。跨模块访问必须具备明确且有文档记载的契约。
