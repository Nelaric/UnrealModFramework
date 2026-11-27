<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# ModSDKTool（MDT）

ModSDKTool（MDT）负责将宿主已解析的导出契约打包构建为不可变、可独立安装的 SDK 发布包（Bundle）。它会生成连接玩家上下文、对象视图与强类型方法的 UE 反射及蓝图包装代码（支持标量与公开 `UObject*` 入参），其中所有 API ID、公开类型、函数名与底层 C 符号均严格源自生产者契约。

在整体工具链中，MHT 负责 C++ AST 投影，`ExportCodeGenerator` 负责导出宿主蓝图快照；MDT 则统一核验 Game 与 Editor 各目标的 v4 生成清单，生成共享 SDK Runtime 的宿主版与作者版插件源码，调用 Unreal AutomationTool（`BuildPlugin`）编译制作端插件，最终输出经过完整哈希核验的发布包。

## 构建与命令行用法

在仓库根目录执行以下命令构建 MDT：

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModSDKTool
```

SDK 发布包相关命令：

```text
ModSDKTool plan --request SdkBuildRequest.json
ModSDKTool build --request SdkBuildRequest.json
ModSDKTool inspect --bundle <发布目录>
```

`SdkBuildRequest.json` 内的相对路径基于该请求文件所在目录解析：

- `plan`：在不触发插件编译的前提下，全面核验所选 v4 生成清单、公开契约与共享 Client 源码的一致性。
- `build`：调用 UAT `BuildPlugin` 编译制作端插件并原子发布完整 SDK 目录。
- `inspect`：重新校验已发布目录中的文件清单、哈希、共享 Client 源码及双端 Runtime 模块声明。

命令将 JSON 结果输出到 stdout，诊断日志输出到 stderr；退出码 `0` 表示成功，`1` 表示失败，`130` 表示任务被取消。

已发布的 SDK 目录具有不可变性：当全部输入完全一致时，再次运行 `build` 会在完整核验现有目录无误后直接返回 `UpToDate`；一旦输入发生任何变化，就必须升级版本号或指定新的输出目录。发布阶段会先在同级独占暂存目录中生成并校验全部产物，最后原子重命名至原本不存在的目标路径，确保中途失败绝不会污染已有 SDK。

### 单独构建 `ModDevelopmentSDKEditor`

如果只需针对某个已安装的 Unreal Editor 单独构建通用制作插件，可运行 `build-editor`：

```text
ModSDKTool build-editor --package <尚不存在的目录>
                       --dotnet <.NET 宿主> --automation-tool <AutomationTool.dll>
                       --editor-modules <UnrealEditor.modules>
                       --architecture <x64|arm64> --mbt <ModBuildTool>
```

CMake 编译 MDT 时，会将 `Resources/DevelopmentEditor/Files.cmake` 列出的插件源码直接内嵌进可执行文件。执行 `build` 或 `build-editor` 时，MDT 会把这些内嵌源码连同生成的插件描述符与打包配置释放到临时工作目录，调用 UAT 编译并核对 Editor `BuildId` 与产出二进制，最后将插件及随附的原生工具一并发布（脚本调用示例见 [.circleci/build-editor-plugin.sh](../../../../../.circleci/build-editor-plugin.sh)）。

打开仓库内的示例 Mod 工程（`ExampleMods/TestMod`）之前，请先将生成的 `ModDevelopmentSDKEditor` 与目标游戏的 `UMFDevSDK` 一并复制到 `ExampleMods/TestMod/Plugins/` 下（完整的 `ModSDKTool build` 发布包已同时包含这两个插件）。

## 生产者契约要求

MDT 消费 `UMF.SdkContract` **v2** 契约（[sdk-contract.schema.json](Protocol/sdk-contract.schema.json)，同时向后兼容使用旧版 ABI 字段名的 **v1** 契约）：

- 契约状态必须为 `"Complete"` 且未解析类型数为 `0`，且请求中所有目标清单的 `contractHash`、`projectionHash` 与 `nativeABIHash` 必须完全一致。
- 若契约包含 Hook/Event 声明且采用 v2 生命周期投影，允许 `apis` 列表为空。
- 契约 JSON 本身必须作为带哈希记录的 `Contract` 产物出现在生成清单中，公开头文件则必须是由 MHT 标记的 `SdkHeader`——宿主的 C++ 业务实现、内部注册/校验源码与私有头文件绝不会进入公开 SDK。

### 原生 ABI 与生成的 UE 包装层

生成的客户端调用统一对接 `ContextHandlesV1` C ABI（具体 C 函数名取自 `api.native.symbol`）：

```cpp
uint32_t ObjectGetter(uint64_t ModContext, uint64_t PlayerScope, uint64_t* OutObject);
uint32_t FloatMethod(uint64_t ModContext, uint64_t Object, float Amount, float* OutValue);
```

返回状态码 `0` 代表成功，非零代表宿主错误；上下文、作用域与对象均以进程内有效的非零 64 位不透明令牌表示：

- **`ObjectReference`**：定义稳定的类型 `id` 与 `cppName`。
- **`ObjectGetter`**：运行在 `Player` 作用域，指定 `provider: LocalPlayerSubsystem`、`resultTypeId` 与 `nullResult: Error`。
- **`FloatMethod` 与 `TypedMethod`**：支持 `Player` 或 `World` 作用域，要求 `thread: GameThread` 与 `failureRule: ReturnError`，并按 API 单独声明权限列表。`TypedMethod` 支持标量参数、`ObjectInput` 参数（指明公开 UE 类、头文件路径与所属模块）以及标量/`void`/投影视图返回值，并通过 `blueprint` 布尔字段控制是否额外生成蓝图节点。
- **宿主蓝图 World API**：契约中额外携带 `hostBlueprint.callingTableHash`，生成的 C++ SDK 会在首次调用前通过当前活跃的 `UModInstance` 自动接通底层函数表，并按该哈希向宿主获取实时提供者引用。

每项 API 均须提供 `id`、稳定的公开 `name`、`displayName`、`signatureHash`、`projectionHash` 与 `native: {header, symbol}`，并与清单中的 `requiredSymbols` 一一对应；遇到不支持的接口形态会直接报错，不会生成残缺的占位实现。

在生成的 `<pluginName>Runtime` 模块中，不透明 `USTRUCT` 句柄搭配 `MinimalAPI` 蓝图函数库使用，标准蓝图节点通过 `ExpandEnumAsExecs` 直接提供成功/失败双执行输出引脚，无需额外附带空的 Editor 模块。UE 编译阶段会通过函数指针 `static_assert` 二次校验 ABI 签名兼容；令牌不开放蓝图手填（由宿主经 `FromNative` 注入），适配层自身错误以负的 `int64` 返回，原生状态码则原样保留为非负 `int64`。

此外，MDT 会核验 MHT 导出的 `Contract/ModLifecycle.json`（与契约中的 `modLifecycle` 摘要比对），据此生成保留 `/Script/ModLoader` 反射路径的 `ModInstance.h` 与 `ModInstanceSubSystem.h`，确保作者派生的蓝图生命周期类在宿主游戏中能被原样识别和实例化。

### 构建样例 SDK

请先通过 MBT 完成宿主 Game 与 Development Editor 两个目标的编译，拿到 `Intermediate/ModTools/ModHeaderTool/Generated/<Target>/<Platform>/<Configuration>/<Architecture>/` 下完整的 v4 清单（命令见[构建开发宿主](../../../../../README.zh-CN.md#构建开发宿主)）。所有参与合并的目标在契约、投影和 ABI 哈希以及公开头文件、连接描述与 Client 源码上均须完全一致。

将下面的 `UMF.SdkBuildRequest` v3 示例（[请求 Schema](Protocol/sdk-build-request.schema.json)）保存为仓库根目录下的 `SdkBuildRequest.json`，填入本机真实的 UE 5.6 路径及从 `UnrealEditor.modules` 读取的 `BuildId`（若需为示例工程打包带 C++ 原生模块的 Mod，请将下方第一个目标及对应路径中的 `UnrealModFrameworkDev` 替换为模块化目标 `UnrealModFrameworkDevModular`）：

```json
{
  "format": "UMF.SdkBuildRequest",
  "schemaVersion": 3,
  "project": "UnrealModFramework/UnrealModFrameworkDev.uproject",
  "gameId": "UnrealModFrameworkDev",
  "sdkVersion": "1.0.0",
  "pluginName": "UMFDevSDK",
  "contract": "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDev/Win64/Development/x64/Contract/UnrealModFrameworkDev.sdkcontract.json",
  "tools": {
    "mbt": "UnrealModFramework/Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe",
    "dotnet": "<engine bundled dotnet.exe>",
    "automationTool": "C:/UE_5.6/Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll"
  },
  "editor": {
    "platform": "Win64",
    "architecture": "x64",
    "configuration": "Development",
    "modules": "C:/UE_5.6/Engine/Binaries/Win64/UnrealEditor.modules",
    "buildId": "<BuildId from UnrealEditor.modules>"
  },
  "targets": [
    {
      "target": "UnrealModFrameworkDev",
      "platform": "Win64",
      "configuration": "Development",
      "architecture": "x64",
      "generatedManifest": "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDev/Win64/Development/x64/UnrealModFrameworkDev.generated-artifacts.json"
    },
    {
      "target": "UnrealModFrameworkDevEditor",
      "platform": "Win64",
      "configuration": "Development",
      "architecture": "x64",
      "generatedManifest": "UnrealModFramework/Intermediate/ModTools/ModHeaderTool/Generated/UnrealModFrameworkDevEditor/Win64/Development/x64/UnrealModFrameworkDev.generated-artifacts.json"
    }
  ]
}
```

未显式覆盖输出目录时，该请求默认将 SDK 发布至 `UnrealModFramework/Intermediate/ModTools/ModSDKTool/UMFDevSDK/1.0.0/`。

### World 扩展契约支持

SDK 契约中的 `extensions` 节定义了所有 World Hook/Event 与跨 Mod 服务的 ID 及签名哈希，用于驱动生成蓝图订阅节点、反射值载荷结构体、只读 `UObject` 视图投影及跨 Mod 服务获取接口，同时在 `UModInstance` 上暴露 **On Mod Register** 与 **On Mod Bind** 生命周期事件，并随包附带供独立原生回调 DLL 包含的纯 C 头文件。使用 v2 扩展清单打包 `.mod` 时，请将发布包内的 `Contract/SdkContract.json` 一并传给打包器（详见[模组开发 SDK 编辑器](Resources/DevelopmentEditor/README.zh-CN.md)）。

## 编辑器菜单入口与发布目录结构

启用 `ModDevelopmentSDKEditor` 后，也可直接在 Unreal Editor 菜单栏打开 **Tools → Mod SDK**，选择 **Plan Mod SDK...** 或 **Build Mod SDK...** 并指定请求 JSON。编辑器支持调用随插件打包的 MDT、`ModRuntime/Tools/build/ModSDKTool` 下的本地构建产物，或在编辑器设置中手动保存的自定义可执行文件路径。任务日志会异步流式输出到 Output Log 而不会卡住编辑器界面，结果 JSON 保存在 `Intermediate/ModTools/ModSDKTool/Results` 下，点击 **Cancel Mod SDK task** 可一键终止整个子进程树。

发布后的 SDK Bundle 目录结构如下（通用制作插件独立于游戏侧 `ModRuntime`，并在 `Plugins/ModDevelopmentSDKEditor/Binaries/ThirdParty/ModSDKTool/<平台>` 下随包附带 MDT、MBT 与第三方许可证说明）：

```text
Manifest.json
Build/Plan.json
Contract/
Native/Include/
Host/Plugins/<game-sdk>/
Plugins/<game-sdk>/
Plugins/ModDevelopmentSDKEditor/
```

## 安装与验证

1. **宿主游戏侧安装**：将发布包中的 `Host/Plugins/<game-sdk>` 复制到宿主游戏工程的 `Plugins/` 目录并启用，随后再编译并 Cook 游戏基础发行版。宿主版 SDK 依赖 `ModRuntime`，直接使用游戏内真实的 `/Script/ModLoader` 类型。
2. **Mod 作者工程侧安装**：将 `Plugins/<game-sdk>` 与 `Plugins/ModDevelopmentSDKEditor` 两个目录复制到每个独立 Mod 工程的 `Plugins/` 下并启用。作者版 SDK 内置 `/Script/ModLoader` 的反射镜像，同时与宿主版共享完全相同的 `<pluginName>Runtime` 模块名、SDK 反射类型、Client 源码及底层 C ABI。
3. **打包与运行验证**：基于匹配的游戏基础发行版打包 Mod（打包当前 C++ 工程时，需提供匹配的 Win64 x64 模块化宿主 `.target` 回执；打包器会将该 Mod 自身的原生模块打入归档，同时保留对共享 `<game-sdk>` 插件的依赖，详见[打包当前 C++ 工程](Resources/DevelopmentEditor/README.zh-CN.md#打包当前-c-工程)）。

`ModSDKTool inspect --bundle <路径>` 负责核验发布清单、双端 SDK 插件的共享 Client 源码与 Runtime 模块声明。由于发布包自身的能力标记仅涵盖制作端静态产物，正式发版时请将宿主最终链接产出的 `host-link.json` 与实机打包回归测试报告随 SDK 一并归档留存（见[共享 Runtime 打包游戏回归](../../../../../Test/SharedRuntime/README.zh-CN.md)）。
