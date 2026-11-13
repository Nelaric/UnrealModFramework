<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModSDKTool

ModSDKTool（MDT）将已经解析的导出结果构建为可独立安装的制作 SDK。玩家上下文和对象视图可以调用按签名生成的方法，方法接受标量和公开 UE 对象入参。API ID、公开类型、名称与原生符号来自契约。

MHT 负责 C++ 分析和投影，ExportCodeGenerator 负责蓝图导出。MDT 消费完整结果，调用 MBT 构建库，生成 UE 适配层，通过 AutomationTool 构建插件，最后发布带完整文件哈希的 SDK。MHT 的 scan 报告不能代替完整生成清单。

## 构建与命令

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

```text
ModSDKTool plan --request SdkBuildRequest.json --output Plan.json
ModSDKTool build --request SdkBuildRequest.json --output Result.json
ModSDKTool inspect --bundle <发布目录>
```

请求路径相对于请求文件。`plan` 在本次拥有的临时目录中调用 MBT 校验，不编译或发布。`build` 使用实际 MBT 配方及 UE `BuildPlugin`。工具路径必须是原生可执行文件，不接受 shell 或批处理。子进程日志和 JSON 进度写入 stderr，最终 JSON 写到 stdout 或 `--output`。每个子进程有超时及进程树取消处理，结果文件不能放入 SDK 目录。

已发布目录不可变。相同输入核对全部文件后返回 `UpToDate`；输入变化须选择新的版本化输出目录。发布先在同级独占目录中准备并验证，再重命名到原先不存在的目标。失败不会替换已有 SDK。

使用 `ModSDKTool build-editor` 为已安装的 Editor 构建独立制作插件。

```text
ModSDKTool build-editor --package <尚不存在的目录>
                       --dotnet <.NET 宿主> --automation-tool <AutomationTool.dll>
                       --editor-modules <UnrealEditor.modules>
                       --architecture <x64|arm64> --mbt <ModBuildTool>
```

CMake 将 `Resources/DevelopmentEditor/Files.cmake` 列出的文件嵌入 MDT。`build-editor` 和 `build` 都在临时目录写出这些文件、插件描述符和打包配置。MDT 调用 UAT 编译，校验 Editor BuildId 和二进制后，将插件与原生工具一并发布。 [CircleCI 调用方式](../../../../../.circleci/build-editor-plugin.sh)

打开 `ExampleMods/TestMod` 前，将生成的 `ModDevelopmentSDKEditor` 和游戏的 `UMFDevSDK` 安装到 `ExampleMods/TestMod/Plugins`。示例游戏的完整 SDK 包包含这两个插件。

## 生产者契约

[sdk-contract.schema.json](Protocol/sdk-contract.schema.json) 定义 `UMF.SdkContract` v2。MDT 仍可读取使用旧 ABI 字段名的 v1 契约。契约必须为 `Complete`、未解析类型数为零，契约、投影和原生 ABI 的 SHA-256 与库批次一致。契约文件本身须为经过哈希校验的 `Contract` 产物，公开头须为 MBT 返回的 `SdkHeader`。宿主实现、注册、验证和私有头不进入作者 SDK。

首版 ABI 为 `ContextHandlesV1`，函数名取自 `api.native.symbol`：

```cpp
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

状态零表示成功，非零表示宿主错误。上下文、作用域和对象均为非零进程内令牌。公开类型 `ObjectReference` 包含稳定 `id` 和 `cppName`。`ObjectGetter` 提供 `provider: LocalPlayerSubsystem`、`resultTypeId` 和 `nullResult: Error`。`FloatMethod` 提供 `targetTypeId`、`parameterName` 和稳定 `parameterId`。`TypedMethod` 描述参数和结果，目标可为玩家上下文或对象视图。`ObjectInput` 指定 UE 类、头文件和模块。对象结果指定投影视图 TypeId。`blueprint` 字段决定是否生成蓝图节点。

获取接口使用 Player Scope，实例方法支持 Player 或 World；均要求 `thread: GameThread` 和 `failureRule: ReturnError`；权限保留为逐 API 列表。每项还提供 `id`、稳定公开 `name`、`displayName`、`signatureHash`、`projectionHash` 和 `native: {header, symbol}`。客户端清单的必需符号记录须匹配实际入口。头路径相对生成根保留，不支持的形状直接报错。

生成的 Runtime SDK 包含不透明 `USTRUCT` 引用与 `MinimalAPI` 蓝图函数库。标准 UE 节点通过 `ExpandEnumAsExecs` 展示成功/失败分支，无需额外空 Editor 模块。函数指针 `static_assert` 在 UE 编译时核对 ABI。令牌不开放蓝图编辑，宿主用 `FromNative` 提供上下文。适配层错误为负数，原生状态以非负蓝图 `int64` 错误码保留。

宿主蓝图的 World API 在契约中记录 `hostBlueprint.callingTableHash`。生成的 C++ SDK 通过活动的 `UModInstance` 连接原生函数表，并按该哈希获取宿主注册的实例。宿主指定授权策略和世界内的提供者。

MHT 将全局生命周期声明导出为 `Contract/ModLifecycle.json`，并在 SDK 契约的 `modLifecycle` 中绑定摘要。MBT 验证该产物并完成库编译后，MDT 生成 `ModInstance.h` 和 `ModInstanceSubSystem.h`，随后用 UAT 编译作者 SDK 和制作编辑器插件。生成模块保留宿主的 `/Script/ModLoader` 类型路径。游戏会话负责实例化及事件分发。

[sdk-build-request.schema.json](Protocol/sdk-build-request.schema.json) 定义 `UMF.SdkBuildRequest` v2。将 `schemaVersion` 设为 `2`，并移除 `developmentPlugin` 字段。

| 字段 | 含义 |
| --- | --- |
| `project`、`gameId`、`sdkVersion` | 实际工程与版本化游戏身份 |
| `pluginName` | 稳定插件和反射模块身份 |
| `contract` | 完整公开契约 |
| `outputDirectory` | 可选发布目录，默认位于 `<工程>/Intermediate/ModTools/ModSDKTool/<pluginName>/<sdkVersion>` |
| `tools.mbt` | 支持输入清单版本和二进制检查的 MBT |
| `tools.dotnet`、`tools.automationTool` | UE 可用的实际 .NET 程序与 UAT 程序集 |
| `editor` | 当前宿主平台、架构、Development、实际引擎 `.modules` 和匹配 `buildId` |
| `libraries` | Host 与 Client 项，各含 `context` 和 `generatedManifest` |
| `workDirectory` | 可选的短暂存根目录，适合 Windows |
| `timeoutSeconds` | 单个子进程超时，1–86400 秒，默认 1800 |

两种库使用 MBT 的所属模块归档能力。Client 上下文是独立客户端库所属模块的生产者环境，不是游戏私有模块的导入环境。至少一个 Client Profile 匹配作者 Editor，不同任务不能写入相同的库。

插件通过 `BuildPlugin -NoTargetPlatforms` 构建作者 Editor 产物；核对实际二进制、架构、BuildId 和完整文件库存。生成的 Build.cs 在缺少匹配客户端 Profile 时拒绝构建。

## 编辑器入口与交付

启用 `ModDevelopmentSDKEditor`，在 **Tools → Mod SDK** 选择 **Plan Mod SDK...** 或 **Build Mod SDK...**，再选请求 JSON。编辑器可使用随插件打包的 MDT 或ModRuntime 插件 `Tools/build/ModSDKTool` 中的本地构建，也可手动选择并保存可执行文件路径。日志异步进入 Output Log，结果 JSON 写入 `Intermediate/ModTools/ModSDKTool/Results`。**Cancel Mod SDK task** 终止进程树。

通用开发插件不依赖游戏 Runtime 模块。发布包在其 `Binaries/ThirdParty/ModSDKTool/<平台>` 中携带 MDT、MBT 与许可证说明。

```text
<SDK>/
  Manifest.json
  Contract/SdkContract.json
  Native/Include/...
  Native/Lib/<平台>/<架构>/<配置>/<Target类型>/...
  Plugins/<游戏SDK>/...
  Plugins/ModDevelopmentSDKEditor/...
  Build/Plan.json
  Build/NativeResults.json
```

`Built` 表示原生库与作者 Editor 插件实际构建并检查，不代表最终游戏链接、Game Target 的 SDK Runtime 或运行时 API 绑定已验证；结果明确将这些能力记为 false。Cook 后调用要求游戏中有匹配的 SDK Runtime。蓝图导出、可信宿主链接回执和玩家上下文交付仍是上游依赖。公开模块、类型和函数/参数名形成反射身份，兼容发布须保持稳定。

MHT 已生成匹配的 `.sdkcontract.json`、纯 C `.modabi.h`、独立客户端入口和 v3 清单。分别使用 Host 和 `<game>ModClient` 上下文作为库任务。连接描述明确列出宿主查询及客户端 Connect/Disconnect 符号。MDT 要求描述文件是经过哈希检查的公共 Contract 产物，核对身份与 API 签名后按声明路径复制到 Bundle 和 SDK 插件。`inspect` 检查两份文件及契约身份。游戏接入层负责本地玩家等其他作用域的上下文交付。超出 MDT 适配形状的 API 图会使 SDK 契约标记 `Unsupported`。[MHT 连接协议](../ModHeaderTool/README.zh-CN.md)
