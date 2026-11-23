<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Modules.md) | 简体中文

# 模块与依赖边界

仓库包含开发宿主工程和游戏侧 ModRuntime 插件。MDT 从内嵌源码资源生成并构建独立制作插件。每个模块只承担明确职责，仅声明实际使用的依赖，并将实现细节放入 `Private`。公开头文件是契约：它们须能够独立包含，且只有当公开头文件确实需要某依赖时，才将该依赖列为 `.Build.cs` 的公开依赖。禁止循环依赖。

UE 工程位于 `UnrealModFramework/`。本文中的模块与配置路径均相对于该工程目录。`ModRuntime` 插件包含 `HAL`、`ExportMetadata`、`ModAPI`、`ModLoader`、`BlueprintIntegration` 和 `ExportCodeGenerator` 模块。`ModDevelopmentSDKEditor` 插件包含 `DevelopmentEditor` 模块。

| 职责 | 位置 | UE 模块类型 |
| --- | --- | --- |
| 原生目录通知 | `Plugins/ModRuntime/Source/Runtime/Core/HAL/` | `Runtime` |
| 导出元数据与配置选项 | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| 生成 API 绑定、World 扩展分发、内容、Patch、服务及兼容检查 | `Plugins/ModRuntime/Source/Runtime/ModAPI/` | `Runtime` |
| 模组加载器 | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| Blueprint 接入 | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| 导出代码生成 | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| 独立的制作工具 | `Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/Source/DevelopmentEditor/` | `Editor` |
| 开发宿主 | `Source/ExportTest/` | `Runtime` |

宿主工程通过 `Config/DefaultEngine.ini` 中的 `[CoreRedirects]` 将 `/Script/ModBlueprintIntegration` 映射到 `/Script/BlueprintIntegration`，保留已有蓝图资产中的引用。

`ExportMetadata` 必须在 Game 和 Editor Target 中编译。它不得依赖编辑器模块、某个宿主游戏的代码、特定内容分发服务或制作编辑器。运行时模块按具体职责划分：`ExportMetadata` 承载现有契约类型和配置选项，后续服务放入按功能命名的模块。宿主游戏可以依赖 ModRuntime 插件，并显式提供扩展点。

`Runtime` 和 `Editor` 是单个 `ModRuntime` 插件内用于归类模块的目录，本身不是 UE 模块。各模块须在 `ModRuntime.uplugin` 中声明。`ExportMetadata` 公开依赖 `Core`，用于 UE 字符串和配置数组。

`BlueprintIntegration` 负责 Editor Target 中的蓝图 Mod 导出声明。它直接依赖 `Core`、`CoreUObject`、`Engine`、`BlueprintGraph`、`GraphEditor`、`Kismet`、`KismetCompiler`、`ExportMetadata`、`ModAPI`、`Slate`、`SlateCore`、`ToolWidgets`、`UMGEditor` 和 `UnrealEd`。导出面板及实现图需要蓝图编辑器和编译器模块，因此独立于运行时契约模块。`GraphEditor` 和 `ToolWidgets` 提供“我的蓝图”原生动作列表与“添加”按钮。`UMGEditor` 依赖使标签页在 Widget 蓝图模式切换后仍可注册。`ExportMetadata` 和独立的 `DevelopmentEditor` 均不依赖它。该模块在引擎初始化后加载，以便 `Kismet` 注册编辑器服务。

模块在蓝图编辑器中注册“蓝图Mod导出”标签页；有“我的蓝图”标签页时停靠在其旁边。接口、宏库、Widget 和动画蓝图编辑器使用不同布局时，由后备标签页入口接入；无法提供所需运行时实现的蓝图类型会禁用对应创建按钮。面板复用“我的蓝图”的 `SGraphActionMenu`、分类样式、“添加”按钮、搜索框和视图选项控件。API、Event、Hook 是可折叠分类，右侧使用圆形添加按钮。条目使用原生调色板图标、选中效果和行内重命名；单击在蓝图编辑器已有的“细节”面板显示属性，双击打开实现图，右键菜单及 F2、Delete 提供重命名和删除。导出标签页内不再嵌入单独的属性区。声明是保存在当前蓝图 `UBlueprintExtension` 中、支持撤销的对象。API 实现图与 Hook 原版逻辑图由对应声明持有，并纳入蓝图图搜索；编译器从这些图生成可执行函数。Event 声明保存载荷类型与发布设置。模块不再注册函数、变量或事件分发器的详情定制。

| 声明 | 保存的字段 | 编辑器图 |
| --- | --- | --- |
| API | 版本、显示名与公开名、说明、作用域、线程、失败规则、权限 | 实现图 |
| Event | 版本、显示名、说明、载荷、来源作用域、发布与退订规则、权限、权威设置 | 宿主业务图中的 `Publish Event` 节点 |
| Hook | 版本、显示名、说明、载荷、`AllowedStages`、`AllowedOperations`、权限、权威设置 | 原版逻辑图及 `Hook Point` 节点 |

候选选项位于 `Config/DefaultGameMod.ini` 的 `[ModExportMetadata]` 节。API 使用 `APIScopes`、`APIThreads`、`APIPermissions`、`APIFailureRules`。Event 使用 `EventSourceScopes`、`EventPublishRules`。使用 `+键=值` 添加数组项。API 权限以标识符数组保存。

蓝图 Mod 导出只由在此面板中创建的独立声明定义。API 和 Hook 的实现图由各自声明独占；Event 保存自己的载荷及发布设置。模块不包含从普通函数或事件分发器导入、迁移、修复归属或兼容旧声明的路径；不受支持的声明需重新创建。实现图的生命周期由导出面板控制。图发现、打开和编译均校验归属，图缺失或脱离声明时给出诊断，不再打开失去所属蓝图的图。

声明名称统一为 `ModAPI_<名称>`、`ModEvent_<名称>` 和 `ModHook_<名称>`。模型提供唯一的显示名称和提示文字，供导出列表、细节标题、原生图标签页、顶部路径、入口节点、搜索结果及编译诊断使用。编辑时可输入基础名称，也可输入带对应类型前缀的完整名称；内部保存基础名称，行内编辑不会重复追加前缀。API 与 Hook 图通过派生自 K2 的 Schema 提供原生显示名，不依赖内部对象名。图发现与打开时，仅为声明独立持有的实现图绑定该 Schema，无需重命名源图或转换旧声明。单独的入口节点绘制工厂已移除。编译函数的实际名称和显示名称元数据均来自声明，使调用节点标题及动作菜单名称保持一致。编译器将该名称应用于中间图、入口与返回节点和局部变量作用域，不在编译时重命名源图。重命名会同步图名、局部变量作用域及已加载的函数引用；当源图内部名与声明编译名不同时，也会更新引用声明编译名的节点。相关修改处于同一编辑器事务中。空名称、重名、过长名称和非法名称会被拒绝，不使用 GUID 或自动编号替代。声明 GUID 仅作内部标识，不在细节面板显示。

蓝图扩展向“在蓝图中查找”提供 API、Event、Hook 的完整名称和提示文字。新建、重命名、属性编辑、删除和面板撤销/重做会刷新已保存资产的搜索索引。面板搜索匹配完整名称和基础名称。刷新选中行时也会刷新细节标题，包括从细节面板发起的改名。源图与编译副本共用同一名称绑定过程，同步入口及返回节点的函数引用、函数显示元数据和局部变量作用域，使原生“查找引用”也使用声明的函数名。

`Mod::ExportMetadata::FOptions` 读取配置中的候选选项。宿主在蓝图执行图中显式放置声明对应的 **Publish Event** 或 **Hook Point** 节点。编译器按 GUID 解析声明并展开为运行时调用。`ModExportBlueprints` 编译配置中列出的已保存资产，将已连接扩展节点的 Hook/Event 声明写入快照，由 MHT 生成描述符和 SDK 投影。

`ModAPI` 是 Game 与 Editor Target 共用的 Runtime 模块，公开依赖 `Core`、`CoreUObject`、`Engine` 和 `ModLoader`，私有依赖 `Json`。`UModAPIHostSubsystem` 管理各 GameInstance 的提供者、权限和生成 API 绑定。`UModExtensionSubsystem` 按 World 准备和停止扩展，负责 Hook/Event 快照、内容槽、Patch 锚点、服务租约和玩法指纹。`ExportTest` 承载样例宿主的具体接入，包括玩法准入和存档处理。

MDT 把内嵌的 `DevelopmentEditor` 源码写入临时目录，再用 UAT `BuildPlugin` 独立构建制作插件。制作插件独立于游戏侧模块构建，通过版本化 JSON 契约和生成的公开头文件取得 SDK 与包接口。跨进程预览仍为设计项。

`ExportCodeGenerator` 是 Editor 模块，公开依赖 `ExportMetadata`，私有依赖 `Core`、`CoreUObject`、`Engine`、`Json`、`Projects`、`AssetRegistry`、`BlueprintIntegration`、`ModAPI` 和 `UnrealEd`。`ModExportOptions` 导出 UE 解析后的 GameMod 选项和函数重定向，`ModExportBlueprints` 导出配置中列出的蓝图声明。MHT 用目标的 libclang AST 解析活动 C++ 声明，并读取蓝图快照，生成宿主桥、公开契约、ABI 声明和校验代码。当前覆盖同步 UObject API，以及带受支持标量、反射值或投影对象载荷的 World Hook/Event。

原生模组 ABI 类型应与面向 UE 的公开 API 分离。UE 内部的 C++ 契约使用 UE 类型；外部 C ABI 使用固定宽度字段、显式长度、不透明句柄及已说明的所有权规则，因为独立构建的二进制不能依赖 UE 对象或 C++ ABI 布局保持可移植。

新增模块时须说明职责、直接依赖、支持的 Target，以及现有模块为何不适合承载该能力。可选集成应放在独立模块或插件中。不要为了让缺失的包含文件通过编译而随意增加依赖。

## 模组加载器

`ModLoader` 负责发现、校验、提取和挂载 `.mod` 归档，并将结果分发给游戏会话。它读取 Cooked 内容包 v1、含 UE 原生模块的 v2，以及带扩展清单并可含原生代码的 v3。公开依赖为 `Core`、`CoreUObject` 和 `Engine`，私有依赖为 `Json`、`PakFile`、`AssetRegistry`、`HAL`、`RenderCore` 和引擎自带的 OpenSSL。Editor Target 另外私有依赖 `UnrealEd`，用于跟踪 PIE 会话和释放已加载的包。运行时模块随 UBT 目标编译，桌面和移动宿主均可注册生成的 Hook。挂载要求 Pak 或 IoStore 后端、游戏、基础发布、引擎和平台身份匹配。Win64 x64 Pak 宿主用 `bAllowNativeModules=True` 开放 UE 模块加载，用 `bAllowNativeExtensions=True` 开放独立扩展 DLL。`ModLoader` 负责进程准入和包租约，`ModAPI` 提供生成契约校验及 World 回调与绑定。

游戏可在引擎初始化完成后由 Game Thread 调用 `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` 或 `LoadDirectory(Directory, Policy)`。C++ 调用方在自身模块的 Build.cs 声明 `ModLoader` 依赖。调用会同步验证文件和准备资源，应在进入玩法前完成。成功结果返回已挂载资源的长包名，宿主可据此按需加载资产。

宿主 C++ 可通过 `OnLoadCompleted()` 订阅启动和手动加载。每次操作广播一次，携带加载来源、输入路径和全部 `FLoadResult`。目录无候选时结果为空，目录扫描失败时携带一项失败。失败项包含 `Error` 和 `Detail`。回调在 Game Thread 执行，回调重入或错误线程的结果延后分发。

打包游戏启动时默认扫描实际游戏可执行文件同级的 `Mods` 目录，读取顶层的 `.mod` 文件。标准 Win64 打包目录中的位置是 `<项目>/Binaries/Win64/Mods`。在宿主的 `Config/DefaultGame.ini` 配置基础发布信息。如需关闭启动扫描，在 `[ModLoader]` 节设置 `bLoadAtStartup=False`。

`HAL` 模块为 Win64、Linux 和 Mac 提供原生目录通知。启用启动加载时，加载器默认监听同一目录。设置 `bWatchForChanges=False` 可只使用启动和手动加载。监听收到变更后等待文件状态稳定，在工作线程校验新包，再回到 Game Thread 挂载。已挂载 ModId 的包会跳过，且不触发加载完成回调。

```ini
[ModLoader]
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<基础发布 AssetRegistry.bin 的 SHA-256>
Backend=IoStore
PreparationTimeoutSeconds=120
```

将游戏和基础发布信息填为制作工具使用的值，`Backend` 与游戏发布的实际容器后端一致。加载器校验包格式、目标平台、UE 构建、Cook 回执及各载荷摘要。

无已配置摘要且未显式指定开关时，白名单关闭。要启用白名单，从打包任务 `Result.json` 的 `sha256` 字段取整包摘要，为每个批准的 `.mod` 添加一条 `+TrustedPackageSha256=<摘要>`。显式设置 `bEnforcePackageAllowlist=False` 可跳过已有名单。设为 `True` 时，空名单也会执行校验并拒绝所有包。手动加载使用 `FLoadPolicy` 的同名字段。

进程准入可在同一个节填写 `+EnabledMods=<ModId>`、`+RequiredMods=<ModId>`、`+PinnedArchives=<ModId>=<整包 SHA-256>` 和 `+ExtensionGrants=<ModId>=<权限 ID>`。设置 `bEnableAllMods=False` 并留空启用列表可选择空集合。宿主用 `FWorldPolicy::Grants` 为指定 Mod 提供完整的 World 授权集，缺省时沿用进程准入授权。首次准备的包集合和授权在同一个 GameInstance 中复用。

游戏在开局前通过 `UModExtensionSubsystem` 注册内容槽和 Patch 锚点，然后调用 `Prepare`。设置 `FPatchAnchorRules::Attachment` 或 `DataAssetType` 可开放组件挂接或数据注册。游戏可按锚点及数据 ID 调用 `ResolvePatchData`，按 Actor 或组件查询 `GetPatchOrigin`。`SetFailureHandler` 接收 World 关闭原因。`GetGameplayDigest` 返回首次就绪时的玩法指纹，`ExpectedGameplayDigest` 可在开放玩法前比对宿主选定的值。`GetPackageRevisions` 提供会话的精确包摘要。

多人模式在 `PreLogin` 核对游戏、基础发布、生成契约与模组发行集合。服务器在 UE Welcome 前发送目标地图与玩法指纹，客户端在切图前检查地图，在 Join 前核对本地决议并回传指纹，服务器只接受已确认的连接。若各目标使用不同的基础资源注册表，将各端的 `NetworkBaseCompatibilitySha256` 设为同一个经批准的基础兼容摘要。不同目标的 Mod 产物通过宿主发行清单使用同一个玩法发行身份。为各目标的整包摘要分别填写 `+GameplayReleases=<整包 SHA-256>=<ReleaseId>=<玩法兼容 SHA-256>`，目标包仍按各自的归档摘要验证。示例 `ModExtensions` 模式使用重新连接的切图路径。

`FModExtensionSave` 在 World 扩展准备阶段校验宿主与必需模组，通过版本迁移表处理游戏数据与各 Mod 的数据块，并验证逻辑内容和持久对象 ID。缺失的可选模组数据随保存结果保留。示例在单机 `ModExtensions` 模式提供 `ModWriteSave <槽位>` 和 `ModLoadSave <槽位>`，存档位于 `Saved/ModSaves`。写入发布到新槽位，迁移失败会保留原始文件。

载荷写入 `Saved/ModCache/<整包 SHA-256>`，复用时重新核对文件。挂载保持 Cook 时的包根，同一个 ModId 同时只接受一个版本。从监听目录移走或删除源文件会开始卸载，此时缓存内容仍可用。资源可以安全释放后，加载器会卸载容器、Shader 库并注销包根。仍被引用的包会保留挂载并记录到日志。首版接受独立包根下的未加密内容，支持将包内可用的 Cooked Asset Registry 追加到游戏索引。

Windows 下启动 PIE 时使用宿主项目的 `Binaries/Win64/Mods` 目录和同一份 `[ModLoader]` 配置。在 **项目设置 → 引擎 → Cooker** 中启用 **Allow Cooked Content in the Editor**。配置 `Backend=Pak` 后，ModLoader 会在编辑器启动时初始化 Pak 平台文件。使用 `Backend=IoStore` 时，以 `-UsePaks -UseIoStore` 启动编辑器。加载无版本号的 Cook 资产时，在 `Config/DefaultEngine.ini` 的 `[ConsoleVariables]` 节设置 `s.AllowUnversionedContentInEditor=1`。

同一进程的 PIE 世界共享挂载，各自持有 Mod 实例。每个 PIE 地图就绪后，其实例收到启动加载结果。结束 PIE 时先停止向会话分发结果，等世界清理后释放已加载的包，再卸载容器并注销包根。若编辑器对象仍引用包内资产，加载器会报告并保留挂载，直到可以安全清理。

`ModSDKTool`（MDT）从宿主导出的生命周期声明生成 `ModInstance` 和 `ModInstanceSubSystem`，保留 `/Script/ModLoader` 类型路径。打包时会自动收集内容插件内的具体蓝图派生类。加载批次结束后，游戏会话为挂载成功的模组创建实例，再向活动实例逐项分发结果。C++ 子类覆盖 `OnModLoadCompleted`，并调用 `Super::OnModLoadCompleted` 触发蓝图事件 `ReceiveModLoadCompleted`。蓝图中显示为 **On Mod Load Completed**。两边接收同一份结果，失败项包含错误码和原因。

**On Mod Before Unload** 在 Game Thread 为即将卸载的模组的每个实例调用一次，此时其内容仍已挂载。C++ 子类覆盖 `OnModBeforeUnload`，并调用 `Super::OnModBeforeUnload` 触发蓝图事件。模组可在此恢复游戏状态并释放资产引用。宿主 C++ 可订阅 `IModLoaderModule::OnBeforeUnload()` 获取挂载记录。

## 仅供框架集成的接口

面向使用者的方法放在第一个 `public:` 区域。仅供框架集成或 Unreal 生命周期调用的方法可以放在第二个 `public:` 区域，详见[公开 API](API.zh-CN.md)。未来若引入内部访问 Key，它只是集成约定，不是授权边界。公开头文件不得泄漏私有实现头文件，也不得将非反射 Key 作为 `UFUNCTION` 参数。跨模块访问需要明确且有文档的契约。
