<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModRuntime UHT 适配层

在 UE 工程中启用 `ModRuntime` 插件后，UnrealBuildTool（UBT）会自动发现并编译 `ModRuntime.ubtplugin.csproj`（优先沿用当前引擎的目标 .NET 框架，缺省回退至 .NET 8），加载其中的 `[UnrealHeaderTool]` 插件类与 `[UhtExporter]` 入口。由于反射模块 `ModLoader` 同时参与 Game 与 Editor 构建，该 Exporter 会在两类目标中自动生效。

在整个构建链路中，各组件按如下边界协作：

- **UHT 适配层（`ModRuntimeUht`）**：收集当前 UBT Target 的编译参数与配置快照，调用 ModHeaderTool（MHT），并将生成的宿主 C++ 包装单元登记回 UBT。
- **ModHeaderTool（MHT）**：基于 `libclang` 解析 C++ 声明，产出宿主桥接代码、公开契约及 C ABI 头文件。
- **ExportCodeGenerator**：在 Editor Commandlet 中编译并导出宿主蓝图声明快照。
- **UBT 与 ModBuildTool（MBT）**：在声明所属模块内编译宿主代码，替换自动 Hook 的编译源码副本，并在链接完成后核验桌面目标二进制。

## 工具发现与默认调用

在 `.Target.cs` 中调用 `ExportMetadata.ConfigureTarget(this)` 会自动注册预构建步骤，负责编译 MHT、MBT 和 ModSourceTool（MST）。若你的构建环境由外部流程提前准备好这三个可执行文件，可改为调用 `ExportMetadata.ConfigureTarget(this, buildNativeTools: false)`（脱离 UBT 直接运行 UHT 前，也可先手动执行原生工具构建脚本）。

UHT 按以下优先级顺序查找 MHT：

1. 环境变量 `UMF_MHT_EXECUTABLE`。
2. 工程可选配置文件 `Config/ModHeaderTool.json` 中的 `executable` 字段。
3. 插件目录下的 `Binaries/ThirdParty/ModHeaderTool/<宿主平台>/MHT[.exe]` 或 `ModHeaderTool[.exe]`（`<宿主平台>` 为 `Win64`、`Linux` 或 `Mac`）。
4. 插件本地默认构建输出 `Tools/build/ModHeaderTool/ModHeaderTool[.exe]`。

MBT 与 MST 遵循一致的查找顺序：MBT 依次检查 `UMF_MBT_EXECUTABLE`、根配置项 `mbt` 以及 `Tools/build/ModBuildTool/ModBuildTool[.exe]`；MST 依次检查 `UMF_MOD_SOURCE_TOOL_EXECUTABLE`、根配置项 `sourceTool` 以及 `Tools/build/ModSourceTool/ModSourceTool[.exe]`（详见 [ModSourceTool 指南](../../Tools/ModSourceTool/README.zh-CN.md)）。

### 默认 `scan` 模式与元数据快照

未提供 `Config/ModHeaderTool.json` 或未指定 `mode` 时，默认运行 `scan` 模式（仓库内的开发宿主工程配置为 `generate`）。在标准 UHT 流程中，`scan` 模式会调用：

```text
ModHeaderTool scan <实际.uproject> <元数据快照.json>
```

MHT 按词法检查导出宏语法与字段取值，向标准输出打印 JSON 报告，再由 UHT 输出工厂将通过校验的报告落盘至 `Intermediate/ModTools/ModHeaderTool/Scan/<Target>/<Platform>/<Configuration>/Scan.json`。

权威元数据选项快照默认存放在 `<工程>/Intermediate/ModTools/ModHeaderTool/MetadataOptions.json`。执行生成前，适配层会先通过 MHT 检查该快照；若快照不存在、已过期或格式版本较旧，适配层会自动运行当前引擎的 `ModExportOptions` Commandlet 重新导出并在校验后发布。修改 `DefaultEngine.ini` 等受跟踪的 INI 文件会在下次增量构建时自动触发刷新。（`scan` 模式也会顺带刷新已存在的默认快照；若工程内的声明均不依赖配置候选项，即使尚无快照也能直接完成扫描；通过 `options` 指向自定义路径的快照则由调用方自行维护。）

当运行 Commandlet 需要工程二进制但尚未编译过编辑器时，`metadataEditorTarget` 用于告知 UBT 自动以引导模式编译哪个 Development Editor Target；未配置该字段的工程可先通过 `-MhtBootstrap` 手动构建一次编辑器。

## 完整生成模式

需要将声明生成为完整宿主桥接代码与 SDK 契约时，请将 `mode` 设为 `generate`，并在 `generation` 中填写游戏 ID 与类型身份映射表。开发宿主使用的配置如下，工具链与 `libclang` 均由适配层从 Setup 产物中自动定位（见仓库中的 [Config/ModHeaderTool.json](../../../../Config/ModHeaderTool.json)）：

```json
{
  "schemaVersion": 1,
  "mode": "generate",
  "metadataEditorTarget": "UnrealModFrameworkDevEditor",
  "blueprintExports": [{
    "asset": "/Game/Blueprints/BP_ModBlueprintAPIExportTest.BP_ModBlueprintAPIExportTest",
    "typeId": "game.blueprint_test",
    "module": "ExportTest"
  }],
  "generation": {
    "gameId": "UnrealModFrameworkDev",
    "typeIdentities": "Config/ModAPITypes.json",
    "analysisArguments": ["-Wno-invalid-constexpr"]
  }
}
```

配置文件中的相对路径基于 `.uproject` 所在目录解析，而底层 `buildInput` 文件内部的路径遵循 MHT 自身的解析规则。`options` 与 `timeoutSeconds`（取值 `1`–`86400`，默认 `600`）均为可选项。默认情况下，适配层通过 `Tools/build/libclang-<system>-<arch>.path` 相对路径文件定位 `libclang` 及配套 Clang 资源头文件（`Dist/ModRuntime` 导出的插件同样沿用该机制），未找到时再回退查找系统安装的 LLVM；环境变量或配置文件中显式指定的路径始终优先。

此外，设置环境变量 `UMF_MHT_BUILD_INPUT` 可直接指定本次使用的 `buildInput`（同时强制切换为 `generate` 模式并覆盖 Target 配置），设置 `UMF_MHT_LIBCLANG` 可覆盖 `libclang` 库路径。调整环境变量后需重新触发 UHT（例如向 UBT 传入 `-ForceHeaderGeneration`）。

### 编译输入收集与产物原子提交

UBT 会将当前平台、配置和 CPU 架构写入 UHT 清单。常规构建时，适配层调用 UBT 的 `GenerateClangDatabase` 生成目标编译数据库，在正式编译动作落盘前展开各导出模块的编译器响应文件，并将 `BuildInput.json` 与 `Ubt/compile_commands.json` 写入 `Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>/`。MHT 解析每个翻译单元时都会核对 Target、所属模块、配置及架构是否匹配（若通过 `buildInput` 传入自定义构建输入，其编译数据库与响应文件同样必须匹配当前 Target，并包含标准 UHT CodeGen 产出的反射头文件）。

生成产物统一输出到 `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>/`，包含 `SDK/`、`Host/`、`Client/`、`Contract/`、`Diagnostics/` 以及 `UMF.GeneratedArtifacts` v4 清单（`<gameId>.generated-artifacts.json`）。每次运行都会校验产物完整性、目标身份与文件哈希：自动清理已移除的旧文件，保留内容未变文件的时间戳，并在全部文件就绪后原子提交 v4 清单（`generationId` 由内容摘要决定）。随后，适配层将宿主桥接与编译校验单元注册给 UBT，将 `ClientSource` 留给 ModSDKTool（MDT）打包至共享 Runtime。

### 模块选择

当 `.uproject` 同级目录不存在 `<项目名>.modexport` 或文件内容为空对象 `{}` 时，MHT 默认通过 `*.Build.cs` 递归扫描工程 `Source/` 目录下的全部模块。若需纳入插件或排除特定模块，可创建 `<项目名>.modexport`：

```json
{
  "ExcludeModules": ["GameEditor"],
  "Plugins": [
    { "Name": "Combat", "ExcludeModules": ["CombatEditor"] },
    "Audio"
  ]
}
```

- `Plugins` 数组元素既可以写字符串（`Plugins/` 目录树下的插件名，或指向 `.uplugin` 的路径），也可以写成带 `Name` 或 `Path` 的对象，并在其中附带仅作用于该插件的 `ExcludeModules`（相对路径以 `.uproject` 目录为基准）。
- 顶层 `ExcludeModules` 对工程自身及全部入选插件生效，插件对象内的 `ExcludeModules` 仅作用于该插件（若同一插件被重复声明，局部排除列表会自动合并）。模块名匹配忽略大小写，而某个插件最终是否参与当前 Target 编译仍由 UE 决定。

独立 `scan` 与自动 `generate` 共用同一套模块筛选逻辑。凡是包含导出宏或挂载了蓝图导出的入选模块，UHT 都会从当前 Target 的编译数据库中提取其实际参与编译的 C++ 翻译单元——后续新增模块或 `.cpp` 源文件会在下次构建时自动纳入。你也可以随时运行 `ModHeaderTool discover <项目.uproject>` 查看当前选中的模块列表及标记命中情况。

### 从宿主蓝图导出 API

要将宿主蓝图中的接口导出给 Mod，请在 `Config/ModHeaderTool.json` 的 `blueprintExports` 中添加条目，填好三个字段：

- `asset`：已保存蓝图资产的对象路径
- `typeId`：跨版本保持稳定的类型标识
- `module`：处于扫描范围内、且至少包含一个 UE 反射头文件的 Runtime 模块（以便 UHT 将生成的包装编译单元挂入该模块）

可参考开发宿主中的 `BP_ModBlueprintAPIExportTest` 示例。正式发布前请在 **蓝图Mod导出** 面板中为声明填写明确的 `PublicName`（若已有声明的公开名或版本留空，则默认回退为当前声明名和 `1.0.0`）。

生成阶段，`ModExportBlueprints` Commandlet 会编译该蓝图资产，从生成的 `UFunction` 提取精确签名并交给 MHT。目前蓝图实例 API 支持 `World`、`GameThread`、`ReturnError` 组合，入参为按值传递的布尔或数值标量，返回值为单个标量或 `void`。登记在配置中的蓝图资产包会自动加入宿主 Cook 列表（首次构建缺少编辑器二进制时同样可通过 `-MhtBootstrap` 引导）。

运行期，宿主在蓝图对象就绪后调用 `UModAPIHostSubsystem::RegisterWorldProvider` 并保存返回的注册句柄，再通过 `SetWorldCallerPolicy` 配置允许访问该提供者的 Mod、类型与权限。在 Mod 工程侧，生成的 `<SDK>API.h` 提供 `Acquire_<对象引用类型>` 辅助函数：C++ Mod 在活跃的 `UModInstance` 子类中调用该函数，即可自动接通底层函数表并拿到强类型引用（在当前 World 存续期间可重复调用，直至该 Mod 卸载、World 关闭或宿主更新授权策略）：

```cpp
FModObject_T_f4f57c0936062c84 Provider;
if (UUMFDevSDKLibrary::Acquire_FModObject_T_f4f57c0936062c84(*this, Provider))
{
	EUMFDevSDKCallOutcome Outcome;
	int64 ErrorCode;
	UUMFDevSDKLibrary::TestModAPIExport(Provider, 1.0, Outcome, ErrorCode);
}
```

宿主收到调用后，会通过 `ProcessEvent` 在真实的蓝图对象实例上执行对应逻辑。

### 从宿主蓝图导出 Hook 和 Event

将蓝图资产加入 `blueprintExports` 后，在 **蓝图Mod导出** 面板中新建 Hook 或 Event 声明，并在业务执行图中连好对应 **Hook Point** 或 **Publish Event** 节点的执行输入引脚——未连接执行线的声明会在导出时自动跳过。

- **蓝图 Hook**：专属原版逻辑图接收一个载荷并返回同类型结果；业务图上的 **Hook Point** 节点则一并输出最终结果、状态码、是否取消以及是否已提交标记。
- **蓝图 Event**：支持 `Immediate`（同步立即广播）与 `NextTick`（下一帧队列投递），并可选开启立即退订。
- **载荷类型**：支持标量、受支持的反射值结构体，以及具备生成 World 视图的 `UObject`。

在 C++ 中，只需在已配置 World 类型身份的成员声明上标注导出宏：通过 MBT 构建时，无参 `MOD_EXPORT_HOOK()` 会自动包装 `.cpp` 中的类外定义；C++ Event 则既可在自定义函数体中直接调用生成的发布辅助函数，也可通过指定 `HostEndpoint` 让 MHT 自动生成发布函数体（详见 [World 扩展产物](../../Tools/ModHeaderTool/README.zh-CN.md#world-扩展产物)）。

### 自动 Hook 构建

请在执行 UBT 编译动作的主机上安装好目标平台的 UE SDK 并运行 MBT，每次调用针对单个 CPU 架构构建（Apple 目标的符号核验还需安装 `python3` 与 Xcode `nm`）。例如，在仓库根目录构建 Linux x64 Development Editor：

```sh
mbt=UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool
"$mbt" build-ue-target --engine /opt/UE_5.6 --project UnrealModFramework/UnrealModFrameworkDev.uproject \
  --target UnrealModFrameworkDevEditor --platform Linux --configuration Development --architecture x64 --mode normal
```

独立 Hook 回归脚本（[Scripts/test_hook_targets.py](../../../../../Scripts/test_hook_targets.py)）覆盖了以下 UBT 平台与架构组合的源码改写与链接校验（其中 Win64 `arm64ec` 用例还需安装对应的 Windows SDK 运行库）：

| UBT 平台 | 测试架构 |
| --- | --- |
| `Win64` | `x64`、`arm64`、`arm64ec` |
| `Linux` | `x64` |
| `LinuxArm64` | `arm64` |
| `Mac`、`Android` | `x64`、`arm64` |
| `IOS`、`VisionOS` | `arm64`、`iossimulator` |
| `TVOS` | `arm64`、`tvossimulator` |

环境变量 `UMF_MHT_LIBCLANG` 与 `UMF_MHT_CLANG_RESOURCE_DIR` 用于指定构建主机自身的 `libclang` 及资源头文件目录（也可在工程配置中通过 `hostLibclang` 按主机系统指定），而交叉编译所需的目标 SDK、包含路径与架构参数均直接取自 UBT 编译数据库（集成测试范围见 [Linux 编译的范围](../../../../../Docs/DevelopmentWorkflow.zh-CN.md#linux-编译的范围)）。

底层机制上，MHT 与 MST 通过 Clang AST 精确定位带标记的类外成员函数并写出改写后的源码副本；MBT 则在对应的 UBT 编译动作（含嵌套响应文件）中将输入替换为该副本，同时保持目标 `.obj`/`.o` 与最终二进制输出路径不变。由于生成的宿主注册代码强引用了仅在改写副本中定义的批次专属符号，一旦原版未改写的目标文件误入链接，COFF 与 ELF 链接器会立即报未解析符号错误；Apple 平台构建还会在后置步骤调用 Xcode `nm` 扫描 Mach-O 符号表，即便开启了动态查找（dynamic lookup）也能揪出遗漏的 Hook 导入。最终 C++ 直接调用与蓝图反射调用都会汇入同一个公开成员入口，由 `Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` 自动化测试双重验证（而 `-MhtBootstrap` 引导构建出的编辑器则直接编译原函数）。

## 宿主编译与链接

UHT 会按 v4 清单中的模块归属，通过 `CompileOutput` 将生成的 `*.umfhost.cpp` 包装单元注册进对应的反射模块，由 UBT 沿用该模块自身的编译规则直接编译。接入模块只需在 `.Build.cs` 中常规声明依赖 `ModAPI` 即可（旧归档流程遗留的 `ConfigureHost` 与 `ConfigureHostLibrary` 仍保留兼容，便于平滑迁移）。

与此同时，MDT 会提取单独生成的 `ClientSource` 放入共享 SDK Runtime 模块。在受支持的单架构桌面目标完成链接后，MBT 会结合 UBT `.target` 回执、`.modules` 清单及各宿主二进制内的批次符号完成核验，并写出 `HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>/host-link.json`（详见[最终链接核对](../../Tools/ModBuildTool/README.zh-CN.md#最终链接核对)与 [SDK 安装与验证](../../Tools/ModSDKTool/README.zh-CN.md#安装与验证)）。

## 构建语义与范围

- **诊断透传与进程托管**：原生工具输出的诊断信息会直接转发至 UHT 日志；非零退出码、产物校验失败或执行超时均会立即中止生成，所有子进程生命周期统一由 UE 的 `ManagedProcessGroup` 托管。
- **跳过生成的特殊模式**：UHT 的 NoOutput/reference 模式、`-MhtBootstrap` 引导模式以及 `-Mode=GenerateClangDatabase` 调用均会自动跳过原生代码生成。由于 MHT 不设 dry-run 模式，传入 `-FailIfGeneratedCodeChanges` 时会直接拒绝执行 `generate`；`scan` 模式的报告则正常参与 UHT 变更检查。
- **增量依赖追踪**：UHT 通过 `AddExternalDependency` 追踪工具可执行文件、配置文件、元数据快照及相关源码，并在变体目录下生成指向 v4 清单的 `watch.json`。每次构建前，MBT 会扫描监视目录与跟踪文件，一旦发现需要 UBT 重新收集编译动作的变更，便会更新 `.mht-watch.stamp`。
- **手动编译适配层程序集**：正常构建时 UBT 会自动生成包含 `EngineDir` 的同级 `.ubtplugin.csproj.props`（已加入 `.gitignore`）。如需单独手动编译该 C# 工程，可显式传入引擎目录：

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
