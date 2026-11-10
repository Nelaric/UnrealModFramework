<!-- Copyright (c) 2026 Nelaric Contributors -->

[English](README.md) | 简体中文

# UnrealModFramework

[![版本](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.zh-CN.md)

UnrealModFramework 是以 UE 5.6 为开发基线的工程，目标是提供可迁移的游戏侧 Mod Runtime 和独立构建的模组开发编辑器。仓库目前只包含工程及插件模块骨架；模组包加载、内容解析、原生模组 API 和编辑器工作流仍待实现与验证。

下表路径均相对于仓库根目录。UE 工程位于 `UnrealModFramework/`，仓库配置、文档和质量检查脚本位于与其同级的根目录中。

| 路径 | 用途 |
| --- | --- |
| `UnrealModFramework/UnrealModFrameworkDev.uproject` | 开发宿主工程 |
| `UnrealModFramework/Plugins/ModRuntime/` | 游戏侧源码插件；`Source/Runtime/` 与 `Source/Editor/` 按目标归类模块 |
| `UnrealModFramework/SourcePlugins/ModDevelopmentSDKEditor/` | 独立构建的模组制作插件 |
| `Docs/CodingStandards/` | 项目编码与贡献规范 |
| `.circleci/config.yml` | UE 5.6.1 Linux 构建配置 |
| `Scripts/` | 本地质量检查 |

`Binaries`、`Intermediate`、`Saved` 和 `Dist` 是生成产物，不进入 Git。制作插件从 `UnrealModFramework/SourcePlugins` 下的描述文件通过 UAT `BuildPlugin` 构建；Windows 上应使用较短的临时 `-Package` 路径，成功后再将插件复制到发布目录。

修改代码或构建文件前，请阅读[编码规范](Docs/CodingStandards/README.zh-CN.md)、[贡献指南](CONTRIBUTING.zh-CN.md)和 [CircleCI 接入说明](Docs/CircleCI.zh-CN.md)。

## SDK 构建工具

`ModSDKTool`（MST）提供 `plan`、`build` 和 `inspect`，根据导出契约及链接产物构建游戏专属 SDK。构建入口为 `Tools/Build.bat --target ModSDKTool`，请求和产物格式见 [MST 文档](UnrealModFramework/Plugins/ModRuntime/Tools/ModSDKTool/README.zh-CN.md)。

## MHT 的 C++ 导出参数校验

MHT 是独立 CMake C++20 工具，只处理 `MOD_EXPORT_API`、`MOD_EXPORT_EVENT`、`MOD_EXPORT_HOOK` 的 C++ 声明，不扫描或校验蓝图。工程采用 `include/ModHeaderTool` 和 `src` 布局，`main.cpp` 只转发入口。构建工具：

```powershell
.\UnrealModFramework\Plugins\ModRuntime\Tools\Build.bat --target ModHeaderTool
```

编译 UE Editor Target 后，先从共用的 `ExportMetadata::FOptions` 提供者导出实际选项。它使用 UE 已解析的 GameMod 配置和全局 GameplayTags 字典，包含原生及数据表标签；MHT 不另行实现一套近似的 INI/标签解析器，也不依赖 `BlueprintIntegration`：

```powershell
$project = (Resolve-Path UnrealModFramework/UnrealModFrameworkDev.uproject).Path
& "C:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=ModExportOptions -unattended -NullRHI -nosound
.\UnrealModFramework\Plugins\ModRuntime\Tools\build\Release\bin\Release\ModHeaderTool.exe scan --project $project --output UnrealModFramework/Intermediate/ModHeaderTool/Exports.json
```

选项目录默认写入工程的 `Saved/ModHeaderTool/MetadataOptions.json`，也可以用导出命令的 `-Output=<文件.json>` 和 MHT 的 `--options <文件.json>` 指定。配置输入新增、删除或改变时，目录指纹检查会报错，须重新导出。原生标签模块重新编译或数据表更新后也应重新导出。未找到任何导出调用时可以不提供选项目录。`.modexpot` 仍为可选的额外扫描目录配置，不会自动创建。

字段规则定义在 `ModExportMetadataSchema.h`，字段名和候选值区分大小写：

| 宏 | 必填字段 | 可选字段 |
| --- | --- | --- |
| 全部 | — | `ExportVersion` 默认 `"1.0.0"`，以及 `DisplayName`、`Description` |
| `MOD_EXPORT_API` | `Scope`、`Thread`、`FailureRule` | `Permissions` 默认空列表，以及 `FullView`、`CompatibleNames` |
| `MOD_EXPORT_EVENT` | `ExportId`（已注册 GameplayTag）、`SourceScope`、`PublishRule` | `PayloadType` |
| `MOD_EXPORT_HOOK` | `ExportId`（已注册 GameplayTag） | `ContextType`、`AllowedStages`（默认 `(Observe)`） |

`Scope` 等候选值接受标识符或字符串。权限和阶段使用括号列表，如 `Permissions = ("WorldRead", "WorldWrite")`、`AllowedStages = (Observe, Modify)`；阶段至少有一个，列表项不能重复。`ExportVersion` 为两段或三段数字版本字符串，各段不允许多余的前导零。`PayloadType`、`ContextType` 校验 C++ 类型名语法，类型是否存在及声明绑定仍留给 AST 阶段。

```cpp
MOD_EXPORT_API(
    Scope = World,
    Thread = GameThread,
    FailureRule = ReturnError,
    Permissions = ("WorldRead"))
```

解析支持注释、续行、相邻字符串、普通/raw string、常规转义、Unicode 转义及 `TEXT("...")`。不接受任意 C++ 表达式、命名常量、用户自定义字符串后缀或嵌套列表。未知/重复字段、缺少必填字段、类型错误、未注册 ID、非法候选值和重复列表项均输出 `文件(行,列): error: 宏名: 具体原因`；行列从 1 开始，列按 UTF-8 字节计。

有错误时继续收集可恢复的诊断，返回 `1`，不写入成功报告；成功返回 `0`。JSON 报告版本为 `2`，保留原始参数并新增解析后的 `Metadata` 和 `ParametersValidated`。`PreprocessorEvaluated` 仍为 `false`：条件分支尚不求值，C++ AST、类型存在性和运行时策略语义不属于本阶段。

## MHT API 代码生成

已加入[薄 UHT 适配工程](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md) `ModRuntime.ubtplugin.csproj`，由 UBT 自动发现。开发工程的 `Config/ModHeaderTool.json` 选择 `generate`；未配置时适配层仍默认 `scan`。输出位于 UHT 同级的 `Inc/ExportMetadata/MHT`。生成模式在 UHT 返回前依次调用 MHT 和独立的 MBT 宿主编译，UBT 通过所属模块的私有 External 依赖链接所得静态库。

同步 UObject 成员 API 支持公开、非静态成员，按值标量或已加载 UObject 指针入参，标量、void 或 World 作用域 UObject 指针返回值，GameThread 与 ReturnError。MHT 使用 libclang 的真实 AST 和 UBT 编译数据库，关联活动宏与具体重载。对象返回值按 `ErrorIfNull` 规则投影为 `FModRuntimeObject<T>`，const 对象返回只读视图。MHT 还生成定宽类型的 ContextHandlesV1 C ABI 与独立客户端入口。Event、Hook、结构体、容器及引用参数缺少投影时，生成阶段报错。

MHT 按类型 ID 和公开函数名生成 API ID。`CompatibleNames = ("OldName")` 为同一实现生成一个可调用的旧名入口。省略该字段时，MHT 使用配置快照中适用的 UE 函数重定向。详见 [API 兼容名](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#api-兼容名)。

`FullView = ("USkeletalMesh")` 选择签名里的 UObject 指针入参类。MHT 检查目标构建可见的公开实例成员和公开基类，自动生成的成员采用选择该类的声明所要求的权限。任一成员缺少投影时，MHT 报出类名和具体成员，整个生成批次失败。详见 [FullView 与对象输入](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md#fullview-与对象输入)。

`ExportMetadata/Public/ModExportAnnotations.h` 是 Game/Shipping 可使用的声明头；原 Editor 模块的 `ModExportMacros.h` 保留转发。`ModAPI` Runtime 模块提供 `TModResult`、类型化调用表绑定、World/Player 作用域、显式提供者、权限检查和撤销。视图不拥有或公开原始 UObject；Actor 结束玩法、World 清理、玩家控制器变化、提供者撤销或调用者撤销后拒绝旧绑定。权限由宿主显式授予，查询角色不会隐含授予写权限。

开发宿主的 `UGameLocalPlayerAPI::GetLocalCharacter` 查询其 LocalPlayer 当前控制的 `AGameCharacter`。角色的 `AddHealth` 返回更新后的生命值，`SetMesh` 接受已加载的骨骼网格并验证骨架兼容性。

生成工作流：

1. 使用 Tools/Build.bat 构建 MHT 和 MBT，并准备匹配的 libclang 与资源头。本环境验证了 LLVM 21.1.8、UE 5.6、MSVC 14.51。仓库只携带 LLVM 18.1.8 的稳定 C API 头及许可证，不携带 LLVM 二进制。
2. 首次没有可运行的配置导出器时，用原生 UE Build 附加 `-MhtBootstrap` 构建 Development Editor，再运行 `-run=ModExportOptions`。该显式引导模式不注册生成 API，禁止用于 Game、Shipping 或发布。
3. 用 UBT `-Mode=GenerateClangDatabase`、`-NoPCH`、实际 Target 和 `-Compiler=VisualStudio2022` 生成编译数据库；预先创建 `-OutputDir`。适配器跳过这次准备调用。按下例准备匹配的 build input；Target 或工具链配置变化后重新准备。
4. 正常构建对应 UE Target。UHT 转交实际所属模块的编译配方，先执行 MHT，再执行 MBT，随后 UBT 链接。Editor 和 Game 使用独立输出根及宿主库。下方直接调用 MHT 的命令可用于独立生成，但不会构建或链接宿主库。

`Intermediate/ModHeaderTool/BuildInputs/Editor.json` 的输入结构如下。`project` 相对输入文件，其他文件/模块路径相对工程目录；LLVM 和 MSVC 兼容参数应按实际工具链填写，示例中的占位路径需替换：

```json
{
  "schemaVersion": 1,
  "project": "../../../UnrealModFrameworkDev.uproject",
  "gameId": "UnrealModFrameworkDev",
  "target": "UnrealModFrameworkDevEditor",
  "platform": "Win64",
  "configuration": "Development",
  "architecture": "x64",
  "objectReturnPolicy": "ErrorIfNull",
  "compilationDatabase": "Intermediate/ModHeaderTool/Clang/Editor/compile_commands.json",
  "typeIdentities": "Config/ModAPITypes.json",
  "modules": [{
    "name": "ExportTest",
    "sourceDirectory": "Source/ExportTest",
    "translationUnits": [
      "Source/ExportTest/GameCharacter.cpp",
      "Source/ExportTest/GameLocalPlayerAPI.cpp"
    ]
  }],
  "analysisArguments": [
    "-resource-dir=<LLVM>/lib/clang/21",
    "-fms-compatibility-version=19.51",
    "-Wno-invalid-constexpr"
  ],
  "pathRoots": { "Engine": "<UE_5.6>", "LLVM": "<LLVM>" }
}
```

`typeIdentities` 保存受版本控制的类型身份，也可直接给出 `types` 对象；两者互斥。`pathRoots` 为本地诊断/输入摘要中的绝对路径提供规范名称。编译数据库采用 [Clang 的标准格式](https://clang.llvm.org/docs/JSONCompilationDatabase.html)，每个指定 TU 必须唯一对应实际编译命令；Target、平台、配置和模块名会与 UBT 宏核对。

独立生成使用同一变体的一次 UHT 构建所准备的完整生产者上下文集合。

```powershell
$mht = ".\UnrealModFramework\Plugins\ModRuntime\Tools\build\Release\bin\Release\ModHeaderTool.exe"
$output = "UnrealModFramework/Plugins/ModRuntime/Intermediate/Build/Win64/UnrealEditor/Inc/ExportMetadata/MHT/UnrealModFrameworkDevEditor/Development/x64"
$contexts = "UnrealModFramework/Intermediate/ModHeaderTool/HostTargets/UnrealModFrameworkDevEditor/Win64/Development/x64/contexts.json"
& $mht generate --build-input UnrealModFramework/Intermediate/ModHeaderTool/BuildInputs/Editor.json --output-dir $output --host-contexts $contexts --libclang "<LLVM>/bin/libclang.dll"
```

输出直接覆盖 MHT 下固定的 `SDK`、`Host`、`Client`、`Contract`、`Diagnostics` 及 `<gameId>.generated-artifacts.json`，不再建立 Profile/批次目录或 `current.json`。已移除的文件会清理，内容相同则保留时间戳；完整清单最后提交，中断的替换不会被当作完整结果消费。`generationId` 仅保留为内容摘要。

生成头文件仅包含声明与签名数据。全部生成函数实现在 `Host/<owner>/<owner>.modhost.cpp`、`<owner>.modabi.cpp`、`<owner>.modverify.cpp`、`SDK/Private/<owner>.modviews.cpp` 及独立的 `Client/<game>ModClient/<game>ModClient.modclient.cpp` 中，只由 MBT 编译。`GameModExports.cpp` 已不包含生成实现。`UGameModAPIHost` 调用声明的 `UMF_ModHost_ExportTest` 入口，定义由 MBT 静态库提供。`ExportTest` 私有依赖 External 模块 `ExportTestHost`，通过 `PublicPreBuildLibraries` 预先登记首次构建尚未产出的库路径，也避免该库传播到其他 DLL。库与回执位于 `Intermediate/MBT/<Target>/<Platform>/<Configuration>/<Architecture>`。

MHT 交付 `.modabi.h`、`.clientabi.json`、`.sdkcontract.json` 和 v2 产物清单，明确列出编译单元及经过库检查的 Host/Client API 符号。[客户端 ABI 文档](UnrealModFramework/Plugins/ModRuntime/Tools/ModHeaderTool/README.zh-CN.md) 规定查询、Connect/Disconnect、token 交付与生命周期；MST 消费已解析 SDK 契约，内部 UE 视图头作为私有宿主产物。

自动编译配方支持 Win64 x64 MSVC，保留真实 UBT 模块的生产者参数。构建前检查目录清单，链接后核对实际所属二进制中的归档见证并发布链接回执。参见 [UHT 接入](UnrealModFramework/Plugins/ModRuntime/Source/ModRuntimeUht/README.zh-CN.md)。

宿主代码通过 `UGameModAPIHost::OpenLocalPlayer(Player, Permissions)` 创建显式调用者和服务绑定，将 `Mod::API::FViewFactory::Make<UGameLocalPlayerAPI>(Binding)` 得到的 SDK 视图交给模组。之后调用 `GetLocalCharacter()`，成功后调用角色视图的 `AddHealth(Amount)`，并检查两个结果。模组结束时调用 `RevokeCaller`；玩家移除或控制器变化会关闭玩家作用域。首次角色尚未准备好时返回 `ObjectUnavailable`。

## 参与贡献

请通过[贡献指南](CONTRIBUTING.zh-CN.md)、[Issue 表单](https://github.com/Nelaric/UnrealModFramework/issues/new/choose)和 [PR 模板](.github/pull_request_template.md)准备主题集中的改动。[开发流程](Docs/DevelopmentWorkflow.zh-CN.md)说明了本地检查、可信 PR 编译及文档发布方式。

## API 文档

[API 文档入口](Docs/API/README.zh-CN.md)和编码规范由 Doxygen 构建，并发布至 [UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/)。经过审查的更新合入 `main` 后会自动发布。发布配置见[组织文档托管指南](Docs/Hosting/README.zh-CN.md)。

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## 许可证

除非另有说明，项目原创内容采用 [MIT 许可证](LICENSE)。Unreal Engine 和第三方内容保留各自的许可条款；归属说明见 [NOTICE.md](NOTICE.md)。

## 致谢

感谢所有参与 UnrealModFramework 的贡献者，感谢 Epic Games 提供 Unreal Engine，以及 CircleCI 提供 Linux 项目构建服务。
