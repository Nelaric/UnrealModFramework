<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModHeaderTool（MHT）

ModHeaderTool（MHT）负责解析当前 UE 构建目标中的活动 C++ 与蓝图导出声明，静态生成 UE 强类型视图、宿主 C++ 桥接代码、公开 SDK 契约以及兼容纯 C 的 `ContextHandlesV1` ABI 边界。其中，宿主侧实现由 UBT 直接在声明所属的游戏模块内编译；客户端（Client）实现则由 ModSDKTool（MDT）作为源码发布到共享 SDK Runtime 模块中，供宿主游戏与独立 Mod 制作编辑器分别编译。

## 构建与命令行用法

在仓库根目录执行构建脚本即可单独编译 MHT：Windows 下运行 `.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModHeaderTool`，Linux 或 macOS 下运行 `sh UnrealModFramework/Plugins/ModRuntime/Tools/Build.sh --target ModHeaderTool`。

```text
ModHeaderTool discover <工程.uproject>
ModHeaderTool check-options <工程.uproject> <MetadataOptions.json>
ModHeaderTool scan <工程.uproject> <MetadataOptions.json>
ModHeaderTool generate <GenerationRequest.json>
```

- `discover`：列出当前工程按规则选中的模块，并标明各模块是否包含导出宏标记。
- `check-options`：检查 Editor 元数据选项快照是否仍然有效；若需要重新运行 `ModExportOptions` 刷新，则返回退出码 `2`。
- `scan`：对导出宏的词法语法和字段取值做快速校验，并将 JSON 报告输出到 stdout。
- `generate`：结合 `libclang`、目标真实的 UBT 编译数据库及标准 UHT 生成头文件，完成完整语义校验与代码生成。日常 UE 构建时，UHT 适配层会自动组装 `GenerationRequest.json` 并调用该命令（详见 [ModRuntime UHT 适配层](../../Source/ModRuntimeUht/README.zh-CN.md)）。

## 核心生成产物

以下路径均相对于目标变体的固定输出目录（`Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>/`），每次生成都会同步刷新：

| 生成文件路径 | 作用说明 |
| --- | --- |
| `SDK/Public/<game>.modabi.h` | 纯 C 声明、定宽整数/标量类型、状态码、ABI 版本与哈希常量，以及强类型宿主函数表结构体 |
| `Host/<owner>/<owner>.modabi.cpp` | 宿主侧 C 入口、精确成员方法调用、运行期线程/作用域/权限校验，以及导出的函数表查询入口 |
| `Client/<game>ModClient/<game>ModClient.modclient.cpp` | 客户端调用桩函数，以及内置版本与哈希核验的 `Connect`、`Disconnect` 和 `ReleaseObject` |
| `Contract/<game>.clientabi.json` | `UMF.ClientABI` v2 连接描述，记录所属模块、函数表查询/连接符号、槽位、签名、哈希与生命周期规则（[Schema](Protocol/client-abi.schema.json)） |
| `Contract/<game>.sdkcontract.json` | 已解析的 `UMF.SdkContract` v2 契约，供 MDT 生成 UE 反射与蓝图包装代码 |
| `<game>.generated-artifacts.json` | `UMF.GeneratedArtifacts` v4 总清单，记录目标身份、全部生成文件哈希及各编译单元的角色与归属模块（[Schema](../ModBuildTool/Protocol/generated-artifacts.schema.json)） |

在 v4 清单中，MHT 将客户端源码标记为 `ClientSource`（逻辑归属名为 `<game>ModClient`），随后由 MDT 放入 SDK 插件的实际模块 `<pluginName>Runtime` 中编译。`.modviews.h` 与 `.modcalls.h` 属于仅供宿主内部使用的 `HostHeader`；`.modabi.h` 以及按需生成的错误码目录和独立扩展 ABI 头文件则标记为公开 `SdkHeader`——MDT 只会复制通过哈希校验且角色公开的文件。

## 连接与生命周期

运行期，宿主与共享 SDK Runtime 按以下六个步骤建立连接并隔离各 Mod 的调用生命周期：

1. **查询宿主函数表**：定位承载 `ownerModule` 的宿主二进制，解析导出的 `querySymbol`，传入生成的 ABI 版本号、`sizeof(table)` 与 `nativeABIHash` 获取函数表。
2. **建立客户端连接**：将该函数表传给客户端对应的 `connectSymbol`；`Connect` 会在逐一校验版本号、结构体字节大小、ABI 哈希及全部必需回调指针非空后完成绑定。
3. **签发调用方与作用域令牌**：由宿主业务代码审批调用方（Caller）、作用域与权限集——仅限宿主内部调用的 `Mod::API::FNativeAccess::Open` 会签发非零的 context 与 scope 令牌，`UModAPIHostSubsystem::OpenLocalPlayer` 则绑定生成的 `LocalPlayer` 提供者并在 `Native` 字段中返回这组令牌。
4. **发起强类型调用**：将令牌交给 Mod 消费端。对象 Getter 接收 `(context, scope)` 并返回 object 令牌；成员方法接收 `(context, object, ...)` 并在每次进入宿主时重新核验调用线程、Caller 存活状态、目标对象类型、作用域及权限。
5. **令牌吊销与显式释放**：当某个 Mod 停止运行时，宿主会立即吊销其 Caller 上下文；作用域或注册表销毁同样会使关联令牌失效。调用 `ReleaseObject` 可显式使某枚对象令牌的所有副本失效，但既不持有也不销毁底层 `UObject`（对仍然存活的对象重复投影会直接复用现有绑定）。
6. **单 Mod 卸载与整模块卸载**：卸载单个 Mod 时，仅吊销该 Mod 自己的 Caller 令牌，其余已挂载 Mod 继续共用模块级 Client 函数表；只有在整体卸载宿主模块或 SDK 模块前，才需要先停发新调用、等待在途调用排空，最后调用 `Disconnect` 断开模块级函数表。

简言之，底层函数表按模块共享，而身份、作用域、权限与对象令牌严格按 Caller 隔离，绝不隐式猜测“全局当前玩家”或“全局当前 World”。所有令牌均为 64 位进程内不透明标识符（绝非裸内存地址），在 Runtime 模块存活期间不会回绕复用，跨 Caller 混用令牌会在查表阶段直接拦截。

### C 边界调用约定与 ABI 哈希

跨 C ABI 边界统一采用显式 `cdecl` 调用约定、64 位进程内令牌、定宽整数/浮点标量、取值严格限定为 `0` 或 `1` 的 `uint8_t` 布尔值，以及 `uint32_t` 返回状态码（`0` 表示成功，`65536` 表示尚未连接，`65537` 表示 ABI 不匹配，`65538` 表示捕获到未预期的原生异常）。输出参数内存由调用方分配：调用失败时自动清零，调用成功时再写入结果。

`nativeABIHash` 是 `UMF.ClientABI` 描述对象在填入 `nativeABIHash`、`contractHash` 和 `projectionHash` 三个摘要字段之前，按 `nlohmann::json` 排序键与紧凑 `dump()` 序列化计算出的 SHA-256，完整锁定了 API 签名形状、连接符号名、状态码定义与生命周期规则；公开头文件 `<game>.modabi.h` 中内嵌了完全一致的版本与哈希常量供编译期核对。

## World 扩展产物

当目标工程包含 World 作用域的 Hook 或 Event 时，MHT 会额外产出三组扩展文件：

- **`Host/<owner>/<owner>.modextensions.h` 与宿主桥接源码**：为宿主 C++ 调用点提供类型化的 `Invoke` 与 `Publish` 辅助函数，在模块启动时统一登记 C++ 与蓝图扩展定义，并为指定了 `HostEndpoint` 的 C++ Event 生成成员函数体。
- **`Contract/<game>.sdkcontract.json` 中的 `extensions` 节**：完整记录扩展 ID、签名哈希、所需授权、阶段/投递策略与载荷投影；制作编辑器的打包器据此校验 v2 扩展清单。
- **`SDK/Public/ModExtensionABI.h` 与 `SDK/Public/<game>.modextensionsabi.h`**：在存在生命周期元数据与扩展定义时一并生成，向独立原生扩展 DLL 提供回调注册契约、扩展 ID、签名常量与紧凑结构体线格式，与负责主动 API 调用的 `ContextHandlesV1` 函数表协同工作。

签名约束上，Hook 接收单个载荷并返回相同类型结果，Event 接收单个载荷并返回 `void`。受支持的载荷类型涵盖 `float`、`double`、`int32`、`int64`、`bool`、`uint8`、受支持的反射值结构体（按声明的公开字段拷贝），以及具备生成 World 视图的 `UObject`（在单次回调持续期间暴露为只读视图）。宿主蓝图的标量 API 与已连线的蓝图 Hook/Event 则从 Editor 导出快照合并生成（见 [ModRuntime UHT 适配层](../../Source/ModRuntimeUht/README.zh-CN.md)）。

### 自动 C++ Hook 与阶段控制标记

在 `.cpp` 中有类外定义的标量成员函数上标注 `MOD_EXPORT_HOOK()`，即可自动生成形如 `hook/game.character/change-health` 的扩展 ID（由所属类的 World 类型 ID 与连字符化函数名拼成）。默认同时开启三个阶段：`Pre`（支持 `ModifyInputs` 与 `Cancel`）、`On`（支持 `Replace`）和 `Post`（只读观察结果）；基础订阅权限名即 Hook ID 本身，写操作权限则在 Hook ID 后追加对应操作后缀。生成期间 MHT 会调用 MST 产出改写副本及 `Diagnostics/HookSources.json` 映射表，供 MBT 在驱动 UBT 编译时替换输入（见[自动 Hook 构建](../../Source/ModRuntimeUht/README.zh-CN.md#自动-hook-构建)）。

可在宏括号内按任意顺序传入无参标记来裁减阶段：

- `NoPre`：关闭 `Pre` 阶段，同时移除 `ModifyInputs` 与 `Cancel` 操作。
- `NoOn`：关闭 `On` 阶段与 `Replace` 操作，确保凡是通过 `Pre` 的调用必定执行宿主原函数。
- `NoPost`：关闭 `Post` 阶段。
- 同时指定 `NoPre, NoOn, NoPost`：MHT 会将该 Hook（含所有曾用 ID）从公开 SDK 中完全剔除，MST 也会原样保留原函数体不做改写。

重构重命名成员函数时，可在同一宏中通过 `CompatibleNames = ("ChangeHealth", "UpdateHealth")` 列出曾用名。新旧名称共用同一套阶段策略与同一条有序回调链：只要函数签名与阶段标记保持不变，所有旧 ID 的契约签名和权限名称都能无缝延续；反之，调整阶段标记会同步改变新旧所有名称的契约签名。若出现重复 ID（包括不同大小写/下划线拼写折叠到同一个连字符 ID 的情况），MHT 会直接报错拦截。

### 生成 C++ Event 发布函数

在 `MOD_EXPORT_EVENT` 中将 `HostEndpoint` 设为类内某个 `Mod::Extension::FEndpoint` 成员的名字，MHT 就会在 `<owner>.modhost.cpp` 中自动生成该导出成员函数的定义，把入参直接转发给 `Publish`；如果不填 `HostEndpoint`，也可以由宿主代码自己实现成员函数并调用 `.modextensions.h` 中的发布辅助接口。（`HostCommitPayload` 目前为生成器预留字段，指定时必须搭配 `HostEndpoint`。）

## API 兼容名（`CompatibleNames`）

显式标注 `MOD_EXPORT_API` 的成员默认按所属类型 ID 与方法名生成导出 ID（如 `api/game.character/increasehealth`）。若在宏中添加 `CompatibleNames = ("AddHealth")`，MHT 会为同一底层实现额外发布 `api/game.character/addhealth`，并在 SDK 中同时生成两个同名策略一致的调用入口。

当未显式填写 `CompatibleNames` 时，MHT 会自动读取引擎与已启用插件 INI 配置中的 `FunctionRedirects`：凡是在同一类内精确指向某个已导出宿主 `UFUNCTION` 的重定向条目（含连续多次改名链），都会自动补入旧名列表（若跨类存在同名函数，请在重定向配置中补全反射类名与模块名加以限定）。若希望明确禁用自动推导，可显式写成空列表 `CompatibleNames = ()`。

遇到非法标识符、ID 冲突或适用范围内的损坏重定向条目时，MHT 会精确指向对应宏声明或 INI 配置报错。常规 UBT 构建会自动刷新配置快照；若在修改 INI 重定向后单独从命令行运行 MHT，请先执行一次 `ModExportOptions` 更新快照。请始终基于同一批次的生成契约同步编译宿主与 SDK。

## FullView 与对象输入

`MOD_EXPORT_API` 支持可选字段 `FullView`，接收带双引号的限定 C++ 类名列表（例如 `FullView = ("USkeletalMesh")`），其中每个类名都必须对应当前函数签名中的某个 `UObject*` 入参。生成阶段会按类归并请求，在当前 Target 的 AST 中收集该类及其公开基类的所有具体 `public` 实例方法，自动合成视图调用绑定，同时让已单独标注导出宏的方法继续保留各自的显式契约。运行期调用这些自动合成的方法时，同时要求可写绑定、发起该 `FullView` 声明所需的权限，以及宿主授予的 `FullView.<TypeId>` 权限。只要目标类中有任何一个公开实例方法无法完成投影，生成就会直接报错并列出所有受影响成员，绝不静默漏掉部分接口。

参数传递方式上：UE 侧 SDK 调用者直接传入强类型 `UObject*` 指针；底层 `ContextHandlesV1` C ABI 则为每个对象入参传递借用的 UTF-8 对象路径字节指针 `const char*` 与字节长度 `uint32_t`（上限 `4096` 字节，空指针以 `nullptr` 和长度 `0` 表示）。每次同步调用进入宿主时，宿主会查找已加载的 `UObject`、核验其类继承关系与所属 World，并在本次同步调用期间借用该对象；返回对象则统一包装为宿主签发的视图令牌。引擎公开类的头文件统一从构建输入的 `Engine` 根路径下解析。

## MBT 与 MDT 接续

三个工具围绕生成清单紧密接力：

1. **MHT** 生成宿主桥接、C ABI 入口、视图实现与编译校验源码，在 `compilationUnits` 中逐个标明角色与所属模块，并在 `requiredSymbols` 中按 `ExportId`、`signatureHash` 与 `projectionHash` 将 Host 与 Client 符号配对。
2. **UHT 与 MBT** 校验完整的 v4 清单，将 `*.umfhost.cpp` 包装单元挂入所属 UE 模块，在自动 Hook 构建中替换 UBT 编译动作输入，并在桌面目标链接完成后核验批次符号、写出 `host-link.json`（见[最终链接核对](../ModBuildTool/README.zh-CN.md#最终链接核对)）。
3. **MDT** 读取 `.sdkcontract.json` 与各目标的 v4 清单，保留原有的对象 Getter 与 `float` 方法适配器，同时为带标量和公开 `UObject*` 入参的方法生成强类型 UE/蓝图包装（若契约中包含当前适配图尚不支持的接口形态，则明确记为 `Unsupported`）。

最终在运行期，Mod 可通过活跃的 `UModInstance` 获取宿主授权的蓝图 World 提供者，由宿主业务代码显式交付 `LocalPlayer` 等其他作用域上下文，并在 World 准备阶段接入独立的原生 C ABI 回调（详见 [ModRuntime UHT 适配层](../../Source/ModRuntimeUht/README.zh-CN.md)与[独立原生回调](../ModSDKTool/Resources/DevelopmentEditor/README.zh-CN.md#独立原生回调)）。
