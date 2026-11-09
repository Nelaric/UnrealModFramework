<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](Modules.md) | 简体中文

# 模块与依赖边界

仓库包含一个开发宿主工程和两个框架插件。每个模块只承担明确职责，仅声明实际使用的依赖，并将实现细节放入 `Private`。公开头文件是契约：它们须能够独立包含，且只有当公开头文件确实需要某依赖时，才将该依赖列为 `.Build.cs` 的公开依赖。禁止循环依赖。

UE 工程位于 `UnrealModFramework/`。本文中的模块与配置路径均相对于该工程目录。`ModRuntime` 插件包含 `ExportMetadata`、`ModApi`、`BlueprintIntegration` 和 `ExportCodeGenerator` 模块；`ModDevelopmentEditor` 插件包含 `DevelopmentEditor` 模块。

| 职责 | 位置 | UE 模块类型 |
| --- | --- | --- |
| 导出标识、容器与配置选项 | `Plugins/ModRuntime/Source/Runtime/ExportMetadata/` | `Runtime` |
| 生成 API 的结果、作用域、绑定、权限与撤销 | `Plugins/ModRuntime/Source/Runtime/ModApi/` | `Runtime` |
| Blueprint 接入 | `Plugins/ModRuntime/Source/Editor/BlueprintIntegration/` | `Editor` |
| 导出代码生成 | `Plugins/ModRuntime/Source/Editor/ExportCodeGenerator/` | `Editor` |
| 独立的制作工具 | `SourcePlugins/ModDevelopmentEditor/Source/DevelopmentEditor/` | `Editor` |
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

`ModApi` 是 Game 与 Editor Target 共用的 Runtime 模块，公开依赖 `Core`、`CoreUObject` 和 `Engine`。它负责生成 API 的结果、绑定、World/Player 作用域、显式实例提供者、权限与撤销；这类服务不放入仅承载声明配置的 `ExportMetadata`。

`DevelopmentEditor` 通过 UAT `BuildPlugin` 独立构建。它不得静态依赖项目侧的 `ExportMetadata` 模块或宿主游戏模块。未来共享的包 Schema 与 SDK 契约放入版本化的 `Shared/ModProtocol` 定义，两个插件分别编译这些定义。跨进程预览使用版本化桥接或 IPC，不通过 UBT 依赖游戏工程。

`ExportCodeGenerator` 是为导出代码生成预留的 Editor 模块，在 `Default` 阶段加载，目前仅私有依赖 `Core`。公开头文件 `ModExportMacros.h` 定义可变参数声明标记 `MOD_EXPORT_API(...)`、`MOD_EXPORT_EVENT(...)` 和 `MOD_EXPORT_HOOK(...)`，分别放在函数、事件源或事件签名、业务 Hook 函数的声明前。标记接受元数据 token，普通 C++ 编译时展开为空，不添加 UE 反射或运行时行为；需要反射的声明仍显式使用 `UFUNCTION` 等 UE 宏。标记解析、UHT 共存验收及代码生成尚未实现。代码生成职责由该模块承载，与运行时服务、宿主接入及蓝图声明界面分开。

原生模组 ABI 类型应与面向 UE 的公开 API 分离。UE 内部的 C++ 契约使用 UE 类型；外部 C ABI 使用固定宽度字段、显式长度、不透明句柄及已说明的所有权规则，因为独立构建的二进制不能依赖 UE 对象或 C++ ABI 布局保持可移植。

新增模块时须说明职责、直接依赖、支持的 Target，以及现有模块为何不适合承载该能力。可选集成应放在独立模块或插件中。不要为了让缺失的包含文件通过编译而随意增加依赖。

## 仅供框架集成的接口

面向使用者的方法放在第一个 `public:` 区域。仅供框架集成或 Unreal 生命周期调用的方法可以放在第二个 `public:` 区域，详见[公开 API](API.zh-CN.md)。未来若引入内部访问 Key，它只是集成约定，不是授权边界。公开头文件不得泄漏私有实现头文件，也不得将非反射 Key 作为 `UFUNCTION` 参数。跨模块访问需要明确且有文档的契约。
