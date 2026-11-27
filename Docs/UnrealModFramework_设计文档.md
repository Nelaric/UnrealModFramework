<!-- Copyright (c) 2026 Nelaric Contributors -->

# UnrealModFramework 技术设计

> 文件版本 **v0.5** \
> 更新日期 **2026年11月26日**

本文阐述 UnrealModFramework 的总体架构、核心契约与分目标验收标准。目前，仓库内的开发宿主已完成 C++ 与蓝图导出、游戏 SDK 及独立制作插件构建、`.mod` 归档打包与挂载，以及 World 作用域扩展的端到端闭环——涵盖 Hook/Event、逻辑内容槽、World Patch、跨 Mod 服务、多人联机方案握手与单机存档迁移。宿主游戏通过显式的业务调用点与注册接口，精确控制开放哪些扩展能力及其生效时机。

在构建链路上，UBT/UHT 负责提供 Target 编译上下文，ModHeaderTool（MHT）负责解析声明并生成契约与桥接代码，生成的宿主代码由 UBT 直接在所属模块内部编译；ModBuildTool（MBT）负责驱动自动 C++ Hook 的源码副本替换并核验桌面目标的最终链接产物；ModSDKTool（MDT）则消费各目标的生成清单，构建供游戏侧和独立 Mod 工程共用的 SDK Runtime（详见 [UHT 适配层说明](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)与 [MDT 指南](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)）。

Shipping 构建、不同容器后端及更多目标平台均按具体组合逐项验收，详见[第 10 节：里程碑与验收门槛](#10-里程碑与验收门槛)。

## 1. 目标与边界

整套框架由四个核心部分组成：嵌入游戏的 **Mod Runtime**、独立构建的 **Mod 制作编辑器（`ModDevelopmentSDKEditor`）**、版本化的 **Mod Protocol / SDK**，以及单文件交付的 **`.mod` 归档包**。

日常协作分工如下：游戏开发者负责接入 Runtime 并声明扩展点；Mod 作者在独立的 UE 工程中安装游戏 SDK 与制作插件，开发内容并打包出 `.mod` 文件；游戏进程启动时完成归档校验、容器挂载与扩展注册，进入 Gameplay 后则直接沿用 UE 原生的对象模型与软引用异步加载链路。

| 能力维度 | 当前支持范围与验收边界 |
| --- | --- |
| Cooked 资产 | 归档格式与加载器同时支持 Pak 和 IoStore；开发宿主默认使用 Pak，Shipping 环境须按平台、Cook 参数和容器后端逐项实测验收 |
| 逻辑内容 ID 与 World Patch | 宿主显式定义内容槽与场景锚点；支持内容的提供（`provide`）、替换（`replace`）与禁用（`disable`），以及生成 Actor、挂接组件和注册数据资产 |
| 原生代码扩展 | 在 Win64 x64 Pak 宿主上同时支持匹配宿主构建的 UE 原生模块与独立的 C ABI 回调 DLL，两者均须由宿主配置显式开启 |
| Blueprint 支持 | 宿主蓝图可导出受支持的实例 API 及已连线的 Hook/Event；生成的 SDK 向作者提供反射调用节点、注册接口与生命周期基类 |
| 材质 Shader Code | 加载器支持随 Cook 内容一并挂载配套的 Shader 库；具体渲染表现须按目标 RHI 与 Shader 平台分别验收 |
| 运行期卸载与更新 | 纯内容包可在会话租约与外部对象引用全部释放后动态卸载；已加载的 Win64 UE 原生模块常驻至进程退出，更新同名原生模块需重启游戏 |
| 任意反射类型或 Global Shader | 在任意构建组合下动态注入新 UE 反射类型或新 Global Shader 类型，目前仍属于独立研究课题 |

需要明确的是，框架的“通用性”是指底层机制可跨项目复用，但它无法替具体游戏自动抹平资源命名约定、Gameplay 语义、打包配置或业务扩展点；游戏代码里硬编码的资产路径也不会自动变成可被 Mod 覆盖的逻辑 ID。

## 2. 参与者与核心组件

```text
Mod 作者独立 UE 工程 ── Mod 制作编辑器 ── 构建与字节级校验 ── xxx.mod
         │                     │                                │
         └── 作者版 Game SDK    │                                ▼
                               │                    游戏启动 ── Mod Runtime
                               │                                │
                               └─ 共享版本化契约 ──► 校验 → 挂载 → 注册 → 按需加载
                                                                │
                                                      UE 原生 Gameplay 环境
```

| 组件 | 核心职责 | 交付与运行形态 |
| --- | --- | --- |
| `ExportMetadata` | 定义导出宏、元数据 Schema 与配置项读取；后续运行时服务按具体领域独立拆模块 | 游戏工程内的源码插件模块（Game/Editor 均编译） |
| `ExportCodeGenerator` | 通过 `ModExportOptions` 导出配置候选项与重定向，通过 `ModExportBlueprints` 编译并导出宿主蓝图声明快照 | 随 `ModRuntime` 编译，仅在 Editor Target 加载 |
| `BlueprintIntegration` | 在蓝图编辑器提供 **蓝图Mod导出** 面板、独立的 API/Event/Hook 事务声明、业务接入节点及编译校验 | 随 `ModRuntime` 编译，仅在 Editor Target 加载 |
| `ModHeaderTool`（MHT） | 结合目标 `libclang` AST 与蓝图快照解析有效签名，生成类型视图、宿主桥接代码、C ABI 头文件及清单 | 独立 C++20 命令行工具，由 C# UHT Exporter 调用 |
| `ModBuildTool`（MBT） | 驱动 UBT 使用改写后的 Hook 源码副本编译，并在链接后核验受支持桌面目标的宿主二进制 | 独立 C++20 命令行工具，由构建入口与 Target 规则调用 |
| `ModSourceTool`（MST） | 基于编译数据库静态分析 API 错误返回/异常路径，并改写自动 Hook 的类外函数定义 | 独立 C++20 命令行工具，在生成阶段由 MHT 调用 |
| `ModLoader`、`ModAPI` | 负责进程级 `.mod` 准入与容器挂载、会话实例管理、API 调用绑定及 World 扩展编排 | 随宿主游戏一起编译的 Runtime 模块 |
| `ModSDKTool`（MDT） | 汇总多目标生成清单，生成并编译游戏侧/作者侧 SDK 与独立制作插件，执行哈希核验与原子发布 | 独立 C++20 命令行工具，接入统一 CMake 构建入口 |
| `DevelopmentEditor` | 在作者工程内提供 SDK 构建菜单、DLC Cook/Stage 调度及 `.mod` 打包能力 | 由 MDT 释放内嵌源码并通过 UAT 独立构建的编辑器插件 |
| 版本化协议族 | 约束生成清单、SDK 契约、`.mod` 归档二进制布局及扩展 C ABI | JSON Schema、生成的纯 C 公开头文件及读写校验器 |
| 游戏 Mod SDK 发布包 | 打包交付游戏侧 SDK 源码插件、预编译作者插件、公开 C ABI 头文件与契约清单 | 由 MDT 核验全部目标输入一致后原子发布 |
| `<PluginName>Runtime` 模块 | 承载公开反射类型、Client 调用表连接与注册适配（如示例中的 `UMFDevSDKRuntime`） | 由 MDT 生成同名模块，在宿主游戏与作者编辑器中分别编译 |
| `.mod` 归档文件 | 单文件封装 Manifest、原生 DLL、Cooked 容器、Shader 库、Asset Registry 与哈希索引 | 单文件分发；挂载时校验并解包至受控缓存目录 |

`DevelopmentEditor` 与游戏侧源码完全解耦，仅通过版本化 SDK 契约与归档协议交换公开信息；后续规划中的游戏专属编辑器节点或跨进程实时预览，也可沿着同一边界以独立适配插件方式接入。

## 3. 仓库布局与构建链路

| 路径 | 内容说明 |
| --- | --- |
| `UnrealModFramework/Source/ExportTest/` | 开发宿主模块，演示角色/玩家接口导出、`ModExtensions` 游戏模式及联机/存档集成 |
| `Plugins/ModRuntime/Source/Runtime/` | 游戏侧运行时模块：`HAL`、`ExportMetadata`、`ModLoader` 与 `ModAPI` |
| `Plugins/ModRuntime/Source/Editor/` | 宿主编辑器模块：`BlueprintIntegration` 与 `ExportCodeGenerator` |
| `Plugins/ModRuntime/Source/ModRuntimeUht/` | C# UHT Exporter 与 UBT 编译适配层 |
| `Plugins/ModRuntime/Tools/` | MHT、MBT、MST、MDT 源码及内嵌的 `DevelopmentEditor` 插件资源 |
| `ExampleMods/TestMod/`、`Test/SharedRuntime/`、`Test/Performance/` | 独立作者示例工程、双 Mod 共享 Runtime 打包回归及调用耗时基准测试 |

（上表中的 `Plugins/` 路径均位于 `UnrealModFramework/` 下；各构建变体的生成产物、编译输入与链接回执按 `<Target>/<Platform>/<Configuration>/<Architecture>` 保存在宿主工程的 `Intermediate/ModTools/ModHeaderTool/` 目录中。）

完整构建分为三步衔接：

1. **工具链准备与代码生成**：运行根目录 Setup 脚本可准备好当前主机的 `libclang`、配套 Clang 资源头文件及四个独立工具（见[快速开始](../README.zh-CN.md#快速开始)）。当 UHT 完成标准反射头文件生成后，适配层调用 MHT，依据真实的 UBT 编译数据库解析当前 Target 的活动 C++ 声明，并合并已保存蓝图资产导出的快照（若首次生成快照时缺少工程二进制，构建流程会自动引导已配置的 Development Editor）。
2. **宿主编译与最终链接核验**：UHT 根据 v4 生成清单将宿主桥接单元与编译校验单元直接登记到各声明所属模块中交由 UBT 编译；若启用了自动 C++ Hook，MBT 还会把对应编译动作的输入替换为 MST 改写后的源码副本。在受支持的桌面平台上，链接完成后 MBT 会依据 UBT `.target` 回执与 `.modules` 清单定位最终二进制，核对本批次专属的生成符号并写出 `host-link.json`（见[宿主编译与链接](../UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md#宿主编译与链接)）。
3. **SDK 发布与双端安装**：MDT 核验 Game 与 Editor 目标 v4 清单中的公开契约、连接描述与 Client 源码完全一致后，调用 UAT `BuildPlugin` 编译作者侧 SDK 插件和 `ModDevelopmentSDKEditor`（同时核对 Editor `BuildId`）。宿主游戏安装发布包中的 `Host/Plugins/<游戏 SDK>` 后再编译并 Cook 基础发行版；Mod 作者则在独立工程中安装 `Plugins/` 下的两个插件（详见 [ModSDKTool 指南](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)）。

## 4. 游戏接入契约与统一 API

框架负责提供底层机制，而一款游戏要让对应的 Mod 能力真正落地，至少需要主动明确以下六项契约：

1. **宿主身份与兼容基线**：声明 `GameId`、基础发行版本号、Runtime SDK 版本、UE 构建标识、目标平台、Shader 平台与 Cook Profile。
2. **逻辑内容目录**：定义允许扩展的逻辑内容槽与 ID、资产/数据类型、原版回退值、覆盖规则及数据 Schema。
3. **对象创建期的解析入口**：在 Actor、组件、道具等业务对象初始化时解析一次逻辑 ID 并缓存结果，避免在每帧热点中反复查表。
4. **事件发布点**：在业务发生处显式发布允许 Mod 订阅的 Gameplay 事件，写清调用线程、投递时机、只读约束及网络权威端。
5. **受控的世界修改锚点**：为场景扩展提供稳定的锚点标识与安全的生效时机，而非让 Mod 在运行期盲目遍历场景猜测 Actor。
6. **实例 API 提供者**：为非静态导出方法注册底层 `UObject` 提供者、有效作用域与失效时机——头文件的导出宏只声明接口形状，不会替游戏决定该调哪个对象实例，更不会凭空创建对象。

对应到运行时模块：`ModLoader` 管理进程准入、解包校验与容器挂载；`UModInstance` 承载加载完成、扩展注册、服务绑定与卸载前回调；`UModAPIHostSubsystem` 按 `GameInstance` 维护 API 提供者与调用授权；`UModExtensionSubsystem` 则统筹单个 `World` 的扩展准备与销毁（详见[模块与依赖边界](CodingStandards/Modules.zh-CN.md)）。

### 4.1 逻辑内容 ID 与优先级决议

逻辑内容 ID 由宿主按业务语义命名（如 `Creature.Wolf.Mesh`）。Mod 提交的每条内容声明包含 `id`、`type`、`action`（`provide`、`replace` 或 `disable`）、`value`、`ownerModId` 与 `priority`，其中 `value` 可为包内软对象路径或版本化结构数据，游戏原版内容默认作为最低优先级兜底项。

World 准备阶段会先按模组依赖图排序，校验内容槽权限与候选合法性。若宿主在 `FWorldPolicy::ContentWinners` 中显式指定了胜出者，则直接采用宿主裁定；否则按 `priority` 取最高优先级条目（一旦出现最高优先级并列，运行时会直接报错并列出所有冲突候选，绝不靠隐式文件名顺序蒙混过关）。同一个 Mod 在同一内容槽内对同一 ID 最多登记一次；当 `disable` 胜出时，查询该 ID 会明确返回“已禁用”。

请特别区分“逻辑 ID 覆盖”与“UE 包路径覆盖”：每个 `.mod` 归档都必须使用独立的 `/<PluginName>/` 包根（例如 `/TestMod/`），加载器在挂载阶段会直接拒绝包根重名或 UE 长包名冲突。所有玩法层面的替换都走逻辑 ID 决议，不把底层 Pak 挂载顺序暴露成公共机制。

### 4.2 资产异步加载与生命周期

逻辑 ID 在初始化或对象创建阶段解析为 `FSoftObjectPath` / `TSoftObjectPtr`，随后交由 UE 的 `FStreamableManager` 异步流式加载（参见 [UE 5.6 异步资产加载文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine?application_version=5.6)）。加载完成后，业务持有方按需保留强引用，不再需要时随常规 `UObject` 生命周期自然释放。

这一设计的核心性能原则是：**Gameplay 帧循环热点不重复穿透框架查询**。依赖拓扑排序、包完整性校验与内容胜出决议全部前置到启动期或对象创建期完成；进入运行期后，调用成本收敛为 UE/Blueprint 原生开销，跨原生 C ABI 边界也仅增加一次定型函数指针间接调用。

### 4.3 API、Hook 与 Event 的声明方式

在 C++ 中，游戏开发者直接在真实业务成员声明上方添加独立的导出宏，原有的 `UFUNCTION` 等 UE 反射宏照常保留。例如开发宿主中导出角色当前生命值查询接口：

```cpp
MOD_EXPORT_API(Scope = World, Thread = GameThread, FailureRule = ReturnError, Permissions = (WorldRead))
EXPORTTEST_API float GetCurrentHealth() const;
```

三类宏职责分明：`MOD_EXPORT_API` 导出可主动调用的成员方法，`MOD_EXPORT_HOOK` 挂载可介入的业务函数，`MOD_EXPORT_EVENT` 声明由宿主主动广播的事件通知。所属 C++ 类的稳定类型身份配置在 `Config/ModAPITypes.json` 中，MHT 只采集当前构建 Target 实际参与编译的活动声明（例如在 [GameCharacter.cpp](../UnrealModFramework/Source/ExportTest/GameCharacter.cpp) 中，`ChangeHealth` 在原函数内直接调用 `ApplyHealth`；各宏支持的完整字段表见 [API 与 Event 的导出字段](../README.zh-CN.md#api-与-event-的导出字段)）。

| 扩展机制 | 声明阶段固定的契约信息 | 宿主代码必须配合完成的运行时接入 |
| --- | --- | --- |
| API | 稳定导出 ID、目标 AST 成员、参数/返回值投影及错误规则 | 注册服务对象提供者（或静态入口），配置允许访问的 World/Player 作用域、线程与权限 |
| C++ Hook | 由 World 类型 ID 与成员名派生的 Hook ID、签名、开放阶段及操作权限 | 在头文件标注 `MOD_EXPORT_HOOK()`，在 `.cpp` 提供类外定义，并通过 MBT 构建目标 |
| Event | 稳定事件 ID、事件源作用域、载荷投影与派发/退订策略 | 持有事件源端点，在业务实际发生时显式触发广播并管理生命周期 |

#### 蓝图侧导出（蓝图Mod导出面板）

对于用蓝图编写的宿主逻辑，`BlueprintIntegration` 模块在蓝图编辑器侧栏提供停靠在“我的蓝图”旁的 **蓝图Mod导出** 面板，分设 `API`、`Event`、`Hook` 三个独立分组与新建按钮。该扩展完全基于编辑器插件公开扩展点实现，不改动引擎源码，也不篡改原生“我的蓝图”内部列表；遇到无法承载运行时实现的特殊蓝图类型时，对应新建按钮会自动置灰并给出提示。

三类蓝图导出均以独立声明对象存储于蓝图资产的 `UBlueprintExtension` 中（参见 [`UBlueprintExtension` API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UBlueprintExtension)），不复用普通蓝图函数或事件分发器上的开关：

| 分组 | 独立声明编辑的内容 | 宿主蓝图中的显式接入动作 |
| --- | --- | --- |
| API | 接口签名、所需权限、作用域、失败规则及专属的 API 实现图 | 编译后生成可调用的 `UFunction`；宿主在运行期注册实例提供者与授权策略 |
| Event | 载荷类型、来源作用域、投递策略（立即/下一帧）与权限 | 在业务执行图中放置对应的 **Publish Event** 节点并连入执行线 |
| Hook | 载荷类型、`AllowedStages`、`AllowedOperations`、权限及专属原版逻辑图 | 在业务执行图中放置对应的 **Hook Point** 节点并连入执行线 |

构建时，`ModExportBlueprints` 会编译配置中登记的蓝图资产，提取所有 API 声明以及执行输入已连线的 Hook/Event 声明写入快照（若签名或载荷不在支持范围内则直接报错）。Shipping 包不依赖任何 Editor-only 元数据做运行期反射查找，一切调用均走生成器产出的静态描述符与绑定代码。

此外，虽然 C++ 动态多播委托（Dynamic Multicast Delegate）本身具备反射信息，但 UE 不保证多播委托监听器的执行顺序（参见 [UE 5.6 多播委托文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine?application_version=5.6)）；凡是需要确定性执行顺序的扩展点，均统一由框架的 Hook/Event 分发器按依赖拓扑顺序调度。

### 4.4 静态生成原则与 Game Mod SDK

**编译期静态生成是本框架不可动摇的核心架构约束。** 所有跨越契约边界的类型映射、参数/返回值投影、可空性与只读性约束、错误码封装、C ABI 线格式及调用桥接代码，全部由生成器在构建阶段静态确定并产出具体代码。换言之，“类型转换层”只存在于构建期的 AST 分析与代码生成阶段；运行时只负责执行已编译好的强类型桥接函数并核查对象存活、作用域与授权，绝不在运行期做动态转换器查找或通用 `FProperty` 反射兜底。

宿主类成员保留最自然的原生 C++ 签名，由生成器按支持矩阵完成投影：对象返回值投影为类型化句柄视图，供独立扩展回调读取的对象载荷投影为回调期内有效的只读视图；声明 `FailureRule = ReturnError` 的 C++ API 统一包装为 `TModResult<T>`，蓝图节点与纯 C ABI 则映射为各自地道的输出引脚或状态码。一个视图包含哪些可调方法在 SDK 生成那一刻即已锁定，不会因为运行期传入了某个派生类实例或授予了更多权限而动态膨胀（具体投影规则见 [FullView 与对象输入](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#fullview-与对象输入)及[连接与生命周期](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#连接与生命周期)）。

目前支持的类型范围由 MHT 与 `ModExportBlueprints` 共同把关：同步 API 支持标量与公开 UE 对象指针入参，以及标量、`void` 或 World 对象视图返回；World Hook/Event 支持标量、受支持的反射值结构体，以及具备生成 World 视图的 `UObject` 载荷。任何未知、有歧义或尚未支持的签名都会在生成期或编译期直接报错终止，绝不把隐患留到对局运行时。

单次完整生成会输出宿主桥接与注册源码、编译期校验源码、公开 C ABI 头文件、共享 Runtime 的 Client 源码、`.sdkcontract.json` / `ModLifecycle.json` 契约，以及汇总所有文件哈希、所属模块与编译角色的 v4 清单 `<game>.generated-artifacts.json`（详见 [ModHeaderTool 文档](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md)）。

在 SDK 交付侧，MDT 将同一批次的公开头文件、Client 源码、契约 JSON、宿主侧源码插件（`Host/Plugins/<game-sdk>`）以及预编译的作者侧插件（`Plugins/<game-sdk>` 与 `Plugins/ModDevelopmentSDKEditor`）打包成不可变的 SDK Bundle。宿主版与作者版 SDK 共用同一个 `<PluginName>Runtime` 模块名与 `/Script/` 反射路径，分别由宿主工程和作者工程通过 UBT 编译，从而保证蓝图节点编译产物在两端二进制兼容，同时让通用制作编辑器完全不依赖具体游戏的私有模块（详见 [SDK 安装与验证](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md#安装与验证)、[Microsoft DLL 链接说明](https://learn.microsoft.com/en-us/cpp/build/linking-an-executable-to-a-dll?view=msvc-170)及 [UE 二进制版本机制](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-version-binaries-in-unreal-engine?application_version=5.6)）。

### 4.5 双执行路径与 C ABI 边界

| 执行路径 | 适用对象 | 运行期开销与边界特征 |
| --- | --- | --- |
| Native Fast Path | 编入宿主的 C++ 服务、类型化 Hook 及已注册的原生回调 | 初始化期完成强类型绑定；运行期直接走生成的检查代码与原成员/定型函数指针数组 |
| Reflection Path | 宿主导出蓝图及基于 `UFunction` 的蓝图回调 | 构建期固定参数内存布局与桥接代码；运行期完成令牌校验后直接衔接 `ProcessEvent` 与蓝图覆写语义 |
| 外部 C ABI 边界 | 独立编译分发的原生扩展 DLL 与共享 Client 表 | 通过定宽标量、显式长度、不透明 64 位令牌与专用 C 函数表交互，完全隔离 C++ ABI 与 `UObject` 布局 |

无论走哪条路径，装载与绑定阶段都由同一个 Host 子系统统一核对导出 ID、主/次版本号、签名哈希、投影哈希与授权项。破坏参数签名、线程约束、所需权限或核心行为语义的改动必须升级导出主版本号；构建期若检测到重复导出 ID 会直接报错，运行期若发现 SDK 契约不兼容则会拒载该 Mod 并输出结构化原因码。

### 4.6 Hook 与 Event 的确定性语义

三类扩展点分工清晰：**API** 供 Mod 主动发起调用，**Hook** 让 Mod 介入正在执行的宿主业务流程，**Event** 则向 Mod 单向广播已经发生的业务事实。一次完整的 Hook 调用按顺序流经 `Pre` → `On` → `Post` 三个阶段：

| 阶段 | 执行时机 | 可向 Mod 开放的干预操作 |
| --- | --- | --- |
| `Pre` | 原业务执行前，读取或校验入参 | `ModifyInputs`（修改入参）、`Cancel`（中止本次调用并返回取消值） |
| `On` | 核心业务执行阶段（默认调用宿主原函数） | `Wrap`（包裹调用下一层 `Next`）、`Replace`（完全接管并替换原实现） |
| `Post` | 核心业务成功返回后，观察或调整结果 | `ModifyResult`（修改最终返回值） |

在 C++ 中，无参 `MOD_EXPORT_HOOK()` 默认同时开放 `Pre`、`On`、`Post` 三阶段，并声明 `ModifyInputs`、`Cancel` 与 `Replace` 操作；传入 `NoPre`、`NoOn`、`NoPost` 可按需裁减阶段及对应操作（`CompatibleNames` 中的曾用名同步遵循同一阶段策略）。而在蓝图的 **蓝图Mod导出** 面板中新建的 Hook，默认只开启安全的只读 `Post` 阶段且操作列表为空，需由宿主开发者显式勾选 `AllowedStages` 与 `AllowedOperations`。即便开放了 `On` 阶段，Mod 也必须拿到对应的 `Wrap` 或 `Replace` 细粒度授权才能改写核心执行。若 `Pre` 阶段触发 `Cancel`，后续 `On` 与 `Post` 会直接跳过。World 准备阶段会为所有订阅者生成确定性的有序执行快照，运行期不再动态重排（详见 [World 扩展产物](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#world-扩展产物)）。

当前版本中，World Hook 与 Event 统一在 Game Thread 同步执行，默认拒绝同一 Hook 键重入；Event 监听器只接收只读载荷（或支持 `NextTick` 入队延迟投递），无法反向篡改宿主状态。需要提醒的是，若获准加载的原生 DLL 内部发生非法内存访问等致命崩溃，进程级崩溃无法仅靠应用层回调包装予以隔离。

### 4.7 World Patch 与跨 Mod 服务

宿主在调用 `Prepare` 前通过 `RegisterAnchor` 注册稳定的场景锚点及规则。Mod 的扩展清单可围绕锚点声明三类 Patch 效果：生成 Actor（`actorClass`）、在宿主允许的挂接点挂载场景组件（`componentClass`），或向锚点注册 Cooked 数据资产（`dataId` + `dataAsset`）。运行时统一完成依赖排序、互斥检查、前置服务校验与权限审核，在安全时机落地变更并记录每项实例的归属 Mod；World 关闭时由框架自动回收自身生成的 Patch 效果（若涉及深层游戏业务状态变更，仍需宿主自行定义补偿逻辑）。

跨 Mod 服务同样通过扩展清单显式声明提供关系（`provides`）与消费依赖（`services`）。服务提供方既可将服务直接绑定在某个 Patch 生成的 Actor 上，也可在 **On Mod Register** 回调中动态登记实现了宿主生成接口的对象；消费方则在随后的 **On Mod Bind** 阶段通过生成的 SDK 获取经过签名与版本核验的强类型服务引用。一旦提供方卸载、权限收回或底层对象销毁，宿主会自动吊销对应绑定（清单字段详见 [`.mod` 布局与扩展清单](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#mod-布局)）。

## 5. `.mod` 包协议

`.mod` 采用自包含的单文件二进制封装，由固定 64 字节的无符号小端序 Header、UTF-8 Manifest JSON、连续排列的二进制载荷区以及末尾的 UTF-8 Index JSON 依次拼接而成。归档主版本演进如下：**v1** 封装纯 Cooked 内容资产；**v2** 新增匹配宿主构建的 UE 原生模块载荷；**v3** 新增 World 扩展清单（支持 Schema v1/v2），并可按需携带 UE 原生模块或独立 C ABI 扩展 DLL。

### 5.1 Manifest 核心字段

外层 `UMF.ModPackage` 清单记录了 `modId`、三段式语义版本号、源插件名与独立的 `/<PluginName>/` 包根、`cookProfileId` 及构建参数，以及包内 `Metadata/CookReceipt.json` 路径。宿主在准入阶段会逐项比对 `GameId`、基础发行版本号、基础 `AssetRegistry.bin` 的 SHA-256、引擎构建信息、目标平台及容器后端。此外，Manifest 还可按需包含 `lifecycleClasses`（派生自 `UModInstance` 的具体蓝图类列表）、`nativeCode`（随包编译的 UE 模块元数据）与 `extensions`（v3 归档中的依赖、互斥、权限请求、Hook/Event 签名、内容槽、Patch 及跨 Mod 服务声明）。字节级偏移表见 [`.mod` 布局](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#mod-布局)。

### 5.2 完整性校验与本地缓存

当前外层归档采用未压缩、未签名的确定性布局。末尾 Index 按区分大小写的规范化路径升序排列，逐条记录偏移、字节长度与载荷 SHA-256，解析器会直接拦截绝对路径、`..` 目录穿越、仅大小写不同的重名条目及越界偏移。`ModLoader` 支持配置整包 SHA-256 白名单及按 `ModId` 锁定修订哈希。校验通过的载荷会解包至 `Saved/ModCache/<整包 SHA-256>/`，后续启动复用缓存前仍会重新核验完整性。非对称签名与公钥信任链属于后续独立设计项；再次强调，允许加载原生 DLL 即代表授予其宿主进程同等执行权限。

### 5.3 Cooked 容器动态挂载

宿主在配置中明确指定使用 `Pak` 或 `IoStore` 后端。归档校验通过后，加载器先为该 Mod 注册独立的 `/<PluginName>/` 包根并挂载底层容器（参见 [UE 5.6 `FPakPlatformFile::Mount` 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/PakFile/FPakPlatformFile/Mount?application_version=5.6)），把包内的 Cooked `AssetRegistry` 追加至引擎资产注册表，随后在 World 准备阶段决议框架层的逻辑内容槽映射（注意引擎 `AssetRegistry` 与框架逻辑内容目录是两套职责不同的数据结构）。

**重要工程前提：Pak 与 IoStore 在 Shipping 包中的启动期动态挂载、包根映射、`AssetRegistry` 增量合并、软引用加载及材质渲染，必须针对每个目标平台和配置分别跑通端到端实测。** 在完成实测前，相关组合仅视为架构目标而非兼容性承诺；正式游戏只需开启已通过自身验收的容器后端，对未启用后端的包直接在准入阶段给出清晰报错。

## 6. 启动编排与失败处理

| 编排阶段 | 运行时执行动作 |
| --- | --- |
| 1. 进程准入 | 校验 `.mod` 头部、索引与载荷哈希；解析启用列表、必需列表、锁定修订、整包白名单、扩展契约、依赖拓扑与互斥规则 |
| 2. 资源挂载 | 解包至缓存目录、挂载 Pak/IoStore 容器、注册独立包根并合并 Cooked `AssetRegistry`；若含 UE 原生模块则先加载 DLL 注册反射类，随后再准备蓝图生命周期类 |
| 3. 会话冻结 | `ModLoader` 向游戏会话广播挂载结果；首个准备就绪的 World 会冻结当前 `GameInstance` 的启用包集合与授权表，同会话后续切图直接沿用 |
| 4. World 准备 | 宿主先注册内容槽与 Patch 锚点，再调用 `UModExtensionSubsystem::Prepare`；运行时按拓扑顺序执行 `On Mod Register`、决议内容与 Patch、完成服务绑定（`On Mod Bind`）并固化 Hook/Event 快照 |
| 5. 对局准入屏障 | World 准备就绪后产出确定性的玩法指纹（Gameplay Digest）；单机开局、联机 `PreLogin` 握手及客户端切图确认均以此作为放行屏障 |
| 6. 停止与卸载 | 先停止事件与回调分发、排空在途调用，再清理本局生成的 Patch 与服务绑定并释放包租约；无外部对象引用时卸载容器，若涉及常驻原生模块等不可逆状态则标记需重启进程 |

上述六阶段编排已在开发宿主的 `ModExtensions` 游戏模式（[GameExtensionMode.cpp](../UnrealModFramework/Source/ExportTest/GameExtensionMode.cpp)）中完整落地；接入具体商业项目时，仍须结合项目自身的 `StartPlay`、无缝/重连切图、断线重连及退出时序逐一联调（配置细节见[模组加载器](CodingStandards/Modules.zh-CN.md#模组加载器)）。

## 7. 原生 ABI 与 Shader 边界

独立扩展 DLL 只需导出纯 C 入口 `UMF_RegisterExtensionsV1`。宿主传入的函数表携带 `StructSize` 与 `Version` 供版本协商，并提供回调注册、主动退订、Hook `Wrap` 续调（`Next`）及宿主生成 API 表查询（`FindHostQuery`）能力。注册时严格核对 SDK 中的扩展 ID 与签名哈希；回调收到的输入缓冲区、输出缓冲区、对象句柄及续调令牌仅在当前同步调用栈帧内有效。World 结束时，宿主保证先排空所有在途回调，再调用 DLL 可选注册的 `Close` 钩子。

生成 API 的客户端连接层则基于 `ContextHandlesV1` C ABI，全程采用定宽整数、显式字节长度与 64 位进程内不透明令牌通信，并在首次 `Connect` 时核对 `nativeABIHash`、结构体大小及全部函数指针非空。C++ 异常必须在各自二进制内部消化，绝不跨越 C ABI 边界传播；`UObject` 内存布局与标准库容器同样严格留在各自的模块内部（详见[独立原生回调](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#独立原生回调)与 [MHT 客户端 ABI 说明](../UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md)）。

对于打包当前 C++ 工程产出的 UE 原生模块，当前方案依托 Win64 x64 模块化宿主的 `.target` 回执编译匹配的模块 DLL，加载器核验 `BuildId` 与目标身份后在蓝图实例化前加载该模块，从而让 Cooked 蓝图顺利解析其 C++ 父类。但需要严格区分两件事：**材质引用随包 Cook 的 Shader 字节码库**与**通过动态模块向引擎注册全新的 Global Shader C++ 类型**完全不是同一概念——正如 [UE 5.6 Global Shader 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)所述，新增 Global Shader 类型的模块必须在引擎初始化早期加载，游戏启动后动态挂载的模块无法补注册 Global Shader 类型。因此，任意构建下的动态新反射与新 Global Shader 仍留作后续专项研究，不纳入当前基线承诺。

## 8. 模组开发编辑器与预览模式

在独立的 Mod 工程中启用 `ModDevelopmentSDKEditor` 后，作者可通过菜单发起 SDK 规划/构建以及可视化 `.mod` 打包任务（操作指南见[模组开发 SDK 编辑器](../UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md)）。示例工程 `TestMod` 展示了工程内角色预览、C++ 原生模块与蓝图生命周期回调；更通用的三层预览体系规划如下：

| 预览层级 | 数据与执行路径 | 适用阶段 |
| --- | --- | --- |
| Local PIE | 作者工程内未 Cook 的源资产 + 游戏提供的轻量 Preview Host + 作者版 SDK | 日常快速调试资产摆放、逻辑内容注册与基础蓝图连线 |
| Remote Preview | 已启动的真实宿主测试进程 + 版本化跨进程 IPC + 按需增量 Cook 资源 | 联调依赖完整游戏状态机、真实 AI/物理与复杂系统的表现 |
| Build & Test | 完整 DLC Cook、打包 `.mod`、字节级复核并在目标发布包中实机挂载 | 发布前的最终权威验收（Local PIE 通过不能替代此步骤） |

在 Local PIE 中，对于宿主未提供本地模拟实现的 API 或 Hook，编辑器应明确提示“当前环境不可预览”，避免返回具有误导性的假成功结果。而在规划中的 Remote Preview 模式下，编辑器进程与游戏进程绝不直接共享未 Cook 的内存 `UObject`：控制指令走版本化 IPC，改动的资产先做增量 Cook 再通知支持热刷新的资产重载；凡涉及原生 DLL、UE 反射结构或不支持热重载的资源变更，一律提示重启测试进程。

此外，`.mod` 打包过程要求具备确定性（Reproducible Builds）：相同的输入资产、配置与工具链应产出字节一致的规范化 Manifest、排序索引与载荷顺序；签名时间戳等非确定性字段须独立于核心摘要域之外。

## 9. 性能、安全可信度与可观测性

- **启动耗时可控**：启动扫描阶段只读取文件头、Manifest 与索引元数据，重型游戏资产留待业务真正引用时再流式加载；同时分段记录扫描、哈希校验、解包、容器挂载与 World 注册耗时，便于性能归因。
- **运行期零反射拼装开销**：内容槽优先级在 World 准备期一次性决议，实例创建后直接缓存；Gameplay 期间只走编译期生成的强类型桥接代码。对象视图包装、结构体字段拷贝、句柄校验与 Blueprint VM 自身的固定开销则按实际调用量如实反映。
- **内存按需驻留**：挂载 `.mod` 容器本身不会把包内所有 `UObject` 读入内存；资产经软引用异步加载后，由业务持有者按常规强/弱引用管理驻留期，并对流式加载失败提供清晰日志。
- **诚实的安全边界**：包完整性哈希、白名单、依赖检查与原生 DLL 开关各司其职；文档与配置均明确告知接入方，启用原生代码意味着赋予该 DLL 与游戏主进程等同的操作系统权限，不将其包装成“沙箱安全执行”。
- **结构化启动诊断**：针对每个发现的 `.mod` 记录启用状态、拒载错误码、依赖决议链、内容槽最终胜出来源、挂载包根、C ABI 握手状态及资产加载异常，让开发者和玩家看一眼启动报告即可定位模组冲突，无需翻阅海量逐帧日志。

## 10. 里程碑与验收门槛

当前主干已打通 UBT 宿主内联编译、桌面目标最终链接校验、双端 SDK 构建、`.mod` 打包挂载及 `ModExtensions` 全流程示例：Linux x64 CI 持续运行 UE 5.6.1 Development Game/Editor 编译、Editor Hook 分发自动化测试及独立制作插件构建（见 [Linux 编译的范围](DevelopmentWorkflow.zh-CN.md#linux-编译的范围)），Win64 x64 Development 则具备双独立 Mod 共用同一套 SDK Runtime 的打包游戏回归（见[共享 Runtime 回归](../Test/SharedRuntime/README.zh-CN.md)）。Shipping 配置下的 Pak/IoStore、更多平台与引擎小版本、长时运行内存回收、Dedicated Server 及跨进程预览仍按下方路线图逐项验收：

| 里程碑 | 核心交付内容 | 验收通过标准 |
| --- | --- | --- |
| P0 | 双插件解耦架构、独立 `BuildPlugin`、共享协议与基础 SDK 发布 | Game/Editor Target 正常编译 Runtime；制作插件可脱离源仓库路径独立构建并安装到外部工程 |
| G1 技术闸门 | 在最小独立游戏中实测 Shipping 资源挂载全链路 | Win64 Development 与 Shipping 下分别验证 Pak 与 IoStore 动态挂载、`AssetRegistry` 合并及网格/材质/蓝图软引用加载，逐后端出具实测结论 |
| P1 | 宿主接入示例、API/Hook/Event 契约、类型化 Hook 与 Runtime Registry | 显式调用点与服务提供者正常工作；无订阅者时保持原版行为；冲突、重入拦截、权限吊销与生命周期回调均可稳定复现 |
| P2 | MHT 统一 IR 与代码生成、UHT/UBT 原生集成、MBT 链接校验、SDK Bundle 与 C ABI | 导出宏与标准 UHT 和谐共存，首次与增量构建时序正确；全部生成代码真实编译链接；独立作者工程可直接消费发布的 SDK Bundle |
| P3 | `.mod` 单文件打包、流式哈希校验、依赖决议及已过 G1 的容器挂载 | 目标 Shipping 游戏可从单个 `.mod` 正确挂载资产与获批 DLL；损坏包、缺失依赖与循环依赖均稳定拦截 |
| P4 | 逻辑内容槽覆盖/禁用、World Patch、跨 Mod 服务、联机握手与存档迁移 | 多 Mod 优先级与并列冲突决议完全确定；场景锚点变更受权限约束；联机摘要比对与存档迁移语义清晰可测 |
| P5 | 制作编辑器的接口浏览、Local PIE 调试、蓝图辅助与一键打包验证 | 作者可在独立工程内完成创作、本地预览、DLC Cook 并产出目标游戏可直接挂载的 `.mod` 包 |
| P6 | Cooked Shader 库闭环、Remote Preview 与增量资源同步 | 自定义材质跨平台渲染验收通过；跨进程 IPC 握手与增量 Cook 刷新稳定复现 |
| P7 前沿研究 | 跨更多构建模式的原生模块、动态新反射类型、Global Shader 与热替换 | 针对具体 UE 版本与 Shipping 链接模式形成专项实验报告，确认可行后再纳入正式支持矩阵 |

其中，G1 技术闸门必须在双 Mod 独立包根、资产注册、异步加载、材质渲染与重启一致性上分别对 Pak 和 IoStore 给出独立结论，绝不用一个后端的表现外推另一个后端；P1–P6 的编号仅代表产品交付依赖顺序，互不耦合的技术原型可并行开展。

## 11. 待验证与待决策事项

1. **Shipping 容器动态挂载矩阵**：在 UE 5.6 Win64（及后续目标平台）的 Shipping 构建中，逐项实测 Pak 与 IoStore 启动期动态挂载、`AssetRegistry` 增量合并与软引用解析，据此确定正式开启的后端列表。
2. **多平台与增量构建覆盖**：每引入新的平台、架构或链接模式，均需单独验证 UHT 生成头时序、`.mht-watch.stamp` 增量失效、C ABI/蓝图类型投影及多目标 SDK 合并。
3. **原生代码演进边界**：在稳固现有独立 C ABI DLL 与同构 Win64 模块化 UE 模块的基础上，审慎评估更广泛的动态反射、Shader 类型与热替换边界。
4. **游戏发行方的 Cook Profile 规范**：接入框架的游戏团队需明确向社区发布 SDK、基础发行版 `AssetRegistry.bin`、标准 Cook 参数及公开可引用资产清单的标准流程。
5. **归档数字签名方案**：在现有 SHA-256 白名单、修订锁定与原生代码开关之上，独立设计 `.mod` 非对称签名格式与公钥分发机制。
6. **生产级多人联机联调**：开发宿主已实现 `PreLogin` 地图与玩法指纹握手；真实联网游戏还需在各自的 Dedicated Server / Client 构建、跨端 `.mod` 映射与断线重连流程上完成完整压测。

## 12. 参考资料

- [UE 5.6 Unreal Header Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6)：UHT 类型模型、C# Exporter 插件机制与生成头文件包含约束。
- [UE 5.6 UBT Target Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-build-tool-target-reference?application_version=5.6)：Target 规则与 `PreBuildSteps` / `PostBuildSteps` 执行时机。
- [UE 5.6 Plugins](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine?application_version=5.6)：插件描述符、模块加载阶段与独立打包约定。
- [UE 5.6 Metadata Specifiers](https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine?application_version=5.6)：`UMETA` 等反射元数据仅存在于 Editor 构建，不可用于 Shipping 运行期查询。
- [UE 5.6 Multi-cast Delegates](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine?application_version=5.6)：多播委托不保证回调触发顺序。
- [UE 5.6 Asynchronous Asset Loading](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine?application_version=5.6)：软对象路径与 `FStreamableManager` 异步加载。
- [UE 5.6 Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine?application_version=5.6)：Primary Asset、Asset Manager 与流式句柄生命周期。
- [UE 5.6 FPakPlatformFile::Mount](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/PakFile/FPakPlatformFile/Mount?application_version=5.6)：Pak 容器挂载接口与 `FCoreDelegates` 挂载委托说明。
- [UE 5.6 Module API Specifiers](https://dev.epicgames.com/documentation/en-us/unreal-engine/module-api-specifiers-in-unreal-engine?application_version=5.6)：Modular 与 Monolithic 构建下的跨模块符号导出语义。
- [UE 5.6 Adding Global Shaders](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)：Global Shader 类型必须在引擎启动早期完成注册的限制。
- [UE 5.6 Packaging Projects](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project?application_version=5.6)：Build、Cook、Stage、Package 全链路与基础发行版产物结构。
