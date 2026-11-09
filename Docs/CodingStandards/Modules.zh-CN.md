<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Modules.md) | 简体中文

# 模块与依赖边界

仓库包含一个开发宿主工程和两个框架插件。每个模块只承担明确职责，仅声明实际使用的依赖，并将实现细节放入 `Private`。公开头文件是契约：它们须能够独立包含，且只有当公开头文件确实需要某依赖时，才将该依赖列为 `.Build.cs` 的公开依赖。禁止循环依赖。

UE 工程位于 `UnrealModFramework/`。本文中的模块与配置路径均相对于该工程目录。`ModRuntime` 插件包含 `ExportMetadata`、`ModApi`、`ModLoader`、`BlueprintIntegration` 和 `ExportCodeGenerator` 模块。`ModDevelopmentSDKEditor` 插件包含 `DevelopmentEditor` 模块。

| 职责 | 位置 | UE 模块类型 |
| --- | --- | --- |
| 导出标识、容器与配置选项 | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| 生成 API 的结果、作用域、绑定、权限与撤销 | `Plugins/ModRuntime/Source/Runtime/ModApi/` | `Runtime` |
| 模组加载器 | `Plugins/ModRuntime/Source/Runtime/ModLoader/` | `Runtime` |
| Blueprint 接入 | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| 导出代码生成 | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| 独立的制作工具 | `SourcePlugins/ModDevelopmentSDKEditor/Source/DevelopmentEditor/` | `Editor` |
| 开发宿主 | `Source/ExportTest/` | `Runtime` |

宿主工程通过 `Config/DefaultEngine.ini` 中的 `[CoreRedirects]` 将 `/Script/ModBlueprintIntegration` 映射到 `/Script/BlueprintIntegration`，保留已有蓝图资产中的引用。

`ExportMetadata` 必须在 Game 和 Editor Target 中编译。它不得依赖编辑器模块、某个宿主游戏的代码、特定内容分发服务或制作编辑器。运行时模块按具体职责划分：`ExportMetadata` 承载现有契约类型和配置选项，后续服务放入按功能命名的模块。宿主游戏可以依赖 ModRuntime 插件，并显式提供扩展点。

`Runtime` 和 `Editor` 是单个 `ModRuntime` 插件内用于归类模块的目录，本身不是 UE 模块。各模块须在 `ModRuntime.uplugin` 中声明。`ExportMetadata` 的公开 API 使用 UE 字符串、反射结构体及带类型的标签和容器，因此公开依赖 `Core`、`CoreUObject` 和 `GameplayTags`。API、Event 与 Hook ID 共用全局 GameplayTags 字典。

`BlueprintIntegration` 负责 Editor Target 中的蓝图 Mod 导出声明。它直接依赖 `Core`、`CoreUObject`、`Engine`、`BlueprintGraph`、`GraphEditor`、`Kismet`、`KismetCompiler`、`ExportMetadata`、`Slate`、`SlateCore`、`ToolWidgets`、`UMGEditor` 和 `UnrealEd`。导出面板及实现图需要蓝图编辑器和编译器模块，因此独立于运行时契约模块。`GraphEditor` 和 `ToolWidgets` 提供“我的蓝图”原生动作列表与“添加”按钮。`UMGEditor` 依赖使标签页在 Widget 蓝图模式切换后仍可注册。`ExportMetadata` 和独立的 `DevelopmentEditor` 均不依赖它。该模块在引擎初始化后加载，以便 `Kismet` 注册编辑器服务。

模块在蓝图编辑器中注册“蓝图Mod导出”标签页；有“我的蓝图”标签页时停靠在其旁边。接口、宏库、Widget 和动画蓝图编辑器使用不同布局时，由后备标签页入口接入；无法提供所需运行时实现的蓝图类型会禁用对应创建按钮。面板复用“我的蓝图”的 `SGraphActionMenu`、分类样式、“添加”按钮、搜索框和视图选项控件。API、Event、Hook 是可折叠分类，右侧使用圆形添加按钮。条目使用原生调色板图标、选中效果和行内重命名；单击在蓝图编辑器已有的“细节”面板显示属性，双击打开实现图，右键菜单及 F2、Delete 提供重命名和删除。导出标签页内不再嵌入单独的属性区。声明是保存在当前蓝图 `UBlueprintExtension` 中、支持撤销的对象。API 实现图与 Hook 原版逻辑图由对应声明持有，并纳入蓝图图搜索；编译器从这些图生成可执行函数。Event 声明保存载荷类型与发布设置。模块不再注册函数、变量或事件分发器的详情定制。

| 声明 | 保存的字段 | 编辑器图 |
| --- | --- | --- |
| API | ID、版本、显示名称、说明、作用域、线程、失败规则、权限 | 实现图 |
| Event | ID、版本、显示名称、说明、载荷结构体、来源作用域、发布规则 | 无 |
| Hook | ID、版本、显示名称、说明、Context 结构体、允许阶段 | 原版逻辑图 |

声明字段统一命名为 `ExportId` 和 `ExportVersion`。

ID 可使用全局字典中任意已注册的 GameplayTag；标签可定义在 `Config/Tags/GameMod.ini`、其他 GameplayTags 来源或 Unreal 的 GameplayTags 编辑器中，不要求固定前缀。其他选项位于 `Config/DefaultGameMod.ini` 的 `[ModExportMetadata]` 节：API 使用 `ApiScopes`、`ApiThreads`、`ApiPermissions`、`ApiFailureRules`；Event 使用 `EventSourceScopes`、`EventPublishRules`。使用 `+键=值` 添加数组项。API 权限以标识符数组保存。外部编辑配置后，重启编辑器或刷新 GameplayTags 界面。

蓝图 Mod 导出只由在此面板中创建的独立声明定义。API 和 Hook 的实现图由各自声明独占；Event 保存自己的载荷及发布设置。模块不包含从普通函数或事件分发器导入、迁移、修复归属或兼容旧声明的路径；不受支持的声明需重新创建。实现图的生命周期由导出面板控制。图发现、打开和编译均校验归属，图缺失或脱离声明时给出诊断，不再打开失去所属蓝图的图。

声明名称统一为 `ModAPI_<名称>`、`ModEvent_<名称>` 和 `ModHook_<名称>`。模型提供唯一的显示名称和提示文字，供导出列表、细节标题、原生图标签页、顶部路径、入口节点、搜索结果及编译诊断使用。编辑时可输入基础名称，也可输入带对应类型前缀的完整名称；内部保存基础名称，行内编辑不会重复追加前缀。API 与 Hook 图通过派生自 K2 的 Schema 提供原生显示名，不依赖内部对象名。图发现与打开时，仅为声明独立持有的实现图绑定该 Schema，无需重命名源图或转换旧声明。单独的入口节点绘制工厂已移除。编译函数的实际名称和显示名称元数据均来自声明，使调用节点标题及动作菜单名称保持一致。编译器将该名称应用于中间图、入口与返回节点和局部变量作用域，不在编译时重命名源图。重命名会同步图名、局部变量作用域及已加载的函数引用；当源图内部名与声明编译名不同时，也会更新引用声明编译名的节点。相关修改处于同一编辑器事务中。空名称、重名、过长名称和非法名称会被拒绝，不使用 GUID 或自动编号替代。声明 GUID 仅作内部标识，不在细节面板显示。

蓝图扩展向“在蓝图中查找”提供 API、Event、Hook 的完整名称、提示文字及导出 ID。新建、重命名、属性编辑、删除和面板撤销/重做会刷新已保存资产的搜索索引。面板搜索同时匹配完整名称、基础名称及导出 ID。刷新选中行时也会刷新细节标题，包括从细节面板发起的改名。源图与编译副本共用同一名称绑定过程，同步入口及返回节点的函数引用、函数显示元数据和局部变量作用域，使原生“查找引用”也使用声明的函数名。

`ExportMetadata` 向 C++ 公开 `FModApiTag`、`FModEventTag` 及对应容器，分别派生自 `FGameplayTag` 和 `FGameplayTagContainer`；编辑器字段使用原生 GameplayTags 属性界面。转换与添加接口检查标签是否存在于全局字典。由于基类接口和导入过程可以绕过这些接口，读取序列化标签的代码还应调用 `IsRegistered`。`ModExportMetadataOptions.h` 中的 `Mod::ExportMetadata::FOptions` 从全局管理器枚举 API 与 Event ID 可用的标签，并从 `[ModExportMetadata]` 读取其他选项；Hook 声明目前使用同一全局标签字典。C++ 元数据参数是编译期字面量，仍需在编写或导出阶段对照配置校验。配置只定义候选标识符，不实现运行时策略。导出面板保存声明并编译 API 和 Hook 图，但尚未创建 Publish Event 或 Hook Point 节点，也不执行运行时契约。后续生成器须在导出前校验已注册的 ID、完整选择及宿主策略语义，运行时代码应消费生成的描述符，而不是 Editor 专用声明。

`ModApi` 是 Game 与 Editor Target 共用的 Runtime 模块，公开依赖 `Core`、`CoreUObject` 和 `Engine`。它负责生成 API 的结果、绑定、World/Player 作用域、显式实例提供者、权限与撤销；这类服务不放入仅承载声明配置的 `ExportMetadata`。宿主 `ExportTest` 提供角色和 LocalPlayer 两个 API，并通过 `UGameModApiHost` 编排生成注册和实例生命周期。

`DevelopmentEditor` 通过 UAT `BuildPlugin` 独立构建。它不得静态依赖项目侧的 `ExportMetadata` 模块或宿主游戏模块。未来共享的包 Schema 与 SDK 契约放入版本化的 `Shared/ModProtocol` 定义，两个插件分别编译这些定义。跨进程预览使用版本化桥接或 IPC，不通过 UBT 依赖游戏工程。

`ExportCodeGenerator` 是 Editor 模块，公开依赖 `ExportMetadata`，私有依赖 `Core`、`CoreUObject`、`Engine`、`GameplayTags`、`Json` 和 `Projects`。`ModExportOptions` commandlet 从共用的 `ExportMetadata::FOptions` 导出实际配置与标签目录，不处理蓝图资产。声明宏的规范头为 Runtime 模块中的 `ModExportAnnotations.h`，旧 `ModExportMacros.h` 仅转发。独立 MHT 的 scan 命令保留词法扫描和参数校验；generate 命令通过实际 UBT 编译数据库和 libclang AST 解析活动 API，生成受限 SDK、具体宿主桥、编译验证、契约和清单。首个同步 UObject 后端要求 GameThread、ReturnError、标量值参数及标量/void/World 对象返回；其他形式和 Event/Hook 代码生成明确拒绝。它不依赖 `BlueprintIntegration`。

原生模组 ABI 类型应与面向 UE 的公开 API 分离。UE 内部的 C++ 契约使用 UE 类型；外部 C ABI 使用固定宽度字段、显式长度、不透明句柄及已说明的所有权规则，因为独立构建的二进制不能依赖 UE 对象或 C++ ABI 布局保持可移植。

新增模块时须说明职责、直接依赖、支持的 Target，以及现有模块为何不适合承载该能力。可选集成应放在独立模块或插件中。不要为了让缺失的包含文件通过编译而随意增加依赖。

## 模组加载器

`ModLoader` 负责发现、验证、提取和挂载 `.mod` v1 内容包，并将挂载结果分发给游戏会话。公开依赖为 `Core`、`CoreUObject` 和 `Engine`，私有依赖为 `Json`、`PakFile`、`AssetRegistry`、`RenderCore` 和引擎自带的 OpenSSL。Editor Target 另外私有依赖 `UnrealEd`，用于跟踪 PIE 会话和释放已加载的包。插件描述文件为 Win64、Linux 和 Mac 的 Game 与 Editor Target 启用模块。挂载要求匹配的 Pak 或 IoStore 后端。它独立于负责 API 绑定的 `ModApi`、导出描述的 `ExportMetadata` 和制作编辑器。

游戏可在引擎初始化完成后由 Game Thread 调用 `Mod::Loader::IModLoaderModule::Get().LoadPackage(Path, Policy)` 或 `LoadDirectory(Directory, Policy)`。C++ 调用方在自身模块的 Build.cs 声明 `ModLoader` 依赖。调用会同步验证文件和准备资源，应在进入玩法前完成。成功结果返回已挂载资源的长包名，宿主可据此按需加载资产。

宿主 C++ 可通过 `OnLoadCompleted()` 订阅启动和手动加载。每次操作广播一次，携带加载来源、输入路径和全部 `FLoadResult`。目录无候选时结果为空，目录扫描失败时携带一项失败。失败项包含 `Error` 和 `Detail`。回调在 Game Thread 执行，回调重入或错误线程的结果延后分发。

在宿主的 `Config/DefaultGame.ini` 写入以下配置，可在引擎启动时扫描 `Saved/Mods` 的顶层 `.mod` 文件。默认关闭自动加载。

```ini
[ModLoader]
bLoadAtStartup=True
GameId=YourGame
BaseReleaseVersion=1.0.0
BaseRegistrySha256=<基础发布 AssetRegistry.bin 的 SHA-256>
Backend=IoStore
+TrustedPackageSha256=<已批准 .mod 的整包 SHA-256>
PreparationTimeoutSeconds=120
```

将游戏和基础发布信息填为制作工具使用的值，`Backend` 与游戏发布的实际容器后端一致。整包摘要来自打包任务 `Result.json` 的 `sha256` 字段，每个批准的包填写一项。加载器同时校验包格式、目标平台、UE 构建、Cook 回执及各载荷摘要。

载荷写入 `Saved/ModCache/<整包 SHA-256>`，复用时重新核对文件。挂载保持 Cook 时的包根，同一个 ModId 同时只接受一个版本。在已 Cook 的游戏中，原生容器、包根和 Shader 库保留到进程退出。首版接受独立包根下的未加密内容，支持将包内可用的 Cooked Asset Registry 追加到游戏索引。

Windows 宿主编辑器启动 PIE 时使用同一份 `[ModLoader]` 配置和 `Saved/Mods` 目录。在 **项目设置 → 引擎 → Cooker** 中启用 **Allow Cooked Content in the Editor**。按后端启动编辑器，`Backend=Pak` 使用 `-UsePaks`，`Backend=IoStore` 使用 `-UsePaks -UseIoStore`。加载无版本号的 Cook 资产时，在 `Config/DefaultEngine.ini` 的 `[ConsoleVariables]` 节设置 `s.AllowUnversionedContentInEditor=1`。

同一进程的 PIE 世界共享挂载，各自持有 Mod 实例。每个 PIE 地图就绪后，其实例收到启动加载结果。结束 PIE 时先停止向会话分发结果，等世界清理后释放已加载的包，再卸载容器并注销包根。若编辑器对象仍引用包内资产，加载器会报告并保留挂载，直到可以安全清理。

`ModSDKTool` 从宿主导出的生命周期声明生成 `ModInstance` 和 `ModInstanceSubSystem`，保留 `/Script/ModLoader` 类型路径。打包时会自动收集内容插件内的具体蓝图派生类。加载批次结束后，游戏会话为挂载成功的模组创建实例，再向活动实例逐项分发结果。C++ 子类覆盖 `OnModLoadCompleted`，并调用 `Super::OnModLoadCompleted` 触发蓝图事件 `ReceiveModLoadCompleted`。蓝图中显示为 **On Mod Load Completed**。两边接收同一份结果，失败项包含错误码和原因。

## 仅供框架集成的接口

面向使用者的方法放在第一个 `public:` 区域。仅供框架集成或 Unreal 生命周期调用的方法可以放在第二个 `public:` 区域，详见[公开 API](API.zh-CN.md)。未来若引入内部访问 Key，它只是集成约定，不是授权边界。公开头文件不得泄漏私有实现头文件，也不得将非反射 Key 作为 `UFUNCTION` 参数。跨模块访问需要明确且有文档的契约。
