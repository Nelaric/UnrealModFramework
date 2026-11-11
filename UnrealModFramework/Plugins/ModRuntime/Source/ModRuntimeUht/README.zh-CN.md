<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModRuntime UHT 适配层

`ModRuntime.ubtplugin.csproj` 是很薄的 .NET 8 UHT Exporter。UE 5.6 在已启用的 ModRuntime 插件内发现工程，构建程序集并加载 `[UnrealHeaderTool]`、`[UhtExporter]` 入口。工程引用当前引擎的程序集。Game 和 Editor 的 manifest 都通过反射模块 `ModLoader` 启用 `ModRuntime` Exporter。

C# 启动独立的 MHT、MBT 进程，传递真实编译配方、登记依赖与传播失败。C++ 分析、参数校验、投影和源码生成由 MHT 负责；独立编译与归档校验由 MBT 负责；蓝图导出仍属于 ExportCodeGenerator。最终游戏/模块链接由 UBT 执行。发现机制参见 Epic 的 [UE 5.6 UHT 扩展文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6)。

## 工具发现与默认调用

调用了 `ExportMetadata.ConfigureTarget(this)` 的 Win64 x64、Linux 和 macOS Target 会在正常构建中先编译 ModHeaderTool、ModBuildTool 和 ModSourceTool，再启动 UHT。直接调用 UHT 时，先运行原生工具构建脚本。UHT 按以下顺序查找 MHT。

1. 环境变量 `UMF_MHT_EXECUTABLE`。
2. 项目可选配置 `Config/ModHeaderTool.json` 中的 `executable`。
3. 插件 `Binaries/ThirdParty/ModHeaderTool/<宿主平台>/MHT[.exe]` 或 `ModHeaderTool[.exe]`，平台为 Win64、Linux、Mac。
4. ModRuntime 插件的 `Tools/build/ModHeaderTool/ModHeaderTool[.exe]` 本地构建位置。

未配置时默认模式是 `scan`。开发工程的 `Config/ModHeaderTool.json` 选择 `generate`。scan 模式下，UHT 执行 `ModHeaderTool scan <实际.uproject> <元数据快照.json>` 校验标记和参数。UHT 从标准输出读取 JSON 报告，并将成功报告提交到工程的 `Intermediate/ModTools/ModHeaderTool/Scan/<Target>/<Platform>/<Configuration>/Scan.json`。

元数据选项默认使用 `Intermediate/ModTools/ModHeaderTool/MetadataOptions.json`。生成前，适配层通过 MHT 检查快照。快照缺失、过期或版本较旧时，适配层调用当前引擎的 `ModExportOptions` commandlet，校验通过后发布快照。修改 `DefaultEngine.ini` 等已登记的配置会触发下一次增量构建的检查。scan 模式刷新已有的默认快照；源码无需快照时，也可以直接扫描。通过 `options` 指定其他路径的快照由调用方维护。

在 Win64 上，`metadataEditorTarget` 指定元数据 commandlet 使用的 Development Editor Target。缺少所需的项目二进制时，适配层会调用 UBT 构建引导版本。未设置此项的项目可用 `-MhtBootstrap` 准备 Editor。

## 完整生成模式

设置 `mode` 为 `generate` 并在 `generation` 中列出导出模块。以下示例使用开发宿主的模块和类型身份，工具和 LLVM 路径应指向本机安装位置。

```json
{
  "schemaVersion": 1,
  "executable": "Plugins/ModRuntime/Binaries/ThirdParty/ModHeaderTool/Win64/MHT.exe",
  "mbt": "Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe",
  "mode": "generate",
  "timeoutSeconds": 600,
  "options": "Intermediate/ModTools/ModHeaderTool/MetadataOptions.json",
  "metadataEditorTarget": "UnrealModFrameworkDevEditor",
  "libclang": "<LLVM>/bin/libclang.dll",
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

适配层配置中的路径相对项目目录。build input 内的路径继续遵循 MHT 的规则。`timeoutSeconds` 支持 1–86400，默认 600。自动准备需要指定 `libclang` 并在同一 LLVM 安装中提供资源头。`options` 可省略。`mode` 支持 `scan` 和 `generate`，默认为 `scan`。

MBT 依次查找环境变量 `UMF_MBT_EXECUTABLE`、根配置项 `mbt`，最后使用ModRuntime 插件的 `Tools/build/ModBuildTool/ModBuildTool[.exe]`。
ModSourceTool（MST）依次查找环境变量 `UMF_MOD_SOURCE_TOOL_EXECUTABLE`、根配置项 `sourceTool`，最后使用 ModRuntime 插件的 `Tools/build/ModSourceTool/ModSourceTool[.exe]`。

也可通过 `UMF_MHT_BUILD_INPUT` 指定本次 build input，它会选择 generate 模式并覆盖 Target 配置；`UMF_MHT_LIBCLANG` 覆盖 libclang。环境变量变化需要让 UHT 再次执行，例如在 UBT 命令中使用 `-ForceHeaderGeneration`。

generate 按实际 Target、平台、配置和架构选择 build input，校验它与当前工程和 UHT 会话一致。适配层在变体的 `HostTargets` 目录写入 `GenerationRequest.json`，其中包含构建输入、生成目录、生产者上下文集，以及已解析的元数据快照、libclang 和生命周期源码路径。UHT 执行 `ModHeaderTool generate <GenerationRequest.json>`，检查完整清单后逐个调用 `ModBuildTool build-host`。工程的生成根目录是 `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>`。每次覆盖当前变体的 `SDK`、`Host`、`Client`、`Contract`、`Diagnostics` 和 `<gameId>.generated-artifacts.json`。相同内容保留时间戳，完整清单最后提交。导出插件所属模块的生成文件也按相对路径写入插件的 `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>`。共享契约、客户端 ABI 和完整清单归工程。

正常构建时，适配层使用当前 Target 和 MSVC 工具链运行 UBT 的 `GenerateClangDatabase`，并在正式编译动作生成前展开导出模块的响应文件。变体的 `HostTargets` 目录保存 `BuildInput.json` 和 `Ubt/compile_commands.json`。UBT 复用 Makefile 时，适配层读取已保存的编译参数。Target 或变体显式配置的 `buildInput` 由调用方维护，其编译数据库及响应文件须匹配当前 Target。Clang 还依赖标准 UHT CodeGen 生成的头文件。

## 独立宿主编译与链接

所属模块的规则调用 `ExportMetadata.ConfigureHost(this, Target)`，配套 External 规则类（例如 `ExportTestHost`）调用 `ExportMetadata.ConfigureHostLibrary(this, Target, "ExportTest")`；所属模块私有依赖这个 External 模块。接入按模块配置，单个 API 的开发者继续只写导出宏和原函数。

规则在工程的 `Intermediate/ModTools/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>` 写入稳定目标描述。`MbtHostBuild` 从真实 UBT 编译数据库中读取 MSVC 动作并展开响应文件，按哈希固定展开后的配方和原始数据库，保留模块生产者宏和 ABI 设置，仅替换源码、对象、依赖输出，并追加生成头目录。当前自动适配要求 Win64 x64 MSVC、关闭 PCH、Unity 和 LTO；缺失配置直接失败，不猜测工具链。

MHT 在公共头中声明签名和常量，在显式 `.cpp` 编译单元中生成实现。MBT 独立编译这些单元，在所属工程或插件的 `Intermediate/ModTools/ModBuildTool/<Target>/<Platform>/<Configuration>/<Architecture>` 发布 `<owner>.lib` 和 `.mbt.json` 回执。另有独立 `<game>ModClient` 生产者上下文，移除 UE/游戏包含与强制定义，保留真实工具链参数，由 MBT 独立编译客户端 ABI 库并写入工程的 `Intermediate/ModTools/ModBuildTool` 对应构建变体目录。v3 清单为单宿主示例列出全部五个 Host/Client 单元，并将两类入口绑定到 ExportId。ModSDKTool（MDT）发布 C ABI 公共头，并为结构化错误码目录发布独立的 `<game>.moderrors.h`。`hostBuildContexts` 锁定每个真实生产者上下文，输入指纹一致时 MBT 复用库。[MHT 连接协议](../../Tools/ModHeaderTool/README.zh-CN.md)

私有 External 模块使用 UE 5.6 的 `PublicPreBuildLibraries` 预先登记库路径。MBT 构建成功后，适配器调用 `ModSourceTool generate-host-link`，在所属工程或插件的 `Intermediate/ModTools/ModSourceTool/<Target>/<Platform>/<Configuration>/<Architecture>` 生成 `<owner>.modlink.h`。UBT 将该头强制包含到所属模块的编译中，链接器由此保留 `UMF_ModHost_<owner>` 及生成的注册器。`UModAPIHostSubsystem` 在 GameInstance 初始化时运行生成的注册入口。MHT 为 Player 作用域中导出的 `ULocalPlayerSubsystem` 类型生成提供者标记，运行时据此从指定玩家取得已有实例。SDK 视图方法、构造和 TypeId 辅助函数也由 MBT 编译。

原生产物发布后，适配器刷新库、回执和生成文件的 UE `FileItem` 缓存。Internal UHT 与 UBT 共享这份缓存；刷新后当前构建才能识别新库并立即重新链接。

Target 规则调用一次 `ExportMetadata.ConfigureTarget(this)`。构建脚本存在时，UBT 在 Windows 执行 `Tools/Build.bat --target ModRuntimeUhtTools`，在 Linux 和 macOS 执行 `sh Tools/Build.sh --target ModRuntimeUhtTools`。脚本复用 `Tools/build/.cmake` 增量构建 Release 工具。脚本失败时，UBT 在启动 UHT 前结束构建。Linux 和 macOS 使用 GNU `g++`，macOS 通过 `CXX` 指定已安装的 GNU C++ 编译器。精简的 Unix 工程副本在缺少工作区 CMake 查找器和 CMake 可执行文件时，仍可继续构建 UE 目标。

Win64 x64 的后续预构建步骤会调用 MBT 检查目录清单和此前缺失的可选输入，有变化时更新 UHT 依赖标记。PostBuildSteps 调用 MBT 做链接核对。MBT 核对 UBT 目标回执、模块归属和最终 DLL 或 EXE 中对应归档的 PE 见证符号，在宿主库旁发布 `<owner>.lib.link.json` 和 `<owner>.lib.linked.json`。独立客户端库有自己的构建回执。

## 构建语义与范围

原生诊断进入 UHT 日志；非零退出码、超时或错误产物使 UHT 失败。子进程生命周期使用 UE ManagedProcessGroup 管理。不启用 `CompileOutput`，不声明 C++ 输出过滤器，MHT 生成源仍不进入 UBT 编译列表。

UHT 的 NoOutput、reference、引导及 `-Mode=GenerateClangDatabase` 调用会跳过原生生成。正常构建在准备完成后运行 MHT。`-FailIfGeneratedCodeChanges` 下拒绝完整生成，因为 MHT 尚无等价 dry-run 协议；scan 的最终报告仍由 UHT 实施变更检查。

已有工具、配置、选项快照及其配置输入、反射头、编译输入、native manifest 输入、编译配方、库、回执及其 `compilerDependencies` 通过 `AddExternalDependency` 登记。已登记的文件或目录依赖标记发生变化时，UHT 会重新生成对应变体。MSVC、Windows SDK 和 LLVM 就绪后，正常 Win64 构建可为干净检出的开发工程准备输入。自动链接核对覆盖标准 Win64 x64 工程 DLL 和 monolithic EXE。Live Coding 与 Hot Reload 需要单独接入。

UBT 自动生成带有 EngineDir 的邻接 `.ubtplugin.csproj.props`，已加入 Git 忽略。手工编译适配程序集可运行：

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
