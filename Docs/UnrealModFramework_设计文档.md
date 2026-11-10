<!-- Copyright (c) 2026 Nelaric Contributors -->

# UnrealModFramework 技术设计

> 状态：设计草案 v0.5（MHT 导出语义与生成、原生 UBT 构建、MBT 产物管理及生成清单）\
> 基线：UE 5.6、Windows Win64；其他引擎版本和平台按兼容性矩阵单独验证\
> 目标读者：框架开发者、接入游戏的开发者、模组工具开发者

游戏端的模块、契约、直接调用、作用域、加载和验收细节见[游戏端详细设计](UnrealModFramework_游戏端详细设计.md)。游戏侧细化决策以该文档为准。

本设计描述目标能力，不代表仓库已经实现这些能力。架构的执行链为：**游戏显式声明并接入扩展点 → 使用原生 UE Build 入口并取得真实 Target 上下文 → UHT 通过 Exporter 适配器调用 MHT → MHT 分析声明并生成宿主连接、公共声明、验证代码与生成清单 → MBT 按显式编译单元独立编译并创建宿主库 → UBT 编译 UE 模块自身代码并链接所属宿主库 → 后续工具核对产物并组织 SDK → 运行时经类型化绑定执行生成代码**。具体阶段时序须按目标构建组合验收。自动生成不能代替游戏业务代码中的 Hook 调用点、API 实例提供者或 Event 发布者。当前 MHT 已有基于 libclang 的同步 UObject API 生成后端，MBT 已有独立宿主库编译和校验，C# UHT 适配器已在 UE 5.6 Win64 MSVC 的 Development Editor/Game 组合接通；完整类型闭包、Event/Hook、通用 UBT 动作图和其他平台自动接入仍需后续实现。

职责固定为：**MHT 负责“导出什么、怎么调用、生成什么代码”；MBT 按生成清单独立编译宿主实现、创建静态库并验证产物；UBT 编译 UE 模块自身代码并将宿主库链接到所属二进制。** MHT 的宿主实现不进入 UBT 的 C++ 编译列表，不能通过游戏 `.cpp` 包含生成实现来接入。

## 1. 目标与边界

框架由游戏内的 **Mod Runtime**、独立构建的 **模组开发编辑器**、版本化的 **Mod Protocol / SDK** 和单文件交付的 **`.mod` 包**组成。游戏开发者主动接入 Runtime 和游戏扩展点；模组作者在自己的 UE 工程中制作内容，经 Editor 构建 `.mod`；游戏启动时验证、挂载和注册模组，Gameplay 阶段使用 UE 原生对象与软引用加载机制。

首个可发布版本的能力边界如下：

| 能力 | 首版设计结论 |
| --- | --- |
| Cooked 资产 | 支持，具体容器后端须通过目标游戏的 Shipping 构建验证 |
| 逻辑内容 ID 与覆盖/禁用 | 支持；要求游戏在相应扩展点调用 Mod API |
| 原生逻辑 | 支持同进程 C ABI DLL；宿主通过稳定服务表暴露能力 |
| Blueprint | 支持使用宿主已编译的 UE 类和游戏 SDK 中可用的类型 |
| 材质 Shader Code | 随 Cooked 资产发布，按目标 Shader 平台验证 |
| 模组自定义 `UCLASS`、`USTRUCT`、Global Shader 类型 | 不作为首版交付承诺，列为独立研究项目 |
| 运行时卸载、热替换 DLL 或 UE 反射类型 | 首版不支持；禁用或移除模组在重启后生效 |
| 任意游戏的零接入适配 | 不支持；游戏必须提供稳定的扩展点和兼容的 Cook 配置 |

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
| `ExportCodeGenerator` | 当前提供导出标记和配置选项 Commandlet；后续承载 Editor 声明导出及生成集成 | 随 ModRuntime 源码插件编译，仅 Editor Target 加载 |
| `BlueprintIntegration` | “蓝图Mod导出”面板、蓝图内的独立 API/Event/Hook 声明、接入节点与编译校验 | 随 ModRuntime 源码插件编译，仅 Editor Target 加载 |
| `ModHeaderTool`（MHT） | 导出宏、真实类型与成员签名、契约校验、完整类型闭包、投影、调用/转换/ABI/验证代码及生成清单 | 独立 C++ 工具；通过 C# UHT Exporter 适配器接入，完整 C++ 分析可使用 Clang |
| `ModBuildTool`（MBT） | 实际 Target 与工具链识别、构建准备、生成输入与缓存管理、SDK/二进制兼容核对、产物收集和发布 | 独立构建管理工具；保留仓库现有 CMake/C++ 工具布局，通过 UBT 构建步骤接入 |
| `ModSDKTool`（MST） | SDK 工具工程骨架；具体 SDK 操作待实现 | 独立 CMake/C++20 可执行工具，接入统一工具构建入口 |
| `DevelopmentEditor` | 模组工程的创作界面、SDK 浏览、Cook/打包、包检查、预览与发布工具 | 单独构建的编辑器二进制插件 |
| `ModProtocol` | Export Schema、包格式、Manifest、内容注册表、IPC 消息和 C ABI 版本定义 | 单一版本化源定义，构建时复制到各独立交付物 |
| 游戏 Mod SDK Bundle | 公共 GameModSDK、受限视图、反射类型、契约、链接/模块元数据、Cook Profile 与能力报告 | 同批生成、按 Game/Editor 等目标构建，原子发布 |
| `GameModSDKEditor` | 消费公开 SDK 的游戏专属节点、注册与制作编辑器适配 | Editor 模块，使用 SDK Bundle 编译并打包 |
| `.mod` | Manifest、原生 DLL、Cooked 容器、Shader 产物、注册表、索引 | 单文件分发；运行时可提取到受控缓存 |

`DevelopmentEditor` 不能在 UBT 构建关系上依赖某个游戏工程中 ModRuntime 插件的运行时模块。它通过共享协议编译，并在预览时使用协议桥接或 IPC。游戏特有的创作功能来自游戏 Mod SDK，而非从任意已安装游戏中反向推导。制作模块名为 `DevelopmentEditor`，所属插件名为 `ModDevelopmentSDKEditor`；`GameModSDKEditor` 是游戏专属 SDK 包中的 Editor 适配模块。

## 3. 源工程与构建

```text
UnrealModFramework/
├── UnrealModFrameworkDev.uproject
├── Plugins/
│   └── ModRuntime/
│       ├── ModRuntime.uplugin
│       ├── Source/
│       │   ├── Runtime/             # 运行时模块按具体功能组织
│       │   │   └── ExportMetadata/
│       │   └── Editor/              # 编辑器侧模块目录，可容纳多个 Editor 模块
│       │       ├── ExportCodeGenerator/
│       │       └── BlueprintIntegration/
│       └── Tools/
│           ├── ModHeaderTool/
│           ├── ModBuildTool/
│           └── ModSDKTool/
├── SourcePlugins/
│   └── ModDevelopmentSDKEditor/
│       ├── ModDevelopmentSDKEditor.uplugin
│       └── Source/DevelopmentEditor/
├── Shared/ModProtocol/            # Export Schema、包格式和 C ABI 的唯一源定义
├── Build/
│   ├── BuildModDevelopmentSDKEditor.ps1
│   └── BuildRelease.ps1
└── Dist/
    ├── RuntimeSDK/
    └── ModDevelopmentSDKEditor/UE_5.X/Win64/ModDevelopmentSDKEditor/
```

`Plugins/ModRuntime` 参与游戏工程的正常 UBT 构建。`SourcePlugins/ModDevelopmentSDKEditor` 位于 UE 的常规插件搜索目录之外，以 `RunUAT BuildPlugin -Plugin=<路径> -Package=<短路径暂存目录>` 独立构建，成功后将二进制插件整理到 `Dist`，交付后安装在 `Engine/Plugins`。Windows 构建会在 `-Package` 下生成较深的临时 `HostProject` 路径，直接指定深层 `Dist` 目录可能触发 260 字符路径检查。UE 文档确认项目插件位于 `Project/Plugins`、引擎插件位于 `Engine/Plugins`，并且模块类型决定 Runtime 或 Editor 的加载目标。[UE 5.6 插件文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine?application_version=5.6)

两个插件的发布副本都必须包含编译所需的 `ModProtocol` 定义，不能让已安装的编辑器插件从开发仓库的相对路径包含文件。`Shared/ModProtocol` 是唯一源定义；生成脚本把版本化快照同步到两个插件的自包含源码目录，CI 检查快照与源定义一致，独立 `BuildPlugin` 再使用短路径暂存目录。

游戏真实成员的访问桥编译进该类型所属模块；公共视图和反射包装进入 GameModSDK，游戏专属编辑器代码进入 GameModSDKEditor。可选的 GameModIntegration 只编排 Provider、玩法屏障及各模块公开注册入口。通用 Runtime 不反向依赖游戏模块；单独建一个桥 DLL 也不会自动获得游戏私有符号的链接能力。

默认保留 IDE、命令行和 CI 的原生 UE Build 入口。当前 UHT 的 C# Exporter 适配器先调用 MHT，再调用 MBT `build-host`；MBT 使用所属模块的真实编译配方独立编译生成实现，UBT 随后通过所属模块的私有 External 依赖链接所得宿主库。更完整的 Prepare/Finalize 动作与可信最终链接记录仍是后续接入。MBT 不解析 C++ 类型、不生成连接代码，也不在构建回调内递归启动当前 UBT Target；MHT 不调用编译器。UE 扩展点的存在不能证明任意构建组合的阶段顺序已经成立。[UE 5.6 UHT](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6)、[UBT Target 参考](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-build-tool-target-reference?application_version=5.6)

MHT 由 UHT 调用时不能假定本轮全部 `.generated.h` 已存在。已反射类型可从 UHT 类型模型取得已解析信息；完整 Clang 分析若需要生成头，须通过经验证的原生扩展或显式多阶段原生构建在头文件就绪后执行。同一轮 Exporter 新生成的反射类型不能假定会被自动重新解析，必须在消费目标的后续正常 UHT 前纳入源码。首次需要 Editor 导出器时使用不可发布的引导构建。MHT 为每个所属模块生成独立的 `.modhost.cpp`、`.modabi.cpp`、`.modverify.cpp` 和 `.modviews.cpp`，另为独立客户端库生成 `.modclient.cpp`，在生成清单的 `compilationUnits` 中逐项列出所属模块与源文件。MBT 编译全部具体调用、转换和校验；UBT 只消费生成声明与宿主库，不包含生成实现。

先验收声明标记与标准 UHT 的共存、实际 Target 分析上下文、首次与增量构建和构建回调时序。非活动条件分支由 MHT 按真实预处理上下文排除，Editor-only 声明不能无条件进入 Shipping 契约。若以后需要隔离源码快照，标记变换及源码映射仍由 MHT 负责，MBT 只管理已声明的输入、依赖与失效；UHT、MHT 和编译器必须读取同一份正式输入。当前导出与 Mod 调用工作不要求 MBT 实现 Cook、资源挂载、模组依赖决议或宿主生命周期。

发布流程必须记录 UE 版本、编译器工具链、Runtime SDK 版本、协议版本和插件包哈希。`Dist` 是构建产物，源仓库中的协议定义是唯一真实来源。模组工程的内容可由模组开发编辑器创建在独立的项目侧内容插件中；该插件用于稳定 Cook 后的包路径，不作为框架的第三个核心插件交付。

## 4. 游戏接入契约与统一 API

框架只提供通用机制。游戏至少接入以下契约，才能让对应功能工作：

1. **宿主身份与兼容性**：游戏 ID、游戏版本、Runtime SDK 版本、UE 构建标识、目标平台、Shader 平台及 Cook Profile ID。
2. **内容目录**：定义可扩展的逻辑 ID、内容类型、原版默认值、合法覆盖规则和数据 Schema。
3. **实例创建入口**：在 Actor、组件、物品或其他对象创建时解析逻辑 ID，并把结果缓存到游戏对象；框架不在每帧重复查找。
4. **事件入口**：显式发布可供模组订阅的 Gameplay 事件，标明线程、时序、可修改性和网络权限。
5. **世界修改入口**：为可修改场景提供稳定的对象 ID 与安全时机；不依赖运行时遍历并猜测任意 Actor。
6. **API 实例入口**：为非静态导出成员指定对象提供者、作用域和失效时机；声明标记不决定本次应调用哪个 `UObject`，也不自动创建实例。

Runtime 的公共模块按职责拆分为 `ModAPI`、`ModManager`、`ModPackageLoader`、`ModNativeLoader`、`ModShaderManager`、`ModAssetResolver`、`ModContentRegistry`、`ModWorldPatch` 和 `ModEventBus`。这些是逻辑边界，最终 UBT 模块数量以依赖和加载阶段需要为准。

### 4.1 逻辑 ID 与覆盖规则

内容 ID 由游戏定义，例如 `Creature.Wolf.Mesh`。注册项包含 `id`、`type`、`action`、`value`、`ownerModId` 和 `priority`；`action` 取 `provide`、`replace` 或 `disable`。`value` 对资产是软对象路径，对数据内容是已版本化的结构化记录。原版内容视为最低优先级候选。

模组先按依赖关系作拓扑排序。对同一逻辑 ID，候选按 **优先级 → 依赖拓扑顺序 → 模组 ID 字典序** 排序，最后一项生效；同一模组不得重复注册同一 ID。`disable` 若成为胜出项，该 ID 返回“已禁用”，不会退回较低优先级资产。冲突决策和候选链写入诊断日志，供模组开发编辑器展示。

加载顺序不等于 UE 包路径覆盖顺序。首版要求每个模组拥有独立 Cooked 包命名空间，例如 `/mod_sample_mod/`；两份模组若声明同一 UE 包路径，包验证直接失败。玩法覆盖只通过逻辑 ID 决议，避免把 Pak 优先级当作公共 API。

### 4.2 资产加载与生命周期

逻辑 ID 在初始化或实例创建时解析为 `FSoftObjectPath` / `TSoftObjectPtr`，随后由 UE 异步加载。游戏在回调后保留需要长期使用的强引用；不再使用时按 UE 对象生命周期释放。UE 5.6 文档明确软引用可避免被引用资产随持有者立即加载，`FStreamableManager` 可请求异步加载。[UE 5.6 异步资产加载](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine?application_version=5.6)

预期性能目标是 **Gameplay 热点路径不反复查询框架**。内容决议、依赖排序和包验证发生在启动或对象创建阶段；实际运行仍包含 UE/Blueprint 自身的开销，Native ABI 调用也保留一次函数指针调用成本。

### 4.3 API、Hook、Event 的声明和接入

游戏开发者在真实声明前使用独立 Mod 标记，标准 UE 标记继续表达反射。以下为拟定语法，完整三类示例及 Schema 见[游戏端详细设计第8节](UnrealModFramework_游戏端详细设计.md#8-游戏声明与蓝图接入)：

```cpp
MOD_EXPORT_API(
    ExportVersion = "1.0",
    Scope = World,
    Thread = GameThread,
    FailureRule = ReturnError,
    Permissions = ("inventory.write"))
UFUNCTION(BlueprintCallable, Category = "Mod")
bool AddItem(FName ItemId, int32 Count);
```

MOD_EXPORT_API 关联后面的具体函数，MOD_EXPORT_EVENT 关联 delegate/EventSource 字段或明确的事件签名锚点，MOD_EXPORT_HOOK 关联游戏业务函数。MHT 只跨过允许的 UE 标记、属性与注释来寻找声明，歧义与非法叠加直接失败。MOD_EXPORT_CLASS 是可选类型级配置，成员导出会建立并持久化类型身份。需要非公开访问时，原类通过 MOD_GENERATED_BODY 显式授权其生成桥；友元不继承、不传递。

一个版本化选项 Schema 同时驱动 C++ DSL、蓝图面板、默认值、验证及兼容摘要，覆盖 API/Event/Hook 的权限、作用域、线程、生命周期、错误、载荷、阶段与执行策略。const 不自动等于 Pure/线程安全/授权。标记不替代模块 API 导出说明符，空宏不保证 UHT 接受语法；Shipping 只消费生成描述符，不扫描编辑器元数据决定公开范围。[UE 5.6 元数据说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine?application_version=5.6)

| 机制 | 声明提供的信息 | 游戏必须显式完成的接入 |
| --- | --- | --- |
| API | ID、具体 AST 成员、投影与调用策略 | 注册服务对象提供者或静态入口；指定 World/玩家作用域、线程与权限 |
| Hook | ID、原版业务、开放阶段、允许操作与可写字段 | 在真实业务中调用生成的 Hook helper，定义提交、取消与错误边界 |
| Event | ID、实际事件源、载荷与回调策略 | 提供事件源实例，在业务发生时通过具体连接发布并管理生命周期 |

#### 蓝图Mod导出

游戏工程的 `BlueprintIntegration` 在蓝图编辑器侧栏提供“蓝图Mod导出”面板，按当前蓝图显示 `API`、`Event`、`Hook` 三个独立分组，每组都有创建入口。面板通过编辑器插件提供的标签页和布局扩展接入，停靠在“我的蓝图”附近；不修改 UE 引擎源码，也不把三个分组伪装成原生“我的蓝图”列表的内部分类。对不能生成运行时实现或缺少相应图能力的蓝图类型，相关创建入口禁用并说明原因。

三个分组分别创建专有声明，不依赖普通函数、事件分发器或结构体详情面板中的布尔开关：

| 分组 | 独立声明及编辑内容 | 显式业务接入 |
| --- | --- | --- |
| API | 签名、权限、作用域、实例提供者和 API 实现图 | 编译生成可调用入口，非静态入口须提供有效实例及生命周期规则 |
| Event | 载荷类型、发布作用域与线程规则 | 在业务图中放置对应的 Publish Event 节点，向 Event Bus 发布 |
| Hook | 类型化 Context、`AllowedStages`、`AllowedOperations`、原版逻辑与失败规则 | 在业务图中放置对应的 Hook Point 节点并连接原版逻辑 |

各声明以独立 GUID 保存在当前蓝图的 `UBlueprintExtension` 中；API 实现图由该扩展持有，编译时生成 UE 可执行函数，编辑器的图搜索与差异比较能访问扩展图。面板负责创建、选择、重命名、删除和跳转；删除时检查引用并支持撤销。Event 与 Hook 的接入节点引用声明 GUID，蓝图编译与 Export Generator 均检查重复 ID、不支持的参数、失效引用和缺失的业务接入。Editor-only 声明不作为 Shipping 时的查询来源，生成器输出运行时描述符及绑定。[`UBlueprintExtension` API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UBlueprintExtension)

C++ 的动态多播 Delegate 仍可作反射事件源，但其广播顺序未定义；需要稳定顺序时由框架 Event Bus 按契约排序分发。[UE 5.6 多播 Delegate](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine?application_version=5.6)

### 4.4 Export Generator 与 Game Mod SDK

**静态生成原则是固定架构约束。** 所有跨契约边界的类型映射、签名投影、参数与结果转换、可空性、只读性、错误表示、ABI 表示和调用连接，均由 Export Generator 在构建期确定并生成。“类型转换层”属于生成器的类型分析与代码生成阶段。运行时执行具体生成代码，处理当前对象、作用域、授权与调用数据，不提供动态转换器选择或通用反射转换回退。

游戏成员保持真实类型和原始签名。生成器自动将对象参数及结果投影为 `FModRuntimeObject<T>`、将 const 对象投影为只读视图，并递归生成结构字段、容器、Event 回调及 Hook 阶段上下文；开发者无需手工维护第二套签名或常规转换。API 的 `FailureRule = ReturnError` 统一生成 `TModResult<T>`，蓝图和 C ABI 使用对应的具体结果表示。视图成员集合在 SDK 生成时固定，派生实例或不同 Mod 授权不扩大该集合；实际授权通过生成入口检查。完整规则见[游戏端详细设计第10节](UnrealModFramework_游戏端详细设计.md#10-契约生成与构建流水线)和[第11节](UnrealModFramework_游戏端详细设计.md#11-类型化调用连接)。

MHT 的 UhtTypeAdapter 消费 UHT 已解析的反射类型信息；完整 C++ 分析由 CppTypeAnalyzer 通过词法记录/PPCallbacks 关联真实 AST，包括受支持的非 UFUNCTION 成员、具体重载及 cv/ref/noexcept。分析上下文来自 UBT 的真实 Target，由 MBT 核对和传递，不能只按函数名或已加载 UFunction 猜测。[Clang LibTooling](https://clang.llvm.org/docs/LibTooling.html)、[PPCallbacks](https://clang.llvm.org/doxygen/classclang_1_1PPCallbacks.html)。当前 MHT 不扫描蓝图；后续蓝图资产发现仍由独立的 ExportCodeGenerator/Commandlet 前端通过 Asset Registry 完成，向生成核心提交声明 IR。[UE 5.6 Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine?application_version=5.6)。MHT 统一执行类型闭包、投影和代码生成，MBT 不解释这些语义；缺失必需输入、非唯一关联及 unresolved 非零时禁止发布。校验后输出：

1. 规范化的 API、Hook、Event Schema，包含稳定 ID、导出版本、源签名、Mod 签名、投影规则及摘要、公开参数/字段类型、线程、权限、作用域、失败语义和文档。
2. 编译到游戏模块的 Runtime Registry、注册入口和成员专用连接；按已注册的服务实例解析 API 与事件源，并将生成的类型化入口或具体 `UFunction` 连接写入绑定。
3. Game Mod SDK 的 C++ 受限/只读视图、递归投影结构、具体结果类型、C ABI DTO/专用表项、Blueprint 包装类型、节点展开规则及 Event/Hook 连接。
4. 生成报告，列出重复 ID、未解析/歧义/不支持的类型与转换、非法借用、错误规则冲突、未声明的实例提供者、签名变更和缺少显式注册的 Hook Point；阻断不合法构建并显示完整依赖路径。实际业务路径是否调用 Hook 仍需集成测试验证。
5. `MyGame.generated-artifacts.json`：记录生成器/Schema 版本、输入与构建上下文摘要、生成批次、契约/投影/ABI 摘要，以及全部生成文件的相对路径、内容哈希、所属模块和用途。MBT 消费清单并对照实际构建凭证核对产物，不重新计算 C++ 类型映射与转换规则。

生成结果按规范化输入稳定排序，包含生成器版本、游戏 SDK 版本、UE 构建标识、源/Mod 签名、投影摘要和输入指纹。CI 重新生成后比较产物，发现漂移即失败。游戏模块显式注册生成的 Runtime Registry；通用 Runtime 不静态依赖游戏模块。生成器只自动适配明确支持的完整类型闭包；任意 `UFUNCTION` 参数不能无条件自动转换成稳定 C ABI。例如 `UObject*` 需要不透明句柄及生命周期规则，复杂容器需要版本化数据传输类型。未知、歧义或不支持的签名在生成或编译阶段失败，不留到 Gameplay 补齐。扫描后新增的反射包装须在后续正常 UHT 前生成，按显式多阶段构建完成。

GameModSDK 拥有公共视图和具体共享反射类型，GameModSDKEditor 消费 SDK 并向通用 DevelopmentEditor 注册节点及编译扩展。通用编辑器保持自包含共享协议边界，不反向依赖项目级 SDK、Runtime 或游戏私有模块。既有公共模块名及 /Script 身份可继续保留，不因角色命名统一而重命名或复制反射定义。蓝图具体包装由 Runtime 模块拥有，编译结果只引用宿主存在的 Runtime 类型和函数；不伪造真实游戏 UCLASS。

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

每个 Hook 独立设置 `AllowedStages` 和 `AllowedOperations`，默认只开放 `Post` 且操作列表为空。每项操作都要求对应阶段开放。开启 `On` 后仍须分别显式授权 `Wrap` 或 `Replace` 才允许 Mod 介入核心执行。取消跳过 `On` 和 `Post`，失败由宿主返回并可单独发布事实通知。注册阶段建立稳定快照，阶段权限、顺序和错误规则见[游戏端详细设计第18节](UnrealModFramework_游戏端详细设计.md#18-hook-语义与执行计划)。

Hook 契约还需指定失败策略、重入规则、调用线程和网络权威。首版默认 Game Thread、同步，同键重入默认拒绝。跨线程或异步 Hook 必须单独声明并验证对象生命周期。Event 监听者接收只读载荷，宿主决定业务结果。Event Bus 在注册阶段缓存订阅者，广播前检查模组与源对象仍有效，Game Thread 为默认回调线程。原生 DLL 崩溃不能靠框架回调层隔离。

### 4.7 World Patch 与跨 Mod 服务

World Patch 以游戏提供的稳定 World/对象 ID、允许的操作类型和执行阶段为契约。模组可声明新增、替换或禁用世界对象，但游戏须提供对应的创建工厂、属性白名单和权威检查；框架不尝试对任意 Actor 进行无约束的反射写入。启动时先解决模组依赖和冲突，再按 World 生命周期的安全阶段应用 Patch，并记录每项操作的来源与结果。首版不承诺运行中撤销已应用的世界修改；存档中的模组来源和版本须可追踪，缺失模组时的迁移由游戏指定。

跨 Mod 服务通过 Host 注册版本化服务 ID、接口 Schema 与拥有者生命周期。消费者声明必需或可选依赖，Host 在就绪后返回连接到生成入口的 C ABI 句柄或 C++ 受限视图；服务失效时句柄不可继续使用。所需签名、投影和连接必须在构建期生成，不支持未知运行时签名的自动适配。服务调用继承契约的线程、权限和版本校验，独立 DLL 之间不直接共享 C++ 对象布局。

## 5. `.mod` 包协议

`.mod` 是**单个不可变文件**；下方树形图只表示其逻辑条目，不表示磁盘目录。外层容器保留 UE 原生 Cook 产物，不把 `.pak`、`.utoc`、`.ucas` 改写成私有资产格式。P0 固定可随机读取的 Header、Manifest 与 Index 布局和版本，未知主版本拒载；完整打包器在 P3 实现。推荐逻辑结构：

```text
Example.mod
├── Header                    # 魔数、格式版本、清单/索引位置
├── Manifest.json             # 模组身份、兼容性、能力与依赖
├── Native/Win64/Example.dll  # 可选；C ABI 原生逻辑
├── Content/Example.pak       # Pak 后端，可选
├── Content/Example.utoc      # IoStore 后端，可选
├── Content/Example.ucas      # 与 utoc 配套
├── Shaders/                  # 与目标平台匹配的 Cook 产物
├── Registry/                 # 内容注册表及 Cooked Asset Registry
├── Index                     # 条目、偏移、长度、SHA-256、压缩方式
└── Signature                 # 可选；受宿主信任策略约束
```

### 5.1 Manifest 的必需信息

| 字段组 | 信息 |
| --- | --- |
| 身份 | `formatVersion`、`modId`、`version`、显示名称 |
| 宿主 | `gameId`、游戏版本范围、Game Mod SDK/Export Metadata 版本与指纹、`cookProfileId` |
| 目标 | 平台、UE 主次版本与构建标识、Shader 平台/格式、容器后端 |
| 依赖 | 必需模组 ID 与版本范围；可选依赖单独声明 |
| 内容 | 注册表路径、包命名空间、声明的能力 |
| 原生 | 如存在 DLL，记录 ABI 主次版本、入口点及目标架构 |
| 冲突 | 模组优先级及显式不兼容模组列表 |

`modId` 使用规范化的小写 ASCII 标识，建议匹配 `[a-z][a-z0-9_]{2,63}`；版本使用语义化版本。`cookProfileId` 由游戏发布，绑定 Cook 关键设置、游戏 SDK、UE 构建与目标平台。Manifest 同时声明所用的 API/Hook/Event 导出版本或兼容范围、所需能力和权限。单靠“UE 5.6”不能证明 Cooked 资产兼容某个 Shipping 游戏。

### 5.2 解析、校验与缓存

Header、Manifest、Index 应能在扫描时随机读取，且不要求先解压完整资产。所有条目使用规范化相对路径；拒绝重复路径、绝对路径、`..` 穿越、越界偏移、长度溢出、重叠条目、符号链接语义和未知压缩方式。索引按路径稳定排序，条目记录原始长度与 SHA-256；已压缩的 UE 容器默认不在外层重复压缩。P0 协议规范应规定字节序、最大字段长度、条目数与总解压大小上限，以及签名所覆盖的规范化字节范围。

Runtime 在加载前检查格式、宿主兼容性、依赖、哈希及宿主规定的签名策略。签名若启用，覆盖整个可执行内容与索引，不能仅签 Manifest。DLL 与需落盘挂载的 UE 容器提取到由内容哈希命名的缓存目录，先写临时文件、验证后原子替换；缓存不改变单文件分发约定。签名证明来源和完整性，不会把 Native DLL 变成沙箱代码。宿主可选择禁止原生模组或只接受可信签名。

### 5.3 Cooked 内容挂载

包后端由宿主显式声明支持 `Pak`、`IoStore` 或二者。运行时在验证后为模组建立独立包命名空间，挂载资源容器，让 Cooked Asset Registry 可见，再发布框架的逻辑内容注册表；两种 Registry 是不同数据结构。UE 5.6 提供 Pak 挂载接口；官方 API 建议通常经 `FCoreDelegates` 的挂载委托调用，且其挂载参数可报告相关 IoStore 状态。[UE 5.6 Pak 挂载 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/PakFile/FPakPlatformFile/Mount?application_version=5.6)

**Pak 与 IoStore 在 Shipping 中的动态挂载、包路径解析、Asset Registry 合并、软引用加载和材质显示，必须分别通过端到端实验。** 在通过实验前，它们是目标能力，不是已验证的兼容承诺。首版实现可以按验证结果只启用已通过的容器后端；不支持的包在启动时给出明确错误。

## 6. 启动编排与失败处理

| 阶段 | 工作 | 完成条件 |
| --- | --- | --- |
| 契约准备 | 游戏模块注册构建期生成的 API/Hook/Event 描述符和服务提供者；核对 Schema 指纹 | Host Export Catalog 可查询，重复或缺失接入被报告 |
| 预启动 | 读取 `Mods/*.mod` 的 Header、Manifest 和 Index；校验宿主/平台/格式/契约范围 | 得到候选模组及错误报告 |
| 依赖决议 | 验证版本、冲突、缺失依赖和依赖环；确定稳定加载顺序 | 得到可用模组集合 |
| 资源准备 | 验签与哈希、提取缓存、挂载 Pak/IoStore、建立包命名空间 | 容器与包路径可访问 |
| 模组注册 | 合并资产元数据，注册逻辑内容、声明式 Hook/Event 订阅和覆盖规则 | 内容可按 ID 查询，回调候选集合已建立 |
| 原生初始化 | 加载 DLL、协商 ABI、提供宿主服务表、执行注册回调 | 原生扩展具备明确生命周期；随后生成稳定回调快照 |
| Host Ready | 向游戏发出就绪事件；游戏进入正常流程 | Gameplay 使用缓存结果，按需异步加载资产 |

具体 UE 模块 `LoadingPhase` 在原型中按所需引擎服务确定，不把以上所有步骤放进 `UGameInstanceSubsystem::Initialize()`。需要更早注册的宿主内建 Shader 类型应由宿主自身的早期模块承担；普通 `.mod` 不在游戏启动后新增 Global Shader 类型。UE 5.6 官方文档要求 Global Shader 类型所在模块在引擎初始化前加载，并说明游戏启动后不能由动态模块再添加 Shader 类型。[UE 5.6 Global Shader 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)

依赖环中的模组及其必需依赖者全部停用；单个模组校验失败时不挂载它或依赖它的模组。若挂载过程中发生部分失败，首版停止后续注册，报告需重启的错误状态，不尝试在运行中热卸载已挂载的 DLL、UObject 或 Shader。所有失败都有可机器读取的错误码与面向作者的解释。

## 7. 原生 ABI 与 Shader 边界

默认 Native Mod 仅导出稳定 C ABI 入口。宿主和 DLL 交换带 `structSize`、ABI 主次版本及能力位的服务表；跨边界数据使用固定宽度整数、UTF-8、长度显式的缓冲区和不透明句柄。内存由分配方释放，回调标明游戏线程或工作线程要求；异常不跨 DLL 边界传播。ABI 主版本不符直接拒载，次版本通过结构长度和能力位协商。每个可跨边界的 API/Hook/Event 都必须有生成的 C ABI 适配和明确的数据布局，不把 `UObject*`、`FString`、`TArray` 或 `USTRUCT` 的内存布局直接传给独立 DLL。

Native DLL 可在初始化阶段注册事件处理器、内容提供器和游戏提供的高频回调。它不得假定能直接链接任意 UE 或游戏 C++ 符号；若宿主愿意开放 UE 对象能力，应通过明确的句柄与服务表提供。所谓 C++ / UObject 的 Native Fast Path，是**已随宿主编译**的游戏代码与 Runtime 间的调用方式；它不构成独立 DLL 获得 UE C++ ABI 的承诺。UE 的模块导出机制会受模块化/Monolithic 构建方式影响。[UE 5.6 模块 API 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/module-api-specifiers-in-unreal-engine?application_version=5.6)

“Native UE Module Mod” 是独立研究线：新 `UCLASS`、`USTRUCT`、自定义组件与 Global Shader 类型需要 UHT 生成代码、模块装载和反射/Shader 注册时序配合。只有在指定 UE 构建、目标平台和 Shipping 链接模式完成验证后，才定义其兼容性与交付方式。材质使用的 Cooked Shader Code 与“新增 Global Shader 类型”分别验收，不能用前者的成功推断后者可用。UE 文档明确新增 Global Shader 类型的模块必须在引擎初始化前加载，游戏启动后动态模块不能再添加 Shader 类型。[UE 5.6 Global Shader 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine?application_version=5.6)

## 8. 模组开发编辑器与预览

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
| P7 研究 | UE 原生模块、新反射/Global Shader 类型、热卸载 | 每项形成指定 UE 构建与 Shipping 模式的技术报告，再决定是否进入支持矩阵 |

G1 在 P0 后立即开展，可与 P1 的契约实现并行，但未通过前不能承诺 P3 的资源能力。实验必须覆盖两个模组的独立包命名空间、资产注册、异步加载、材质显示和游戏重启后的加载一致性；Pak 与 IoStore 分别判定，不能用一个后端的结果推断另一个。只有通过的 UE/平台/游戏 Cook Profile 组合进入兼容性矩阵。P1 至 P6 的排序表达产品依赖，不阻止不共享状态的原型实验并行。

## 11. 待验证与待决定事项

1. **Shipping 资源链路**：UE 5.6 Win64 下，Pak 与 IoStore 分别能否在宿主进程启动期动态挂载，并让 Cooked Asset Registry 与软引用正确解析？验证结果决定首版启用哪些后端。
2. **原生构建接入与生成器类型集**：固定 MHT/MBT/UBT 职责和生成清单边界，验收 UHT Exporter、Prepare/Finalize 时序、完整 Clang 分析所需生成头与增量失效；确定首版源类型、公开投影、C ABI/Blueprint 组合及 SDK Bundle 分目标独立消费流程。
3. **原生能力范围**：默认 C ABI 已确定；是否投入 P7 的任意 UE 模块、反射类和 Global Shader 类型，需要针对目标游戏构建逐项决策。
4. **Cook Profile 的发布责任**：接入游戏须确定作者工程所需的游戏 SDK、Cook 参数、可引用类和兼容版本发布流程。
5. **模组信任策略**：由游戏决定是否允许未签名资产包、是否允许原生 DLL，以及签名公钥如何分发和轮换。
6. **多人游戏语义**：若接入游戏有联网功能，需定义服务器权威、双方模组列表/内容哈希协商，以及世界修改和事件回调的权限。首版单机路径不隐含多人兼容。

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
