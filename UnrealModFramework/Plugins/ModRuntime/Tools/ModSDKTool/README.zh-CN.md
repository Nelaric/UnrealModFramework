<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModSDKTool

ModSDKTool（MDT）将已解析的导出结果构建为可独立安装的制作 SDK，并将玩家上下文和对象视图连接到按签名生成的方法，支持带类型的标量和公开 UE 对象入参。API ID、公开类型、名称和原生符号均来自生产者契约。

MHT 负责 C++ 投影，ExportCodeGenerator 负责蓝图导出。MDT 校验各 UBT 目标的 v4 生成清单，生成共享 Runtime 的宿主与制作端插件，用 UE AutomationTool 构建制作端插件并发布带哈希的文件清单。

## 构建与命令

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

```text
ModSDKTool plan --request SdkBuildRequest.json --output Plan.json
ModSDKTool build --request SdkBuildRequest.json --output Result.json
ModSDKTool inspect --bundle <发布目录>
```

请求路径相对于请求文件解析。`plan` 校验生成清单、宿主和客户端的连接信息与输入哈希。`build` 通过 UAT `BuildPlugin` 编译制作端并发布。诊断写入 stderr，结果可写到发布目录之外。

已发布的 SDK 目录不可变。当输入完全相同时，核对全部文件无误后直接复用已有发布包并返回 `UpToDate`；若输入发生变化，则必须选择新的版本化输出目录。发布过程会先在同级独占暂存目录中准备并校验产物，再将其重命名到原先不存在的目标目录，因此失败不会覆盖已有的 SDK。

使用 `ModSDKTool build-editor` 为已安装的 Editor 构建独立制作插件。

```text
ModSDKTool build-editor --package <尚不存在的目录>
                       --dotnet <.NET 宿主> --automation-tool <AutomationTool.dll>
                       --editor-modules <UnrealEditor.modules>
                       --architecture <x64|arm64> --mbt <ModBuildTool>
```

CMake 会将 `Resources/DevelopmentEditor/Files.cmake` 中列出的文件嵌入 MDT。`build-editor` 与 `build` 均会在临时工作目录中写出这些文件、插件描述符和打包配置，随后由 MDT 调用 UAT 编译，并在校验 Editor 的 BuildId 与二进制文件后，将插件连同原生工具一并发布。参见 [桌面 CI 调用方式](../../../../../Scripts/run_desktop_ci.py)。

打开 `ExampleMods/TestMod` 之前，请先将生成的 `ModDevelopmentSDKEditor` 与目标游戏的 `UMFDevSDK` 安装到 `ExampleMods/TestMod/Plugins`。示例游戏的完整 SDK 发布包同时包含这两个插件。

## 生产者契约

`UMF.SdkContract` v2 由 [sdk-contract.schema.json](Protocol/sdk-contract.schema.json) 定义；MDT 同时也兼容使用原始 ABI 字段名的 v1 契约。契约状态必须为 `Complete` 且未解析类型数为零，其契约、投影与原生 ABI 的 SHA-256 标识必须与各 UBT 目标保持一致。包含 Hook 或 Event 声明且采用生命周期投影 v2 的契约允许使用空 API 列表。契约文件本身须作为经过哈希校验的 `Contract` 产物记录在生成清单中，公开头文件则须为 MHT 清单声明的 `SdkHeader` 产物。宿主实现、注册与校验代码以及私有头文件绝不会进入公开的作者 SDK。

支持的原生客户端 ABI 为 `ContextHandlesV1`，实际函数名取自 `api.native.symbol`：

```cpp
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

状态码零表示成功，非零表示宿主错误。上下文、作用域和对象均为不透明、进程内有效的非零令牌。公开类型 `ObjectReference` 包含稳定的 `id` 和 `cppName`。`ObjectGetter` 指定 `provider: LocalPlayerSubsystem`、`resultTypeId` 和 `nullResult: Error`；`FloatMethod` 指定 `targetTypeId`、`parameterName` 和稳定的 `parameterId`；`TypedMethod` 描述参数和返回值，目标可为玩家提供者或对象视图。`ObjectInput` 参数指定公开的 UE 类、头文件与模块，对象结果则指定投影视图的 TypeId。`blueprint` 字段决定除原生 UE 声明外是否同时生成蓝图节点。

获取接口使用 Player 作用域，方法支持 Player 或 World 作用域；所有 API 均要求 `thread: GameThread` 和 `failureRule: ReturnError`，权限以逐 API 列表形式声明。每项 API 还需提供 `id`、稳定的公开 `name`、`displayName`、`signatureHash`、`projectionHash` 和 `native: {header, symbol}`。客户端配置中的必需符号记录须与实际入口匹配。头文件路径相对于生成批次根目录保留；遇到不支持的接口形态时会直接报错，而不是生成占位实现。

生成的 Runtime SDK 包含不透明 `USTRUCT` 引用与 `MinimalAPI` 蓝图函数库。标准类型化 UE 节点通过 `ExpandEnumAsExecs` 暴露成功/失败执行分支，无需额外创建空 Editor 模块。函数指针 `static_assert` 会在 UE 编译阶段核对 ABI 兼容性。令牌不开放蓝图编辑，宿主通过 `FromNative` 提供上下文。适配层错误以负数表示，原生状态码则保留为非负的蓝图 `int64` 错误码。

World 作用域的宿主蓝图 API 在契约中记录 `hostBlueprint.callingTableHash`。生成的 C++ SDK 会在首次调用前通过活动的 `UModInstance` 连接原生函数表，并按该哈希获取宿主注册的提供者实例。宿主负责提供授权策略和世界内的实时提供者。

MHT 会将全局生命周期声明导出为 `Contract/ModLifecycle.json`，并在 SDK 契约的 `modLifecycle` 字段中记录其摘要。MDT 校验该产物后 会生成 `ModInstance.h` 和 `ModInstanceSubSystem.h`，随后调用 UAT 编译作者 SDK 与制作编辑器插件。生成的反射模块保留宿主的 `/Script/ModLoader` 类型路径，由游戏会话负责管理实例并分发生命周期事件。

`UMF.SdkBuildRequest` v3 包含 `project`、`gameId`、`sdkVersion`、`pluginName`、`contract`、`tools`、`editor` 和 `targets`。每个目标列出 `target`、`platform`、`configuration`、`architecture`、`generatedManifest`。MDT 比对文件哈希、归属模块、API 签名、连接符号和 ABI，所有目标使用相同的公开头文件及 Client 实现。工具与路径字段见[请求协议](Protocol/sdk-build-request.schema.json)。

### World 扩展契约

SDK 的 `extensions` 契约用于定义并校验扩展 ID 与签名，同时驱动生成蓝图订阅、值结构体、对象投影及跨 Mod 服务获取所需的适配代码。生成的生命周期投影暴露 **On Mod Register** 和 **On Mod Bind** 两个事件，且 SDK 随包附带供独立原生回调使用的扩展 ABI 头文件。使用扩展清单 v2 打包时，请将同一份 `Contract/SdkContract.json` 交给打包器。详见[制作插件打包与注册流程](Resources/DevelopmentEditor/README.zh-CN.md)。

## 编辑器入口与交付

启用 `ModDevelopmentSDKEditor`，在 **Tools → Mod SDK** 菜单下选择 **Plan Mod SDK...** 或 **Build Mod SDK...**，再选择请求 JSON 文件。编辑器既可使用随插件打包的 MDT，也可使用 ModRuntime 插件 `Tools/build/ModSDKTool` 中的本地构建，还可以手动选择可执行文件并在编辑器设置中保存路径。日志会异步输出到 Output Log 而不会阻塞编辑器，结果 JSON 写入 `Intermediate/ModTools/ModSDKTool/Results`。**Cancel Mod SDK task** 用于终止整个进程树。

通用开发插件独立于游戏 Runtime 模块。发布包会在插件的 `Binaries/ThirdParty/ModSDKTool/<平台>` 目录中携带 MDT、MBT 与许可证说明。

```text
Manifest.json
Build/Plan.json
Contract/
Native/Include/
Host/Plugins/<game-sdk>/
Plugins/<game-sdk>/
Plugins/ModDevelopmentSDKEditor/
```

游戏发行方将 `Host/Plugins/<game-sdk>` 安装到提供共享 Runtime 的游戏，再构建 Game 和 Cook。宿主版依赖实际的 `ModRuntime`。独立作者工程使用 `Plugins/` 下的两个插件，制作端 SDK 提供 `/Script/ModLoader` 的反射镜像。两份 SDK 的 Runtime 模块名、反射类型、Client 源与 ABI 相同。

Mod 打包时仅收集该 Mod 的私有模块。共享 SDK 在生成的插件中保留为依赖。游戏为每个 Mod 提供独立调用者上下文，在回收一个调用者时保留其他调用者的连接。

`inspect` 校验发布包的清单、宿主版和作者版的共享 Client 源与插件标记。实际游戏链接另有 `host-link.json` 记录。发布方用暂存后的游戏验证 ABI 和两个独立 Mod 的调用，并将结果保存在 SDK 目录外。[运行验证](../../../../../Test/SharedRuntime/README.md)
