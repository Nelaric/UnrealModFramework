<!-- Copyright (c) 2026 Nelaric Contributors -->

# UnrealModFramework 技术设计

> 设计基线 v0.5 · 实现快照 `54d7ae3`（2026 年 11 月 21 日核对）\
> 开发基线 Unreal Engine 5.6 · 自动宿主生成 Win64 x64 MSVC\
> 读者 游戏接入、框架与模组工具开发者

开发宿主已接入 C++ 和蓝图导出、游戏 SDK 与独立制作插件构建、`.mod` 打包加载，以及 World 作用域的模组扩展。World 集成覆盖 Hook/Event、内容槽、Patch、跨模组服务、多人玩法计划核对及存档迁移。游戏通过实际业务调用点和显式注册决定这些能力在哪里生效。

当前构建链由 UBT/UHT 提供 Target 上下文，MHT 生成契约和具体连接，MBT 独立编译宿主库，由 UBT 链接到所属二进制。Win64 的 UBT 后置步骤核对实际链接产物。MDT 使用同批生成清单与库上下文发布作者 SDK 和制作插件。[UHT 接入](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md) · [MDT 指南](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)

下文保留发布目标。当前代码覆盖、样例入口及待验收范围见[游戏端详细设计](UnrealModFramework_游戏端详细设计.md#当前实现与限制)。Shipping、各容器后端及更多平台分别验收。

## 1. 目标与边界

框架由游戏内的 **Mod Runtime**、独立构建的 **模组开发编辑器**、版本化的 **Mod Protocol / SDK** 和单文件交付的 **`.mod` 包**组成。游戏开发者主动接入 Runtime 和游戏扩展点；模组作者在自己的 UE 工程中制作内容，经 Editor 构建 `.mod`；游戏启动时验证、挂载和注册模组，Gameplay 阶段使用 UE 原生对象与软引用加载机制。

| 能力 | 当前范围 |
| --- | --- |
| Cooked 资产 | 归档和运行时覆盖 Pak/IoStore 内容路径。样例宿主使用 Pak，Shipping 按平台、Cook 配置和后端逐项验收 |
| 内容 ID 与 World Patch | 宿主定义内容槽和锚点。支持内容提供、替换及禁用，以及 Actor 生成、组件挂接和数据注册 |
| 原生代码 | Win64 x64 Pak 上分别支持匹配宿主构建的 UE 模块与独立 C ABI 扩展 DLL，由宿主显式启用 |
| Blueprint | 宿主蓝图可导出受支持的实例 API 和已接入的 Hook/Event。生成 SDK 提供反射调用、注册和生命周期类型 |
| 材质 Shader Code | 加载器处理与 Cook 内容匹配的 Shader 库，渲染支持按目标 RHI 和 Shader 平台验收 |
| 卸载与替换 | 内容包可在会话租约和对象引用释放后卸载。UE 原生模块驻留至进程退出，同身份原生版本替换需要重启 |
| 任意 UE 反射或全局 Shader 插件 | 任意构建组合的类型与全局 Shader 动态注册仍属研究范围 |

这里的“通用”指框架机制可迁移，不能绕过不同游戏的资源命名、Gameplay 语义、打包配置和扩展接口。硬编码在游戏中的资源路径不会自动变为逻辑 ID。

## 2. 参与者与组件

```text
模组作者 UE 工程 ── 模组开发编辑器 ── 构建/验证 ── xxx.mod
       │                 │                           │
       └── Runtime SDK + Game Mod SDK                 ▼
                                         游戏启动 ── Mod Runtime
                                                       │
                                 校验 → 挂载 → 注册 → 按需加载
                                                       │
                                             UE 原生运行环境
```

| 组件 | 职责 | 交付方式 |
| --- | --- | --- |
| `ExportMetadata` | 导出元数据与配置选项。其余运行时服务按具体功能扩展 | 游戏工程中的源码插件 |
| `ExportCodeGenerator` | `ModExportOptions` 导出配置与重定向，`ModExportBlueprints` 导出已配置的宿主蓝图 | 随 ModRuntime 源码插件编译，仅 Editor Target 加载 |
| `BlueprintIntegration` | “蓝图Mod导出”面板、蓝图内的独立 API/Event/Hook 声明、接入节点与编译校验 | 随 ModRuntime 源码插件编译，仅 Editor Target 加载 |
| `ModHeaderTool`（MHT） | 按 Target 的 libclang AST 和蓝图快照解析受支持的签名，生成投影、宿主代码、ABI 及清单 | 独立 C++ 工具，通过 C# UHT Exporter 适配器接入 |
| `ModBuildTool`（MBT） | 核对编译上下文、构建和缓存生成库，并校验链接产物 | 独立 C++ 工具，通过 UHT 和 UBT 步骤接入 |
| `ModSourceTool`（MST） | 分析失败返回与异常路径，并生成用于保留宿主注册锚点的链接头 | 独立 C++ 工具，参与宿主生成和编译 |
| `ModLoader`、`ModAPI` | 进程包准入及挂载、会话实例、API 绑定与 World 扩展 | 随游戏编译的 Runtime 模块 |
| `ModSDKTool`（MDT） | 生成并构建制作插件与游戏专属 SDK，校验并发布产物 | 独立 CMake/C++20 可执行工具，接入统一工具构建入口 |
| `DevelopmentEditor` | 作者工程 SDK 任务、DLC Cook/Stage 和 `.mod` 打包 | 由 MDT 生成并独立构建的编辑器插件 |
| 版本化协议 | 生成清单、SDK 契约、包归档及扩展 ABI | 工具协议 Schema、生成的公开头、打包器和加载器的读写契约 |
| 游戏 Mod SDK Bundle | 公共 GameModSDK、受限视图、反射类型、契约、链接/模块元数据、Cook Profile 与能力报告 | 同批生成、按 Game/Editor 等目标构建，原子发布 |
| 游戏 SDK 的 UE 模块 | 公开反射类型、调用与注册适配，例如 `UMFDevSDK` | MDT 生成。作者 Editor 版与游戏端配套版本分别构建 |
| `.mod` | Manifest、原生 DLL、Cooked 容器、Shader 产物、注册表、索引 | 单文件分发；运行时可提取到受控缓存 |

`DevelopmentEditor` 保持独立构建，通过 SDK 与包协议取得游戏公开信息。目标中的游戏专属 Editor 节点和跨进程预览仍可作为独立适配接入。

## 3. 源工程与构建

| 路径 | 内容 |
| --- | --- |
| `UnrealModFramework/Source/ExportTest/` | 开发宿主、角色/玩家导出、World 游戏模式与网络/存档接入 |
| `Plugins/ModRuntime/Source/Runtime/` | `HAL`、`ExportMetadata`、`ModLoader` 和 `ModAPI` |
| `Plugins/ModRuntime/Source/Editor/` | `BlueprintIntegration` 和 `ExportCodeGenerator` |
| `Plugins/ModRuntime/Source/ModRuntimeUht/` | UHT Exporter 与 UBT 构建适配 |
| `Plugins/ModRuntime/Tools/` | MHT、MBT、MST、MDT 和内嵌制作插件资源 |
| `ExampleMods/TestMod/`、`Test/Performance/` | 独立作者样例和调用基准 |

表中的插件路径相对于 `UnrealModFramework/`。Host/Client 的生成文件和库按 Target、平台、配置、架构分别保存在工程或插件的 `Intermediate/ModTools/`。

正常 Win64 构建按需准备独立工具、UBT 编译数据库、配置及蓝图快照。Editor 导出器缺少模块二进制时，可借助 `-MhtBootstrap` 准备；正式生成由后续正常构建完成。MHT 在标准 UHT CodeGen 后解析活动 C++ 声明与蓝图快照。MBT 按清单 `compilationUnits` 编译宿主和客户端单元。宿主通过私有 External 模块依赖登记库，并强制包含 MST 的链接头，UBT 链接后由 MBT 发布关联回执。

MDT 用 UAT `BuildPlugin` 编译内嵌的制作插件和游戏专属 SDK 插件，核对 Editor BuildId、原生库和文件库存。作者可把输出安装到独立工程的 `Plugins/`。游戏端需要从同批次输出安装配套的 SDK Runtime，再构建和 Cook。通用 UBT 动作图与跨平台宿主生成属于后续适配。[UHT 构建、引导及产物路径](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md) · [SDK 输入与交付](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)

## 4. 游戏接入契约与统一 API

框架只提供通用机制。游戏至少接入以下契约，才能让对应功能工作：

1. **宿主身份与兼容性**：游戏 ID、游戏版本、Runtime SDK 版本、UE 构建标识、目标平台、Shader 平台及 Cook Profile ID。
2. **内容目录**：定义可扩展的逻辑 ID、内容类型、原版默认值、合法覆盖规则和数据 Schema。
3. **实例创建入口**：在 Actor、组件、物品或其他对象创建时解析逻辑 ID，并把结果缓存到游戏对象；框架不在每帧重复查找。
4. **事件入口**：显式发布可供模组订阅的 Gameplay 事件，标明线程、时序、可修改性和网络权限。
5. **世界修改入口**：为可修改场景提供稳定的对象 ID 与安全时机；不依赖运行时遍历并猜测任意 Actor。
6. **API 实例入口**：为非静态导出成员指定对象提供者、作用域和失效时机；声明标记不决定本次应调用哪个 `UObject`，也不自动创建实例。

`ModLoader` 负责进程准入、归档和挂载。`UModInstance` 承载加载完成、注册、绑定及卸载回调。`UModAPIHostSubsystem` 管理 GameInstance 的 API Registry，`UModExtensionSubsystem` 管理单个 World 的扩展计划及生命周期。[模块与依赖边界](CodingStandards/Modules.zh-CN.md)

### 4.1 逻辑 ID 与覆盖规则

内容 ID 由游戏定义，例如 `Creature.Wolf.Mesh`。注册项包含 `id`、`type`、`action`、`value`、`ownerModId` 和 `priority`；`action` 取 `provide`、`replace` 或 `disable`。`value` 对资产是软对象路径，对数据内容是已版本化的结构化记录。原版内容视为最低优先级候选。

模组准备顺序由依赖图决定。对同一逻辑 ID，运行时先检查内容槽的权限和候选资格，宿主可通过 `FWorldPolicy::ContentWinners` 指定合法候选，否则按优先级决议。最高优先级并列时报告冲突并列出候选。同一模组在同一个槽内只能为一个逻辑 ID 登记一次。若 `disable` 胜出，该 ID 固定返回“已禁用”。

加载顺序不等于 UE 包路径覆盖顺序。当前归档以 `/<PluginName>/` 为独立 Cooked 包根，例如 `/TestMod/`。加载器拒绝包根和 UE 包路径冲突。玩法覆盖只通过逻辑 ID 决议，避免把 Pak 优先级当作公共 API。

### 4.2 资产加载与生命周期

逻辑 ID 在初始化或实例创建时解析为 `FSoftObjectPath` / `TSoftObjectPtr`，随后由 UE 异步加载。游戏在回调后保留需要长期使用的强引用；不再使用时按 UE 对象生命周期释放。UE 5.6 文档明确软引用可避免被引用资产随持有者立即加载，`FStreamableManager` 可请求异步加载。[UE 5.6 异步资产加载](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine?application_version=5.6)

预期性能目标是 **Gameplay 热点路径不反复查询框架**。内容决议、依赖排序和包验证发生在启动或对象创建阶段；实际运行仍包含 UE/Blueprint 自身的开销，Native ABI 调用也保留一次函数指针调用成本。

### 4.3 API、Hook、Event 的声明和接入

游戏开发者在真实声明前使用独立 Mod 标记，标准 UE 标记继续表达反射。开发宿主的角色读接口声明如下。

```cpp
MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldRead))
EXPORTTEST_API float GetCurrentHealth() const;
```

`MOD_EXPORT_API` 关联具体成员，`MOD_EXPORT_EVENT` 关联显式发布函数，`MOD_EXPORT_HOOK` 关联业务函数。类型身份保存在宿主 `Config/ModAPITypes.json`。生成器使用当前 Target 的活动声明。`ChangeHealth` 直接调用 `ApplyHealth`。[角色业务实现](../UnrealModFramework/Source/ExportTest/GameCharacter.cpp)字段及声明说明见[游戏端详细设计第 8 节](UnrealModFramework_游戏端详细设计.md#8-游戏声明与蓝图接入)。

| 机制 | 声明提供的信息 | 游戏必须显式完成的接入 |
| --- | --- | --- |
| API | ID、具体 AST 成员、投影与调用策略 | 注册服务对象提供者或静态入口；指定 World/玩家作用域、线程与权限 |
| C++ Hook | 生成器根据所属类型、成员名和签名生成 ID 与阶段 | 在声明上写 `MOD_EXPORT_HOOK()`，在 `.cpp` 中实现原函数 |
| Event | ID、实际事件源、载荷与回调策略 | 提供事件源实例，在业务发生时通过具体连接发布并管理生命周期 |

#### 蓝图Mod导出

游戏工程的 `BlueprintIntegration` 在蓝图编辑器侧栏提供“蓝图Mod导出”面板，按当前蓝图显示 `API`、`Event`、`Hook` 三个独立分组，每组都有创建入口。面板通过编辑器插件提供的标签页和布局扩展接入，停靠在“我的蓝图”附近；不修改 UE 引擎源码，也不把三个分组伪装成原生“我的蓝图”列表的内部分类。对不能生成运行时实现或缺少相应图能力的蓝图类型，相关创建入口禁用并说明原因。

三个分组分别创建专有声明，不依赖普通函数、事件分发器或结构体详情面板中的布尔开关：

| 分组 | 独立声明及编辑内容 | 显式业务接入 |
| --- | --- | --- |
| API | 签名、权限、作用域、实例提供者和 API 实现图 | 编译生成可调用入口，非静态入口须提供有效实例及生命周期规则 |
| Event | 载荷类型、发布作用域与线程规则 | 在业务图中放置对应的 Publish Event 节点，向 Event Bus 发布 |
| Hook | 类型化 Context、`AllowedStages`、`AllowedOperations`、原版逻辑与失败规则 | 在业务图中放置对应的 Hook Point 节点并连接原版逻辑 |

各声明以独立 GUID 保存在蓝图的 `UBlueprintExtension` 中。API 与 Hook 图由对应声明持有，编译时生成 UE 可执行函数。面板负责创建、选择、重命名、删除和跳转；删除时检查引用并支持撤销。Event 与 Hook 节点引用声明 GUID。`ModExportBlueprints` 编译配置中的保存资产，并导出执行输入已连接的扩展节点及对应声明。不支持的载荷或无效签名会产生错误。Editor-only 声明不作为 Shipping 时的查询来源，生成器输出运行时描述符及绑定。[`UBlueprintExtension` API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UBlueprintExtension)

C++ 的动态多播 Delegate 仍可作反射事件源，但其广播顺序未定义；需要稳定顺序时由框架 Event Bus 按契约排序分发。[UE 5.6 多播 Delegate](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine?application_version=5.6)

### 4.4 Export Generator 与 Game Mod SDK

**静态生成原则是固定架构约束。** 所有跨契约边界的类型映射、签名投影、参数与结果转换、可空性、只读性、错误表示、ABI 表示和调用连接，均由 Export Generator 在构建期确定并生成。“类型转换层”属于生成器的类型分析与代码生成阶段。运行时执行具体生成代码，处理当前对象、作用域、授权与调用数据，不提供动态转换器选择或通用反射转换回退。

游戏成员保持真实类型和原始签名。生成器按受支持的签名投影参数、结果与载荷。对象返回可获得类型化视图，供独立扩展使用的对象载荷可投影为只读视图。递归容器和更广的 C++ 类型闭包按具体后端扩展。API 的 `FailureRule = ReturnError` 统一生成 `TModResult<T>`，蓝图和 C ABI 使用对应的具体结果表示。视图成员集合在 SDK 生成时固定，派生实例或不同 Mod 授权不扩大该集合；实际授权通过生成入口检查。完整规则见[游戏端详细设计第10节](UnrealModFramework_游戏端详细设计.md#10-契约生成与构建流水线)和[第11节](UnrealModFramework_游戏端详细设计.md#11-类型化调用连接)。

MHT 用真实 UBT 编译数据库和 libclang AST 解析活动 C++ 声明。`ModExportBlueprints` 从已配置的蓝图资产提取受支持的编译后签名，MHT 读取快照并合并生成。同步 API 支持标量和公开 UE 对象输入，以及标量、`void` 或 World 对象结果。World Hook/Event 支持标量、受支持的反射值结构体和带只读投影的 UObject 载荷。

当前批次输出宿主桥与注册代码、编译校验、公共 C ABI 头、独立客户端实现、SDK 与生命周期契约、扩展描述和 v3 生成清单。`<game>.generated-artifacts.json` 记录输入哈希、所有者及用途，并显式列出编译单元。MDT 根据 `.sdkcontract.json` 生成 UE 反射包装。更广的递归容器投影和通用类型闭包仍以实际生成后端的支持范围为准。[MHT 生成产物](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md)

生成结果按规范化输入稳定排序，包含生成器版本、游戏 SDK 版本、UE 构建标识、源/Mod 签名、投影摘要和输入指纹。Win64 构建校验生成输入及产物哈希，通过生成注册入口向运行时登记。通用 Runtime 保持独立于具体游戏模块。生成器只自动适配明确支持的完整类型闭包；任意 `UFUNCTION` 参数不能无条件自动转换成稳定 C ABI。例如 `UObject*` 需要不透明句柄及生命周期规则，复杂容器需要版本化数据传输类型。未知、歧义或不支持的签名在生成或编译阶段失败，不留到 Gameplay 补齐。扫描后新增的反射包装须在后续正常 UHT 前生成，按显式多阶段构建完成。

游戏 SDK 拥有公共视图和共享反射类型。现有适配由 MDT 生成到游戏 SDK UE 模块，独立的游戏专属 Editor 模块可按实际需要接入。通用编辑器保持自包含共享协议边界，不反向依赖项目级 SDK、Runtime 或游戏私有模块。既有公共模块名及 /Script 身份可继续保留，不因角色命名统一而重命名或复制反射定义。蓝图具体包装由 Runtime 模块拥有，编译结果只引用宿主存在的 Runtime 类型和函数；不伪造真实游戏 UCLASS。

SDK 交付单位为同批 Bundle：公开头、适配源码或等价编译输入、SDK 清单、匹配 Editor 的 DLL 与链接输入、.uplugin/.Build.cs/.modules 及所需预编译元数据，再加契约、Cook Profile 和能力报告。Windows 隐式链接使用导入库；显式加载仍需正确 ABI 和调用声明。[Microsoft DLL 链接说明](https://learn.microsoft.com/en-us/cpp/build/linking-an-executable-to-a-dll?view=msvc-170)。DLL 不是再次编译成编辑器插件的源码。

宿主连接由 MBT 独立编译成所属模块的静态库，再由 UBT 链入对应目标；公开 SDK 的 UE 模块和游戏 Editor 适配按各自原生 Target 构建。MBT Finalize 只记录本次目标实际成功生成的产物。普通 Game Target 成功不代表 GameModSDKEditor 的 DLL 已生成；跨目标 SDK 发布必须等待所需 Game/Editor 产物齐备。Shipping 游戏 DLL 不能替代 UnrealEditor 模块，monolithic 游戏也不承诺独立出口 DLL；若另需 Native Bridge DLL，单独生成稳定 C ABI 桥并验收。独立作者工程无需游戏私有源码即可加载 SDK、浏览并编译契约；真实业务仍需 Provider 或 Preview Host。[UE 二进制版本](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-version-binaries-in-unreal-engine?application_version=5.6)。生成清单、工具职责与编译规则见[游戏端详细设计第10节](UnrealModFramework_游戏端详细设计.md#10-契约生成与构建流水线)。

### 4.5 双执行路径与 C ABI 边界

| 路径 | 适用范围 | 运行时成本与边界 |
| --- | --- | --- |
| Native Fast Path | 已编入宿主的 C++ 服务、类型化 Hook、缓存的 Native 回调 | 初始化建立类型化绑定；Gameplay 执行已生成的具体转换/检查，调用原成员或固定类型化回调数组 |
| Reflection Path | Blueprint 和只提供 `UFunction` 的服务 | 构建期固定参数布局、转换和分派规则；绑定具体目标后执行生成连接及 UE 原有反射/覆盖路径 |
| C ABI 边界 | 独立分发的 Native DLL | 通过已生成的专用类型化函数表、DTO 和不透明句柄进入具体宿主连接，按生成规则保留相应 UE 执行语义 |

两条执行路径及 C ABI 入口在装载/绑定阶段由同一 Mod Host 核对导出 ID、版本、签名、投影和能力，绑定结果必须指向已生成的类型化入口。生成器固定对象生命周期、线程和权限的检查方式，调用时处理当前状态；UE 虚调用和蓝图覆盖语义按契约保留。Schema 以稳定 ID 加主/次导出版本及签名/投影摘要识别接口；破坏签名、权限、线程或语义的变更提升主版本。构建期重复 ID 直接失败；运行时 SDK 版本不兼容则拒载相关模组并给出可机器读取的原因。

普通调用不按 TypeId 查找转换器，不解释字符串参数类型，不遍历 FProperty 决定转换，不使用通用参数盒或统一 `Invoke(ExportId, Arguments)` 分发。Reflection Path 保留 UE 本身的执行机制，框架参数连接已在构建期确定。生成代码对本次数组执行遍历、视图构造、复制、分配或句柄验证时，仍按实际工作计量。

### 4.6 Hook 与 Event 的确定性语义

API 提供可主动调用的能力。Hook 是游戏在真实业务操作中设置的扩展点，Event 由宿主显式发布已发生的事实。成功路径依次经过 `Pre`、`On`、`Post`，宿主可以在 Hook 完成后按业务规则发布 Event。

| 阶段 | 时机与职责 | 可声明的操作 |
| --- | --- | --- |
| `Pre` | 核心执行前，读取输入并检查条件 | `ModifyInputs` 修改声明允许的输入，`Cancel` 取消执行 |
| `On` | 执行核心业务，默认调用宿主原版逻辑 | `Wrap` 包装执行，`Replace` 接管执行 |
| `Post` | 核心业务成功后，读取结果并执行后续处理 | `ModifyResult` 修改声明允许的结果 |

C++ 的 `MOD_EXPORT_HOOK()` 默认生成 `Pre`、`On` 和 `Post`，并声明 `ModifyInputs`、`Cancel` 和 `Replace`。`NoPre`、`NoOn`、`NoPost` 分别关闭对应阶段及其操作，对 `CompatibleNames` 中的曾用名同样生效。蓝图编辑器中单独声明的 Hook 通过 `AllowedStages` 和 `AllowedOperations` 保存策略，默认只开放 `Post` 且操作列表为空。每项操作都要求对应阶段开放。开启 `On` 后仍须分别显式授权 `Wrap` 或 `Replace` 才允许 Mod 介入核心执行。取消跳过 `On` 和 `Post`，失败由宿主返回并可单独发布事实通知。注册阶段建立稳定快照，阶段权限、顺序和错误规则见[游戏端详细设计第18节](UnrealModFramework_游戏端详细设计.md#18-hook-语义与执行计划)。

Hook 契约还需指定失败策略、重入规则、调用线程和网络权威。首版默认 Game Thread、同步，同键重入默认拒绝。跨线程或异步 Hook 必须单独声明并验证对象生命周期。Event 监听者接收只读载荷，宿主决定业务结果。Event Bus 在注册阶段缓存订阅者，广播前检查模组与源对象仍有效，Game Thread 为默认回调线程。原生 DLL 崩溃不能靠框架回调层隔离。

### 4.7 World Patch 与跨 Mod 服务

宿主用 `RegisterAnchor` 开放稳定锚点及权限。扩展清单可请求生成 Actor、在已批准的挂接点注册场景组件，或注册 Cooked 数据资产。运行时检查依赖、冲突、服务前置条件与权限，在 World 安全阶段应用并记录来源。清理仅回收框架持有的效果，游戏副作用需要宿主定义补偿。

跨模组服务在扩展清单声明提供与依赖。提供者在 **On Mod Register** 登记实现，或在已声明的 Patch Actor 上提供服务。消费者在 **On Mod Bind** 用生成 SDK 获取匹配签名与版本的 World 绑定；停止、权限改变或服务目标失效时由宿主管理调用资格。[包和服务字段](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#mod-布局)

## 5. `.mod` 包协议

`.mod` 是单个文件。当前归档以固定 64 字节小端 Header 开始，后接 UTF-8 Manifest、连续载荷和 UTF-8 Index。归档主版本 v1 携带内容，v2 增加匹配宿主的 UE 原生模块，v3 携带扩展清单，可包含 UE 模块或独立回调 DLL。扩展清单有自己的 Schema v1/v2。

### 5.1 Manifest 的必需信息

`UMF.ModPackage` 记录 `modId`、三段版本、插件身份及 `/<PluginName>/` 包根、`cookProfileId` 与配置详情，以及包内 Cook 回执的条目路径。宿主核对 GameId、基础发布及其 Asset Registry SHA-256、引擎构建、目标平台、Cook 平台与后端。可以附带 `lifecycleClasses`、`nativeCode`、`extensions`，分别描述实例类、UE 模块及世界扩展。归档 v3 的扩展清单记录 Hook/Event 签名、依赖、互斥、所需授权、内容、Patch 和版本化服务。[归档字节布局和字段](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#mod-布局)

### 5.2 解析、校验与缓存

外层当前使用不压缩、无签名的归档。索引对规范化路径排序，记录偏移、长度及条目 SHA-256，拒绝目录穿越、重复路径和范围错误。`ModLoader` 支持按整包 SHA-256 启用许可名单与固定修订。提取后的载荷位于 `Saved/ModCache/<整包 SHA-256>`，复用前需要重新核对。归档签名和公钥管理属于设计项。获准执行的原生代码具有宿主进程权限。

### 5.3 Cooked 内容挂载

包后端由宿主显式声明支持 `Pak`、`IoStore` 或二者。运行时在验证后为模组建立独立包命名空间，挂载资源容器，让 Cooked Asset Registry 可见，再发布框架的逻辑内容注册表；两种 Registry 是不同数据结构。UE 5.6 提供 Pak 挂载接口；官方 API 建议通常经 `FCoreDelegates` 的挂载委托调用，且其挂载参数可报告相关 IoStore 状态。[UE 5.6 Pak 挂载 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/PakFile/FPakPlatformFile/Mount?application_version=5.6)

**Pak 与 IoStore 在 Shipping 中的动态挂载、包路径解析、Asset Registry 合并、软引用加载和材质显示，必须分别通过端到端实验。** 在通过实验前，它们是目标能力，不是已验证的兼容承诺。首版实现可以按验证结果只启用已通过的容器后端；不支持的包在启动时给出明确错误。

## 6. 启动编排与失败处理

| 阶段 | 当前接入 |
| --- | --- |
| 进程准入 | 验证归档、选择启用或必需集合，核对固定摘要、权限、契约、依赖与冲突 |
| 资源准备 | 提取并挂载容器，登记包根，追加可用的 Cooked Registry。需要时先加载符合宿主构建的 UE 模块，再实例化蓝图类 |
| 会话实例 | `ModLoader` 向会话分发加载结果。首次准备冻结包集合和授权，同 GameInstance 的后续 World 复用 |
| World 准备 | 宿主先定义内容槽和锚点，再调用 `UModExtensionSubsystem::Prepare`。运行时执行注册、内容和 Patch 决议、服务绑定及 Hook/Event 快照 |
| 玩法准入 | World 准备完成并提供玩法指纹。宿主据此在启动、服务器登录或客户端进图时接入玩法屏障 |
| 关闭 | 停止分发，清理自有 Patch 和绑定，释放包租约。仍有引用时保留挂载；涉及不可逆状态时可要求重启 |

`ModExtensions` 模式把这些步骤接入开发宿主。每个 World 仍需按游戏实际的 `StartPlay`、切图、玩家登录和关闭时序验收。[游戏端就绪屏障](UnrealModFramework_游戏端详细设计.md#13-宿主状态机与就绪屏障)

## 7. 原生 ABI 与 Shader 边界

独立扩展 DLL 以 `UMF_RegisterExtensionsV1` 注册 World 回调。宿主表通过 `StructSize` 和 `Version` 描述版本，提供注册、退订、Hook 续调及宿主 API 表查询。注册时按 SDK 中的 ID 与签名校验；输入、输出、对象与续调令牌随调用帧失效。World 停止时先排空回调，再调用可选的 `Close`。

生成 API 的客户端使用 `ContextHandlesV1`。它通过定宽标量、显式长度和不透明句柄交换数据，使用对应的 ABI 哈希校验函数表。调用边界使用规定的借用期和缓冲区规则，异常在各自模块处理。UE 对象或 C++ 容器布局留在 UE 模块内。宿主已编译的 C++ Fast Path 仍通过具体类型化入口调用游戏服务。[独立 DLL 回调](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#独立原生回调) · [生成 API 连接](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md)

当前工程打包模式使用宿主 Target 回执，为 Win64 x64 Pak 发布编译 UE 模块。加载器核对归档、构建身份和 BuildId 后加载模块，以保留 Cooked 蓝图父类身份。任意 Target 上动态扩展反射或新增 Global Shader 类型仍是独立研究方向。新反射和 Shader 类型需要相应的 UHT 与引擎装载阶段。只有在指定 UE 构建、目标平台和 Shipping 链接模式完成验证后，才定义其兼容性与交付方式。材质使用的 Cooked Shader Code 与“新增 Global Shader 类型”分别验收，不能用前者的成功推断后者可用。UE 文档明确新增 Global Shader 类型的模块必须在引擎初始化前加载，游戏启动后动态模块不能再添加 Shader 类型。[UE 5.6 Global Shader 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)

## 8. 模组开发编辑器与预览

当前制作插件可在独立作者工程发起 SDK 计划、构建与 `.mod` 打包任务。`TestMod` 提供作者工程内的角色预览，以及原生模块与生命周期蓝图样例。其余通用预览模式按下表设计和验收。[制作编辑器操作](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md)

| 模式 | 数据路径 | 适用范围 |
| --- | --- | --- |
| Local PIE | 模组作者工程中的未 Cook 资产 + Preview Host + Runtime SDK | 快速测试逻辑 ID、内容注册和编辑中的资产 |
| Remote Preview | 已启动的真实游戏测试进程 + 版本化 IPC + 必要的增量 Cook 资源 | 验证游戏特有流程与实际运行表现 |
| Build & Test | 完整 Cook、生成 `.mod`、包验证、目标游戏加载 | 发布前的权威结果 |

Local PIE 只模拟已由游戏 SDK/Preview Host 提供的行为。需要完整游戏逻辑时，游戏开发者必须提供可供作者工程使用的运行时模块或测试宿主。Remote Preview 中编辑器与游戏进程不能共享未 Cook 的 `UObject`；普通指令走 IPC，资产变更先增量 Cook，再对明确支持重载的资产通知测试进程刷新。DLL、UE 反射类型和不支持重载的资产仍需重启测试进程。PIE 成功不替代 Shipping 包加载测试。

`DevelopmentEditor` 的接口浏览器读取 Game Mod SDK Schema，展示每项契约的 ID、版本、参数、调用线程、权限、示例和弃用状态。Blueprint 创作节点只暴露 SDK 中可用的接口，并在 Cook 前检查父类与软引用能在目标游戏中解析。Local PIE 使用游戏提供的 Preview Host；没有相应宿主实现的 API 或 Hook 应显示“不可预览”，不能返回看似成功的模拟结果。

模组开发编辑器至少提供 Manifest 编辑、依赖与冲突检查、内容 ID 浏览、包结构/哈希检查、构建日志和兼容性报告。发布包构建要可重复：同一输入与工具链生成相同的规范化 Manifest、索引和内容顺序；签名时间戳等非确定性元数据放在规范化签名域之外或明确固定。

## 9. 性能、可信度与可观测性

- **启动成本**：模组数量、包大小和哈希校验影响启动耗时；扫描仅读元数据，资产只在需求出现时加载。记录扫描、验证、提取、挂载、注册各阶段耗时。
- **运行成本**：内容候选链在注册阶段决议，对象创建时缓存结果；Gameplay 热点使用已建立的类型化绑定及生成连接。运行时不分析类型或选择转换器；对象视图、容器递归投影、复制/分配、句柄检查和 Blueprint VM 仍按实际工作计量。
- **内存**：挂载容器不等于加载全部 `UObject`。异步加载完成后，持有者负责强引用与释放；记录活动资产和流式加载失败。
- **可信度**：完整性、签名、依赖合法性和原生能力策略是不同检查。允许 Native DLL 就意味着允许该 DLL 在游戏进程权限内执行代码，不能把它描述为隔离运行。
- **诊断**：每个模组输出启用状态、拒载原因、依赖链、最终内容 ID 来源、挂载路径、Native ABI 协商结果及资产加载错误。用户可在启动报告中定位冲突，而不必依赖逐帧日志。

## 10. 里程碑与验收门槛

下表是发布验收目标。当前 Win64 宿主生成已接入真实构建，Linux CI 覆盖 Game/Editor 编译和制作插件构建。开发链路已包含 SDK、归档打包加载和 World 扩展样例。Shipping 的 Pak/IoStore、目标引擎与工具链配对、长期会话与资源清理、Dedicated Server 和远程预览仍需分别验收。

| 阶段 | 交付范围 | 可验收结果 |
| --- | --- | --- |
| P0 | 两插件结构、独立 `BuildPlugin`、共享协议、Runtime SDK 发布 | Game/Editor Target 编译 Runtime；制作编辑器独立构建并安装；发布副本不依赖源仓库路径 |
| G1 技术闸门 | 在最小游戏中先验证 Shipping 资源链路，可使用一次性实验包 | Win64 Development 与 Shipping 分别挂载 Pak/IoStore、读取 Asset Registry、软引用加载静态网格/材质/蓝图资产；逐后端记录通过或失败 |
| P1 | 游戏接入样例、API/Hook/Event 契约、类型化 Hook 和 Runtime Registry | 显式调用点与服务实例生效；无监听者走原版；冲突、重入、权限和生命周期行为可重复验证 |
| P2 | MHT 统一 IR 与代码生成、UHT Exporter/UBT 原生接入、MBT 清单校验和 SDK Bundle、C ABI 样例 | 宏/UHT 共存、生成头时序和首次/增量构建通过；全部转换函数体实际编译；生成清单与分目标产物一致，独立作者环境消费匹配 Editor Bundle |
| P3 | `.mod` 读写、校验、依赖决议、已通过 G1 的容器挂载 | 从单文件 `.mod` 在目标 Shipping 游戏中加载资产与 DLL；损坏包、缺依赖和依赖环稳定拒载 |
| P4 | 逻辑 ID 的提供/覆盖/禁用、World Patch、跨 Mod 服务与生命周期 | 两个模组的冲突决议可重现；世界修改在指定时机及权限下生效，重启与存档迁移规则明确 |
| P5 | 模组开发编辑器的 SDK 浏览、Local PIE、Blueprint 工具、Build & Test | 作者工程可制作、预览、Cook 并生成可在目标 Shipping 游戏加载的包 |
| P6 | Cooked Shader Library、Remote Preview 与增量资源同步 | 材质显示及 Shader 产物按目标平台通过；IPC 版本协商、增量 Cook 和真实游戏预览可复现 |
| P7 研究 | 更广的原生模块目标、新反射/Global Shader 类型、热替换 | 每项形成指定 UE 构建与 Shipping 模式的技术报告，再决定是否进入支持矩阵 |

G1 作为每种资源后端进入 Shipping 支持矩阵的门槛，可与契约和运行时开发并行。实验必须覆盖两个模组的独立包命名空间、资产注册、异步加载、材质显示和游戏重启后的加载一致性；Pak 与 IoStore 分别判定，不能用一个后端的结果推断另一个。只有通过的 UE/平台/游戏 Cook Profile 组合进入兼容性矩阵。P1 至 P6 的排序表达产品依赖，不阻止不共享状态的原型实验并行。

## 11. 待验证与待决定事项

1. **Shipping 资源链路**：UE 5.6 Win64 下，Pak 与 IoStore 分别能否在宿主进程启动期动态挂载，并让 Cooked Asset Registry 与软引用正确解析？验证结果决定首版启用哪些后端。
2. **原生构建与类型集**。从现行 Win64 接入扩展到更多构建模式和平台，分别验证生成头时序、增量失效、C ABI／蓝图支持的类型组合及分目标 SDK 消费。
3. **原生能力范围**。分别验收现行独立 C ABI 和同构 UE 模块的目标组合，再评估更广的反射、Shader 类型与热替换。
4. **Cook Profile 的发布责任**：接入游戏须确定作者工程所需的游戏 SDK、Cook 参数、可引用类和兼容版本发布流程。
5. **模组信任策略**。现有宿主可固定整包摘要、设置许可名单并分别启用两种原生 DLL。签名格式及公钥发布需要独立设计。
6. **多人游戏验收**。开发宿主已有登录身份与玩法指纹握手。正式游戏须在自己的 Client/Server 构建、模组发行集合和切图路径上验证拒绝、断线与重连。

## 12. 参考资料

- [UE 5.6 Unreal Header Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6)：UHT 类型模型、C# Exporter、输出目录与显式包含生成文件的约束。
- [UE 5.6 UBT Target Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-build-tool-target-reference?application_version=5.6)：PreBuildSteps/PostBuildSteps；精确时序由目标 Profile 验收。
- [UE 5.6 Plugins](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine?application_version=5.6)：插件目录、模块类型与目标。
- [UE 5.6 Metadata Specifiers](https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine?application_version=5.6)：元数据仅存在于 Editor，不能作为 Shipping 注册表。
- [UE 5.6 Multi-cast Delegates](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine?application_version=5.6)：广播顺序不保证稳定。
- [UE 5.6 Asynchronous Asset Loading](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine?application_version=5.6)：软引用与异步加载。
- [UE 5.6 Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine?application_version=5.6)：Primary Asset、Asset Manager 和 Streamable Handle。
- [UE 5.6 FPakPlatformFile::Mount](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/PakFile/FPakPlatformFile/Mount?application_version=5.6)：Pak 挂载 API 与委托建议。
- [UE 5.6 Module API Specifiers](https://dev.epicgames.com/documentation/en-us/unreal-engine/module-api-specifiers-in-unreal-engine?application_version=5.6)：模块化与 Monolithic 构建的符号导出边界。
- [UE 5.6 Adding Global Shaders](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)：Shader 类型的早期注册限制。
- [UE 5.6 Packaging Projects](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project?application_version=5.6)：Build、Cook、Stage、Package 与目标平台产物。
