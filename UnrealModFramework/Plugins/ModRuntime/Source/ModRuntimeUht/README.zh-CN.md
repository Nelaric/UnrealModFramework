<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModRuntime UHT 适配层

`ModRuntime.ubtplugin.csproj` 是很薄的 .NET 8 UHT Exporter。UE 5.6 在已启用的 ModRuntime 插件内发现工程，构建程序集并加载 `[UnrealHeaderTool]`、`[UhtExporter]` 入口。工程引用当前引擎的程序集，不使用 ProjectReference，也不修改引擎。Exporter 名为 `ModRuntime`，绑定 Runtime 模块 `ExportMetadata`，因此 Game 和 Editor manifest 都可以包含入口。

C# 启动独立的 MHT、MBT 进程，传递真实编译配方、登记依赖与传播失败。C++ 分析、参数校验、投影和源码生成由 MHT 负责；独立编译与归档校验由 MBT 负责；蓝图导出仍属于 ExportCodeGenerator。最终游戏/模块链接由 UBT 执行。发现机制参见 Epic 的 [UE 5.6 UHT 扩展文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-header-tool-for-unreal-engine?application_version=5.6)。

## 工具发现与默认调用

先构建原生工具，查找优先级为：

1. 环境变量 `UMF_MHT_EXECUTABLE`。
2. 项目可选配置 `Config/ModHeaderTool.json` 中的 `executable`。
3. 插件 `Binaries/ThirdParty/ModHeaderTool/<宿主平台>/MHT[.exe]` 或 `ModHeaderTool[.exe]`，平台为 Win64、Linux、Mac。
4. 插件 `Tools/build/Release/bin/Release/ModHeaderTool[.exe]` 本地构建位置。

未配置时默认模式是 `scan`；开发工程的 `Config/ModHeaderTool.json` 已选择 `generate`。scan 模式每次正常 UHT 执行时启动 MHT 参数校验，成功报告由 UHT output factory 提交到 `Inc/ExportMetadata/MHT/Scan.json`，与 `Inc/ExportMetadata/UHT` 同级。scan 仍只是标记与参数校验，不代表已经生成宿主连接或完成 Clang 类型分析。

元数据选项默认使用 `Saved/ModHeaderTool/MetadataOptions.json`，存在时通过 `--options` 传入；配置项 `options` 可覆盖。通过已有 `ModExportOptions` Commandlet 导出权威快照。缺少快照但源码需要它时，由 MHT 报错。首次需要构建配置导出器时使用 `-MhtBootstrap` 构建 Development Editor；适配器跳过这个显式引导过程。

## 完整生成模式

项目配置示例；工具与 LLVM 路径需要替换为实际路径：

```json
{
  "schemaVersion": 1,
  "executable": "Plugins/ModRuntime/Binaries/ThirdParty/ModHeaderTool/Win64/MHT.exe",
  "mbt": "Plugins/ModRuntime/Tools/build/Release/bin/Release/ModBuildTool.exe",
  "mode": "generate",
  "timeoutSeconds": 600,
  "options": "Saved/ModHeaderTool/MetadataOptions.json",
  "targets": {
    "UnrealModFrameworkDevEditor": {
      "variants": {
        "Win64/Development/x64": {
          "buildInput": "Intermediate/ModHeaderTool/BuildInputs/Editor.json"
        }
      },
      "libclang": "<LLVM>/bin/libclang.dll"
    }
  }
}
```

适配层配置中的路径相对项目目录；build input 内的路径继续遵循 MHT 的规则。`timeoutSeconds` 支持 1–86400，默认 600。`options`、`libclang` 可省略。`mode` 只接受 `scan` 或 `generate`，默认 scan。

MBT 依次查找环境变量 `UMF_MBT_EXECUTABLE`、根配置项 `mbt`，最后使用插件 `Tools/build/Release/bin/Release/ModBuildTool[.exe]`。

也可通过 `UMF_MHT_BUILD_INPUT` 指定本次 build input，它会选择 generate 模式并覆盖 Target 配置；`UMF_MHT_LIBCLANG` 覆盖 libclang。环境变量变化需要让 UHT 再次执行，例如在 UBT 命令中使用 `-ForceHeaderGeneration`。

generate 按实际 Target、平台、配置和架构选择 build input，校验它与当前工程和 UHT 会话一致。适配层将所有生产者上下文通过 `--host-contexts` 交给 MHT，检查完整清单后逐个调用 `ModBuildTool build-host`。生成根目录是平台对应的 `Inc/ExportMetadata/MHT/<Target>/<Configuration>/<Architecture>`。每次覆盖当前变体的 `SDK`、`Host`、`Client`、`Contract`、`Diagnostics` 和 `<gameId>.generated-artifacts.json`。相同内容保留时间戳，完整清单最后提交。

必须先准备真实 UBT 编译数据库及对应的 MHT build input，选用正确的平台、配置和架构，避免失效响应文件。UHT manifest 没有完整编译上下文，适配器不从中猜测。Exporter API 也没有公开的跨 Exporter 就绪屏障；依赖 Clang 的完整生成要求标准 CodeGen 已提交所需头。本机 UE 5.6 的执行顺序已完成实际验证，其他引擎版本不能据此推定。

## 独立宿主编译与链接

所属模块的规则调用 `ExportMetadata.ConfigureHost(this, Target)`，配套 External 规则类（例如 `ExportTestHost`）调用 `ExportMetadata.ConfigureHostLibrary(this, Target, "ExportTest")`；所属模块私有依赖这个 External 模块。接入按模块配置，单个 API 的开发者继续只写导出宏和原函数。

规则在 `Intermediate/ModHeaderTool/HostTargets/<Target>/<Platform>/<Configuration>/<Architecture>` 写入稳定目标描述。`MbtHostBuild` 从真实 UBT 编译数据库中读取 MSVC 动作并展开响应文件，按哈希固定展开后的配方和原始数据库，保留模块生产者宏和 ABI 设置，仅替换源码、对象、依赖输出，并追加生成头目录。当前自动适配要求 Win64 x64 MSVC、关闭 PCH、Unity 和 LTO；缺失配置直接失败，不猜测工具链。

MHT 的公共头只包含声明和签名数据，全部实现在显式 `.cpp` 编译单元中。MBT 独立编译这些单元，发布 `Intermediate/MBT/<Target>/<Platform>/<Configuration>/<Architecture>/<owner>.lib` 和 `.mbt.json` 回执。另有独立 `<game>ModClient` 生产者上下文，移除 UE/游戏包含与强制定义，保留真实工具链参数，由 MBT 独立编译客户端 ABI 库。v2 清单为单宿主示例列出全部五个 Host/Client 单元，并将两类入口绑定到 ExportId；MST 只收到纯 C ABI 公共头，见 [MHT 连接协议](../../Tools/ModHeaderTool/README.zh-CN.md)。`hostBuildContexts` 锁定每个真实生产者上下文；输入指纹不变时 MBT 复用库。

私有 External 模块使用 UE 5.6 的 `PublicPreBuildLibraries` 登记首次 UHT 前尚不存在的库，不创建空占位库。UHT 内 MBT 必须成功，UBT 才会继续最终链接；该库不会通过所属 DLL 的私有依赖传播。游戏源码仅调用声明的 `UMF_ModHost_<owner>` 入口，不包含生成 `.cpp` 或 `.inl`。SDK 视图方法、构造和 TypeId 辅助函数也全部由 MBT 编译。

原生产物发布后，适配器刷新库、回执和生成文件的 UE `FileItem` 缓存。Internal UHT 与 UBT 共享这份缓存；刷新后当前构建才能识别新库并立即重新链接。

Target 规则调用一次 `ExportMetadata.ConfigureTarget(this)`。UE 5.6 的 PreBuildSteps 检查目录清单和此前缺失的可选输入，有变化时更新 UHT 依赖标记。PostBuildSteps 调用 MBT 做链接核对。MBT 核对 UBT 目标回执、模块归属和最终 DLL 或 EXE 中对应归档的 PE 见证符号，在宿主库旁发布 `<owner>.lib.link.json` 和 `<owner>.lib.linked.json`。独立客户端库有自己的构建回执。

## 构建语义与范围

原生诊断进入 UHT 日志；非零退出码、超时或错误产物使 UHT 失败。子进程生命周期使用 UE ManagedProcessGroup 管理。不启用 `CompileOutput`，不声明 C++ 输出过滤器，MHT 生成源仍不进入 UBT 编译列表。

UHT 的 NoOutput、reference 及 `-Mode=GenerateClangDatabase` 准备模式跳过原生副作用。`-FailIfGeneratedCodeChanges` 下拒绝完整生成，因为 MHT 尚无等价 dry-run 协议；scan 的最终报告仍由 UHT 实施变更检查。

已有工具、配置、选项快照、反射头、编译输入、native manifest 输入、编译配方、库、回执及其 `compilerDependencies` 通过 `AddExternalDependency` 登记。已登记的文件或目录依赖标记发生变化时，UHT 会重新生成对应变体。干净检出需要先完成文档所述引导、导出选项快照，并准备对应变体的真实编译数据库。自动链接核对覆盖标准 Win64 x64 工程 DLL 和 monolithic EXE。Live Coding 与 Hot Reload 需要单独接入。

UBT 自动生成带有 EngineDir 的邻接 `.ubtplugin.csproj.props`，已加入 Git 忽略。手工编译适配程序集可运行：

```text
dotnet build ModRuntime.ubtplugin.csproj -c Development -p:EngineDir=<UE>/Engine
```
