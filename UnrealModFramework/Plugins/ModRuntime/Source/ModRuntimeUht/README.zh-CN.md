<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModRuntime UHT 适配层

`ModRuntime.ubtplugin.csproj` 从当前引擎继承 UHT Exporter 的目标框架（UE 5.6 对应 .NET 8，UE 5.8 对应 .NET 10）。UBT 会在已启用的 ModRuntime 插件内发现该工程，构建程序集并加载 `[UnrealHeaderTool]` 类与 `[UhtExporter]` 入口。该工程直接引用当前引擎的程序集，无需项目引用或修改引擎。Exporter 名称为 `ModRuntime`，反射运行时模块 `ModLoader` 在 Game 和 Editor 清单中均会启用它。

C# 适配层负责启动独立的 MHT 与 MBT 进程，传递真实编译配方、登记依赖并传播失败信息。C++ 分析、参数校验、投影与源码生成由 MHT 负责；独立编译与归档校验由 MBT 负责；蓝图导出仍归属 ExportCodeGenerator 编辑器模块；最终的游戏与模块链接由 UBT 执行。发现机制参见 Epic 的 [UE 5.6 UHT 扩展文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6)。

## 工具发现与默认调用

调用了 `ExportMetadata.ConfigureTarget(this)` 的 Target 会在正常构建中先编译 ModHeaderTool、ModBuildTool 和 ModSourceTool，再启动 UHT。若直接调用 UHT，请先运行原生工具构建脚本。UHT 按以下顺序查找 MHT：

1. 环境变量 `UMF_MHT_EXECUTABLE`。
2. 项目可选配置 `Config/ModHeaderTool.json` 中的 `executable` 字段。
3. 插件目录下的 `Binaries/ThirdParty/ModHeaderTool/<宿主平台>/MHT[.exe]` 或 `ModHeaderTool[.exe]`，其中宿主平台为 `Win64`、`Linux` 或 `Mac`。
4. ModRuntime 插件的本地构建位置 `Tools/build/ModHeaderTool/ModHeaderTool[.exe]`。

未配置时默认模式为 `scan`；开发工程的 `Config/ModHeaderTool.json` 则指定了 `generate`。在 `scan` 模式下，UHT 执行 `ModHeaderTool scan <实际.uproject> <元数据快照.json>` 来校验标记语法与参数，从标准输出读取 JSON 报告，并将成功的报告提交到工程的 `Intermediate/ModTools/ModHeaderTool/Scan/<Target>/<Platform>/<Configuration>/Scan.json`。

元数据选项快照默认位于 `Intermediate/ModTools/ModHeaderTool/MetadataOptions.json`。生成前，适配层会通过 MHT 检查该快照；若快照缺失、过期或版本较旧，适配层会调用当前引擎的 `ModExportOptions` commandlet 重新导出，并在校验通过后发布。修改 `DefaultEngine.ini` 等已登记的配置会在下一次增量构建时触发检查。`scan` 模式会刷新已有的默认快照；当源码声明不依赖快照时，即使不存在快照也可直接扫描。通过 `options` 指定其他路径的快照由调用方自行维护。

在构建主机上，`metadataEditorTarget` 用于指定元数据 commandlet 使用的 Development Editor Target。缺少所需的项目二进制时，适配层会调用 UBT 以引导模式构建该目标；未配置此项的项目可通过 `-MhtBootstrap` 准备 Editor。

## 完整生成模式

将 `mode` 设为 `generate`，并在 `generation` 中列出导出模块。UBT 会把目标平台、配置和架构写入 UHT 清单，MHT 据此使用该目标的实际编译参数。以下开发宿主配置使用 Setup 提供的工具与 libclang，由适配层自动查找。[仓库中的配置文件](../../../../Config/ModHeaderTool.json)

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
    "modules": [{
      "name": "ExportTest",
      "sourceDirectory": "Source/ExportTest",
      "translationUnits": ["Source/ExportTest/GameCharacter.cpp", "Source/ExportTest/GameLocalPlayerAPI.cpp"]
    }],
    "analysisArguments": ["-Wno-invalid-constexpr"]
  }
}
```

适配层配置中的路径相对于项目目录解析，build input 内部路径遵循 MHT 的解析规则。`options` 和 `timeoutSeconds`（范围 1–86400，默认 600）均为可选项；`mode` 支持 `scan` 和 `generate`，默认为 `scan`。默认情况下，适配层读取 `Tools/build/libclang-<system>-<arch>.path` 中相对于 `Tools/build/` 的路径定位 `libclang`，未找到时回退到主机已安装的 LLVM；`Dist/ModRuntime` 导出的插件同样使用该相对路径文件。环境变量或工程配置中显式指定的库路径优先。

MBT 按以下顺序查找：环境变量 `UMF_MBT_EXECUTABLE`、根配置项 `mbt`，以及 ModRuntime 插件的 `Tools/build/ModBuildTool/ModBuildTool[.exe]`。
ModSourceTool（MST）按以下顺序查找：环境变量 `UMF_MOD_SOURCE_TOOL_EXECUTABLE`、根配置项 `sourceTool`，以及 ModRuntime 插件的 `Tools/build/ModSourceTool/ModSourceTool[.exe]`。

也可通过 `UMF_MHT_BUILD_INPUT` 指定本次构建使用的 build input（同时启用 `generate` 模式并覆盖 Target 配置），并通过 `UMF_MHT_LIBCLANG` 覆盖 `libclang` 路径。环境变量变更后需要重新触发 UHT 执行，例如在 UBT 命令中传入 `-ForceHeaderGeneration`。

`generate` 模式按实际 Target、平台、配置和架构选择 build input，并校验其与当前工程和 UHT 会话一致。适配层在对应变体的 `HostTargets` 目录写入 `GenerationRequest.json`，再执行 `ModHeaderTool generate <GenerationRequest.json>`。在 Win64 x64 MSVC 上，适配层提供独立编译的生产者上下文，由 MHT 发布 `UMF.GeneratedArtifacts` 清单、UHT 调用 MBT 构建宿主与客户端静态库；其他工具链使用 `UMF.UbtHostArtifacts` 清单，通过 UHT 的 `CompileOutput` 在所属 UBT 模块内编译生成的宿主单元。

工程的生成根目录为 `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>`。每次生成会更新当前变体的 `SDK`、`Host`、`Client`、`Contract`、`Diagnostics` 和 `<gameId>.generated-artifacts.json`；内容未变更的文件保留时间戳，完整清单在所有文件替换完成后原子提交。独立编译路径还会将导出插件所属模块的生成文件按相同相对路径同步发布到插件目录，工程目录则保留共享契约、客户端 ABI 和完整清单。

正常构建时，适配层会使用当前 Target 和目标编译器运行 UBT 的 `GenerateClangDatabase`，并在正式编译动作生成前展开导出模块的响应文件。对应变体的 `HostTargets` 目录保存 `BuildInput.json` 和 `Ubt/compile_commands.json`；UBT 复用 Makefile 时，适配层直接读取已保存的编译参数。Target 或变体中显式配置的 `buildInput` 由调用方维护，其编译数据库及响应文件须与当前 Target 匹配。Clang 还依赖标准 UHT CodeGen 生成的头文件。

### 从宿主蓝图导出 API

在工程的 `Config/ModHeaderTool.json` 中配置 `blueprintExports`。每项需提供已保存蓝图的资产对象路径 `asset`、发布后保持稳定的 `typeId` 以及参与生成的 Runtime 模块 `module`（工程中已包含 `BP_ModBlueprintAPIExportTest` 的配置示例）。发布新 API 前，请在 **蓝图Mod导出** 面板中设置 `PublicName`；已有声明若公开名或版本为空，则分别采用当前声明名和 `1.0.0`。

生成时，`ModExportBlueprints` commandlet 会编译已保存的资产，从 Kismet 生成的 `UFunction` 提取签名，并将快照交给 MHT。当前支持 `World`、`GameThread`、`ReturnError` 规格的实例 API，参数为按值传递的布尔或数值标量，结果为一个标量输出或 `void`。配置中的资产包会自动加入宿主 Cook；首次需要准备 Editor 时可使用 `-MhtBootstrap`。

游戏在实例就绪后调用 `UModAPIHostSubsystem::RegisterWorldProvider` 并持有返回的注册凭据，再通过 `SetWorldCallerPolicy` 指定允许访问的模组、类型和权限。生成的 SDK 在 `<SDK>API.h` 中提供 `Acquire_<对象引用类型>`；原生 Mod 在活跃的 `UModInstance` 中调用它即可建立函数表连接并取得对象引用，随后调用同一头文件中生成的类型化方法。宿主会在实际的蓝图实例上执行 `ProcessEvent`。模组卸载、世界关闭或宿主更换授权策略时，相应的调用者会被撤销。

例如，样例工程的 `game.blueprint_test` 会在 `UMFDevSDK` 中生成以下调用（需在 `UModInstance` 子类的方法中使用，且前提是宿主已注册该 Actor 并授权当前模组；取得引用后，可在当前世界存续期间重复调用）：

```cpp
FModObject_T_f4f57c0936062c84 Provider;
if (UUMFDevSDKLibrary::Acquire_FModObject_T_f4f57c0936062c84(*this, Provider))
{
	EUMFDevSDKCallOutcome Outcome;
	int64 ErrorCode;
	UUMFDevSDKLibrary::TestModAPIExport(Provider, 1.0, Outcome, ErrorCode);
}
```

### 从宿主蓝图导出 Hook 和 Event

先在 `blueprintExports` 中配置资产。在 **蓝图Mod导出** 面板中声明 Hook 或 Event 后，在业务图中连接对应 **Hook Point** 或 **Publish Event** 节点的执行输入；未连线的声明将跳过导出。Hook 的原版逻辑图接收一个载荷并返回同类型结果，节点同时输出结果、状态码、取消标志和已提交标志。Event 支持 `Immediate` 和 `NextTick`，并支持可选的立即退订。载荷类型支持受支持的标量、反射值结构体或具有已生成 World 视图的 UObject。

C++ Hook/Event 通过导出宏关联具备 World 类型身份的成员签名。MBT 构建目标时，无参 `MOD_EXPORT_HOOK()` 会包装已有的 `.cpp` 定义。C++ Event 既可以直接调用生成的发布函数，也可以使用 `HostEndpoint` 自动生成发布函数体。详见[生成契约与载荷限制](../../Tools/ModHeaderTool/README.zh-CN.md#world-扩展产物)。

### 自动 Hook 构建

在执行 UBT 编译动作的主机上运行 MBT，并确保已安装目标平台的 UE SDK。每次调用构建一个架构。Apple 目标的链接校验还需要 `python3` 和 Xcode 的 `nm`。以下示例从仓库根目录构建 Linux Development Editor。

```sh
mbt=UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool
"$mbt" build-ue-target --engine /opt/UE_5.6 --project UnrealModFramework/UnrealModFrameworkDev.uproject \
  --target UnrealModFrameworkDevEditor --platform Linux --configuration Development --architecture x64 --mode normal
```

独立 Hook 用例覆盖下列 UBT 平台和架构标识。Win64 的 `arm64ec` 用例还需提供匹配的 Windows SDK 运行库。[Hook 目标用例](../../../../../Scripts/test_hook_targets.py)

| UBT 平台 | 架构 |
| --- | --- |
| `Win64` | `x64`、`arm64`、`arm64ec` |
| `Linux` | `x64` |
| `LinuxArm64` | `arm64` |
| `Mac`、`Android` | `x64`、`arm64` |
| `IOS`、`VisionOS` | `arm64`、`iossimulator` |
| `TVOS` | `arm64`、`tvossimulator` |

`UMF_MHT_LIBCLANG` 指定构建主机上的 `libclang`，`UMF_MHT_CLANG_RESOURCE_DIR` 指定配套的资源目录。项目也可通过 `hostLibclang` 按构建主机设置库路径。目标 SDK、头文件路径和架构均来自 UBT 的编译数据库。[UE 集成构建与自动化测试范围](../../../../../Docs/DevelopmentWorkflow.zh-CN.md#linux-编译的范围)

MHT 和 MST 通过 Clang 定位标记的成员函数并生成源码副本。MBT 会替换对应 UBT 编译动作及嵌套响应文件中的源码输入，同时沿用原定的对象文件和链接输出。生成的注册代码引用源码副本中定义的版本符号，因此 COFF 和 ELF 链接器会拒绝未经替换的原目标文件。Apple 构建还会使用 Xcode 的 `nm` 检查实际的 Mach-O 产物，即便在动态查找模式下也能发现遗留的 Hook 导入。C++ 与蓝图调用最终进入同一个公开函数。当工具链需要引导编辑器时，会以 `-MhtBootstrap` 调用 UBT，生成的 Development Editor 直接执行原函数。`Nelaric.UnrealModFramework.Host.ChangeHealthDispatch` 测试覆盖了这两种调用入口。

构建规则对 Win64 x64 MSVC 使用独立 MBT 静态库，对其他工具链则通过 UBT 编译生成的宿主单元（直接编译路径的清单格式为 `UMF.UbtHostArtifacts`）。主机专有 SDK、远程 Mac 编译动作、通用二进制打包和第三方原生 Mod 安装各自需要单独的平台接入。

## 独立宿主编译与链接

各导出模块的构建规则调用 `ExportMetadata.ConfigureHost(this, Target)`。Win64 x64 MSVC 独立编译路径需要配套的 External 规则类（例如 `ExportTestHost`），调用 `ExportMetadata.ConfigureHostLibrary(this, Target, "ExportTest")`，由所属模块私有依赖该 External 模块。

构建规则会在工程的 `Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>` 下写入稳定的目标描述。`MbtHostBuild` 从真实 UBT 编译数据库中读取 MSVC 动作并展开响应文件，按哈希锁定展开后的配方与原始数据库：保留模块生产者宏和 ABI 设置，仅替换源码、对象和依赖输出，并追加生成头目录。当前自动适配要求 Win64 x64 MSVC 且关闭 PCH、Unity 和 LTO；缺失配置时直接失败，不猜测工具链。

MHT 在公共头中声明签名和常量，在显式 `.cpp` 编译单元中生成实现。MBT 独立编译这些单元，并在所属工程或插件的 `Intermediate/ModTools/ModBuildTool/<Target>/<Platform>/<Configuration>/<Architecture>` 下发布 `<owner>.lib` 和 `.mbt.json` 回执。另有独立的 `<game>ModClient` 生产者上下文，移除 UE/游戏包含路径与强制定义并保留真实工具链参数，由 MBT 独立编译客户端 ABI 库并写入工程的 `Intermediate/ModTools/ModBuildTool` 对应变体目录。v3 清单为单宿主示例列出了全部五个 Host/Client 单元，并将两类入口绑定到 ExportId。ModSDKTool（MDT）发布 C ABI 公共头，并为结构化错误码目录发布独立的 `<game>.moderrors.h`。清单中的 `hostBuildContexts` 锁定每个真实生产者上下文；输入指纹一致时，MBT 会直接复用已有库。详见 [MHT 连接协议](../../Tools/ModHeaderTool/README.zh-CN.md)。

私有 External 模块使用 UE 5.6 的 `PublicPreBuildLibraries` 预先登记宿主库路径（首次 UHT 调用前库文件可以不存在，无需创建空占位文件）。MBT 构建成功后，适配层调用 `ModSourceTool generate-host-link`，在所属工程或插件的 `Intermediate/ModTools/ModSourceTool/<Target>/<Platform>/<Configuration>/<Architecture>` 生成 `<owner>.modlink.h`。UBT 将该头文件强制包含到所属模块的编译中，使链接器保留 `UMF_ModHost_<owner>` 及生成的注册器。`UModAPIHostSubsystem` 在 GameInstance 初始化时运行生成的注册入口。MHT 为 Player 作用域中导出的 `ULocalPlayerSubsystem` 类型生成提供者标记，运行时据此从指定玩家取得已有实例。SDK 视图方法、构造函数和 TypeId 辅助函数也由 MBT 编译。

原生产物发布后，适配层会刷新宿主库、回执和生成文件的 UE `FileItem` 缓存。由于 Internal UHT 与 UBT 共享该缓存，刷新后当前构建才能识别新库并立即重新链接。

Target 规则调用一次 `ExportMetadata.ConfigureTarget(this)` 即可安装 UE 5.6 构建钩子。构建脚本存在时，UBT 在 Windows 上执行 `Tools/Build.bat --target ModRuntimeUhtTools`，在 Linux 和 macOS 上执行 `sh Tools/Build.sh --target ModRuntimeUhtTools`。脚本复用 `Tools/build/.cmake` 执行增量 Release 构建，工具构建失败会中止 UE 构建。Windows 上由本地 MBT 驱动 UE 构建时，预构建步骤会复用该 MBT 并构建 MHT 和 MST。Linux 和 macOS 使用 GNU `g++`，macOS 通过 `CXX` 指定已安装的 GNU C++ 编译器。已备齐工具的 Unix 暂存工程在缺少 CMake 时可跳过工具重建。

已随包提供预编译原生工具及其依赖的宿主可以调用 `ExportMetadata.ConfigureTarget(this, buildNativeTools: false)`，跳过工具编译并保留 MHT 输入跟踪与链接校验。

后续预构建步骤调用 MBT 检查受监控目录和此前缺失的可选输入，发生变更时更新 UHT 依赖标记。Win64 x64 MSVC 独立编译路径的 PostBuildSteps 核对 UBT 目标回执、模块归属及最终 DLL 或 EXE 中对应归档的 PE 见证符号，并在宿主库旁发布 `<owner>.lib.link.json` 和 `<owner>.lib.linked.json`。独立客户端库有自己的构建回执。

## 构建语义与范围

原生诊断会转发至 UHT 日志。非零退出码、超时或无效产物都会导致 UHT 失败。子进程由 UE `ManagedProcessGroup` 管理。采用 UBT 宿主编译的目标通过 `CompileOutput` 将 `*.umfhost.cpp` 编译单元登记到所属模块。UBT 编译宿主单元，MHT 将独立客户端源码作为单独产物输出。

UHT 的 NoOutput、reference、引导模式及 `-Mode=GenerateClangDatabase` 调用会跳过原生生成；正常构建在准备完成后运行 MHT。由于 MHT 尚无等价的 dry-run 协议，`-FailIfGeneratedCodeChanges` 下拒绝完整生成；`scan` 模式的最终报告仍由 UHT 执行常规的变更检查。

工具、配置、选项快照及其配置输入、反射头、编译输入、生成清单输入、编译配方、库文件、回执及其 `compilerDependencies` 均通过 `AddExternalDependency` 登记。对应文件或依赖标记变化时，UHT 会重新生成该变体。新检出的工程先运行 Setup 准备 LLVM 和工具，再通过 MBT 构建使用自动 C++ Hook 的目标。独立编译路径的链接核对覆盖标准 Win64 x64 工程 DLL 和 monolithic EXE。Live Coding 与 Hot Reload 需要单独接入。

UBT 会自动生成包含 `EngineDir` 的邻接 `.ubtplugin.csproj.props` 文件（已加入 Git 忽略）。手工编译适配程序集可运行：

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
