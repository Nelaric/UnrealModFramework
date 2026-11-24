<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModSDKTool

ModSDKTool（MDT）将已解析的导出结果构建为可独立安装的制作 SDK，并将玩家上下文和对象视图连接到按签名生成的方法，支持带类型的标量和公开 UE 对象入参。API ID、公开类型、名称和原生符号均来自生产者契约。

MHT 负责 C++ 分析与投影，ExportCodeGenerator 负责蓝图导出。MDT 消费完整导出结果，调用 MBT 编译生成的库，生成 UE 适配层，通过 AutomationTool 构建插件，最终发布附带完整文件哈希的 SDK 包。MHT 的 scan 报告不是完整的契约，不能代替完整生成清单。

## 构建与命令

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

```text
ModSDKTool plan --request SdkBuildRequest.json --output Plan.json
ModSDKTool build --request SdkBuildRequest.json --output Result.json
ModSDKTool inspect --bundle <发布目录>
```

请求文件中的路径均相对于请求文件本身解析。`plan` 会在本次任务独占的临时目录中调用 MBT 校验输入，不执行编译或发布；`build` 则使用实际的 MBT 配方与 UE `BuildPlugin`。工具路径必须指向原生可执行文件，不接受 Shell 命令或批处理文件。子进程诊断日志和 JSON 进度写入 stderr，最终 JSON 结果输出到 stdout 或 `--output` 指定的文件。每个子进程均设有超时限制并支持进程树取消；结果文件不得位于已发布的 SDK 目录内。

已发布的 SDK 目录不可变。当输入完全相同时，核对全部文件无误后直接复用已有发布包并返回 `UpToDate`；若输入发生变化，则必须选择新的版本化输出目录。发布过程会先在同级独占暂存目录中准备并校验产物，再将其重命名到原先不存在的目标目录，因此失败不会覆盖已有的 SDK。

使用 `ModSDKTool build-editor` 为已安装的 Editor 构建独立制作插件。

```text
ModSDKTool build-editor --package <尚不存在的目录>
                       --dotnet <.NET 宿主> --automation-tool <AutomationTool.dll>
                       --editor-modules <UnrealEditor.modules>
                       --architecture <x64|arm64> --mbt <ModBuildTool>
```

CMake 会将 `Resources/DevelopmentEditor/Files.cmake` 中列出的文件嵌入 MDT。`build-editor` 与 `build` 均会在临时工作目录中写出这些文件、插件描述符和打包配置，随后由 MDT 调用 UAT 编译，并在校验 Editor 的 BuildId 与二进制文件后，将插件连同原生工具一并发布。参见 [CircleCI 调用方式](../../../../../.circleci/build-editor-plugin.sh)。

打开 `ExampleMods/TestMod` 之前，请先将生成的 `ModDevelopmentSDKEditor` 与目标游戏的 `UMFDevSDK` 安装到 `ExampleMods/TestMod/Plugins`。示例游戏的完整 SDK 发布包同时包含这两个插件。

## 生产者契约

`UMF.SdkContract` v2 由 [sdk-contract.schema.json](Protocol/sdk-contract.schema.json) 定义；MDT 同时也兼容使用原始 ABI 字段名的 v1 契约。契约状态必须为 `Complete` 且未解析类型数为零，其契约、投影与原生 ABI 的 SHA-256 标识必须与所有库批次保持一致。包含 Hook 或 Event 声明且采用生命周期投影 v2 的契约允许使用空 API 列表。契约文件本身须作为经过哈希校验的 `Contract` 产物记录在生成清单中，公开头文件则须为 MBT 返回的 `SdkHeader` 产物。宿主实现、注册与校验代码以及私有头文件绝不会进入公开的作者 SDK。

支持的原生客户端 ABI 为 `ContextHandlesV1`，实际函数名取自 `api.native.symbol`：

```cpp
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

状态码零表示成功，非零表示宿主错误。上下文、作用域和对象均为不透明、进程内有效的非零令牌。公开类型 `ObjectReference` 包含稳定的 `id` 和 `cppName`。`ObjectGetter` 指定 `provider: LocalPlayerSubsystem`、`resultTypeId` 和 `nullResult: Error`；`FloatMethod` 指定 `targetTypeId`、`parameterName` 和稳定的 `parameterId`；`TypedMethod` 描述参数和返回值，目标可为玩家提供者或对象视图。`ObjectInput` 参数指定公开的 UE 类、头文件与模块，对象结果则指定投影视图的 TypeId。`blueprint` 字段决定除原生 UE 声明外是否同时生成蓝图节点。

获取接口使用 Player 作用域，方法支持 Player 或 World 作用域；所有 API 均要求 `thread: GameThread` 和 `failureRule: ReturnError`，权限以逐 API 列表形式声明。每项 API 还需提供 `id`、稳定的公开 `name`、`displayName`、`signatureHash`、`projectionHash` 和 `native: {header, symbol}`。客户端配置中的必需符号记录须与实际入口匹配。头文件路径相对于生成批次根目录保留；遇到不支持的接口形态时会直接报错，而不是生成占位实现。

生成的 Runtime SDK 包含不透明 `USTRUCT` 引用与 `MinimalAPI` 蓝图函数库。标准类型化 UE 节点通过 `ExpandEnumAsExecs` 暴露成功/失败执行分支，无需额外创建空 Editor 模块。函数指针 `static_assert` 会在 UE 编译阶段核对 ABI 兼容性。令牌不开放蓝图编辑，宿主通过 `FromNative` 提供上下文。适配层错误以负数表示，原生状态码则保留为非负的蓝图 `int64` 错误码。

World 作用域的宿主蓝图 API 在契约中记录 `hostBlueprint.callingTableHash`。生成的 C++ SDK 会在首次调用前通过活动的 `UModInstance` 连接原生函数表，并按该哈希获取宿主注册的提供者实例。宿主负责提供授权策略和世界内的实时提供者。

MHT 会将全局生命周期声明导出为 `Contract/ModLifecycle.json`，并在 SDK 契约的 `modLifecycle` 字段中记录其摘要。待 MBT 校验该产物并完成库编译后，MDT 会生成 `ModInstance.h` 和 `ModInstanceSubSystem.h`，随后调用 UAT 编译作者 SDK 与制作编辑器插件。生成的反射模块保留宿主的 `/Script/ModLoader` 类型路径，由游戏会话负责管理实例并分发生命周期事件。

`UMF.SdkBuildRequest` v2 由 [sdk-build-request.schema.json](Protocol/sdk-build-request.schema.json) 定义。请将 `schemaVersion` 设为 `2`，并省略 `developmentPlugin` 字段。

| 字段 | 含义 |
| --- | --- |
| `project`、`gameId`、`sdkVersion` | 实际工程路径与版本化游戏标识 |
| `pluginName` | 稳定的 SDK 插件与反射模块标识 |
| `contract` | 完整的公开生产者契约 |
| `outputDirectory` | 可选的发布目录，默认位于 `<工程>/Intermediate/ModTools/ModSDKTool/<pluginName>/<sdkVersion>` |
| `tools.mbt` | 支持输入清单版本与二进制校验的 MBT 可执行文件 |
| `tools.dotnet`、`tools.automationTool` | 与 UE 兼容的实际 .NET 宿主程序和 UAT 程序集 |
| `editor` | 当前宿主平台、架构、Development 配置、实际引擎 `.modules` 文件及匹配的 `buildId` |
| `libraries` | Host 与 Client 条目，各项均包含 `context` 和 `generatedManifest` |
| `workDirectory` | 可选的短路径临时根目录（在 Windows 上尤为实用） |
| `timeoutSeconds` | 单个子进程的超时时间，范围 1–86400 秒，默认 1800 秒 |

Host 与 Client 两种库均使用 MBT 的所属模块归档能力。Client 上下文是独立客户端库所属模块的生产者环境，而非游戏私有模块的导入环境。构建请求中至少须有一个 Client Profile 与作者 Editor 匹配，且不同任务不得写入相同的输出库。

插件通过 `BuildPlugin -NoTargetPlatforms` 为作者 Editor 编译产物，MDT 会核对实际的模块二进制文件、目标架构、匹配的 BuildId 以及完整文件清单。生成的 Build.cs 在缺少匹配客户端 Profile 时会拒绝构建。

### World 扩展契约

SDK 的 `extensions` 契约用于定义并校验扩展 ID 与签名，同时驱动生成蓝图订阅、值结构体、对象投影及跨 Mod 服务获取所需的适配代码。生成的生命周期投影暴露 **On Mod Register** 和 **On Mod Bind** 两个事件，且 SDK 随包附带供独立原生回调使用的扩展 ABI 头文件。使用扩展清单 v2 打包时，请将同一份 `Contract/SdkContract.json` 交给打包器。详见[制作插件打包与注册流程](Resources/DevelopmentEditor/README.zh-CN.md)。

## 编辑器入口与交付

启用 `ModDevelopmentSDKEditor`，在 **Tools → Mod SDK** 菜单下选择 **Plan Mod SDK...** 或 **Build Mod SDK...**，再选择请求 JSON 文件。编辑器既可使用随插件打包的 MDT，也可使用 ModRuntime 插件 `Tools/build/ModSDKTool` 中的本地构建，还可以手动选择可执行文件并在编辑器设置中保存路径。日志会异步输出到 Output Log 而不会阻塞编辑器，结果 JSON 写入 `Intermediate/ModTools/ModSDKTool/Results`。**Cancel Mod SDK task** 用于终止整个进程树。

通用开发插件独立于游戏 Runtime 模块。发布包会在插件的 `Binaries/ThirdParty/ModSDKTool/<平台>` 目录中携带 MDT、MBT 与许可证说明。

```text
<SDK>/
  Manifest.json
  Contract/SdkContract.json
  Native/Include/...
  Native/Lib/<平台>/<架构>/<配置>/<Target类型>/...
  Plugins/<游戏SDK>/...
  Plugins/ModDevelopmentSDKEditor/...
  Host/Plugins/<游戏SDK>/...  （提供非 Editor 的 Client 配置时生成）
  Build/Plan.json
  Build/NativeResults.json
```

`Built` 状态表示 MDT 已完成原生库与作者 Editor 插件的编译和校验。最终游戏链接、Game Target 下的 SDK Runtime 构建以及运行时 API 绑定均由宿主侧执行和验证。UE 5.6 Win64 宿主的后置构建步骤可生成 MBT 链接记录。Cook 后的蓝图调用要求游戏中存在匹配的 SDK Runtime 模块。公开的模块名、类型名、函数名和参数名共同构成反射标识，在兼容版本之间必须保持稳定。

可选的 `Host/Plugins/<游戏SDK>` 宿主版本与制作端 SDK 使用相同的运行时模块名和蓝图名称。请为参与构建和 Cook 的所有目标提供对应的 Host 与 Client 库请求，并在构建与 Cook 之前将宿主版本安装并启用到游戏工程中。如果导出 Commandlet 需要加载尚未编译的新模块，请先使用 `-MhtBootstrap` 执行 Development Editor 引导构建。

MHT 会生成匹配的 `.sdkcontract.json`、纯 C `.modabi.h`、独立客户端入口和 v3 清单，详见 [MHT 连接协议](../ModHeaderTool/README.zh-CN.md)。请将其中的 Host 和 `<game>ModClient` 上下文分别作为独立的库任务。原生连接描述明确列出了宿主查询及客户端 Connect/Disconnect 符号；MDT 要求该描述文件为经过哈希校验的公共 Contract 产物，核对其标识与 API 签名后，会按声明的相对路径同时复制到 Bundle 和 SDK 插件中，`inspect` 则负责校验这两份副本及其契约身份。游戏接入层负责本地玩家等其他作用域的上下文交付。超出 MDT 适配形态的 API 图会使 SDK 契约被标记为 `Unsupported`。内部 UE 视图使用的 `.modcontract.json` 仍属于独立契约，不能作为 MDT 的 SDK 契约使用。
