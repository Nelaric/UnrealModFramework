<!-- Copyright (c) 2026 Nelaric Contributors -->

# UnrealModFramework 技术设计

> 设计基线 v0.5 · 实现快照 `b0db0aa`（2026 年 11 月 24 日核对）\
> 开发基线 Unreal Engine 5.6\
> 读者：游戏接入、框架与模组工具开发者

开发宿主现已接入 C++ 与蓝图导出、游戏 SDK 及独立制作插件构建、`.mod` 打包与加载，以及 World 作用域的模组扩展。其中，World 集成覆盖 Hook/Event、内容槽、Patch、跨模组服务、多人玩法计划核对及存档迁移；游戏通过实际业务调用点和显式注册来决定这些能力在何处生效。

UBT/UHT 提供 Target 上下文，MHT 生成契约与连接代码，UBT 在所属模块内编译宿主。MBT 驱动自动 Hook 源替换并核对桌面最终链接。MDT 消费各目标的生成清单，提供游戏和独立作者工程的共享 SDK Runtime。[UHT 接入](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md) · [MDT 指南](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)

下文保留完整的发布目标；当前代码覆盖、样例入口及待验收范围参见[游戏端详细设计](UnrealModFramework_游戏端详细设计.md#当前实现与限制)。Shipping 构建、各容器后端及更多平台将分别开展验收。

## 1. 目标与边界

框架由游戏内的 **Mod Runtime**、独立构建的 **模组开发编辑器**、版本化的 **Mod Protocol / SDK** 以及单文件交付的 **`.mod` 包** 组成。游戏开发者负责主动接入 Runtime 与游戏扩展点；模组作者在独立的 UE 工程中制作内容，经 Editor 构建 `.mod` 包；游戏启动时验证、挂载并注册模组，Gameplay 阶段则沿用 UE 原生对象与软引用加载机制。

| 能力 | 当前范围 |
| --- | --- |
| Cooked 资产 | 归档与运行时覆盖 Pak/IoStore 内容路径；样例宿主使用 Pak，Shipping 按平台、Cook 配置和后端逐项验收 |
| 内容 ID 与 World Patch | 由宿主定义内容槽与锚点；支持内容提供、替换和禁用，以及 Actor 生成、组件挂接和数据注册 |
| 原生代码 | 在 Win64 x64 Pak 上分别支持匹配宿主构建的 UE 模块与独立 C ABI 扩展 DLL，均由宿主显式启用 |
| Blueprint | 宿主蓝图可导出受支持的实例 API 及已接入的 Hook/Event；生成的 SDK 提供反射调用、注册与生命周期类型 |
| 材质 Shader Code | 加载器处理与 Cook 内容匹配的 Shader 库，渲染支持按目标 RHI 和 Shader 平台验收 |
| 卸载与替换 | 内容包可在会话租约与对象引用释放后卸载；UE 原生模块驻留至进程退出，同身份原生版本替换需要重启 |
| 任意 UE 反射或全局 Shader 插件 | 任意构建组合下的类型与全局 Shader 动态注册仍属研究范围 |

需要说明的是，这里的“通用”仅指框架机制本身可迁移，不能绕过不同游戏各自的资源命名、Gameplay 语义、打包配置和扩展接口；硬编码在游戏中的资源路径也不会自动变为逻辑 ID。

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
| `ExportMetadata` | 导出元数据与配置选项；其余运行时服务按具体功能扩展 | 游戏工程中的源码插件 |
| `ExportCodeGenerator` | 通过 `ModExportOptions` 导出配置与重定向，通过 `ModExportBlueprints` 导出已配置的宿主蓝图 | 随 ModRuntime 源码插件编译，仅 Editor Target 加载 |
| `BlueprintIntegration` | 提供“蓝图Mod导出”面板、蓝图内独立的 API/Event/Hook 声明、接入节点及编译校验 | 随 ModRuntime 源码插件编译，仅 Editor Target 加载 |
| `ModHeaderTool`（MHT） | 基于 Target 的 libclang AST 与蓝图快照解析受支持的签名，生成投影、宿主代码、ABI 及清单 | 独立 C++ 工具，通过 C# UHT Exporter 适配器接入 |
| `ModBuildTool`（MBT） | 驱动 UBT 使用生成的 Hook 源码，构建并校验独立宿主归档及其链接结果 | 独立 C++ 工具，通过 UHT 和 UBT 步骤接入 |
| `ModSourceTool`（MST） | 改写 Hook 源码副本、分析失败返回与异常路径、生成宿主链接头 | 独立 C++ 工具，参与宿主生成和编译 |
| `ModLoader`、`ModAPI` | 负责进程级包准入与挂载、会话实例管理、API 绑定及 World 扩展 | 随游戏编译的 Runtime 模块 |
| `ModSDKTool`（MDT） | 生成并构建制作插件与游戏专属 SDK，负责产物校验与发布 | 独立 CMake/C++20 可执行工具，接入统一工具构建入口 |
| `DevelopmentEditor` | 负责作者工程内的 SDK 任务、DLC Cook/Stage 及 `.mod` 打包 | 由 MDT 生成并独立构建的编辑器插件 |
| 版本化协议 | 定义生成清单、SDK 契约、包归档及扩展 ABI | 工具协议 Schema、生成的公开头文件，以及打包器与加载器的读写契约 |
| 游戏 Mod SDK Bundle | 包含公共 GameModSDK、受限视图、反射类型、契约、链接/模块元数据、Cook Profile 与能力报告 | 同批生成、按 Game/Editor 等目标构建并原子发布 |
| 游戏 SDK 的 UE 模块 | 提供公开反射类型、调用与注册适配（例如 `UMFDevSDK`） | 由 MDT 生成；作者 Editor 版与游戏端配套版本分别构建 |
| `.mod` | 封装 Manifest、原生 DLL、Cooked 容器、Shader 产物、注册表及索引 | 单文件分发；运行时可提取至受控缓存 |

`DevelopmentEditor` 保持独立构建，仅通过 SDK 与包协议获取游戏公开信息；规划中的游戏专属 Editor 节点及跨进程预览仍可作为独立适配接入。

## 3. 源工程与构建

| 路径 | 内容 |
| --- | --- |
| `UnrealModFramework/Source/ExportTest/` | 开发宿主、角色/玩家导出、World 游戏模式与网络/存档接入 |
| `Plugins/ModRuntime/Source/Runtime/` | `HAL`、`ExportMetadata`、`ModLoader` 和 `ModAPI` |
| `Plugins/ModRuntime/Source/Editor/` | `BlueprintIntegration` 和 `ExportCodeGenerator` |
| `Plugins/ModRuntime/Source/ModRuntimeUht/` | UHT Exporter 与 UBT 构建适配 |
| `Plugins/ModRuntime/Tools/` | MHT、MBT、MST、MDT 和内嵌制作插件资源 |
| `ExampleMods/TestMod/`、`Test/Performance/` | 独立作者样例与调用基准 |

表中的插件路径均相对于 `UnrealModFramework/`。Host/Client 的生成文件和库按 Target、平台、配置与架构分别保存在工程或插件的 `Intermediate/ModTools/` 目录下。

Setup 负责准备构建主机上的 `libclang`、配套 Clang 资源头文件和四个独立工具。正式构建使用真实的 UBT 编译数据库，在标准 UHT CodeGen 后由 MHT 解析活动 C++ 声明与蓝图快照；当缺少导出快照所需的工程二进制时，适配层会自动引导已配置的 Development Editor。在 Win64 x64 MSVC 上，MBT 按清单编译宿主与客户端单元，UBT 通过所属模块的 UHT 链接单元及 MST 生成的链接头链接宿主库；其他工具链则由 UHT 登记生成的编译单元，交给 UBT 编译。[Setup 与宿主构建](../README.zh-CN.md#快速开始)

MDT 通过默认引擎插件已禁用的专用项目 Editor 目标编译内嵌的制作插件和游戏专属 SDK 插件，并核对 Editor BuildId、原生库及文件清单。模组作者可将制作插件和游戏 SDK 安装到独立工程的 `Plugins/` 目录。若游戏的 Cook 或 Runtime 需要 SDK 反射类型，游戏端需先安装同批次的宿主 SDK 插件。跨目标的 SDK 发布与原生包交付须单独验收。[SDK 输入与交付](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)

## 4. 游戏接入契约与统一 API

框架本身只提供通用机制，游戏至少需要接入以下契约，对应的模组能力才能正常工作：

1. **宿主身份与兼容性**：声明游戏 ID、游戏版本、Runtime SDK 版本、UE 构建标识、目标平台、Shader 平台及 Cook Profile ID。
2. **内容目录**：定义可扩展的逻辑 ID、内容类型、原版默认值、合法覆盖规则和数据 Schema。
3. **实例创建入口**：在 Actor、组件、物品或其他对象创建时解析逻辑 ID，并将结果缓存至游戏对象中；框架不会在每帧重复查找。
4. **事件入口**：显式发布可供模组订阅的 Gameplay 事件，并标明线程、时序、可修改性和网络权限。
5. **世界修改入口**：为可修改场景提供稳定的对象 ID 与安全的修改时机，不依赖运行时遍历并猜测任意 Actor。
6. **API 实例入口**：为非静态导出成员指定对象提供者、作用域及失效时机；声明标记既不决定本次应调用哪个 `UObject`，也不会自动创建实例。

在运行时职责划分上，`ModLoader` 负责进程准入、归档解析与挂载；`UModInstance` 承载加载完成、注册、绑定及卸载回调；`UModAPIHostSubsystem` 管理 GameInstance 的 API Registry；`UModExtensionSubsystem` 管理单个 World 的扩展计划与生命周期。相关划分详见[模块与依赖边界](CodingStandards/Modules.zh-CN.md)。

### 4.1 逻辑 ID 与覆盖规则

内容 ID 由游戏侧定义，例如 `Creature.Wolf.Mesh`。每条注册项包含 `id`、`type`、`action`、`value`、`ownerModId` 和 `priority`，其中 `action` 取值为 `provide`、`replace` 或 `disable`。`value` 在资产场景下表示软对象路径，在数据内容场景下表示已版本化的结构化记录。原版内容默认视为最低优先级候选。

模组的准备顺序由依赖图决定。针对同一逻辑 ID，运行时会先校验内容槽的权限与候选资格；宿主可通过 `FWorldPolicy::ContentWinners` 显式指定合法候选，否则按优先级决议。若出现最高优先级并列，运行时将报告冲突并列出全部候选。同一模组在同一内容槽内只能为同一个逻辑 ID 登记一次。若 `disable` 最终胜出，该 ID 将固定返回“已禁用”。

需要注意的是，加载顺序并不等于 UE 包路径覆盖顺序。当前归档以 `/<PluginName>/` 作为独立的 Cooked 包根（例如 `/TestMod/`），加载器会拒绝包根及 UE 包路径冲突。玩法覆盖一律通过逻辑 ID 决议，避免将 Pak 优先级当作公共 API 使用。

### 4.2 资产加载与生命周期

逻辑 ID 在初始化或实例创建时解析为 `FSoftObjectPath` / `TSoftObjectPtr`，随后交由 UE 异步加载。游戏在加载回调后对需要长期使用的资产保留强引用，不再使用时则按 UE 对象生命周期正常释放。UE 5.6 官方文档明确指出，软引用可避免被引用资产随持有者立即加载，并可通过 `FStreamableManager` 请求异步加载（参见[UE 5.6 异步资产加载](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine?application_version=5.6)）。

预期性能目标是 **Gameplay 热点路径不反复查询框架**。内容决议、依赖排序和包验证均发生在启动或对象创建阶段；实际运行仍保留 UE/Blueprint 自身的开销，Native ABI 调用也保留一次函数指针调用成本。

### 4.3 API、Hook、Event 的声明和接入

游戏开发者在真实声明前标注独立的 Mod 标记，标准 UE 标记继续用于表达反射。以开发宿主的角色读取接口为例，其声明如下：

```cpp
MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldRead))
EXPORTTEST_API float GetCurrentHealth() const;
```

其中，`MOD_EXPORT_API` 关联具体成员，`MOD_EXPORT_EVENT` 关联显式发布函数，`MOD_EXPORT_HOOK` 关联业务函数；类型身份保存在宿主的 `Config/ModAPITypes.json` 中，生成器仅使用当前 Target 下的活动声明。在开发宿主的[角色业务实现](../UnrealModFramework/Source/ExportTest/GameCharacter.cpp)中，`ChangeHealth` 直接调用 `ApplyHealth`。各字段及声明说明详见[游戏端详细设计第 8 节](UnrealModFramework_游戏端详细设计.md#8-游戏声明与蓝图接入)。

| 机制 | 声明提供的信息 | 游戏必须显式完成的接入 |
| --- | --- | --- |
| API | ID、具体 AST 成员、投影与调用策略 | 注册服务对象提供者或静态入口；指定 World/玩家作用域、线程与权限 |
| C++ Hook | 依据 World 类型 ID 与成员名构成的 ID，以及签名、阶段和授权 | 在声明处标注 `MOD_EXPORT_HOOK()`，在 `.cpp` 中实现原函数，并通过 MBT 构建目标 |
| Event | ID、实际事件源、载荷与回调策略 | 提供事件源实例，在业务发生时通过具体连接发布并管理生命周期 |

#### 蓝图Mod导出

游戏工程中的 `BlueprintIntegration` 在蓝图编辑器侧栏提供“蓝图Mod导出”面板，按当前蓝图显示 `API`、`Event`、`Hook` 三个独立分组，每组均设有创建入口。该面板通过编辑器插件提供的标签页和布局扩展接入，停靠在“我的蓝图”附近；既不修改 UE 引擎源码，也不将这三个分组伪装成原生“我的蓝图”列表的内部分类。对于无法生成运行时实现或缺少相应图能力的蓝图类型，相关创建入口将被禁用并说明原因。

上述三个分组分别创建专有声明，不依赖普通函数、事件分发器或结构体详情面板中的布尔开关：

| 分组 | 独立声明及编辑内容 | 显式业务接入 |
| --- | --- | --- |
| API | 签名、权限、作用域、实例提供者与 API 实现图 | 编译生成可调用入口；非静态入口须提供有效实例及生命周期规则 |
| Event | 载荷类型、发布作用域与线程规则 | 在业务图中放置对应的 Publish Event 节点，向 Event Bus 发布 |
| Hook | 类型化 Context、`AllowedStages`、`AllowedOperations`、原版逻辑与失败规则 | 在业务图中放置对应的 Hook Point 节点并连接原版逻辑 |

各声明均以独立 GUID 保存在蓝图的 `UBlueprintExtension` 中（参见[`UBlueprintExtension` API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UBlueprintExtension)）。API 与 Hook 图由对应声明持有，编译时生成 UE 可执行函数。面板负责创建、选择、重命名、删除和跳转，其中删除时会检查引用并支持撤销。Event 与 Hook 节点均引用声明 GUID。导出时，`ModExportBlueprints` 会编译配置中已保存的资产，并导出执行输入已连接的扩展节点及对应声明；若遇到不支持的载荷或无效签名，将直接报错。Editor-only 声明不作为 Shipping 时的查询来源，而是由生成器输出运行时描述符及绑定。

C++ 的动态多播 Delegate 仍可作为反射事件源，但其广播顺序未定义（参见[UE 5.6 多播 Delegate](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine?application_version=5.6)）；需要稳定顺序时，应由框架 Event Bus 按契约排序分发。

### 4.4 Export Generator 与 Game Mod SDK

**静态生成原则是本框架的固定架构约束。** 所有跨契约边界的类型映射、签名投影、参数与结果转换、可空性、只读性、错误表示、ABI 表示及调用连接，均由 Export Generator 在构建期确定并生成。“类型转换层”归属于生成器的类型分析与代码生成阶段；运行时仅执行已生成的具体代码，处理当前对象、作用域、授权与调用数据，不提供动态转换器选择或通用反射转换回退。

游戏成员保持真实类型与原始签名，生成器按受支持的签名投影参数、结果与载荷。对象返回可投影为类型化视图，供独立扩展使用的对象载荷可投影为只读视图；递归容器与更广泛的 C++ 类型闭包随具体后端扩展。API 的 `FailureRule = ReturnError` 统一生成 `TModResult<T>`，蓝图与 C ABI 则分别使用对应的具体结果表示。视图成员集合在 SDK 生成时即已固定，不会因派生实例或不同 Mod 授权而扩大；实际授权校验均通过生成的入口完成。完整规则参见[游戏端详细设计第10节](UnrealModFramework_游戏端详细设计.md#10-契约生成与构建流水线)和[第11节](UnrealModFramework_游戏端详细设计.md#11-类型化调用连接)。

在解析来源方面，MHT 基于真实 UBT 编译数据库和 libclang AST 解析活动 C++ 声明；`ModExportBlueprints` 从已配置的蓝图资产中提取受支持的编译后签名，再由 MHT 读取该快照并合并生成。当前同步 API 支持标量与公开 UE 对象作为输入，以及标量、`void` 或 World 对象作为结果；World Hook/Event 支持标量、受支持的反射值结构体以及带只读投影的 UObject 载荷。

当前批次的生成产物包括宿主桥与注册代码、编译校验代码、公共 C ABI 头文件、独立客户端实现、SDK 与生命周期契约、扩展描述以及 v3 生成清单。其中，`<game>.generated-artifacts.json` 记录输入哈希、所有者及用途，并显式列出编译单元；MDT 则依据 `.sdkcontract.json` 生成 UE 反射包装。更广泛的递归容器投影与通用类型闭包仍以实际生成后端的支持范围为准，详见[MHT 生成产物](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md)。

生成结果按规范化输入保持稳定排序，内含生成器版本、游戏 SDK 版本、UE 构建标识、源/Mod 签名、投影摘要及输入指纹。Win64 构建会校验生成输入与产物哈希，并通过生成的注册入口向运行时登记；通用 Runtime 始终独立于具体游戏模块。生成器仅自动适配明确支持的完整类型闭包，不会将任意 `UFUNCTION` 参数无条件自动转换为稳定 C ABI——例如 `UObject*` 需要不透明句柄与生命周期规则，复杂容器需要版本化数据传输类型。未知、歧义或不支持的签名均在生成或编译阶段直接失败，而不留待 Gameplay 阶段补齐。扫描后新增的反射包装须在后续正常 UHT 运行前生成，以显式多阶段构建完成。

游戏 SDK 持有公共视图与共享反射类型：现有适配由 MDT 生成至游戏 SDK UE 模块，独立的游戏专属 Editor 模块可按实际需要接入；通用编辑器则保持自包含的共享协议边界，不反向依赖项目级 SDK、Runtime 或游戏私有模块。既有的公共模块名及 `/Script` 身份可继续保留，不因角色命名统一而重命名或复制反射定义。蓝图的具体包装由 Runtime 模块持有，其编译结果仅引用宿主中存在的 Runtime 类型和函数，不伪造真实游戏 UCLASS。

SDK 以同批 Bundle 为交付单位，包含公开头文件、适配源码或等价编译输入、SDK 清单、匹配 Editor 的 DLL 与链接输入、`.uplugin`/`.Build.cs`/`.modules` 及所需预编译元数据，以及契约、Cook Profile 和能力报告。在 Windows 平台上，隐式链接使用导入库，显式加载仍需正确的 ABI 与调用声明（参见[Microsoft DLL 链接说明](https://learn.microsoft.com/en-us/cpp/build/linking-an-executable-to-a-dll?view=msvc-170)）；DLL 并非可再次编译成编辑器插件的源码。

游戏与作者插件使用同名的 Runtime，各自通过 UBT 编译。游戏发行验证将生成身份与最终目标回执、模块二进制及实际 ABI 调用关联。作者 Editor 插件匹配其目标 Editor 的 BuildId。[构建与生成清单](UnrealModFramework_游戏端详细设计.md#10-契约生成与构建流水线) · [UE 二进制版本](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-version-binaries-in-unreal-engine?application_version=5.6)

### 4.5 双执行路径与 C ABI 边界

| 路径 | 适用范围 | 运行时成本与边界 |
| --- | --- | --- |
| Native Fast Path | 已编入宿主的 C++ 服务、类型化 Hook 及缓存的 Native 回调 | 初始化阶段建立类型化绑定；Gameplay 阶段执行已生成的具体转换与检查，调用原成员或固定类型化回调数组 |
| Reflection Path | Blueprint 及仅提供 `UFunction` 的服务 | 构建期固定参数布局、转换与分派规则；绑定具体目标后执行生成连接及 UE 原有的反射/覆盖路径 |
| C ABI 边界 | 独立分发的 Native DLL | 通过已生成的专用类型化函数表、DTO 与不透明句柄进入具体宿主连接，按生成规则保留相应的 UE 执行语义 |

两条执行路径及 C ABI 入口在装载与绑定阶段均由同一 Mod Host 核对导出 ID、版本、签名、投影和能力，绑定结果必须指向已生成的类型化入口。对象生命周期、线程与权限的检查方式由生成器固定，调用时再针对当前状态执行检查；UE 虚调用和蓝图覆盖语义按契约保留。Schema 以“稳定 ID + 主/次导出版本 + 签名/投影摘要”识别接口，破坏签名、权限、线程或语义的变更须提升主版本。构建期出现重复 ID 会直接失败；运行时若 SDK 版本不兼容，则拒载相关模组并给出可机器读取的原因。

普通调用既不按 TypeId 查找转换器、不解析字符串形式的参数类型、不遍历 FProperty 决定转换，也不使用通用参数盒或统一 `Invoke(ExportId, Arguments)` 分发。Reflection Path 保留 UE 本身的执行机制，而框架侧的参数连接已在构建期确定。生成代码在对数组执行遍历、视图构造、复制、分配或句柄校验时，其开销仍按实际工作计量。

### 4.6 Hook 与 Event 的确定性语义

在三类扩展机制中，API 提供可主动调用的能力，Hook 是游戏在真实业务操作中设置的扩展点，Event 则由宿主显式发布已发生的业务事实。Hook 的成功路径依次经过 `Pre`、`On`、`Post` 三个阶段，宿主可在 Hook 完成后按业务规则发布 Event。

| 阶段 | 时机与职责 | 可声明的操作 |
| --- | --- | --- |
| `Pre` | 核心执行前，读取输入并检查条件 | `ModifyInputs` 修改声明允许的输入；`Cancel` 取消执行 |
| `On` | 执行核心业务，默认调用宿主原版逻辑 | `Wrap` 包装执行；`Replace` 接管执行 |
| `Post` | 核心业务成功后，读取结果并执行后续处理 | `ModifyResult` 修改声明允许的结果 |

C++ 的 `MOD_EXPORT_HOOK()` 默认生成 `Pre`、`On` 和 `Post` 阶段，并声明 `ModifyInputs`、`Cancel` 和 `Replace` 操作；`NoPre`、`NoOn`、`NoPost` 分别关闭对应阶段及其操作，且对 `CompatibleNames` 中的曾用名同样生效。在蓝图编辑器中单独声明的 Hook 通过 `AllowedStages` 和 `AllowedOperations` 保存策略，默认仅开放 `Post` 阶段且操作列表为空。每项操作都要求对应阶段开放；即使开启了 `On` 阶段，仍须分别显式授权 `Wrap` 或 `Replace` 才允许 Mod 介入核心执行。若在 `Pre` 阶段取消，将跳过 `On` 和 `Post`；若执行失败，则由宿主返回错误并可按需单独发布事实通知。注册阶段会建立稳定的执行快照，阶段权限、顺序与错误规则详见[游戏端详细设计第18节](UnrealModFramework_游戏端详细设计.md#18-hook-语义与执行计划)。

Hook 契约还须指定失败策略、重入规则、调用线程与网络权威。首版默认采用 Game Thread 同步执行，并默认拒绝同键重入；跨线程或异步 Hook 必须单独声明并验证对象生命周期。Event 监听者仅接收只读载荷，业务结果仍由宿主决定。Event Bus 在注册阶段缓存订阅者，广播前校验模组与源对象是否有效，默认在 Game Thread 上回调。需要指出的是，原生 DLL 自身的崩溃无法依赖框架回调层隔离。

### 4.7 World Patch 与跨 Mod 服务

宿主通过 `RegisterAnchor` 开放稳定锚点及对应权限。扩展清单可请求生成 Actor、在已批准的挂接点注册场景组件，或注册 Cooked 数据资产。运行时会统一检查依赖、冲突、服务前置条件与权限，并在 World 的安全阶段应用变更并记录来源。清理阶段仅回收框架持有的效果，游戏业务副作用则需宿主自行定义补偿逻辑。

跨模组服务的提供与依赖均在扩展清单中声明。提供者在 **On Mod Register** 阶段登记实现，或在已声明的 Patch Actor 上提供服务；消费者则在 **On Mod Bind** 阶段通过生成的 SDK 获取匹配签名与版本的 World 绑定。当模组停止、权限变更或服务目标失效时，调用资格由宿主统一管理。相关字段详见[包和服务字段](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#mod-布局)。

## 5. `.mod` 包协议

`.mod` 采用单文件封装格式。当前归档以固定 64 字节的小端序 Header 开始，后接 UTF-8 Manifest、连续载荷和 UTF-8 Index。归档主版本中，v1 仅携带内容资产，v2 增加了匹配宿主的 UE 原生模块支持，v3 则进一步携带扩展清单，并可包含 UE 模块或独立回调 DLL；其中扩展清单拥有独立的 Schema v1/v2。

### 5.1 Manifest 的必需信息

`UMF.ModPackage` 记录 `modId`、三段版本、插件身份及 `/<PluginName>/` 包根、`cookProfileId` 与配置详情，以及包内 Cook 回执的条目路径。宿主依次核对 GameId、基础发布及其 Asset Registry SHA-256、引擎构建、目标平台、Cook 平台与后端。Manifest 还可附带 `lifecycleClasses`、`nativeCode` 与 `extensions`，分别描述实例类、UE 模块及世界扩展。归档 v3 的扩展清单则进一步记录 Hook/Event 签名、依赖、互斥、所需授权、内容、Patch 和版本化服务。详见[归档字节布局和字段](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#mod-布局)。

### 5.2 解析、校验与缓存

外层归档当前采用不压缩、无签名的格式。索引对规范化路径进行排序，记录偏移、长度及条目 SHA-256，直接拒绝目录穿越、重复路径与范围错误。`ModLoader` 支持按整包 SHA-256 启用许可名单与固定修订。提取后的载荷存放在 `Saved/ModCache/<整包 SHA-256>` 目录下，复用前须重新核对。归档签名与公钥管理仍属于设计项；需注意，获准执行的原生代码拥有宿主进程权限。

### 5.3 Cooked 内容挂载

包后端由宿主显式声明支持 `Pak`、`IoStore` 或二者。运行时在验证完成后，先为模组建立独立包命名空间并挂载资源容器，使 Cooked Asset Registry 可见，再发布框架自身的逻辑内容注册表（两种 Registry 为不同数据结构）。UE 5.6 提供了 Pak 挂载接口，官方 API 建议通常经 `FCoreDelegates` 的挂载委托调用，其挂载参数亦可报告相关的 IoStore 状态（参见[UE 5.6 Pak 挂载 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/PakFile/FPakPlatformFile/Mount?application_version=5.6)）。

**Pak 与 IoStore 在 Shipping 中的动态挂载、包路径解析、Asset Registry 合并、软引用加载和材质显示，必须分别通过端到端实验。** 在通过实验前，它们仅代表目标能力，而非已验证的兼容承诺。首版实现可依据验证结果只启用已通过的容器后端；对于不支持的包，则在启动阶段给出明确错误。

## 6. 启动编排与失败处理

| 阶段 | 当前接入 |
| --- | --- |
| 进程准入 | 验证归档完整性，确定启用或必需集合，核对固定摘要、权限、契约、依赖与冲突 |
| 资源准备 | 提取并挂载容器、登记包根并追加可用的 Cooked Registry；需要时先加载匹配宿主构建的 UE 模块，再实例化蓝图类 |
| 会话实例 | `ModLoader` 向会话分发加载结果；首次准备冻结包集合与授权，同 GameInstance 的后续 World 直接复用 |
| World 准备 | 宿主先定义内容槽与锚点，再调用 `UModExtensionSubsystem::Prepare`；运行时随后执行注册、内容与 Patch 决议、服务绑定及 Hook/Event 快照 |
| 玩法准入 | World 准备完成后提供玩法指纹；宿主据此在启动、服务器登录或客户端进图时接入玩法屏障 |
| 关闭 | 停止分发，清理自有 Patch 与绑定，释放包租约；仍有引用时保留挂载，涉及不可逆状态时可要求重启 |

上述步骤已通过 `ModExtensions` 模式接入开发宿主，但每个 World 仍须按游戏实际的 `StartPlay`、切图、玩家登录及关闭时序分别验收。详见[游戏端就绪屏障](UnrealModFramework_游戏端详细设计.md#13-宿主状态机与就绪屏障)。

## 7. 原生 ABI 与 Shader 边界

独立扩展 DLL 通过 `UMF_RegisterExtensionsV1` 注册 World 回调。宿主表以 `StructSize` 和 `Version` 描述版本，提供注册、退订、Hook 续调及宿主 API 表查询能力。注册时按 SDK 中的 ID 与签名进行校验；输入、输出、对象及续调令牌均随当前调用帧失效。World 停止时，先排空回调，再调用可选的 `Close`。

生成 API 的客户端采用 `ContextHandlesV1`，通过定宽标量、显式长度和不透明句柄交换数据，并依据对应的 ABI 哈希校验函数表。调用边界遵循既定的借用期与缓冲区规则，异常须在各自模块内部处理，UE 对象或 C++ 容器布局均留在 UE 模块内。宿主已编译的 C++ Fast Path 仍通过具体类型化入口调用游戏服务。详见[独立 DLL 回调](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#独立原生回调)与[生成 API 连接](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md)。

当前工程打包模式依托宿主 Target 回执，为 Win64 x64 Pak 发布编译 UE 模块；加载器在核对归档、构建身份与 BuildId 后加载模块，以保留 Cooked 蓝图的父类身份。相比之下，在任意 Target 上动态扩展反射或新增 Global Shader 类型仍是独立研究方向——新反射与 Shader 类型分别依赖相应的 UHT 与引擎装载阶段，只有在指定 UE 构建、目标平台及 Shipping 链接模式完成验证后，才会定义其兼容性与交付方式。材质使用的 Cooked Shader Code 与“新增 Global Shader 类型”须分别验收，不能用前者的成功推断后者可用。UE 文档明确指出，新增 Global Shader 类型的模块必须在引擎初始化前加载，游戏启动后的动态模块不能再添加 Shader 类型（参见[UE 5.6 Global Shader 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)）。

## 8. 模组开发编辑器与预览

当前制作插件可在独立作者工程中发起 SDK 计划、构建与 `.mod` 打包任务，具体操作详见[制作编辑器操作](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md)。其中，`TestMod` 提供作者工程内的角色预览以及原生模块与生命周期蓝图样例，其余通用预览模式按下表设计与验收：

| 模式 | 数据路径 | 适用范围 |
| --- | --- | --- |
| Local PIE | 模组作者工程中的未 Cook 资产 + Preview Host + Runtime SDK | 快速测试逻辑 ID、内容注册和编辑中的资产 |
| Remote Preview | 已启动的真实游戏测试进程 + 版本化 IPC + 必要的增量 Cook 资源 | 验证游戏特有流程与实际运行表现 |
| Build & Test | 完整 Cook、生成 `.mod`、包验证与目标游戏加载 | 发布前的权威结果 |

Local PIE 仅模拟已由游戏 SDK 或 Preview Host 提供的行为；需要验证完整游戏逻辑时，游戏开发者必须提供可供作者工程使用的运行时模块或测试宿主。在 Remote Preview 模式下，编辑器与游戏进程不能共享未 Cook 的 `UObject`：普通指令走 IPC，资产变更则先经增量 Cook，再对明确支持重载的资产通知测试进程刷新；至于 DLL、UE 反射类型和不支持重载的资产，仍须重启测试进程。PIE 通过不能替代 Shipping 包加载测试。

`DevelopmentEditor` 的接口浏览器读取 Game Mod SDK Schema，展示每项契约的 ID、版本、参数、调用线程、权限、示例及弃用状态。Blueprint 创作节点只暴露 SDK 中可用的接口，并在 Cook 前检查父类与软引用能否在目标游戏中解析。Local PIE 使用游戏提供的 Preview Host；对于没有相应宿主实现的 API 或 Hook，应显示“不可预览”，而不能返回看似成功的模拟结果。

模组开发编辑器至少提供 Manifest 编辑、依赖与冲突检查、内容 ID 浏览、包结构/哈希检查、构建日志与兼容性报告。发布包构建须可重复：同一输入与工具链应生成相同的规范化 Manifest、索引和内容顺序；签名时间戳等非确定性元数据则应置于规范化签名域之外或予以明确固定。

## 9. 性能、可信度与可观测性

- **启动成本**：模组数量、包体大小和哈希校验都会影响启动耗时；因此扫描阶段仅读取元数据，资产在需求出现时再加载，同时记录扫描、验证、提取、挂载与注册各阶段的耗时。
- **运行成本**：内容候选链在注册阶段决议，并在对象创建时缓存结果；Gameplay 热点直接使用已建立的类型化绑定及生成连接。运行时不分析类型或选择转换器，但对象视图、容器递归投影、复制/分配、句柄检查与 Blueprint VM 仍按实际工作计量。
- **内存**：挂载容器并不等同于加载全部 `UObject`。异步加载完成后，由持有者负责管理强引用与释放，并记录活动资产与流式加载失败信息。
- **可信度**：完整性、签名、依赖合法性与原生能力策略分属不同检查维度。允许加载 Native DLL 即意味着允许该 DLL 在游戏进程权限内执行代码，不应将其表述为隔离运行。
- **诊断**：为每个模组输出启用状态、拒载原因、依赖链、最终内容 ID 来源、挂载路径、Native ABI 协商结果及资产加载错误，使用户可在启动报告中直接定位冲突，而不必依赖逐帧日志。

## 10. 里程碑与验收门槛

Win64 x64 MSVC 已接入独立宿主归档与链接核对。Linux x64 CI 覆盖 UE 5.6.1 Development Game/Editor 构建、Editor Hook 分发自动化测试与制作插件构建。开发链路还包含 SDK、归档打包加载及 World 扩展样例。Shipping Pak/IoStore、更多引擎与工具链组合、长期会话、资源清理、Dedicated Server 及远程预览仍需分别验收。[CI 构建范围](DevelopmentWorkflow.zh-CN.md#linux-编译的范围)

| 阶段 | 交付范围 | 可验收结果 |
| --- | --- | --- |
| P0 | 两插件结构、独立 Editor 目标、共享协议、Runtime SDK 发布 | Game/Editor Target 可编译 Runtime；制作编辑器可独立构建并安装；发布副本不依赖源仓库路径 |
| G1 技术闸门 | 在最小游戏中先验证 Shipping 资源链路，可使用一次性实验包 | Win64 Development 与 Shipping 分别挂载 Pak/IoStore、读取 Asset Registry、软引用加载静态网格/材质/蓝图资产，并逐后端记录通过或失败 |
| P1 | 游戏接入样例、API/Hook/Event 契约、类型化 Hook 和 Runtime Registry | 显式调用点与服务实例生效；无监听者时走原版逻辑；冲突、重入、权限与生命周期行为均可重复验证 |
| P2 | MHT 统一 IR 与代码生成、UHT Exporter/UBT 原生接入、MBT 清单校验、SDK Bundle 与 C ABI 样例 | 宏与 UHT 共存、生成头时序及首次/增量构建均通过；全部转换函数体实际编译；生成清单与分目标产物一致，独立作者环境可消费匹配的 Editor Bundle |
| P3 | `.mod` 读写、校验、依赖决议及已通过 G1 的容器挂载 | 可从单文件 `.mod` 在目标 Shipping 游戏中加载资产与 DLL；损坏包、缺依赖与依赖环稳定拒载 |
| P4 | 逻辑 ID 的提供/覆盖/禁用、World Patch、跨 Mod 服务与生命周期 | 两个模组的冲突决议可重现；世界修改在指定时机及权限下生效；重启与存档迁移规则明确 |
| P5 | 模组开发编辑器的 SDK 浏览、Local PIE、Blueprint 工具、Build & Test | 作者工程可完成制作、预览、Cook，并生成可在目标 Shipping 游戏中加载的包 |
| P6 | Cooked Shader Library、Remote Preview 与增量资源同步 | 材质显示及 Shader 产物按目标平台验收通过；IPC 版本协商、增量 Cook 与真实游戏预览可复现 |
| P7 研究 | 更广泛的原生模块目标、新反射/Global Shader 类型与热替换 | 每项针对指定 UE 构建与 Shipping 模式形成技术报告，再决定是否进入支持矩阵 |

G1 作为每种资源后端进入 Shipping 支持矩阵的门槛，可与契约及运行时开发并行推进。该实验必须覆盖两个模组的独立包命名空间、资产注册、异步加载、材质显示以及游戏重启后的加载一致性；Pak 与 IoStore 须分别判定，不能用一个后端的结果推断另一个。只有通过验证的 UE/平台/游戏 Cook Profile 组合才会进入兼容性矩阵。此外，P1 至 P6 的排序仅表达产品依赖，不妨碍不共享状态的原型实验并行开展。

## 11. 待验证与待决定事项

1. **Shipping 资源链路**：在 UE 5.6 Win64 下，Pak 与 IoStore 分别能否在宿主进程启动期动态挂载，并让 Cooked Asset Registry 与软引用正确解析？验证结果将决定首版启用哪些后端。
2. **原生构建与类型集**：新增构建模式和平台时，须分别验证生成头时序、增量失效、C ABI 与蓝图的类型组合，以及分目标 SDK 消费。
3. **原生能力范围**：须分别验收现行独立 C ABI 与同构 UE 模块的目标组合，再评估更广泛的反射、Shader 类型与热替换。
4. **Cook Profile 的发布责任**：接入游戏须明确作者工程所需的游戏 SDK、Cook 参数、可引用类及兼容版本的发布流程。
5. **模组信任策略**：现有宿主已支持固定整包摘要、设置许可名单并分别启用两种原生 DLL；签名格式及公钥发布需要独立设计。
6. **多人游戏验收**：开发宿主已有登录身份与玩法指纹握手；正式游戏须在自身的 Client/Server 构建、模组发行集合和切图路径上验证拒绝、断线与重连。

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
